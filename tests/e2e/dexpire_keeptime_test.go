package e2e

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// TestDexpireKeeptimeTwoSpools (D-76): dexpire recomputes a spool's "age" (age of its oldest
// remaining directory) after a removal from the NEXT entry of the globally sorted
// directory list, which belongs to another spool when the spools' directories interleave.
// A directory younger than keeptime is then removed.
//
// Spool 01 holds directories 180, 42 and 12 minutes old, spool 02 holds 120 and 90 minutes
// old, keeptime is 60 minutes for both. Only the 180- and the 120-minute directories are
// older than keeptime. (Each spool's newest directory is never removed.)
func TestDexpireKeeptimeTwoSpools(t *testing.T) {
	now := time.Now().Unix() / 60
	type dir struct {
		spool string
		ageM  int64
		old   bool // older than keeptime, may go
	}
	dirs := []dir{
		{"P.01", 180, true}, {"P.01", 42, false}, {"P.01", 12, false},
		{"P.02", 120, true}, {"P.02", 90, false},
	}
	s := NewSite(t, SiteOptions{
		NoStart: true,
		SpoolCtl: `spool 01
    keeptime 3600
end
spool 02
    keeptime 3600
end
metaspool m
    spool 01
    spool 02
end
expire * m
`,
		Setup: func(tb testing.TB, s *Site) {
			for _, d := range dirs {
				p := filepath.Join(s.Dir, "spool", "news", d.spool, fmt.Sprintf("D.%08x", now-d.ageM))
				if err := os.MkdirAll(p, 0o755); err != nil {
					tb.Fatal(err)
				}
			}
		},
	})
	out := s.Run(t, "dexpire", "-a", "-h0", "-v")
	for _, d := range dirs {
		p := filepath.Join(s.Dir, "spool", "news", d.spool, fmt.Sprintf("D.%08x", now-d.ageM))
		_, err := os.Stat(p)
		gone := err != nil
		switch {
		case d.old && !gone:
			t.Errorf("%s (%d min old, keeptime 60) was not removed", p, d.ageM)
		case !d.old && gone:
			t.Errorf("%s is only %d minutes old (keeptime 60 minutes) but dexpire removed it", p, d.ageM)
		}
	}
	if t.Failed() {
		t.Logf("dexpire output:\n%s", out)
	}
}
