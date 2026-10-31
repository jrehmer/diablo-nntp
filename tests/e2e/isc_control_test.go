package e2e

import (
	"bytes"
	"embed"
	"errors"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// Real control messages of ISC (the Big-8 management board's group-admin), posted twice: once
// signed with the old-style key (C25D3AD3B88DA9C1) and once with the modern one
// (FAFE7B550C18C8B7, news.announce.newgroups). testdata/isc/*-old.art and *-modern.art are the
// articles as the archive server returned them, with LF line ends. PGPKEYS is the key file of
// https://ftp.isc.org/pub/pgpcontrol/PGPKEYS; it has the modern key, but not the old-style one
// (only as a signature on the modern key); oldkey.asc is that key, a PGP 2.7 key of 1996. GnuPG
// 2 cannot read it, and gpgv of GnuPG 1 refuses it for want of a valid self-signature, so
// pgpverify cannot verify the old-style articles on any platform with GnuPG: the tests expect
// that, and accept a verification where a keyring does allow it (PGP 2.x as the verifier). trustedkeys-v4.gpg is what GnuPG 2.4 makes of PGPKEYS (it skips the 84 PGP 2 keys of
// other hierarchies); it is used when the platform has no GnuPG to import with.
//
//go:embed testdata/isc
var iscFS embed.FS

const (
	iscSigner   = "news.announce.newgroups"
	iscOldKeyID = "C25D3AD3B88DA9C1"
)

var iscNames = []string{"checkgroups", "newgroup", "rmgroup"}

// iscOldKey returns the old-style key (PGP 2.7, version 3, 1024 bit RSA, 1996) of
// news.announce.newgroups, testdata/isc/oldkey.asc. It is not in ISC's PGPKEYS.
func iscOldKey() []byte {
	b, err := iscFS.ReadFile("testdata/isc/oldkey.asc")
	if err != nil {
		panic(err)
	}
	return b
}

// iscArticle returns the lines of the fixture testdata/isc/<kind>-<style>.art.
func iscArticle(t *testing.T, kind, style string) []string {
	t.Helper()
	b, err := iscFS.ReadFile("testdata/isc/" + kind + "-" + style + ".art")
	if err != nil {
		t.Fatal(err)
	}
	return strings.Split(strings.TrimSuffix(string(b), "\n"), "\n")
}

// iscGpg returns a GnuPG that can import keys (gpg2, gpg or gpg1), or "".
func iscGpg() string {
	for _, d := range []string{"/usr/bin", "/usr/local/bin", "/opt/local/bin", "/usr/pkg/bin", "/opt/ooce/bin"} {
		for _, n := range []string{"gpg2", "gpg", "gpg1"} {
			if p := filepath.Join(d, n); fileExists(p) {
				return p
			}
		}
	}
	return ""
}

func fileExists(p string) bool { _, err := os.Stat(p); return err == nil }

// iscKeyring writes a keyring in the classic format with the keys of PGPKEYS that this
// platform's GnuPG imports; without GnuPG it is the keyring of testdata/isc, made with GnuPG 2.4.
func iscKeyring(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	keyring := filepath.Join(dir, "trustedkeys.gpg")
	if gpg := iscGpg(); gpg != "" {
		keys, _ := iscFS.ReadFile("testdata/isc/PGPKEYS")
		home := filepath.Join(dir, "gnupg")
		if err := os.Mkdir(home, 0o700); err != nil {
			t.Fatal(err)
		}
		imp := exec.Command(gpg, "--batch", "--homedir", home, "--import")
		imp.Stdin = bytes.NewReader(keys)
		_ = imp.Run() // keys it cannot read are skipped, and some GnuPG versions exit non-zero for that
		// the old-style key: GnuPG 2 skips it (PGP 2 key); GnuPG 1 takes it only without its self-signature
		imp = exec.Command(gpg, "--batch", "--homedir", home, "--allow-non-selfsigned-uid", "--import")
		imp.Stdin = bytes.NewReader(iscOldKey())
		_ = imp.Run()
		exp := exec.Command(gpg, "--batch", "--homedir", home, "--export")
		if out, err := exp.Output(); err == nil && len(out) > 0 {
			if err := os.WriteFile(keyring, out, 0o644); err != nil {
				t.Fatal(err)
			}
			return keyring
		}
	}
	b, err := iscFS.ReadFile("testdata/isc/trustedkeys-v4.gpg")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keyring, b, 0o644); err != nil {
		t.Fatal(err)
	}
	return keyring
}

// iscVerify runs "pgpverify -d -k keyring" on the lines of an article and returns stdout, stderr
// and the exit status.
func iscVerify(t *testing.T, keyring string, art []string) (string, string, int) {
	t.Helper()
	cmd := exec.Command(filepath.Join(bin(t), "pgpverify"), "-d", "-k", keyring)
	cmd.Stdin = strings.NewReader(strings.Join(art, "\n") + "\n")
	var out, errb bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &errb
	err := cmd.Run()
	var ee *exec.ExitError
	switch {
	case err == nil:
		return out.String(), errb.String(), 0
	case errors.As(err, &ee):
		return out.String(), errb.String(), ee.ExitCode()
	}
	t.Fatal(err)
	return "", "", -1
}

// iscVerified reports whether this platform verifies the article with the keyring: true for a
// good signature by iscSigner; false only for a signature whose key the keyring lacks (GnuPG
// status NO_PUBKEY), which is what a platform that cannot read the old-style key gives. Anything
// else fails the test.
func iscVerified(t *testing.T, keyring, kind, style string) bool {
	t.Helper()
	out, errb, rc := iscVerify(t, keyring, iscArticle(t, kind, style))
	switch {
	case rc == 0:
		if strings.TrimSpace(out) != iscSigner {
			t.Fatalf("%s-%s: verified, but the signer is %q, want %q", kind, style, strings.TrimSpace(out), iscSigner)
		}
		return true
	case rc == 3 && strings.Contains(errb, "NO_PUBKEY"):
		return false
	}
	t.Fatalf("%s-%s: pgpverify exit status %d, stdout %q\n%s", kind, style, rc, out, errb)
	return false
}

// TestISCControlPGPVerify: the ISC control messages signed with the modern key verify, as
// news.announce.newgroups, with the keys of ISC's PGPKEYS. The old-style signed ones fail, for
// exactly one reason, the signing key is not in the keyring (NO_PUBKEY), unless the keyring has
// it, and then they verify as well. With no keys at all, none verifies; a changed Subject fails.
func TestISCControlPGPVerify(t *testing.T) {
	if !haveGpgv() {
		t.Skip("no gpgv")
	}
	keyring := iscKeyring(t)
	for _, kind := range iscNames {
		t.Run(kind+"/modern", func(t *testing.T) {
			if !iscVerified(t, keyring, kind, "modern") {
				t.Errorf("the %s signed with the modern key does not verify", kind)
			}
		})
		t.Run(kind+"/old", func(t *testing.T) {
			if iscVerified(t, keyring, kind, "old") {
				t.Logf("%s: the old-style key %s is in the keyring and verifies", kind, iscOldKeyID)
			} else {
				t.Logf("%s: the old-style key %s cannot be used by gpgv here (NO_PUBKEY), as expected", kind, iscOldKeyID)
			}
		})
	}
	t.Run("no keys", func(t *testing.T) {
		empty := filepath.Join(t.TempDir(), "empty.gpg")
		if err := os.WriteFile(empty, nil, 0o644); err != nil {
			t.Fatal(err)
		}
		for _, kind := range iscNames {
			if _, _, rc := iscVerify(t, empty, iscArticle(t, kind, "modern")); rc != 3 {
				t.Errorf("%s with an empty keyring: exit status %d, want 3", kind, rc)
			}
		}
	})
	t.Run("tampered", func(t *testing.T) {
		art := iscArticle(t, "newgroup", "modern")
		for i, l := range art {
			if strings.HasPrefix(l, "Subject: ") {
				art[i] = l + " x"
			}
		}
		if _, _, rc := iscVerify(t, keyring, art); rc != 3 {
			t.Errorf("a changed Subject: exit status %d, want 3", rc)
		}
	})
}

// TestISCControlOldKey: the old-style signatures are good, by the old-style key of
// news.announce.newgroups, which is a PGP 2 key: GnuPG 2 cannot read it at all, and GnuPG 1 reads it
// only when told to accept a key without a valid self-signature (--allow-non-selfsigned-uid),
// which gpgv, and so pgpverify, cannot be told. So this runs gpg itself, on the text and
// signature pgpverify rebuilds from the article (-d2 keeps them), and only with GnuPG 1.
func TestISCControlOldKey(t *testing.T) {
	gpg := iscGpg()
	if gpg == "" {
		t.Skip("no GnuPG")
	}
	if out, _ := exec.Command(gpg, "--version").Output(); !strings.Contains(strings.SplitN(string(out), "\n", 2)[0], "(GnuPG) 1.") {
		t.Skip("GnuPG 1 is needed: GnuPG 2 cannot read a PGP 2 key")
	}
	home := t.TempDir()
	if err := os.Chmod(home, 0o700); err != nil {
		t.Fatal(err)
	}
	imp := exec.Command(gpg, "--batch", "--homedir", home, "--allow-non-selfsigned-uid", "--import")
	imp.Stdin = bytes.NewReader(iscOldKey())
	if out, err := imp.CombinedOutput(); err != nil {
		t.Fatalf("import of the old-style key: %v\n%s", err, out)
	}
	keyring := filepath.Join(t.TempDir(), "empty.gpg")
	if err := os.WriteFile(keyring, nil, 0o644); err != nil {
		t.Fatal(err)
	}
	for _, kind := range iscNames {
		tmp := t.TempDir()
		cmd := exec.Command(filepath.Join(bin(t), "pgpverify"), "-d2", "-k", keyring)
		cmd.Env = append(os.Environ(), "TMPDIR="+tmp)
		cmd.Stdin = strings.NewReader(strings.Join(iscArticle(t, kind, "old"), "\n") + "\n")
		_ = cmd.Run() // fails: the keyring is empty. We want the files it kept.
		sig, _ := filepath.Glob(filepath.Join(tmp, "pgpverify.sig.*"))
		msg, _ := filepath.Glob(filepath.Join(tmp, "pgpverify.msg.*"))
		if len(sig) != 1 || len(msg) != 1 {
			t.Fatalf("%s: pgpverify -d2 kept %v and %v", kind, sig, msg)
		}
		out, err := exec.Command(gpg, "--batch", "--homedir", home, "--allow-non-selfsigned-uid", "--verify", sig[0], msg[0]).CombinedOutput()
		if err != nil || !strings.Contains(string(out), `Good signature from "news.announce.newgroups"`) {
			t.Errorf("%s-old: gpg --verify: %v\n%s", kind, err, out)
		}
	}
}

// iscDcontrol is dcontrol.ctl as samples/dcontrol.ctl has it for ISC: the control messages of
// group-admin@isc.org are acted on when the signature is by news.announce.newgroups.
const iscDcontrol = `checkgroups:group-admin@isc.org:*:verify-news.announce.newgroups
newgroup:group-admin@isc.org:comp.*|misc.*|news.*|rec.*|sci.*|soc.*|talk.*|humanities.*:verify-news.announce.newgroups
rmgroup:group-admin@isc.org:comp.*|misc.*|news.*|rec.*|sci.*|soc.*|talk.*|humanities.*:verify-news.announce.newgroups
`

// iscSite starts a reader site that acts on the ISC control messages when the PGP signature
// verifies against the keyring.
func iscSite(t *testing.T, groups []string) (*Site, *fakeserver.Store, string) {
	t.Helper()
	return iscSiteCtl(t, groups, iscDcontrol)
}

// iscSiteCtl is iscSite with the given dcontrol.ctl.
func iscSiteCtl(t *testing.T, groups []string, dcontrol string) (*Site, *fakeserver.Store, string) {
	t.Helper()
	if !haveGpgv() {
		t.Skip("no gpgv")
	}
	keyring := iscKeyring(t)
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:   spool,
		Groups:  groups,
		Files:   map[string]string{"dcontrol.ctl": dcontrol},
		Config:  map[string]string{"pgpverifyargs": "-k " + keyring},
		NoStart: true,
	})
	// dreaderd runs path_home/dbin/pgpverify
	linkBin(t, s)
	_, port, err := net.SplitHostPort(s.Addr)
	if err != nil {
		t.Fatal(err)
	}
	startReader(t, s, "[127.0.0.1]:"+port)
	return s, st, keyring
}

// iscList returns the lines of "LIST <what>" of the reader.
func iscList(t *testing.T, s *Site, what string) []string {
	t.Helper()
	c := s.Reader(t)
	defer expect(t, c, 205, "QUIT")
	expect(t, c, 215, "LIST %s", what)
	return c.ReadDotLines()
}

// iscDescriptions returns group -> description of LIST NEWSGROUPS lines, for the groups in only
// (all when empty); the white space between name and description is not compared.
func iscDescriptions(lines []string, only ...string) map[string]string {
	m := map[string]string{}
	for _, l := range lines {
		name, desc, _ := strings.Cut(l, "\t")
		if name == "" || (len(only) > 0 && !containsLine(only, name)) {
			continue
		}
		m[name] = strings.TrimSpace(desc)
	}
	return m
}

// iscGroupNames returns the group names of LIST ACTIVE lines.
func iscGroupNames(lines []string) map[string]bool {
	m := map[string]bool{}
	for _, l := range lines {
		if f := strings.Fields(l); len(f) > 0 {
			m[f[0]] = true
		}
	}
	return m
}

// iscRefusedWait is how long a refused control message is given to (wrongly) take effect.
const iscRefusedWait = 4 * time.Second

// iscExpected returns the descriptions of the groups in only that the body of the checkgroups
// fixture gives (lines of name, white space, description).
func iscExpected(t *testing.T, art []string, only ...string) map[string]string {
	t.Helper()
	_, body := splitArticle(art)
	m := map[string]string{}
	for _, l := range body {
		f := strings.Fields(l)
		if len(f) < 2 || !containsLine(only, f[0]) {
			continue
		}
		m[f[0]] = strings.TrimSpace(strings.TrimPrefix(l, f[0]))
	}
	for _, g := range only {
		if m[g] == "" {
			t.Fatalf("the checkgroups fixture has no line for %s", g)
		}
	}
	return m
}

// TestISCControlCheckgroups: the ISC checkgroups message is acted on when its PGP signature
// verifies: the descriptions of the groups the reader has are updated, and the groups it names
// that the reader lacks are not created. The old-style signed copy does the same on a platform
// whose keyring has the old-style key, and is refused otherwise.
func TestISCControlCheckgroups(t *testing.T) {
	have := []string{"news.announce.newgroups", "news.groups", "comp.ai", "comp.ai.genetic", "comp.admin.policy"}
	absent := "comp.ai.neural-nets" // in the ISC list, not on the reader
	for _, style := range []string{"modern", "old"} {
		t.Run(style, func(t *testing.T) {
			s, st, keyring := iscSite(t, have)
			art := iscArticle(t, "checkgroups", style)
			want := iscExpected(t, art, "comp.ai", "comp.ai.genetic", "comp.admin.policy")
			if iscDescriptions(iscList(t, s, "NEWSGROUPS"), "comp.ai")["comp.ai"] == want["comp.ai"] {
				t.Fatal("comp.ai already has the ISC description before the message")
			}
			accepted := style == "modern" || iscVerified(t, keyring, "checkgroups", "old")
			feedReader(t, s, st, "news.announce.newgroups", art)

			if accepted {
				var got map[string]string
				waitFor(t, 20*time.Second, "the ISC descriptions in LIST NEWSGROUPS", func() bool {
					got = iscDescriptions(iscList(t, s, "NEWSGROUPS"), "comp.ai", "comp.ai.genetic", "comp.admin.policy")
					return got["comp.ai"] == want["comp.ai"] && got["comp.ai.genetic"] == want["comp.ai.genetic"] &&
						got["comp.admin.policy"] == want["comp.admin.policy"]
				})
				for g, d := range want {
					if got[g] != d {
						t.Errorf("%s: description %q, want %q", g, got[g], d)
					}
				}
			} else {
				time.Sleep(iscRefusedWait)
				got := iscDescriptions(iscList(t, s, "NEWSGROUPS"), "comp.ai")
				if got["comp.ai"] == want["comp.ai"] {
					t.Errorf("the checkgroups signed with a key the keyring lacks was acted on: comp.ai has %q", got["comp.ai"])
				}
			}
			if iscGroupNames(iscList(t, s, "ACTIVE"))[absent] {
				t.Errorf("checkgroups created %s", absent)
			}
		})
	}
}

// TestISCControlCheckgroupsAll: with /maxdocheckgroups/ raised, the real ISC checkgroups (about
// 1,900 groups) is acted on as INN's docheckgroups does it: every group of the list is created,
// the ones with "(Moderated)" as moderated groups; a group of the hierarchies of the list that it
// lacks is removed, and the groups of other hierarchies stay. With the default limit of 10, none
// of this is done (TestISCControlCheckgroups).
func TestISCControlCheckgroupsAll(t *testing.T) {
	have := []string{"news.announce.newgroups", "comp.ai", "comp.local.test", "local.keep"}
	s, st, _ := iscSiteCtl(t, have, "/maxdocheckgroups/:*:*:100000\n"+iscDcontrol)
	art := iscArticle(t, "checkgroups", "modern")
	_, body := splitArticle(art)
	want := map[string]string{}
	for _, l := range body {
		f := strings.Fields(l)
		if len(f) < 2 || !strings.Contains(f[0], ".") || f[0] != strings.ToLower(f[0]) {
			continue
		}
		flag := "y"
		if strings.Contains(l, "(Moderated)") {
			flag = "m"
		}
		want[f[0]] = flag
	}
	if len(want) < 1500 {
		t.Fatalf("only %d groups in the checkgroups fixture", len(want))
	}
	feedReader(t, s, st, "news.announce.newgroups", art)

	var act map[string]string
	waitFor(t, 60*time.Second, "the groups of the list in LIST ACTIVE", func() bool {
		act = cgActive(t, s)
		_, ok := act["comp.lang.c"]
		return ok
	})
	missing, wrong := 0, 0
	for g, flag := range want {
		got, ok := act[g]
		if !ok {
			missing++
		} else if got != flag {
			wrong++
		}
	}
	if missing > 0 || wrong > 0 {
		t.Errorf("of %d groups in the list, %d were not created and %d have the wrong flag", len(want), missing, wrong)
	}
	if act["comp.ai"] != "m" {
		t.Errorf("comp.ai: flag %q, want m (Moderated)", act["comp.ai"])
	}
	if _, ok := act["comp.local.test"]; ok {
		t.Error("comp.local.test (comp is in the list, the group is not) was not removed")
	}
	if act["local.keep"] != "y" {
		t.Errorf("local.keep (another hierarchy): %q, want it untouched", act["local.keep"])
	}
}

// TestISCControlNewgroup: the ISC newgroup message for comp.lang.go creates the group, with the
// description of the message, when its PGP signature verifies; the old-style signed copy only
// where the keyring has the old-style key.
func TestISCControlNewgroup(t *testing.T) {
	const group = "comp.lang.go"
	const desc = "The Go programming language (Golang)."
	for _, style := range []string{"modern", "old"} {
		t.Run(style, func(t *testing.T) {
			s, st, keyring := iscSite(t, []string{"news.announce.newgroups"})
			art := iscArticle(t, "newgroup", style)
			if iscGroupNames(iscList(t, s, "ACTIVE"))[group] {
				t.Fatalf("%s exists before the message", group)
			}
			accepted := style == "modern" || iscVerified(t, keyring, "newgroup", "old")
			feedReader(t, s, st, "news.announce.newgroups", art)

			if accepted {
				waitFor(t, 20*time.Second, group+" in LIST ACTIVE", func() bool {
					return iscGroupNames(iscList(t, s, "ACTIVE"))[group]
				})
				c := s.Reader(t)
				defer expect(t, c, 205, "QUIT")
				expect(t, c, 211, "GROUP %s", group)
				if got := iscDescriptions(iscList(t, s, "NEWSGROUPS"), group)[group]; got != desc {
					t.Errorf("%s: description %q, want %q", group, got, desc)
				}
			} else {
				time.Sleep(iscRefusedWait)
				if iscGroupNames(iscList(t, s, "ACTIVE"))[group] {
					t.Errorf("the newgroup signed with a key the keyring lacks created %s", group)
				}
			}
		})
	}
}

// TestISCControlRmgroup: the ISC rmgroup message for soc.sexuality.general removes the group when
// its PGP signature verifies; the old-style signed copy only where the keyring has the old-style key.
func TestISCControlRmgroup(t *testing.T) {
	const group = "soc.sexuality.general"
	for _, style := range []string{"modern", "old"} {
		t.Run(style, func(t *testing.T) {
			s, st, keyring := iscSite(t, []string{"news.announce.newgroups", group, "soc.culture.usa"})
			art := iscArticle(t, "rmgroup", style)
			if !iscGroupNames(iscList(t, s, "ACTIVE"))[group] {
				t.Fatalf("%s does not exist before the message", group)
			}
			accepted := style == "modern" || iscVerified(t, keyring, "rmgroup", "old")
			feedReader(t, s, st, "news.announce.newgroups", art)

			if accepted {
				waitFor(t, 20*time.Second, group+" gone from LIST ACTIVE", func() bool {
					return !iscGroupNames(iscList(t, s, "ACTIVE"))[group]
				})
				c := s.Reader(t)
				defer expect(t, c, 205, "QUIT")
				if code, line := c.Cmd("GROUP %s", group); code != 411 {
					t.Errorf("GROUP %s: %d %q, want 411", group, code, line)
				}
				if !iscGroupNames(iscList(t, s, "ACTIVE"))["soc.culture.usa"] {
					t.Error("rmgroup removed another group")
				}
			} else {
				time.Sleep(iscRefusedWait)
				if !iscGroupNames(iscList(t, s, "ACTIVE"))[group] {
					t.Errorf("the rmgroup signed with a key the keyring lacks removed %s", group)
				}
			}
		})
	}
}
