package e2e

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// TestOverToolsHierMD5LikeNames (D-335, go-while's #519 r2 review): ExtractGroupHashInfoGz()
// (lib/hash.c) tried the md5 name patterns first, and the hierarchy names of a group whose name
// starts like "o.<n>.<n>" or "d.<n>.<n>" have their form: the over file o.0.o.1.2 of the group
// o.1.2 read as the md5 over file of the hash "o", its data file d.0.o.1.2 as that of the hash
// "d", and the files o.0.d.5.6.x and d.0.d.5.6.x of d.5.6.x as md5 data files. In a hierarchy
// spool no group has such a hash, so `dexpireover -a` and `doverctl clean` removed the live
// files of these groups as of no group, and dexpireover did not recognize their -R temporary
// files. The parse now tries the names of the configured layout first.
//
// Input: the groups e2e.test, o.1.2 and d.5.6.x, two articles crossposted to all three (each
// group gets its over file and the data file of the base 0), dreaderd stopped. Subtest
// dexpireover: in the directory of o.1.2 its temporary names ".<over name>" and ".<data name>"
// (left by an interrupted -R), and with the hierarchy layouts the over file o.0.o.1.9 of a group
// o.1.9 that does not exist (same directory); `dexpireover -a -v`. Subtest doverctl_clean (a
// site of its own): `doverctl -v clean`. Then dreaderd is restarted. Cases: crc, md5-32\1,
// hierarchy and hierarchy-full, each with both subtests. Expected: the over and data files of
// the three groups are there; nothing is reported as "not found, removing", "stale overview
// data", "bad file header" or "Deleting unknown" for them; dexpireover removes the two
// temporary files ("Removing old temp file: <path>") and o.0.o.1.9 ("Group for over file not
// found, removing"); after the restart OVER 1-2 of each group gives the same lines as before.
// **Fails** on e19924fb (main) with hierarchy and hierarchy-full, with both tools: the four
// files of o.1.2 and d.5.6.x are removed as "Group for over file not found", and dexpireover
// leaves the temporary files. crc and md5 pass there too.
func TestOverToolsHierMD5LikeNames(t *testing.T) {
	groups := []string{"e2e.test", "o.1.2", "d.5.6.x"}
	for _, tc := range []struct{ name, hash string }{
		{"crc", ""},
		{"md5", `md5-32\1`},
		{"hierarchy", "hierarchy"},
		{"hierarchy_full", "hierarchy-full"},
	} {
		for _, tool := range []string{"dexpireover", "doverctl_clean"} {
			t.Run(tc.name+"/"+tool, func(t *testing.T) {
				st := fakeserver.NewStore()
				for _, g := range groups {
					st.AddGroup(g, "D-335 test group")
				}
				spool := fakeserver.New(t, fakeserver.Options{Store: st})
				var cfg map[string]string
				if tc.hash != "" {
					cfg = map[string]string{"readergrouphash": tc.hash}
				}
				s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups, Config: cfg})
				for i := 1; i <= 2; i++ {
					feedReader(t, s, st, "e2e.test", newArticle(msgID(t), strings.Join(groups, ","),
						fmt.Sprintf("md5-like names %d", i), "body"))
				}
				before := map[string][]string{}
				for _, g := range groups {
					if before[g] = readOver(t, s, g, 2); len(before[g]) != 2 {
						t.Fatalf("OVER 1-2 of %s: %q, want 2 lines", g, before[g])
					}
				}
				s.Proc.Stop()

				home := strings.TrimSpace(s.Run(t, "dpath", "group"))
				var live []string
				var dir, over string // the directory and over file name of o.1.2
				for _, g := range groups {
					rel := cleanDirsOver(t, s, g)
					d, o := filepath.Join(home, filepath.Dir(rel)), filepath.Base(rel)
					if g == "o.1.2" {
						dir, over = d, o
					}
					live = append(live, filepath.Join(d, o), filepath.Join(d, tfDataName(t, o, 0)))
				}
				for _, f := range live {
					if _, err := os.Stat(f); err != nil {
						t.Fatalf("before %s: %v", tool, err)
					}
				}
				// said: a line of out starts with prefix and ends in path below the group home
				// (the tools print the group home as they expand it)
				said := func(out, prefix, path string) bool {
					for _, l := range strings.Split(out, "\n") {
						if strings.HasPrefix(l, prefix) && strings.HasSuffix(l, strings.TrimPrefix(path, home)) {
							return true
						}
					}
					return false
				}

				var out string
				var allowed []string
				if tool == "dexpireover" {
					temps := []string{"." + over, "." + tfDataName(t, over, 0)}
					for _, n := range temps {
						writeOver(t, filepath.Join(dir, n), []byte("left by an interrupted run: "+n+"\n"))
					}
					var noGroup string
					if strings.HasPrefix(tc.hash, "hierarchy") {
						// o.1.9 has the directory of o.1.2 in both hierarchy layouts
						noGroup = filepath.Join(dir, "o.0.o.1.9")
						writeOver(t, noGroup, hierOverFile("o.1.9", 1, 500))
						allowed = []string{strings.TrimPrefix(noGroup, home)}
					}
					out = s.Run(t, "dexpireover", "-a", "-v")
					for _, n := range temps {
						p := filepath.Join(dir, n)
						if _, err := os.Lstat(p); !os.IsNotExist(err) {
							t.Errorf("the temporary file %s of o.1.2 is still there (%v)", n, err)
						}
						if !said(out, "Removing old temp file: ", p) {
							t.Errorf("dexpireover did not report the removal of %s", n)
						}
					}
					if noGroup != "" {
						if _, err := os.Stat(noGroup); err == nil {
							t.Errorf("o.0.o.1.9, the over file of no group, is still there")
						}
						if !said(out, "Group for over file not found, removing ", noGroup) {
							t.Errorf("dexpireover did not report o.0.o.1.9 as the file of no group")
						}
					}
				} else {
					out = s.Run(t, "doverctl", "-v", "clean")
				}
				for _, f := range live {
					if _, err := os.Stat(f); err != nil {
						t.Errorf("after %s: the group file %s is gone: %v", tool, strings.TrimPrefix(f, home+"/"), err)
					}
				}
				hierNoBadReport(t, out, allowed)
				for _, l := range strings.Split(out, "\n") {
					if strings.HasPrefix(l, "Deleting unknown") {
						t.Errorf("%s reports: %s", tool, l)
					}
				}
				if t.Failed() {
					t.Logf("%s:\n%s", tool, out)
				}

				restartReader(t, s)
				for _, g := range groups {
					if after := readOver(t, s, g, 2); strings.Join(after, "\n") != strings.Join(before[g], "\n") {
						t.Errorf("OVER 1-2 of %s after %s differs:\nbefore %q\nafter  %q", g, tool, before[g], after)
					}
				}
			})
		}
	}
}
