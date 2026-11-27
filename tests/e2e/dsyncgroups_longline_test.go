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

	// the limit: a line of 4096 bytes with its CR LF fits the 4096-byte buffer (4095 bytes and the
	// NUL, the line feed is the next byte read), one byte more does not and is skipped whole
	t.Run("limit", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "long.kp")
		fit := "e2e.fit 10 1 " + strings.Repeat("y", 4096-2-len("e2e.fit 10 1 "))
		over := "e2e.over 10 1 " + strings.Repeat("y", 4097-2-len("e2e.over 10 1 "))
		port := dsyncPeer{Active: []string{fit, over, "e2e.after 20 1 y"}}.listen(t)
		out, err := s.TryRun("dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-F", db, "-g")
		if err != nil {
			t.Fatalf("dsyncgroups: %v\n%s", err, out)
		}
		for g, want := range map[string]int{"e2e.fit": 1, "e2e.over": 0, "e2e.after": 1} {
			if live, _, _ := kpRecords(t, db, g); len(live) != want {
				t.Errorf("group %s: %d records, want %d\n%.600s", g, len(live), want, out)
			}
		}
		if !strings.Contains(out, "2 groups returned by list active") {
			t.Errorf("want 2 groups returned by list active:\n%.600s", out)
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
}
