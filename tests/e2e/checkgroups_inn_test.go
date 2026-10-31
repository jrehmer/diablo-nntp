package e2e

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// A checkgroups control message as dreaderd acts on it with "doit": like INN's docheckgroups,
// see CtlCheckGroups in dreaderd/control.c.

// cgActive returns group -> flag of LIST ACTIVE.
func cgActive(t *testing.T, s *Site) map[string]string {
	t.Helper()
	m := map[string]string{}
	for _, l := range iscList(t, s, "ACTIVE") {
		if f := strings.Fields(l); len(f) >= 4 {
			m[f[0]] = f[3]
		}
	}
	return m
}

// cgSite starts a reader site with the groups, the dcontrol.ctl lines and feeds it the checkgroups
// with the lines as the body.
func cgSite(t *testing.T, groups []string, dcontrol string, lines ...string) *Site {
	t.Helper()
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Groups: groups,
		Files:  map[string]string{"dcontrol.ctl": dcontrol},
	})
	a := newArticle(msgID(t), "e2e.test", "cmsg checkgroups", lines...)
	a = append(a[:3:3], append([]string{"Control: checkgroups"}, a[3:]...)...)
	feedReader(t, s, st, "e2e.test", a)
	return s
}

// TestCheckgroupsInn: a group the list names is created (moderated if the description says
// "(Moderated)"); a group that exists gets the description and the moderated flag of the list; a
// group of a hierarchy of the list that it does not name is removed; the groups of other
// hierarchies are not touched.
func TestCheckgroupsInn(t *testing.T) {
	s := cgSite(t,
		[]string{"e2e.test", "e2e.obsolete", "e2e.flip", "other.keep"},
		"checkgroups:*:e2e.*:doit=checkgroups.log\n",
		"e2e.test\tThe test group.",
		"e2e.new\tA new group.",
		"e2e.mod\tA new moderated group. (Moderated)",
		"e2e.flip\tNow moderated. (Moderated)",
		"other.listed\tOutside the pattern of dcontrol.ctl.",
	)
	waitFor(t, 10*time.Second, "e2e.new in LIST ACTIVE", func() bool { _, ok := cgActive(t, s)["e2e.new"]; return ok })
	act := cgActive(t, s)
	for g, flag := range map[string]string{"e2e.new": "y", "e2e.mod": "m", "e2e.flip": "m", "e2e.test": "y", "other.keep": "y"} {
		if act[g] != flag {
			t.Errorf("%s: flag %q, want %q (LIST ACTIVE: %v)", g, act[g], flag, act)
		}
	}
	if _, ok := act["e2e.obsolete"]; ok {
		t.Error("e2e.obsolete is still there")
	}
	if _, ok := act["other.listed"]; ok {
		t.Error("other.listed was created although dcontrol.ctl limits the check to e2e.*")
	}
	desc := iscDescriptions(iscList(t, s, "NEWSGROUPS"))
	if desc["e2e.test"] != "The test group." || desc["e2e.new"] != "A new group." || desc["e2e.flip"] != "Now moderated. (Moderated)" {
		t.Errorf("descriptions: %v", desc)
	}
	c := s.Reader(t)
	defer expect(t, c, 205, "QUIT")
	if code, line := c.Cmd("GROUP e2e.new"); code != 211 || !strings.HasPrefix(line, "211 0 ") {
		t.Errorf("GROUP e2e.new: %d %q", code, line)
	}
	log, _ := os.ReadFile(filepath.Join(s.Dir, "log", "checkgroups.log"))
	if !strings.Contains(string(log), "Checkgroups: 2 to add, 1 to change, 1 to remove (limit 10): made") {
		t.Errorf("control log:\n%s", log)
	}
}

// TestCheckgroupsInnLimit: as INN, more than /maxdocheckgroups/ (default 10) additions, changes
// and removals are not made but written to the control log as the commands that make them; the
// descriptions of the groups that exist are updated all the same. A /maxdocheckgroups/ line in
// dcontrol.ctl raises the limit.
func TestCheckgroupsInnLimit(t *testing.T) {
	lines := []string{"e2e.test\tDescribed."}
	for _, n := range []string{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k"} {
		lines = append(lines, "e2e.new"+n+"\tNew group "+n+".")
	}
	t.Run("default", func(t *testing.T) {
		s := cgSite(t, []string{"e2e.test", "e2e.obsolete"}, "checkgroups:*:*:doit=checkgroups.log\n", lines...)
		waitFor(t, 10*time.Second, "the description", func() bool {
			return iscDescriptions(iscList(t, s, "NEWSGROUPS"))["e2e.test"] == "Described."
		})
		act := cgActive(t, s)
		if _, ok := act["e2e.newa"]; ok {
			t.Error("11 additions were made with the default limit of 10")
		}
		if _, ok := act["e2e.obsolete"]; !ok {
			t.Error("the removal was made with the default limit of 10 exceeded")
		}
		log, _ := os.ReadFile(filepath.Join(s.Dir, "log", "checkgroups.log"))
		for _, want := range []string{"11 to add, 0 to change, 1 to remove (limit 10): NOT made", `dgrpctl newgroup e2e.newa y "" "New group a."`, "dgrpctl rmgroup e2e.obsolete"} {
			if !strings.Contains(string(log), want) {
				t.Errorf("control log lacks %q:\n%s", want, log)
			}
		}
	})
	t.Run("raised", func(t *testing.T) {
		s := cgSite(t, []string{"e2e.test", "e2e.obsolete"}, "/maxdocheckgroups/:*:*:12\ncheckgroups:*:*:doit\n", lines...)
		waitFor(t, 10*time.Second, "e2e.newa in LIST ACTIVE", func() bool { _, ok := cgActive(t, s)["e2e.newa"]; return ok })
		act := cgActive(t, s)
		if _, ok := act["e2e.newk"]; !ok {
			t.Error("e2e.newk was not created with the limit raised to 12")
		}
		if _, ok := act["e2e.obsolete"]; ok {
			t.Error("e2e.obsolete was not removed with the limit raised to 12")
		}
	})
}
