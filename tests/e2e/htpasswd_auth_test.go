package e2e

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// The htpasswd hashes of the password htPass, made with openssl, perl's crypt and htpasswd on
// Debian 13. The ones that the platform's crypt(3) computes are checked against it first.
const htPass = "Passw0rd!"

var htVectors = []struct {
	name, hash string
	crypt      bool // computed by the platform's crypt(3), so it may be missing here
}{
	{"apr1", `$apr1$abcdefgh$ElRfpvhVnK425ytdWjSkO.`, false},
	{"sha", `{SHA}9KaZc+ewv50WD59g48Os0klL6w0=`, false},
	{"ssha", `{SSHA}m+W3osqTMUkI6CjX3egyYBpvJ4ZzYWx0c2FsdA==`, false},
	{"md5", `$1$abcdefgh$eM/aoiszMiOVsp5uZio3M0`, true},
	{"sha256", `$5$abcdefgh$3x1z26ReTJy9eygnbKoWlybVrqJj/JoyQt7FSU6/Do0`, true},
	{"sha512", `$6$abcdefgh$G3i42qTZe63hgVy54mQUuBvitBt/nQ/hp8LV2cOyTT.oa4c0u9c2hhYWkSaVleEm27Df1frAHu8V9fvMg2FML0`, true},
	{"des", `abiR63Zcw.s2g`, true},
	{"bcrypt", `$2y$05$abcdefghijklmnopqrstuuFAKRAH04u6Xw79ex7hAKlkJe/GeWyTm`, true},
	{"yescrypt", `$y$j9T$abcdefghijklmnopqrstu.$db/9jJE8qKgtysPq9yoHA4nnqn4yPMt/TuYANVnzuX4`, true},
}

// cryptWorks asks perl, as the script will, whether this platform's crypt(3) reproduces hash.
func cryptWorks(hash string) bool {
	out, err := exec.Command("perl", "-e", "print crypt($ARGV[0], $ARGV[1])", htPass, hash).Output()
	return err == nil && string(out) == hash
}

// TestReaderHtpasswdAuth: contrib/htpasswd-auth.pl as the "perl" of an authdef checks a user
// against an Apache htpasswd file, which it reads again for every login: the formats Apache MD5
// ($apr1$), {SHA} and {SSHA} everywhere, the ones of the platform's crypt(3) (MD5 $1$, SHA-256
// and SHA-512, DES, bcrypt, yescrypt) where it has them; a wrong password, an unknown user and a
// plain text entry are refused, and a changed, added or removed entry counts at once without a
// restart. Two authdefs with the script and two -f files each have their own interpreter (D-167). Skipped without Perl support in dreaderd (./configure) or without perl.
func TestReaderHtpasswdAuth(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(bin(t), "dreaderd"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(b, []byte("PERL auth request")) {
		t.Skip("dreaderd is built without Perl support (PERL_ENABLED)")
	}
	if _, err := exec.LookPath("perl"); err != nil {
		t.Skip("no perl to check crypt(3) with")
	}
	src, err := os.ReadFile(filepath.Join(filepath.Dir(bin(t)), "contrib", "htpasswd-auth.pl"))
	if err != nil {
		t.Skip("no contrib/htpasswd-auth.pl next to DIABLO_BIN: " + err.Error())
	}
	dir := t.TempDir()
	htfile := filepath.Join(dir, "htpasswd")
	htfile2 := filepath.Join(dir, "other users") // a space in the name: the quotes keep it one word
	script := filepath.Join(dir, "htpasswd-auth.pl")
	if err := os.WriteFile(script, src, 0o644); err != nil {
		t.Fatal(err)
	}

	write := func(lines ...string) {
		t.Helper()
		// replace the file by rename, as htpasswd does: a login never sees a half-written file
		tmp := htfile + ".new"
		if err := os.WriteFile(tmp, []byte(strings.Join(lines, "\n")+"\n"), 0o600); err != nil {
			t.Fatal(err)
		}
		if err := os.Rename(tmp, htfile); err != nil {
			t.Fatal(err)
		}
	}
	var lines []string
	supported := map[string]bool{}
	for _, v := range htVectors {
		if v.crypt && !cryptWorks(v.hash) {
			t.Logf("%s: not supported by this platform's crypt(3)", v.name)
			continue
		}
		supported[v.name] = true
		lines = append(lines, v.name+":"+v.hash)
	}
	lines = append(lines, "# a comment", "", "plain:"+htPass, "twice:{SHA}9KaZc+ewv50WD59g48Os0klL6w0=", "twice:$apr1$wrong")
	write(lines...)

	access := `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef webusers
    perl ` + script + ` -f ` + htfile + `
end
authdef otherusers
    perl ` + script + ` -f "` + htfile2 + `"
end
readerdef members
    read yes
    auth webusers
end
readerdef others
    read yes
    auth otherusers
end
access 127.0.0.1 members
access ` + PeerIP + ` others
`
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access})
	login := func(user, pass string) bool {
		t.Helper()
		c := s.Reader(t)
		expect(t, c, 381, "AUTHINFO USER %s", user)
		code, _ := c.Cmd("AUTHINFO PASS %s", pass)
		c.Close()
		return code == 281
	}

	for _, v := range htVectors {
		if !supported[v.name] {
			continue
		}
		if !login(v.name, htPass) {
			t.Errorf("%s: the right password was refused", v.name)
		}
		// the traditional DES crypt only looks at the first 8 characters, so change an early one
		if login(v.name, "X"+htPass[1:]) {
			t.Errorf("%s: a wrong password was accepted", v.name)
		}
	}
	if login("plain", htPass) {
		t.Error("a plain text entry was accepted")
	}
	if login("nobody", htPass) {
		t.Error("an unknown user was accepted")
	}
	if !login("twice", htPass) {
		t.Error("the first line of a user that is listed twice must decide")
	}

	// the file is checked at every login and read again when it changed: change, add and remove
	// entries without a restart
	write("apr1:"+htVectors[0].hash, "newuser:{SHA}9KaZc+ewv50WD59g48Os0klL6w0=")
	if !login("newuser", htPass) {
		t.Error("a user added to the file was not accepted at once")
	}
	if login("sha", htPass) {
		t.Error("a user removed from the file was still accepted")
	}
	write("apr1:{SHA}0JQeaNqPOBUf+Gph/Fn3xc+fyqI=", "newuser:{SHA}9KaZc+ewv50WD59g48Os0klL6w0=") // apr1's password is now another one
	if login("apr1", htPass) {
		t.Error("the old password of a changed entry was still accepted")
	}
	// a second authdef with the script and another -f file (its own interpreter and file; the
	// first one a worker loads must not answer for both), for clients from the other address
	if err := os.WriteFile(htfile2, []byte("carol:"+htVectors[1].hash+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	login2 := func(user, pass string) bool {
		t.Helper()
		c := s.dialFrom(t, PeerIP)
		expect(t, c, 381, "AUTHINFO USER %s", user)
		code, _ := c.Cmd("AUTHINFO PASS %s", pass)
		c.Close()
		return code == 281
	}
	if !login2("carol", htPass) {
		t.Error("the authdef with the other -f file did not accept its user")
	}
	if login2("newuser", htPass) {
		t.Error("the authdef with the other -f file answered from the first file")
	}
	if !login("newuser", htPass) {
		t.Error("the first authdef stopped working after the second one was used")
	}
	// a missing file refuses everybody
	if err := os.Remove(htfile); err != nil {
		t.Fatal(err)
	}
	if login("newuser", htPass) {
		t.Error("a user was accepted although the htpasswd file is gone")
	}
}

// TestReaderHtpasswdNoFile: the script without -f cannot start; the login is refused and the
// reader keeps serving (before, a script that failed to load made the DNS worker exit).
func TestReaderHtpasswdNoFile(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(bin(t), "dreaderd"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(b, []byte("PERL auth request")) {
		t.Skip("dreaderd is built without Perl support (PERL_ENABLED)")
	}
	src, err := os.ReadFile(filepath.Join(filepath.Dir(bin(t)), "contrib", "htpasswd-auth.pl"))
	if err != nil {
		t.Skip("no contrib/htpasswd-auth.pl next to DIABLO_BIN: " + err.Error())
	}
	script := filepath.Join(t.TempDir(), "htpasswd-auth.pl")
	if err := os.WriteFile(script, src, 0o644); err != nil {
		t.Fatal(err)
	}
	access := `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef broken
    perl ` + script + `
end
readerdef members
    read yes
    auth broken
end
readerdef feed
    feed yes
end
access 127.0.0.1 members
access ` + PeerIP + ` feed
`
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access})
	for i := 0; i < 3; i++ {
		c := s.Reader(t)
		expect(t, c, 381, "AUTHINFO USER pat")
		if code, line := c.Cmd("AUTHINFO PASS patspass"); code == 281 {
			t.Errorf("logged in through a script that cannot start: %s", line)
		}
		c.Close()
	}
}
