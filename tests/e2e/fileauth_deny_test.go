package e2e

import (
	"net"
	"os"
	"path/filepath"
	"testing"

	"github.com/go-while/nntp-testkit/proc"
)

// TestReaderFileDenyListFdLeak (D-168): a deny-list file authdef ("file !path")
// must fclose() the file when it finds the user, or every rejected login leaks
// one descriptor in the long-lived DNS worker.  With a 64-descriptor limit the
// worker then cannot fopen() the list and refuses every later login.
//
// The test rejects the listed user 100 times, then requires a user that is not
// in the deny list (and therefore allowed) to authenticate.  Base leaks and
// fails the last login; with the fix it succeeds.
func TestReaderFileDenyListFdLeak(t *testing.T) {
	deny := filepath.Join(t.TempDir(), "deny")
	if err := os.WriteFile(deny, []byte("fred:pw\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	access := `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef byfile
    file !` + deny + `
    user alice
    pass anything
end
readerdef rf
    read yes
    auth byfile
end
access 127.0.0.1 rf
`
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access, NoStart: true})
	_, port, err := net.SplitHostPort(s.Addr)
	if err != nil {
		t.Fatal(err)
	}
	s.Proc = proc.StartTCP(t, proc.Options{
		Path: filepath.Join(s.Bin, "dreaderd"),
		Args: []string{"-d", "-p", PathHost, "-h", PathHost,
			"-P", "[127.0.0.1]:" + port},
		Env:    s.Env,
		Dir:    s.Dir,
		Ulimit: []string{"-n 64"},
	}, s.Addr)

	const n = 100
	for i := 0; i < n; i++ {
		c := s.Reader(t)
		expect(t, c, 381, "AUTHINFO USER fred")
		code, line := c.Cmd("AUTHINFO PASS pw")
		if code == 281 {
			t.Fatalf("login %d: the denied user was accepted: %s", i, line)
		}
		c.Close()
	}

	// A user that is not in the deny list is allowed.  If the worker leaked
	// a FILE per rejection it can no longer open the list here, and this
	// login is refused as well.
	c := s.Reader(t)
	expect(t, c, 381, "AUTHINFO USER alice")
	expect(t, c, 281, "AUTHINFO PASS anything")
	expect(t, c, 205, "QUIT")
}
