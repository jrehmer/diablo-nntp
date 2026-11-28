package e2e

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/fakeserver"
)

// D-336: a hierarchy file name holds the whole group name, but `hierarchy` and `hierarchy-full`
// put the files of many groups in different directories. dexpireover and doverctl clean took a
// file as the group's whatever directory it was in; now it is the group's only in the directory
// GFName() gives the group (or the same directory by another path), as long as the group has its
// over file there. These tests switch a spool between the two layouts by editing
// readergrouphash, as an admin can (doverctl convert refuses it, D-325), and check what the two
// tools do with the copies left in the old directories.

// swGroups are the groups of the switch tests, with their directories below the group home in
// each layout.
var swGroups = []struct{ name, hier, full string }{
	{"e2e.test", "e2e", "e2e"},      // control: the same directory in both layouts
	{"e2e.x", "e2e", "e2e"},         // the same directory; X of the pair X and X.gz (D-318)
	{"e2e.x.gz", "e2e", "e2e/x"},    // moves
	{"a.b.c", "a.b", "a/b"},         // moves
	{"o.1.2", "o.1", "o/1"},         // moves; its names also have the form of md5 ones (D-335)
	{"e2e.q.quiet", "e2e", "e2e/q"}, // moves, but gets no article after the switch
}

const (
	swQuiet = "e2e.q.quiet"
	swArts  = 6 // articles before the switch, and as many after it
)

// swDir is the directory of group g below the group home in layout.
func swDir(g, layout string) string {
	for _, x := range swGroups {
		if x.name == g {
			if layout == "hierarchy-full" {
				return x.full
			}
			return x.hier
		}
	}
	return ""
}

// swNames are the names of the swGroups.
func swNames(skip string) []string {
	var out []string
	for _, x := range swGroups {
		if x.name != skip {
			out = append(out, x.name)
		}
	}
	return out
}

// swFiles are the regular files below the group home, by path.
func swFiles(t *testing.T, home string) map[string][]byte {
	t.Helper()
	m := map[string][]byte{}
	err := filepath.WalkDir(home, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.Type().IsRegular() {
			b, err := os.ReadFile(p)
			if err != nil {
				return err
			}
			m[p] = b
		}
		return nil
	})
	if err != nil {
		t.Fatalf("walk %s: %v", home, err)
	}
	return m
}

// swOver is GROUP g and OVER 1-12 of g on a running dreaderd: the GROUP reply and the OVER lines
// (none when OVER does not answer 224).
func swOver(t *testing.T, s *Site, g string) (string, []string) {
	t.Helper()
	c := s.Reader(t)
	defer c.Close()
	_, line := c.Cmd("GROUP %s", g)
	if !strings.HasPrefix(line, "211 ") {
		return line, nil
	}
	if code, _ := c.Cmd("OVER 1-%d", 2*swArts); code != 224 {
		return line, nil
	}
	return line, c.ReadDotLines()
}

// swFeed feeds n articles crossposted to groups.
func swFeed(t *testing.T, s *Site, st *fakeserver.Store, groups []string, n int, what string) {
	t.Helper()
	for i := 1; i <= n; i++ {
		feedReader(t, s, st, "e2e.test", newArticle(msgID(t), strings.Join(groups, ","),
			fmt.Sprintf("layout switch %s %02d", what, i), "body"))
	}
}

// swCheckOutput fails for each line of out that reports a removal or a problem, except the
// removals of the files in gone, each of which must be reported with "Group for over file not
// found, removing <path>".
func swCheckOutput(t *testing.T, tool, out string, gone []string) {
	t.Helper()
	const removing = "Group for over file not found, removing "
	want := map[string]bool{}
	for _, f := range gone {
		want[f] = true
	}
	said := map[string]bool{}
	bad := append([]string{"Deleting", "Removing"}, hfxBad...)
	for _, l := range strings.Split(out, "\n") {
		if strings.HasPrefix(l, removing) && want[strings.TrimPrefix(l, removing)] {
			said[strings.TrimPrefix(l, removing)] = true
			continue
		}
		for _, b := range bad {
			if strings.Contains(l, b) {
				t.Errorf("%s reports: %s", tool, l)
				break
			}
		}
	}
	for _, f := range gone {
		if !said[f] {
			t.Errorf("%s did not report %q%s", tool, removing, f)
		}
	}
}

// TestOverToolsHierarchyFullSwitchLeftovers: after a switch between `hierarchy` and
// `hierarchy-full` made by editing readergrouphash, the over and data files left in the old
// directories are not the files of any group: dexpireover -a and doverctl clean remove them as
// "Group for over file not found", and keep every file dreaderd uses.
//
// Input: the swGroups; readergrouphash <from>, dexpire.ctl "*:x30:e4"; 6 articles crossposted to
// all groups (bases 0 and 4); dreaderd stopped, readergrouphash changed to <to> (dgrpctl
// listgrouphashfile gives the new directory), dreaderd started, 6 more articles to all groups but
// e2e.q.quiet; GROUP and OVER 1-12 of every group read, dreaderd stopped. Subtests
// hierarchy_to_full and full_to_hierarchy, each with `dexpireover -a -v` and `doverctl -v clean`
// on a site of its own. Expected:
//   - before the tool runs, dreaderd uses only the files of the new layout: OVER 1-12 gives the
//     12 articles of e2e.test and e2e.x (the same directory in both layouts), only the articles
//     7-12 of e2e.x.gz, a.b.c and o.1.2, and nothing of e2e.q.quiet;
//   - the tool removes the over and data files of e2e.x.gz, a.b.c and o.1.2 in their old
//     directories, each reported with "Group for over file not found, removing <path>", and
//     reports nothing else (no other removal, no bad header, nothing unknown or failed);
//   - every other file is there and its contents unchanged (data files; over files: there). Two
//     kinds of old files stay, by design: with hierarchy_to_full the old plain data files
//     e2e/d.<n>.e2e.x.gz of e2e.x.gz, which in e2e/ are the compressed data name of e2e.x and are
//     kept while e2e.x has articles in those bases (D-318); and the old files of e2e.q.quiet,
//     which has no over file in its new directory: dreaderd reads readergrouphash only when it
//     starts, so one started before the change could still be writing them, and such files are
//     taken as the group's as before (`-v` says "taken as the group's: it has no over file in its
//     own directory yet");
//   - after a restart OVER 1-12 of every group is as before the tool ran (GROUP is logged).
//
// On main (c27f812d) both tools keep every old file (taken as the files of the group named in
// them); dexpireover also expires the old over file of a moved group after the new one, which
// moves NB past NE (StartNo=13 EndNo=12), so OVER of e2e.x.gz, a.b.c and o.1.2 is empty after the
// restart, and it removes d.4.e2e.q.quiet as misaligned (the data entries per file were read from
// the group's over file in its new directory, which is not there, so 256).
func TestOverToolsHierarchyFullSwitchLeftovers(t *testing.T) {
	for _, sw := range []struct{ name, from, to string }{
		{"hierarchy_to_full", "hierarchy", "hierarchy-full"},
		{"full_to_hierarchy", "hierarchy-full", "hierarchy"},
	} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(sw.name+"/"+tool, func(t *testing.T) {
				st := fakeserver.NewStore()
				all := swNames("")
				for _, g := range all {
					st.AddGroup(g, "layout switch test group")
				}
				spool := fakeserver.New(t, fakeserver.Options{Store: st})
				s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: all,
					Config: map[string]string{"readergrouphash": sw.from},
					Files:  map[string]string{"dexpire.ctl": "*:x30:e4\n"}})
				home := strings.TrimSpace(s.Run(t, "dpath", "group"))
				swFeed(t, s, st, all, swArts, "before")
				for _, g := range all {
					readOver(t, s, g, swArts)
				}
				s.Proc.Stop()

				// the files of each group in the old layout
				var gone []string
				for _, g := range all {
					rel := hierListPath(t, s, g)
					if want := swDir(g, sw.from) + "/o.0." + g; rel != want {
						t.Fatalf("listgrouphashfile %s with %s = %q, want %q", g, sw.from, rel, want)
					}
					if swDir(g, sw.from) == swDir(g, sw.to) || g == swQuiet {
						continue
					}
					dir := filepath.Join(home, swDir(g, sw.from))
					old := []string{filepath.Join(dir, "o.0."+g)}
					for _, b := range []int{0, 4} {
						old = append(old, filepath.Join(dir, fmt.Sprintf("d.%d.%s", b, g)))
					}
					for _, f := range old {
						if !hfxExists(f) {
							t.Fatalf("after the feed with %s: %s is missing", sw.from, f)
						}
						// in e2e/ of hierarchy-full "d.<n>.e2e.x.gz" is e2e.x's compressed name
						if sw.to == "hierarchy-full" && g == "e2e.x.gz" && strings.HasPrefix(filepath.Base(f), "d.") {
							continue
						}
						gone = append(gone, f)
					}
				}

				conf := s.ReadFile(t, "diablo.config")
				from := "readergrouphash\t" + sw.from + "\n"
				if !strings.Contains(conf, from) {
					t.Fatalf("diablo.config has no %q:\n%s", from, conf)
				}
				s.WriteFile(t, "diablo.config", strings.Replace(conf, from, "readergrouphash\t"+sw.to+"\n", 1))
				for _, g := range all {
					if rel, want := hierListPath(t, s, g), swDir(g, sw.to)+"/o.0."+g; rel != want {
						t.Fatalf("listgrouphashfile %s with %s = %q, want %q", g, sw.to, rel, want)
					}
				}
				restartReader(t, s)
				swFeed(t, s, st, swNames(swQuiet), swArts, "after")

				// dreaderd reads only the files of the new layout
				before := map[string][]string{}
				groupLine := map[string]string{}
				for _, g := range all {
					want := swArts // articles 7-12, in the new directory
					switch {
					case g == swQuiet:
						want = 0
					case swDir(g, sw.from) == swDir(g, sw.to):
						want = 2 * swArts
					}
					testkit.WaitFor(t, 10*time.Second, fmt.Sprintf("OVER of %s: %d lines", g, want), func() bool {
						groupLine[g], before[g] = swOver(t, s, g)
						return len(before[g]) == want
					})
				}
				s.Proc.Stop()

				files := swFiles(t, home)
				isGone := map[string]bool{}
				for _, f := range gone {
					isGone[f] = true
					if _, ok := files[f]; !ok {
						t.Fatalf("%s is not there before %s", f, tool)
					}
				}

				var out string
				var err error
				if tool == "dexpireover" {
					out, err = s.TryRun("dexpireover", "-a", "-v")
				} else {
					out, err = s.TryRun("doverctl", "-v", "clean")
				}
				if err != nil {
					t.Errorf("%s: %v", tool, err)
				}

				for _, f := range gone {
					if hfxExists(f) {
						t.Errorf("%s: the old copy %s is still there", tool, strings.TrimPrefix(f, home+"/"))
					}
				}
				var kept []string
				for f, b := range files {
					if isGone[f] {
						continue
					}
					kept = append(kept, strings.TrimPrefix(f, home+"/"))
					got, err := os.ReadFile(f)
					if err != nil {
						t.Errorf("%s: %s is gone: %v", tool, strings.TrimPrefix(f, home+"/"), err)
					} else if strings.HasPrefix(filepath.Base(f), "d.") && string(got) != string(b) {
						t.Errorf("%s: %s was changed", tool, strings.TrimPrefix(f, home+"/"))
					}
				}
				sort.Strings(kept)
				swCheckOutput(t, tool, out, gone)
				quietDir := filepath.Join(home, swDir(swQuiet, sw.from))
				if note := "file of " + swQuiet + " in " + quietDir + " taken as the group's: it has no over file in its own directory yet"; !strings.Contains(out, note) {
					t.Errorf("%s -v does not say %q", tool, note)
				}
				t.Logf("%s removed %d old files, kept %d: %q", tool, len(gone), len(kept), kept)
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}

				restartReader(t, s)
				for _, g := range all {
					line, over := swOver(t, s, g)
					if strings.Join(over, "\n") != strings.Join(before[g], "\n") {
						t.Errorf("after %s: OVER 1-12 of %s %q; before %q", tool, g, over, before[g])
					}
					if line != groupLine[g] {
						t.Logf("after %s: GROUP %s %q, before %q", tool, g, line, groupLine[g])
					}
				}
			})
		}
	}
}

// TestOverToolsHierarchySameDirControl: the controls of D-336, without a layout switch. With plain
// `hierarchy` the groups e2e.x and e2e.x.gz share the directory e2e/ (D-325), so both readings of
// "d.<n>.e2e.x.gz" (the plain data of e2e.x.gz, the compressed name of e2e.x) are in their
// group's directory and the D-318 rule stays; with `hierarchy-full` and crc every file is in its
// group's directory.
//
// Input: the swGroups but e2e.q.quiet, readergrouphash hierarchy, hierarchy-full or crc,
// dexpire.ctl "*:x30:e4", 12 articles crossposted to all (bases 0, 4, 8, 12), dreaderd stopped;
// `dexpireover -a -v`, then `doverctl -v clean`. Expected: neither tool removes, changes (data
// files) or reports anything; after a restart GROUP and OVER 1-12 of every group as before.
// Passes on main too.
func TestOverToolsHierarchySameDirControl(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full", "crc"} {
		t.Run(layout, func(t *testing.T) {
			st := fakeserver.NewStore()
			all := swNames(swQuiet)
			for _, g := range all {
				st.AddGroup(g, "layout control test group")
			}
			spool := fakeserver.New(t, fakeserver.Options{Store: st})
			s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: all,
				Config: map[string]string{"readergrouphash": layout},
				Files:  map[string]string{"dexpire.ctl": "*:x30:e4\n"}})
			home := strings.TrimSpace(s.Run(t, "dpath", "group"))
			swFeed(t, s, st, all, 2*swArts, "control")
			before := map[string][]string{}
			for _, g := range all {
				before[g] = readOver(t, s, g, 2*swArts)
			}
			s.Proc.Stop()
			files := swFiles(t, home)
			if layout == "hierarchy" {
				if _, ok := files[filepath.Join(home, "e2e", "d.0.e2e.x.gz")]; !ok {
					t.Fatalf("e2e/d.0.e2e.x.gz is missing")
				}
			}

			for _, run := range [][]string{{"dexpireover", "-a", "-v"}, {"doverctl", "-v", "clean"}} {
				out, err := s.TryRun(run[0], run[1:]...)
				if err != nil {
					t.Errorf("%s: %v", strings.Join(run, " "), err)
				}
				swCheckOutput(t, run[0], out, nil)
				for f, b := range files {
					got, err := os.ReadFile(f)
					if err != nil {
						t.Errorf("after %s: %s is gone: %v", run[0], strings.TrimPrefix(f, home+"/"), err)
					} else if base := filepath.Base(f); (strings.HasPrefix(base, "d.") || strings.HasPrefix(base, "data.")) && string(got) != string(b) {
						t.Errorf("after %s: %s was changed", run[0], strings.TrimPrefix(f, home+"/"))
					}
				}
				if t.Failed() {
					t.Logf("%s:\n%s", strings.Join(run, " "), out)
				}
			}

			restartReader(t, s)
			for _, g := range all {
				line, over := swOver(t, s, g)
				if !strings.HasPrefix(line, fmt.Sprintf("211 %d 1 %d ", 2*swArts, 2*swArts)) ||
					strings.Join(over, "\n") != strings.Join(before[g], "\n") {
					t.Errorf("after the tools: GROUP %s %q, OVER 1-12 %q; before %q", g, line, over, before[g])
				}
			}
		})
	}
}
