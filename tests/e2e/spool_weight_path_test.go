package e2e

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// TestSpoolDefaultWeightPath: a spool in dspool.ctl without "weight" gets the size of its file system as
// its weight. addSpool() asked statfs about the spool's path as written in dspool.ctl (P.01), relative to
// the working directory of the program (dexpire, diablo and dreaderd do not run in the spool directory
// when they read dspool.ctl), so every program logged "unable to statfs P.01 (No such file or
// directory)" for each spool, at every start, and the weight was 1 instead.
func TestSpoolDefaultWeightPath(t *testing.T) {
	s := NewSite(t, SiteOptions{
		NoStart: true,
		Config:  map[string]string{"path_generallog": "%s/general.log"},
		SpoolCtl: `spool 01
    maxsize	50g
    keeptime	30d
end
spool 02
    minfree	150g
end
metaspool binaries
    spool       02
    arttypes	binary
end
metaspool nonbinaries
    spool	01
    arttypes	!binary
end
expire * nonbinaries
expire * binaries
`,
		Setup: func(tb testing.TB, s *Site) {
			for _, d := range []string{"P.01", "P.02"} {
				if err := os.MkdirAll(filepath.Join(s.Dir, "spool", "news", d), 0o755); err != nil {
					tb.Fatal(err)
				}
			}
		},
	})
	out := s.Run(t, "dexpire", "-a", "-h0", "-v")
	b, _ := os.ReadFile(filepath.Join(s.Dir, "log", "general.log"))
	if strings.Contains(string(b), "unable to statfs") || strings.Contains(out, "unable to statfs") {
		t.Errorf("dexpire could not statfs the spools:\n%s\n%s", b, out)
	}
}
