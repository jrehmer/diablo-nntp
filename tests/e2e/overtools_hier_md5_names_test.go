package e2e

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// TestOverToolsHierMD5LikeNames (D-335, go-while's #519 r2 and #521 reviews): ExtractGroupHashInfoGz()
// (lib/hash.c) tried the md5 name patterns first, and the hierarchy names of a group whose name
// starts like "o.<n>.<n>" or "d.<n>.<n>" have their form: the over file o.0.o.1.2 of the group
// o.1.2 read as the md5 over file of the hash "o", its data file d.0.o.1.2 as that of the hash
// "d", and the files o.0.d.5.6.x and d.0.d.5.6.x of d.5.6.x as md5 data files (o.0.o.12345.64:
// md5 bits 12345, base 64). In a hierarchy spool no group has such a hash, so `dexpireover -a`
// and `doverctl clean` removed the live files of these groups as of no group; when a group "o"
// or "d" exists (its hierarchy hash is its name), they were taken as that group's files ("bad
// file header - removing", "Deleting stale overview data for o"); and dexpireover did not
// recognize their temporary files. The parse now tries the names of the configured layout
// first.
//
// Input: the groups e2e.test, o.1.2, d.5.6.x and o.12345.64 (variant with_o_d: also the groups
// o and d), two articles crossposted to all of them (each group gets its over file and the data
// file of the base 0), dreaderd stopped. Tool dexpireover: in the directory of o.1.2 the
// temporary names ".<over name>", ".<data name of base 0>" (an interrupted -R) and ".<data name
// of base 256>.gz" (an interrupted -z), the same over temporary for o.12345.64, and with the
// hierarchy layouts the over file o.0.o.1.9 of a group o.1.9 that does not exist (same
// directory); `dexpireover -a -v`. Tool dexpireover_R: `dexpireover -a -R -v3`. Tool
// doverctl_clean: `doverctl -v clean`. Each case on a site of its own, then dreaderd is
// restarted. Cases: crc, md5-32\1, hierarchy and hierarchy-full, each with the three tools and
// both variants (no group is X with X.gz beside it: with hierarchy and -R that is D-325).
// Expected: the over and data files of all groups are there; nothing is reported as "not
// found, removing", "stale overview data", "bad file header" or "Deleting unknown" for them;
// dexpireover removes the temporary files ("Removing old temp file: <path>") and o.0.o.1.9
// ("Group for over file not found, removing"); -R rewrites every group ("End of rewrite:
// <group>"; with the hierarchy layouts not o and d, whose files are in the group home itself,
// which neither tool scans, on main too), reports no failed rewrite and leaves no '.' file in
// the groups' directories; after the restart OVER 1-2 of each group gives the same lines as
// before. **Fails** on e19924fb (main) with hierarchy and hierarchy-full, every tool and
// variant: the files of the D-335 groups are removed as "Group for over file not found" or,
// with the groups o and d, taken as their files, -R rewrites none of the D-335 groups, and
// dexpireover leaves the temporary files. crc and md5 pass there too.
func TestOverToolsHierMD5LikeNames(t *testing.T) {
	for _, tc := range []struct{ name, hash string }{
		{"crc", ""},
		{"md5", `md5-32\1`},
		{"hierarchy", "hierarchy"},
		{"hierarchy_full", "hierarchy-full"},
	} {
		for _, tool := range []string{"dexpireover", "dexpireover_R", "doverctl_clean"} {
			for _, variant := range []string{"plain", "with_o_d"} {
				t.Run(tc.name+"/"+tool+"/"+variant, func(t *testing.T) {
					hierMD5LikeCase(t, tc.hash, tool, variant == "with_o_d")
				})
			}
		}
	}
}

// hierMD5LikeCase is one case of TestOverToolsHierMD5LikeNames: layout hash ("" = crc), tool
// dexpireover, dexpireover_R or doverctl_clean, withOD: the groups o and d exist too.
func hierMD5LikeCase(t *testing.T, hash, tool string, withOD bool) {
	groups := []string{"e2e.test", "o.1.2", "d.5.6.x", "o.12345.64"}
	if withOD {
		groups = append(groups, "o", "d")
	}
	st := fakeserver.NewStore()
	for _, g := range groups {
		st.AddGroup(g, "D-335 test group")
	}
	spool := fakeserver.New(t, fakeserver.Options{Store: st})
	var cfg map[string]string
	if hash != "" {
		cfg = map[string]string{"readergrouphash": hash}
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
	dirs := map[string]string{}  // group -> its directory
	overs := map[string]string{} // group -> its over file name
	for _, g := range groups {
		rel := cleanDirsOver(t, s, g)
		d, o := filepath.Join(home, filepath.Dir(rel)), filepath.Base(rel)
		dirs[g], overs[g] = d, o
		live = append(live, filepath.Join(d, o), filepath.Join(d, tfDataName(t, o, 0)))
	}
	for _, f := range live {
		if _, err := os.Stat(f); err != nil {
			t.Fatalf("before %s: %v", tool, err)
		}
	}
	// said: a line of out starts with prefix and ends in path below the group home (the tools
	// print the group home as they expand it)
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
	switch tool {
	case "dexpireover":
		dir, over := dirs["o.1.2"], overs["o.1.2"]
		temps := []string{
			filepath.Join(dir, "."+over),
			filepath.Join(dir, "."+tfDataName(t, over, 0)),
			filepath.Join(dir, "."+tfDataName(t, over, 256)+".gz"),
			filepath.Join(dirs["o.12345.64"], "."+overs["o.12345.64"]),
		}
		for _, p := range temps {
			writeOver(t, p, []byte("left by an interrupted run: "+filepath.Base(p)+"\n"))
		}
		var noGroup string
		if strings.HasPrefix(hash, "hierarchy") {
			// o.1.9 has the directory of o.1.2 in both hierarchy layouts
			noGroup = filepath.Join(dir, "o.0.o.1.9")
			writeOver(t, noGroup, hierOverFile("o.1.9", 1, 500))
			allowed = []string{strings.TrimPrefix(noGroup, home)}
		}
		out = s.Run(t, "dexpireover", "-a", "-v")
		for _, p := range temps {
			if _, err := os.Lstat(p); !os.IsNotExist(err) {
				t.Errorf("the temporary file %s is still there (%v)", strings.TrimPrefix(p, home+"/"), err)
			}
			if !said(out, "Removing old temp file: ", p) {
				t.Errorf("dexpireover did not report the removal of %s", strings.TrimPrefix(p, home+"/"))
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
	case "dexpireover_R":
		out = s.Run(t, "dexpireover", "-a", "-R", "-v3")
		for _, g := range groups {
			if dirs[g] == home {
				continue // hierarchy o and d: files in the group home itself, which is not scanned
			}
			if !strings.Contains(out, "End of rewrite: "+g+"\n") {
				t.Errorf("dexpireover -R did not rewrite %s", g)
			}
		}
		for _, l := range strings.Split(out, "\n") {
			if strings.HasPrefix(l, "Rewrite ") || strings.Contains(l, "unable to rewrite") {
				t.Errorf("dexpireover -R reports: %s", l)
			}
		}
		for _, g := range groups {
			ents, err := os.ReadDir(dirs[g])
			if err != nil {
				t.Fatal(err)
			}
			for _, e := range ents {
				if strings.HasPrefix(e.Name(), ".") {
					t.Errorf("after -R: %s is in the directory of %s", e.Name(), g)
				}
			}
		}
	default:
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
}
