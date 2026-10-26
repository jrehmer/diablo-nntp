package e2e

import (
	"bytes"
	_ "embed"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// The fixtures are embedded, so a test binary built with go test -c works from any directory.
var (
	//go:embed testdata/pgp/good.art
	pgpGoodArt []byte
	//go:embed testdata/pgp/trustedkeys.gpg
	pgpKeyring []byte
)

// haveGpgv reports whether pgpverify will find gpgv (it looks in these directories only).
func haveGpgv() bool {
	for _, d := range []string{"/usr/bin", "/usr/local/bin", "/opt/local/bin", "/usr/pkg/bin", "/opt/ooce/bin"} {
		for _, n := range []string{"gpgv2", "gpgv", "gpgv1"} {
			if _, err := os.Stat(filepath.Join(d, n)); err == nil {
				return true
			}
		}
	}
	return false
}

// runPgpverify runs pgpverify with the test keyring on art and returns stdout, stderr and the
// exit status.
func runPgpverify(t *testing.T, art []byte) (string, string, int) {
	t.Helper()
	keyring := filepath.Join(t.TempDir(), "trustedkeys.gpg")
	if err := os.WriteFile(keyring, pgpKeyring, 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(filepath.Join(bin(t), "pgpverify"), "-k", keyring)
	cmd.Stdin = bytes.NewReader(art)
	var out, errb bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &errb
	err := cmd.Run()
	code := 0
	var ee *exec.ExitError
	if errors.As(err, &ee) {
		code = ee.ExitCode()
	} else if err != nil {
		t.Fatal(err)
	}
	return out.String(), errb.String(), code
}

// TestPgpverify: pgpverify (GnuPG gpgv) accepts a correctly signed control message and names
// its signer, and refuses everything where the text it verifies could differ from what
// dreaderd acts on: a changed signed header, a signed header (or X-PGP-Sig) that occurs
// twice in any letter case, a NUL byte, a bare CR. Without the fixes for GHSA-3g54-68r8-rx6c
// the duplicate-header and NUL cases verified as good. Skipped without gpgv.
func TestPgpverify(t *testing.T) {
	if !haveGpgv() {
		t.Skip("no gpgv")
	}
	good := pgpGoodArt
	head, body, ok := bytes.Cut(good, []byte("\n\n"))
	if !ok {
		t.Fatal("good.art has no blank line")
	}
	withHeader := func(h string) []byte { // an extra header after the others
		return []byte(string(head) + "\n" + h + "\n\n" + string(body))
	}
	const signer = "news.announce.newgroups"
	cases := []struct {
		name     string
		art      []byte
		code     int
		stderr   string // must contain, if set
		accepted bool
	}{
		{"good", good, 0, "", true},
		{"changed signed header", bytes.Replace(good, []byte("Control: newgroup e2e.test.group"), []byte("Control: newgroup e2e.test.evil"), 1), 3, "", false},
		{"duplicate Control", withHeader("Control: rmgroup comp.lang.c"), 3, "duplicate signed header Control", false},
		{"duplicate control, lower case", withHeader("control: rmgroup comp.lang.c"), 3, "duplicate signed header Control", false},
		{"duplicate From", withHeader("From: someone@example.invalid"), 3, "duplicate signed header From", false},
		{"duplicate X-PGP-Sig", withHeader("X-PGP-Sig: GnuPG2 Subject AAAA"), 3, "duplicate X-PGP-Sig", false},
		{"NUL in the body", append(append([]byte{}, good...), []byte("\x00\nalt.evil\tInjected group.\n")...), 3, "NUL byte", false},
		{"bare CR in the body", bytes.Replace(good, []byte("unmoderated group"), []byte("unmoderated\rgroup"), 1), 3, "bare CR", false},
		{"no signature", bytes.Replace(good, []byte("X-PGP-Sig:"), []byte("X-Not-Sig:"), 1), 1, "", false},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			out, errOut, code := runPgpverify(t, c.art)
			if code != c.code {
				t.Errorf("exit status %d, want %d (stdout %q, stderr %q)", code, c.code, out, errOut)
			}
			if c.accepted && strings.TrimSpace(out) != signer {
				t.Errorf("stdout %q, want the signer %q", out, signer)
			}
			if !c.accepted && out != "" {
				t.Errorf("stdout %q, want nothing (lib/pgp.c takes any output as a good signature)", out)
			}
			if c.stderr != "" && !strings.Contains(errOut, c.stderr) {
				t.Errorf("stderr %q, want %q", errOut, c.stderr)
			}
		})
	}
}
