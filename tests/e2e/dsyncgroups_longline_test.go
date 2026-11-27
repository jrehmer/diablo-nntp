package e2e

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// TestDsyncgroupsLongListLine (D-320): dsyncgroups read the lists with fgets() into 4096 bytes,
// so a line over 4095 bytes came in two pieces and the tail was read as a line of its own: its
// first word became a group name (with -g -i the group was made), or in LIST ACTIVE a group
// with its numbers. A line that does not fit is now read to its end and skipped whole, with one
// note; the lines after it, the count and the "." that ends the list are not affected.
//
// Each subtest has a line whose first 4095 bytes are the start of a real entry and whose tail
// is "junk.tail ..." (a valid group name and, for LIST ACTIVE, with its numbers), then a short
// line. Fails on a9d79551: the group junk.tail / junk.act is made, and (LIST ACTIVE) so is
// e2e.longact with a flags field of 4000 bytes.
func TestDsyncgroupsLongListLine(t *testing.T) {
	// head is the first 4095 bytes of a line: start, then pad up to the break.
	pad := func(start string, fill byte) string {
		return start + strings.Repeat(string(fill), 4095-len(start))
	}

	t.Run("newsgroups", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		long := pad("e2e.long\t", 'x') + "junk.tail some more text " + strings.Repeat("y", 900)
		if len(long) < 4096+500 {
			t.Fatalf("test line is %d bytes", len(long))
		}
		port := dsyncPeer{
			Active: []string{"e2e.real 10 1 y", "e2e.long 5 1 y", "e2e.after 20 1 y"},
			Newsgroups: []string{
				"e2e.real\tShort real description.",
				long,
				"e2e.only\tNamed only in LIST NEWSGROUPS.",
				"e2e.after\tDescribed after the long line.",
			},
		}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port),
			"-F", db, "-g", "-i")
		if err != nil {
			t.Fatalf("dsyncgroups: %v\n%s", err, out)
		}
		if live, _, _ := kpRecords(t, db, "junk.tail"); len(live) != 0 {
			t.Errorf("the tail of the long line was made a group: junk.tail %q\n%s", live, out)
		}
		// LIST NEWSGROUPS sets descriptions; it does not make a group (D-320)
		if live, _, _ := kpRecords(t, db, "e2e.only"); len(live) != 0 {
			t.Errorf("e2e.only, named only in LIST NEWSGROUPS, was made: %q\n%s", live, out)
		}
		for _, g := range []string{"e2e.real", "e2e.long", "e2e.after"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Errorf("group %s of LIST ACTIVE was not made: %q\n%s", g, live, out)
			}
		}
		gd := func(g string) string {
			live, _, _ := kpRecords(t, db, g)
			if len(live) != 1 {
				return ""
			}
			for _, w := range strings.Fields(live[0]) {
				if v, ok := strings.CutPrefix(w, "GD="); ok {
					return v
				}
			}
			return ""
		}
		for _, g := range []string{"e2e.real", "e2e.after"} {
			if gd(g) == "" {
				t.Errorf("group %s has no description: the lines around the long one must still be read\n%s", g, out)
			}
		}
		if d := gd("e2e.long"); d != "" {
			t.Errorf("e2e.long has a description (%d bytes) from the skipped line", len(d))
		}
		if n := strings.Count(out, "Line over 4095 bytes - skipping : e2e.long"); n != 1 {
			t.Errorf("want one note about the long line, got %d:\n%s", n, out)
		}
		// real, only (counted: it is a valid line) and after; the pieces of the long line are not
		if !strings.Contains(out, "3 groups returned by list newsgroups") {
			t.Errorf("want 3 groups returned by list newsgroups:\n%s", out)
		}
		if strings.Contains(out, "Did not receive . terminator") {
			t.Errorf("the . that ends LIST NEWSGROUPS was not seen:\n%s", out)
		}
	})

	t.Run("active", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		long := pad("e2e.longact 10 1 ", 'y') + "junk.act 10 1 y"
		port := dsyncPeer{
			Active: []string{"e2e.real 10 1 y", long, "e2e.after 20 1 y"},
		}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port),
			"-F", db, "-g")
		if err != nil {
			t.Fatalf("dsyncgroups: %v\n%s", err, out)
		}
		for _, g := range []string{"junk.act", "e2e.longact"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 0 {
				t.Errorf("a piece of the long LIST ACTIVE line was made a group: %s %.200q\n%s", g, live, out)
			}
		}
		for _, g := range []string{"e2e.real", "e2e.after"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Errorf("group %s of LIST ACTIVE was not made: %q\n%s", g, live, out)
			}
		}
		if n := strings.Count(out, "Line over 4095 bytes - skipping : e2e.longact"); n != 1 {
			t.Errorf("want one note about the long line, got %d:\n%s", n, out)
		}
		if !strings.Contains(out, "2 groups returned by list active") {
			t.Errorf("want 2 groups returned by list active:\n%s", out)
		}
	})

	// the limit is 4095 bytes before the line end: a line of 4095 bytes and its CR LF fits (the
	// buffer of 4096 is full after them, the next byte read is the CR, then the LF), as it did with
	// fgets() (the rest was a bare CR LF the parsers skip); one byte more does not and is skipped whole
	t.Run("limit", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		fit := "e2e.fit 10 1 " + strings.Repeat("y", 4095-len("e2e.fit 10 1 "))
		port := dsyncPeer{Active: []string{fit, "e2e.after 20 1 y"}}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-F", db, "-g")
		if err != nil {
			t.Fatalf("dsyncgroups: %v\n%s", err, out)
		}
		for _, g := range []string{"e2e.fit", "e2e.after"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Errorf("group %s: %d records, want 1\n%.600s", g, len(live), out)
			}
		}
		if !strings.Contains(out, "2 groups returned by list active") {
			t.Errorf("want 2 groups returned by list active:\n%.600s", out)
		}
		if strings.Contains(out, "Line over") {
			t.Errorf("a line of 4095 bytes and its CR LF was skipped:\n%.600s", out)
		}
	})

	t.Run("limit_over", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		over := "e2e.over 10 1 " + strings.Repeat("y", 4096-len("e2e.over 10 1 "))
		port := dsyncPeer{Active: []string{over, "e2e.after 20 1 y"}}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-F", db, "-g")
		if err != nil {
			t.Fatalf("dsyncgroups: %v\n%s", err, out)
		}
		for g, want := range map[string]int{"e2e.over": 0, "e2e.after": 1} {
			if live, _, _ := kpRecords(t, db, g); len(live) != want {
				t.Errorf("group %s: %d records, want %d\n%.600s", g, len(live), want, out)
			}
		}
		if n := strings.Count(out, "Line over 4095 bytes - skipping : e2e.over"); n != 1 {
			t.Errorf("want one note about e2e.over, got %d:\n%.600s", n, out)
		}
	})

	// the same limit with a bare LF as the line end (a -G file)
	t.Run("limit_lf", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		fit := "e2e.fit 10 1 " + strings.Repeat("y", 4095-len("e2e.fit 10 1 "))
		over := "e2e.over 10 1 " + strings.Repeat("y", 4096-len("e2e.over 10 1 "))
		list := filepath.Join(s.Dir, "lf.txt")
		if err := os.WriteFile(list, []byte(fit+"\n"+over+"\ne2e.after 20 1 y\n.\n"), 0o644); err != nil {
			t.Fatal(err)
		}
		out, err := s.TryRun("dsyncgroups", "-G", list, "-F", db, "-g")
		if err != nil {
			t.Fatalf("dsyncgroups -G: %v\n%s", err, out)
		}
		for g, want := range map[string]int{"e2e.fit": 1, "e2e.over": 0, "e2e.after": 1} {
			if live, _, _ := kpRecords(t, db, g); len(live) != want {
				t.Errorf("group %s: %d records, want %d\n%.600s", g, len(live), want, out)
			}
		}
		if n := strings.Count(out, "Line over 4095 bytes - skipping : e2e.over"); n != 1 {
			t.Errorf("want one note about e2e.over, got %d:\n%.600s", n, out)
		}
	})

	// the same read loop takes the lines of a -G file
	t.Run("file", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		long := pad("e2e.longfile 10 1 ", 'y') + "junk.file 10 1 y"
		lines := "e2e.real 10 1 y\r\n" + long + "\r\ne2e.after 20 1 y\r\n.\r\n"
		list := filepath.Join(s.Dir, "list.txt")
		if err := os.WriteFile(list, []byte(lines), 0o644); err != nil {
			t.Fatal(err)
		}
		out, err := s.TryRun("dsyncgroups", "-G", list, "-F", db, "-g")
		if err != nil {
			t.Fatalf("dsyncgroups -G: %v\n%s", err, out)
		}
		for _, g := range []string{"junk.file", "e2e.longfile"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 0 {
				t.Errorf("a piece of the long line of the -G file was made a group: %s %.200q\n%s", g, live, out)
			}
		}
		for _, g := range []string{"e2e.real", "e2e.after"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Errorf("group %s of the -G file was not made: %q\n%s", g, live, out)
			}
		}
		if !strings.Contains(out, "2 groups found") {
			t.Errorf("want 2 groups found:\n%s", out)
		}
	})

	// -D deletes the local groups that the lists do not name. A line that was dropped may have
	// named one, so the lists are incomplete: with a dropped line nothing is deleted, with a note.
	// Each setup makes e2e.keep, e2e.gone, e2e.ctl and e2e.other; the first run has a long line
	// for e2e.keep and must delete nothing, the control run has none and deletes what the list
	// does not name (e2e.gone, e2e.ctl). Fails on cd366c0e: e2e.keep (named only on the long
	// line, which the fix drops whole) is deleted, with e2e.gone and e2e.ctl.
	setup := func(t *testing.T, s *Site, db string) {
		t.Helper()
		out, err := s.TryRun("dsyncgroups", "-G", writeList(t, s, "e2e.keep 10 1 y", "e2e.gone 10 1 y",
			"e2e.ctl 10 1 y", "e2e.other 10 1 y"), "-F", db, "-g")
		if err != nil {
			t.Fatalf("setup dsyncgroups -G: %v\n%s", err, out)
		}
		for _, g := range []string{"e2e.keep", "e2e.gone", "e2e.ctl", "e2e.other"} {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Fatalf("setup: group %s has %d records\n%s", g, len(live), out)
			}
		}
	}
	want := func(t *testing.T, db, out string, keep, gone []string) {
		t.Helper()
		for _, g := range keep {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Errorf("group %s was deleted by -D: %d records\n%s", g, len(live), out)
			}
		}
		for _, g := range gone {
			if live, _, _ := kpRecords(t, db, g); len(live) != 0 {
				t.Errorf("group %s was not deleted by -D: %q\n%s", g, live, out)
			}
		}
	}
	const note = "1 list lines over 4095 bytes were skipped: not deleting groups (-D)"
	all := []string{"e2e.keep", "e2e.gone", "e2e.ctl", "e2e.other"}

	t.Run("delete_active", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		setup(t, s, db)
		long := pad("e2e.keep 10 1 ", 'y') + "junk.act 10 1 y"
		port := dsyncPeer{Active: []string{long, "e2e.other 10 1 y"}}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-f", db, "-D")
		if err != nil {
			t.Fatalf("dsyncgroups -D: %v\n%s", err, out)
		}
		want(t, db, out, all, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want the note %q:\n%s", note, out)
		}
		// control: no dropped line, the same list without the long one is a whole list
		port = dsyncPeer{Active: []string{"e2e.keep 10 1 y", "e2e.other 10 1 y"}}.listen(t)
		out, err = s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-f", db, "-D")
		if err != nil {
			t.Fatalf("dsyncgroups -D (control): %v\n%s", err, out)
		}
		want(t, db, out, []string{"e2e.keep", "e2e.other"}, []string{"e2e.gone", "e2e.ctl"})
		if strings.Contains(out, "not deleting groups") {
			t.Errorf("the note is printed with no line dropped:\n%s", out)
		}
	})

	t.Run("delete_file", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		setup(t, s, db)
		list := writeList(t, s, pad("e2e.keep 10 1 ", 'y')+"junk.file 10 1 y", "e2e.other 10 1 y")
		out, err := s.TryRun("dsyncgroups", "-G", list, "-f", db, "-D")
		if err != nil {
			t.Fatalf("dsyncgroups -G -D: %v\n%s", err, out)
		}
		want(t, db, out, all, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want the note %q:\n%s", note, out)
		}
		// control: no dropped line, and the whole list ends with its "." line (writeList adds
		// it; -D with a -G file deletes only then, D-328): e2e.gone and e2e.ctl are deleted
		out, err = s.TryRun("dsyncgroups", "-G", writeList(t, s, "e2e.keep 10 1 y", "e2e.other 10 1 y"),
			"-f", db, "-D")
		if err != nil {
			t.Fatalf("dsyncgroups -G -D (control): %v\n%s", err, out)
		}
		want(t, db, out, []string{"e2e.keep", "e2e.other"}, []string{"e2e.gone", "e2e.ctl"})
		if strings.Contains(out, "not deleting groups") {
			t.Errorf("the note is printed with no line dropped:\n%s", out)
		}
	})

	// a LIST NEWSGROUPS entry also marks a local group as listed, so a dropped line of it counts too:
	// e2e.keep is named only on the long LIST NEWSGROUPS line
	t.Run("delete_newsgroups", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		setup(t, s, db)
		long := pad("e2e.keep\t", 'x') + "junk.tail some more text"
		port := dsyncPeer{
			Active:     []string{"e2e.other 10 1 y"},
			Newsgroups: []string{long, "e2e.other\tShort description."},
		}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-f", db, "-D", "-i")
		if err != nil {
			t.Fatalf("dsyncgroups -D -i: %v\n%s", err, out)
		}
		want(t, db, out, all, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want the note %q:\n%s", note, out)
		}
		// control: no dropped line; e2e.keep is named only in LIST NEWSGROUPS (it marks the group
		// as listed), so it stays, and e2e.gone and e2e.ctl, named in neither list, are deleted
		port = dsyncPeer{
			Active:     []string{"e2e.other 10 1 y"},
			Newsgroups: []string{"e2e.keep\tShort description.", "e2e.other\tShort description."},
		}.listen(t)
		out, err = s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-f", db, "-D", "-i")
		if err != nil {
			t.Fatalf("dsyncgroups -D -i (control): %v\n%s", err, out)
		}
		want(t, db, out, []string{"e2e.keep", "e2e.other"}, []string{"e2e.gone", "e2e.ctl"})
		if strings.Contains(out, "not deleting groups") {
			t.Errorf("the note is printed with no line dropped:\n%s", out)
		}
	})
}

// writeList writes the lines of a -G file (CR LF, ended by ".") and returns its path.
func writeList(t *testing.T, s *Site, lines ...string) string {
	t.Helper()
	list := filepath.Join(s.Dir, "glist"+strconv.Itoa(len(lines))+".txt")
	if err := os.WriteFile(list, []byte(strings.Join(lines, "\r\n")+"\r\n.\r\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	return list
}
