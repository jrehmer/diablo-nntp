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
// file as the group's whatever directory it was in. Now a reading of the name counts only in the
// group's directory of the configured layout; a file in the group's directory of the OTHER
// hierarchy layout is left alone (neither expired nor removed: a dreaderd started before
// readergrouphash was changed may still write it, D-337), and a file in neither is no file of
// that group. These tests switch a spool between the two layouts by editing readergrouphash, as
// an admin can (doverctl convert refuses it, D-325).

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

// swTopGroups have their files at the top of the group home in plain hierarchy (a component
// of one character is joined to the next, and the last is no directory), which neither tool
// scans (R2-F3 of the #523 r2 reviews).
var swTopGroups = []struct{ name, hier, full string }{
	{"x.y", "", "x"},
	{"a.b", "", "a"},
}

// swDir is the directory of group g below the group home in layout ("" for the top).
func swDir(g, layout string) string {
	for _, x := range append(append([]struct{ name, hier, full string }{}, swGroups...), swTopGroups...) {
		if x.name == g {
			if layout == "hierarchy-full" {
				return x.full
			}
			return x.hier
		}
	}
	return ""
}

// swNames are the names of the swGroups but skip.
func swNames(skip string) []string {
	var out []string
	for _, x := range swGroups {
		if x.name != skip {
			out = append(out, x.name)
		}
	}
	return out
}

// swFiles are the regular files below the group home, by path, with their contents.
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

// swRel is the path of the over file of g below the group home in layout, as dgrpctl
// listgrouphashfile gives it.
func swRel(g, layout string) string {
	if d := swDir(g, layout); d != "" {
		return d + "/o.0." + g
	}
	return "o.0." + g
}

// swOldFiles are the over file and the data files of the bases 0 and 4 of group g in its
// directory of layout below home.
func swOldFiles(home, g, layout string) []string {
	dir := filepath.Join(home, swDir(g, layout))
	return []string{filepath.Join(dir, "o.0."+g), filepath.Join(dir, "d.0."+g), filepath.Join(dir, "d.4."+g)}
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

// swSwitch changes readergrouphash of the stopped site s from one layout to the other and
// checks with dgrpctl listgrouphashfile that groups now have the directory of to.
func swSwitch(t *testing.T, s *Site, from, to string, groups []string) {
	t.Helper()
	conf := s.ReadFile(t, "diablo.config")
	old := "readergrouphash\t" + from + "\n"
	if !strings.Contains(conf, old) {
		t.Fatalf("diablo.config has no %q:\n%s", old, conf)
	}
	s.WriteFile(t, "diablo.config", strings.Replace(conf, old, "readergrouphash\t"+to+"\n", 1))
	for _, g := range groups {
		if rel, want := hierListPath(t, s, g), swRel(g, to); rel != want {
			t.Fatalf("listgrouphashfile %s with %s = %q, want %q", g, to, rel, want)
		}
	}
}

// swAloneLine is what -v prints for a file left alone in the directory of the other layout.
const swAloneLine = " in the directory of the other hierarchy layout, left alone: "

// swCheckOutput fails for each line of out that reports a removal or a problem (as hfxBad, and
// "Deleting", "Removing"), except the "left alone" lines of the files in alone, each of which
// must be reported so ("file of <group> in the directory of the other hierarchy layout, left
// alone: <path>"); with alone nil no such line is expected.
func swCheckOutput(t *testing.T, tool, out string, alone []string) {
	t.Helper()
	want := map[string]bool{}
	for _, f := range alone {
		want[f] = true
	}
	said := map[string]bool{}
	bad := append([]string{"Deleting", "Removing"}, hfxBad...)
	for _, l := range strings.Split(out, "\n") {
		if i := strings.Index(l, swAloneLine); strings.HasPrefix(l, "file of ") && i > 0 &&
			want[l[i+len(swAloneLine):]] {
			said[l[i+len(swAloneLine):]] = true
			continue
		}
		for _, b := range bad {
			if strings.Contains(l, b) {
				t.Errorf("%s reports: %s", tool, l)
				break
			}
		}
	}
	for _, f := range alone {
		if !said[f] {
			t.Errorf("%s -v did not say %q for %s", tool, swAloneLine, f)
		}
	}
}

// TestOverToolsHierarchyFullSwitchLeftovers: after a switch between `hierarchy` and
// `hierarchy-full` made by editing readergrouphash, the over and data files left in the old
// directories are left alone by dexpireover -a and doverctl clean (not expired, not removed),
// and the files dreaderd now uses are expired as usual.
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
//   - the old over file and data files d.0, d.4 of e2e.x.gz, a.b.c, o.1.2 and e2e.q.quiet are
//     there with the same bytes, and -v says "file of <group> in the directory of the other
//     hierarchy layout, left alone: <path>" for each; nothing is removed or reported otherwise;
//   - the other files are there, the data files unchanged; dexpireover takes the first article
//     of the moved groups to 7, the first one in their new over files (logged);
//   - after a restart OVER 1-12 of every group is as before the tool ran.
//
// On main (c27f812d) dexpireover expires the old over file of a moved group as well as the new
// one, which moves NB past NE (StartNo=13 EndNo=12): OVER of e2e.x.gz, a.b.c and o.1.2 is empty
// after the restart, the old data files are removed as stale and the old over files resized;
// doverctl keeps the old files, but without the -v line. On b14b5805 (r1) both tools remove the
// old copies of the groups that have their new over file.
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

				// the files of the moved groups in the old layout
				var alone []string
				for _, g := range all {
					if rel, want := hierListPath(t, s, g), swRel(g, sw.from); rel != want {
						t.Fatalf("listgrouphashfile %s with %s = %q, want %q", g, sw.from, rel, want)
					}
					if swDir(g, sw.from) == swDir(g, sw.to) {
						continue
					}
					for _, f := range swOldFiles(home, g, sw.from) {
						if !hfxExists(f) {
							t.Fatalf("after the feed with %s: %s is missing", sw.from, f)
						}
						alone = append(alone, f)
					}
				}

				swSwitch(t, s, sw.from, sw.to, all)
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
				isAlone := map[string]bool{}
				for _, f := range alone {
					isAlone[f] = true
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

				var kept []string
				for f, b := range files {
					rel := strings.TrimPrefix(f, home+"/")
					got, err := os.ReadFile(f)
					switch {
					case err != nil:
						t.Errorf("%s: %s is gone: %v", tool, rel, err)
					case isAlone[f] && string(got) != string(b):
						t.Errorf("%s: the old copy %s was changed", tool, rel)
					case strings.HasPrefix(filepath.Base(f), "d.") && string(got) != string(b):
						t.Errorf("%s: %s was changed", tool, rel)
					}
					kept = append(kept, rel)
				}
				sort.Strings(kept)
				swCheckOutput(t, tool, out, alone)
				t.Logf("%s left %d old files alone; files: %q", tool, len(alone), kept)
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

// TestOverToolsHierarchySwitchLiveOldLayout: go-while's and chatgpt-1's reviews of b14b5805
// (#523): a file in the group's directory of the other hierarchy layout may be the live file of
// a dreaderd started before readergrouphash was changed, also when the group has an over file in
// its directory of the configured layout. Such an over file appears without the new layout being
// in use: a remnant (here an empty file), `dcancel -r group:N` run after the change (dcancel
// opens the over file of the configured layout with O_CREAT), or the copy left by an earlier
// switch the other way. The tools must lose nothing of the files the old dreaderd uses, and
// dexpireover must not move the first article number (NB) of such a group by what its over file
// of the configured layout lacks. x.y and a.b have their files at the top of the group home in
// plain hierarchy, which the scan does not enter (R2-F3 of the #523 r2 reviews): their NB is
// kept by the over file found there, not by the scan.
//
// Input: groups e2e.test, e2e.x.gz, a.b.c, o.1.2, x.y and a.b; readergrouphash hierarchy,
// dexpire.ctl "*:x30:e4", 6 articles crossposted to all, OVER 1-6 of each read, dreaderd stopped
// (its files are as a running one would leave them); readergrouphash changed to hierarchy-full;
// then e2e/x/o.0.e2e.x.gz written empty, `dcancel -r` with "a.b.c:1", "x.y:1" and "a.b:1" (it
// makes the over files a/b/o.0.a.b.c, x/o.0.x.y and a/o.0.a.b), and o/1/o.0.o.1.2 a copy of
// o.1/o.0.o.1.2. Subtests dexpireover (`-a -v`) and doverctl (`-v clean`). Expected: the
// hierarchy files of every group but e2e.test (over file, d.0, d.4) are there with the same
// bytes; readergrouphash set back to hierarchy and dreaderd started, OVER 1-6 of every group is
// as before. Fails on b14b5805 (r1) for both tools: the old files are removed as "Group for
// over file not found". On ce927151 (r2) dexpireover fails for x.y and a.b (NB moved to 7, OVER
// empty). On main c27f812d doverctl passes; dexpireover changes the old over files (taken as the
// groups' and resized).
func TestOverToolsHierarchySwitchLiveOldLayout(t *testing.T) {
	groups := []string{"e2e.test", "e2e.x.gz", "a.b.c", "o.1.2", "x.y", "a.b"}
	for _, tool := range []string{"dexpireover", "doverctl"} {
		t.Run(tool, func(t *testing.T) {
			st := fakeserver.NewStore()
			for _, g := range groups {
				st.AddGroup(g, "layout switch test group")
			}
			spool := fakeserver.New(t, fakeserver.Options{Store: st})
			s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups,
				Config: map[string]string{"readergrouphash": "hierarchy"},
				Files:  map[string]string{"dexpire.ctl": "*:x30:e4\n"}})
			home := strings.TrimSpace(s.Run(t, "dpath", "group"))
			swFeed(t, s, st, groups, swArts, "live")
			before := map[string][]string{}
			for _, g := range groups {
				before[g] = readOver(t, s, g, swArts)
			}
			s.Proc.Stop()
			var old []string
			for _, g := range groups[1:] {
				old = append(old, swOldFiles(home, g, "hierarchy")...)
			}
			snap := hfxSnapshot(t, old)

			swSwitch(t, s, "hierarchy", "hierarchy-full", groups)
			// a remnant
			if err := os.MkdirAll(filepath.Join(home, "e2e", "x"), 0o755); err != nil {
				t.Fatal(err)
			}
			writeOver(t, filepath.Join(home, "e2e", "x", "o.0.e2e.x.gz"), nil)
			// dcancel after the change
			out, err := runWith(t, s, "a.b.c:1\nx.y:1\na.b:1\n", nil, "dcancel", "-r")
			t.Logf("dcancel -r a.b.c:1 x.y:1 a.b:1: %v\n%s", err, out)
			for _, g := range []string{"a.b.c", "x.y", "a.b"} {
				if f := filepath.Join(home, swRel(g, "hierarchy-full")); !hfxExists(f) {
					t.Errorf("dcancel -r did not make %s", swRel(g, "hierarchy-full"))
				}
			}
			// the copy of an earlier switch
			if err := os.MkdirAll(filepath.Join(home, "o", "1"), 0o755); err != nil {
				t.Fatal(err)
			}
			writeOver(t, filepath.Join(home, "o", "1", "o.0.o.1.2"), snap[filepath.Join(home, "o.1", "o.0.o.1.2")])

			if tool == "dexpireover" {
				out, err = s.TryRun("dexpireover", "-a", "-v")
			} else {
				out, err = s.TryRun("doverctl", "-v", "clean")
			}
			t.Logf("%s: %v\n%s", tool, err, out)
			for _, f := range old {
				got, err := os.ReadFile(f)
				if err != nil {
					t.Errorf("%s: %s of the old layout is gone: %v", tool, strings.TrimPrefix(f, home+"/"), err)
				} else if string(got) != string(snap[f]) {
					t.Errorf("%s: %s of the old layout was changed", tool, strings.TrimPrefix(f, home+"/"))
				}
			}

			swSwitch(t, s, "hierarchy-full", "hierarchy", groups)
			restartReader(t, s)
			for _, g := range groups {
				if _, over := swOver(t, s, g); strings.Join(over, "\n") != strings.Join(before[g], "\n") {
					t.Errorf("after %s, back with hierarchy: OVER of %s %q; before %q", tool, g, over, before[g])
				}
			}
		})
	}
}

// TestOverToolsHierarchyLinkedGroupDir: go-while's F2 on b14b5805: a group directory reached
// through a symlink below the group home ("lnk" -> "a/b") is the same directory by stat(), so
// the files seen there are the group's (as on main), although "lnk" is the directory of no group
// in either layout; without that check they would be removed as the files of no group, through
// the link, from the real directory.
//
// Input: readergrouphash hierarchy-full, groups e2e.test and a.b.c, dexpire.ctl "*:x30:e4", 6
// articles crossposted to both, dreaderd stopped, the symlink <group home>/lnk -> a/b. Subtests
// dexpireover (`-a -v`) and doverctl (`-v clean`): every file is still there, the data files
// unchanged, nothing removed or reported; after a restart OVER 1-6 of both groups as before.
// Passes on main c27f812d too.
func TestOverToolsHierarchyLinkedGroupDir(t *testing.T) {
	groups := []string{"e2e.test", "a.b.c"}
	for _, tool := range []string{"dexpireover", "doverctl"} {
		t.Run(tool, func(t *testing.T) {
			st := fakeserver.NewStore()
			for _, g := range groups {
				st.AddGroup(g, "linked directory test group")
			}
			spool := fakeserver.New(t, fakeserver.Options{Store: st})
			s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups,
				Config: map[string]string{"readergrouphash": "hierarchy-full"},
				Files:  map[string]string{"dexpire.ctl": "*:x30:e4\n"}})
			home := strings.TrimSpace(s.Run(t, "dpath", "group"))
			swFeed(t, s, st, groups, swArts, "linked")
			before := map[string][]string{}
			for _, g := range groups {
				before[g] = readOver(t, s, g, swArts)
			}
			s.Proc.Stop()
			files := swFiles(t, home)
			if err := os.Symlink(filepath.Join("a", "b"), filepath.Join(home, "lnk")); err != nil {
				t.Fatal(err)
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
			for f, b := range files {
				got, err := os.ReadFile(f)
				if err != nil {
					t.Errorf("%s: %s is gone: %v", tool, strings.TrimPrefix(f, home+"/"), err)
				} else if strings.HasPrefix(filepath.Base(f), "d.") && string(got) != string(b) {
					t.Errorf("%s: %s was changed", tool, strings.TrimPrefix(f, home+"/"))
				}
			}
			swCheckOutput(t, tool, out, nil)
			if t.Failed() {
				t.Logf("%s output:\n%s", tool, out)
			}

			restartReader(t, s)
			for _, g := range groups {
				if _, over := swOver(t, s, g); strings.Join(over, "\n") != strings.Join(before[g], "\n") {
					t.Errorf("after %s: OVER of %s %q; before %q", tool, g, over, before[g])
				}
			}
		})
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
