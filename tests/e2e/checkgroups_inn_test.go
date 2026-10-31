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
	return cgSiteWith(t, groups, map[string]string{"dcontrol.ctl": dcontrol}, nil, lines...)
}

// cgSiteWith is cgSite with more files in the home directory (localgroups) and groups with a
// description, "name=description", made before the message is fed.
func cgSiteWith(t *testing.T, groups []string, files map[string]string, described []string, lines ...string) *Site {
	t.Helper()
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Groups: groups,
		Files:  files,
	})
	for _, d := range described {
		name, desc, _ := strings.Cut(d, "=")
		s.Run(t, "dgrpctl", "newgroup", name, "y", "", desc)
	}
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

// TestCheckgroupsInnLimit: as INN, more than "maxcheckgroups" (default 10) additions, status
// (moderated flag) changes and removals are not made but written to the control log as the
// commands that make them; the descriptions of the groups that exist are updated all the same. A
// maxcheckgroups line in dcontrol.ctl sets the limit, for the sender and the group it matches; the
// last matching line is used.
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
		s := cgSite(t, []string{"e2e.test", "e2e.obsolete"}, "maxcheckgroups:*:*:12\ncheckgroups:*:*:doit\n", lines...)
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

// more ways to set the limit, and what it covers
func TestCheckgroupsInnLimitLines(t *testing.T) {
	eleven := []string{"e2e.test\tDescribed."}
	for _, n := range []string{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k"} {
		eleven = append(eleven, "e2e.new"+n+"\tNew group "+n+".")
	}
	made := func(t *testing.T, dcontrol string) bool {
		s := cgSite(t, []string{"e2e.test"}, dcontrol, eleven...)
		waitFor(t, 10*time.Second, "the description", func() bool {
			return iscDescriptions(iscList(t, s, "NEWSGROUPS"))["e2e.test"] == "Described."
		})
		_, ok := cgActive(t, s)["e2e.newk"]
		return ok
	}
	t.Run("last line that matches wins", func(t *testing.T) {
		if !made(t, "maxcheckgroups:*:*:1\nmaxcheckgroups:*:e2e.*:20\ncheckgroups:*:*:doit\n") {
			t.Error("the later line for e2e.* did not win over the earlier one for *")
		}
		if made(t, "maxcheckgroups:*:e2e.*:20\nmaxcheckgroups:*:*:1\ncheckgroups:*:*:doit\n") {
			t.Error("the later line for * did not win over the earlier one for e2e.*")
		}
	})
	t.Run("other hierarchy", func(t *testing.T) {
		if made(t, "maxcheckgroups:*:fr.*:20\ncheckgroups:*:*:doit\n") {
			t.Error("a limit for fr.* was used for e2e.*")
		}
	})
	t.Run("other sender", func(t *testing.T) {
		if made(t, "maxcheckgroups:nobody@example.invalid:*:20\ncheckgroups:*:*:doit\n") {
			t.Error("a limit for another sender was used")
		}
	})
	t.Run("flags are limited", func(t *testing.T) {
		var groups, lines []string
		for _, n := range []string{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l"} {
			groups = append(groups, "e2e.flag"+n)
			lines = append(lines, "e2e.flag"+n+"\tNow moderated "+n+". (Moderated)")
		}
		// 12 status changes are over the limit of 10: the flags stay, the descriptions are updated
		s := cgSite(t, groups, "checkgroups:*:*:doit=checkgroups.log\n", lines...)
		waitFor(t, 10*time.Second, "the descriptions", func() bool {
			return iscDescriptions(iscList(t, s, "NEWSGROUPS"))["e2e.flaga"] == "Now moderated a. (Moderated)"
		})
		act := cgActive(t, s)
		for _, g := range groups {
			if act[g] != "y" {
				t.Errorf("%s: flag %q, want y: 12 status changes are over the limit of 10", g, act[g])
			}
		}
		log, _ := os.ReadFile(filepath.Join(s.Dir, "log", "checkgroups.log"))
		if !strings.Contains(string(log), "0 to add, 12 to change, 0 to remove (limit 10): NOT made") || !strings.Contains(string(log), `dgrpctl modgroup e2e.flaga m "" "Now moderated a. (Moderated)"`) {
			t.Errorf("control log:\n%s", log)
		}
		// and with the limit raised they are changed
		s = cgSite(t, groups, "maxcheckgroups:*:*:12\ncheckgroups:*:*:doit\n", lines...)
		waitFor(t, 10*time.Second, "the flags", func() bool { return cgActive(t, s)["e2e.flagl"] == "m" })
	})
}

// TestCheckgroupsInnLocal: a group of the file localgroups (a name or a wildcard per line) is
// left alone by checkgroups: no description, no flag change, never removed.
func TestCheckgroupsInnLocal(t *testing.T) {
	s := cgSiteWith(t, []string{"e2e.test", "e2e.priv.a"},
		map[string]string{"dcontrol.ctl": "checkgroups:*:*:doit\n", "localgroups": "# groups of this site\ne2e.local\ne2e.priv.*\n"},
		[]string{"e2e.local=Our own description.", "e2e.obsolete=Obsolete group."},
		"e2e.test\tThe test group.",
		"e2e.local\tThe description of the sender. (Moderated)",
	)
	waitFor(t, 10*time.Second, "e2e.test described", func() bool {
		return iscDescriptions(iscList(t, s, "NEWSGROUPS"))["e2e.test"] == "The test group."
	})
	waitFor(t, 10*time.Second, "e2e.obsolete removed", func() bool { _, ok := cgActive(t, s)["e2e.obsolete"]; return !ok })
	act := cgActive(t, s)
	if act["e2e.local"] != "y" {
		t.Errorf("e2e.local: flag %q, want y: it is in localgroups", act["e2e.local"])
	}
	if _, ok := act["e2e.priv.a"]; !ok {
		t.Error("e2e.priv.a (localgroups wildcard e2e.priv.*) was removed")
	}
	if d := iscDescriptions(iscList(t, s, "NEWSGROUPS"))["e2e.local"]; d != "Our own description." {
		t.Errorf("e2e.local: description %q, want its own", d)
	}
}

// TestCheckgroupsInnKeepsDescriptions: the description of a group that still exists is never
// removed or changed when the message does not list it: a group not removed because the limit is
// exceeded, one outside the dcontrol.ctl pattern, one of another hierarchy.
func TestCheckgroupsInnKeepsDescriptions(t *testing.T) {
	lines := []string{"e2e.test\tDescribed."}
	for _, n := range []string{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k"} {
		lines = append(lines, "e2e.new"+n+"\tNew group "+n+".")
	}
	s := cgSiteWith(t, []string{"e2e.test"},
		map[string]string{"dcontrol.ctl": "checkgroups:*:e2e.*:doit\n"},
		[]string{"e2e.obsolete=Not removed: over the limit.", "other.keep=Another hierarchy."},
		lines...)
	waitFor(t, 10*time.Second, "the description", func() bool {
		return iscDescriptions(iscList(t, s, "NEWSGROUPS"))["e2e.test"] == "Described."
	})
	act := cgActive(t, s)
	if _, ok := act["e2e.newa"]; ok {
		t.Fatal("the additions were made although they are over the limit")
	}
	desc := iscDescriptions(iscList(t, s, "NEWSGROUPS"))
	if desc["e2e.obsolete"] != "Not removed: over the limit." || desc["other.keep"] != "Another hierarchy." {
		t.Errorf("descriptions of groups that still exist: %v", desc)
	}
}
