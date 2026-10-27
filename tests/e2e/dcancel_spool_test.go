package e2e

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func dcancelRun(t *testing.T, s *Site, in string, args ...string) string {
	cmd := exec.Command(filepath.Join(s.Bin, "dcancel"), args...)
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	cmd.Stdin = strings.NewReader(in)
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("dcancel %v: %v\n%s", args, err, out)
	}
	return string(out)
}

// D-67: dcancel -a and -s: cancelling the first of three articles stored in one spool file must leave the
// other two readable.
func TestDcancelSpoolOnlyUpdatesHistory(t *testing.T) {
	for _, mode := range []string{"-a", "-s"} {
		t.Run(mode, func(t *testing.T) {
			s := NewSite(t, SiteOptions{})
			ids := s.Inject(t, "e2e.test", 3)
			out := dcancelRun(t, s, ids[0]+"\n", mode)
			t.Logf("dcancel %s:\n%s", mode, out)
			p := s.Peer(t)
			for i, id := range ids {
				code, line := p.Cmd("STAT %s", id)
				t.Logf("STAT %d -> %d %s", i, code, line)
				if i > 0 && code != 223 {
					t.Errorf("article %d (%s) after dcancel %s: %d %s", i, id, mode, code, line)
				}
				if i == 0 && code == 223 {
					t.Errorf("cancelled article still there: %d %s", code, line)
				}
			}
			for i, id := range ids[1:] {
				code, line := p.Cmd("ARTICLE %s", id)
				t.Logf("ARTICLE %d -> %d %s", i+1, code, line)
				if code == 220 {
					body := p.ReadDotLines()
					if !strings.Contains(strings.Join(body, "\n"), id) {
						t.Errorf("ARTICLE %s returned a different article:\n%s", id, strings.Join(body, "\n"))
					}
				}
			}
		})
	}
}

