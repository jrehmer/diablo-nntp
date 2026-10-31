package e2e

import (
	"strings"
	"testing"
	"time"
)

// TestReaderCheckgroupsControl (D-53, INN's docheckgroups): an accepted checkgroups control
// message updates the description of the groups that exist, and creates the groups it names
// that do not exist, with a complete record. It used to write a description into a record
// that did not exist, which appeared in LIST ACTIVE as `0 0 n` and answered GROUP with
// "211 1 -1 -1". The group of the hierarchy that the list does not name is removed.
func TestReaderCheckgroupsControl(t *testing.T) {
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool: spool,
		Files: map[string]string{"dcontrol.ctl": "checkgroups:*:*:doit\n"},
	})
	a := newArticle(msgID(t), "e2e.test", "cmsg checkgroups",
		"e2e.ghost\tA group that did not exist.", "e2e.test\tRenamed test group.")
	a = append(a[:3:3], append([]string{"Control: checkgroups"}, a[3:]...)...)
	feedReader(t, s, st, "e2e.test", a)

	c := s.Reader(t)
	defer expect(t, c, 205, "QUIT")
	// the control message is applied by dreaderd after it took the article
	waitFor(t, 10*time.Second, "the new group in LIST ACTIVE", func() bool {
		expect(t, c, 215, "LIST ACTIVE")
		return strings.Contains(strings.Join(c.ReadDotLines(), "\n"), "e2e.ghost ")
	})
	expect(t, c, 215, "LIST NEWSGROUPS")
	all := strings.Join(c.ReadDotLines(), "\n")
	if !strings.Contains(all, "e2e.test\tRenamed test group.") {
		t.Errorf("the description of the existing group was not updated:\n%s", all)
	}
	if !strings.Contains(all, "e2e.ghost\tA group that did not exist.") {
		t.Errorf("the description of the new group is missing:\n%s", all)
	}
	// a complete record: GROUP works, with the right numbers
	if code, line := c.Cmd("GROUP e2e.ghost"); code != 211 || !strings.HasPrefix(line, "211 0 ") {
		t.Errorf("GROUP e2e.ghost: %d %q, want 211 0 ...", code, line)
	}
	if code, line := c.Cmd("GROUP e2e.other"); code != 411 {
		t.Errorf("GROUP e2e.other: %d %q, want 411 (a group of the hierarchy that the list lacks is removed)", code, line)
	}
}
