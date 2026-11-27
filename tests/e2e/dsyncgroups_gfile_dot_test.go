package e2e

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// TestDsyncgroupsFileDelete (D-328): the -G file loop did not look for the "." that ends a
// list, so a file that was cut short (a full disk, a bad copy) was read to its end like a whole
// one, and -D deleted every local group it did not name. The lists of a remote host end at the
// "." (without it dsyncgroups exits 1 before it writes anything); a -G file now does too: lines
// after the "." are not read, and with -D groups are deleted only when the file ended with a "."
// line (LF or CR LF). Without one a note is printed, nothing is deleted, and the creates and
// updates go on; the exit status is 0. Without -D nothing is different, with or without a ".".
//
// Each subtest makes e2e.keep, e2e.gone, e2e.ctl and e2e.other first (no -D), then runs -G -D
// with a file that names e2e.keep, e2e.other (end number 20 in the file, 10 in the database) and
// e2e.new. Fails on main: `no_dot` (everything absent is deleted, no note) and the `dot_middle*`
// subtests (the lines after the "." are read: e2e.gone is kept and e2e.late is made); the
// `dot_end*` subtests are controls that pass on main too.
func TestDsyncgroupsFileDelete(t *testing.T) {
	const note = "has no \".\" line at its end: not deleting groups (-D)"
	local := []string{"e2e.keep", "e2e.gone", "e2e.ctl", "e2e.other"}

	setup := func(t *testing.T) (*Site, string) {
		t.Helper()
		s := NewSite(t, SiteOptions{NoStart: true})
		db := filepath.Join(s.Dir, "gfile.kp")
		list := filepath.Join(s.Dir, "setup.txt")
		body := "e2e.keep 10 1 y\r\ne2e.gone 10 1 y\r\ne2e.ctl 10 1 y\r\ne2e.other 10 1 y\r\n.\r\n"
		if err := os.WriteFile(list, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
		out, err := s.TryRun("dsyncgroups", "-G", list, "-F", db, "-g")
		if err != nil {
			t.Fatalf("setup dsyncgroups -G: %v\n%s", err, out)
		}
		for _, g := range local {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Fatalf("setup: group %s has %d records\n%s", g, len(live), out)
			}
		}
		return s, db
	}
	run := func(t *testing.T, s *Site, db, body string, args ...string) string {
		t.Helper()
		list := filepath.Join(s.Dir, "gfile.txt")
		if err := os.WriteFile(list, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
		a := append([]string{"-G", list, "-f", db}, args...)
		out, err := s.TryRun("dsyncgroups", a...)
		if err != nil {
			t.Fatalf("dsyncgroups %v: %v\n%s", a, err, out)
		}
		return out
	}
	have := func(t *testing.T, db, out string, present, absent []string) {
		t.Helper()
		for _, g := range present {
			if live, _, _ := kpRecords(t, db, g); len(live) != 1 {
				t.Errorf("group %s: %d records, want 1\n%s", g, len(live), out)
			}
		}
		for _, g := range absent {
			if live, _, _ := kpRecords(t, db, g); len(live) != 0 {
				t.Errorf("group %s: %q, want it absent\n%s", g, live, out)
			}
		}
	}
	ne := func(t *testing.T, db, g string) int {
		t.Helper()
		n, err := strconv.Atoi(deepRevNE(t, db, g))
		if err != nil {
			t.Fatalf("NE of %s: %v", g, err)
		}
		return n
	}

	// the file is cut short: no "." (either line end). -D deletes nothing, the note is printed,
	// e2e.new is made (-g) and e2e.other gets its end number from the file (-NE).
	// Fails on main: e2e.gone and e2e.ctl are deleted, no note.
	for _, eol := range []struct{ name, e string }{{"no_dot", "\n"}, {"no_dot_crlf", "\r\n"}} {
		t.Run(eol.name, func(t *testing.T) {
			s, db := setup(t)
			e := eol.e
			out := run(t, s, db, "e2e.keep 10 1 y"+e+"e2e.other 20 1 y"+e+"e2e.new 5 1 y"+e,
				"-g", "-NE", "-D")
			have(t, db, out, append(local, "e2e.new"), nil)
			if !strings.Contains(out, note) {
				t.Errorf("want a note with %q:\n%s", note, out)
			}
			if !strings.Contains(out, "-G file "+filepath.Join(s.Dir, "gfile.txt")+" has no") {
				t.Errorf("the note does not name the file:\n%s", out)
			}
			if strings.Contains(out, "Deleting") {
				t.Errorf("-D deleted with a file that has no \".\":\n%s", out)
			}
			if n := ne(t, db, "e2e.other"); n != 20 {
				t.Errorf("e2e.other: NE=%d, want 20 (updates go on without the \".\")\n%s", n, out)
			}
		})
	}

	// the last line has no line end at all: the file ends after the groups
	t.Run("no_dot_no_eol", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y", "-g", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want a note with %q:\n%s", note, out)
		}
	})

	// an empty file names nothing and has no "." either: nothing is deleted
	t.Run("empty", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want a note with %q:\n%s", note, out)
		}
	})

	// without -D nothing changes: no note, the groups are made and updated
	t.Run("no_dot_without_D", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\ne2e.new 5 1 y\n", "-g", "-NE")
		have(t, db, out, append(local, "e2e.new"), nil)
		if strings.Contains(out, "not deleting") || strings.Contains(out, "Deleting") {
			t.Errorf("a note or a deletion without -D:\n%s", out)
		}
		if n := ne(t, db, "e2e.other"); n != 20 {
			t.Errorf("e2e.other: NE=%d, want 20\n%s", n, out)
		}
	})

	// controls: the file ends with "." (LF, CR LF): e2e.gone and e2e.ctl are deleted, no note.
	// Pass on main too.
	for _, eol := range []struct{ name, e string }{{"dot_end_lf", "\n"}, {"dot_end_crlf", "\r\n"}} {
		t.Run(eol.name, func(t *testing.T) {
			s, db := setup(t)
			e := eol.e
			out := run(t, s, db, "e2e.keep 10 1 y"+e+"e2e.other 20 1 y"+e+"e2e.new 5 1 y"+e+"."+e,
				"-g", "-NE", "-D")
			have(t, db, out, []string{"e2e.keep", "e2e.other", "e2e.new"}, []string{"e2e.gone", "e2e.ctl"})
			if strings.Contains(out, "not deleting") {
				t.Errorf("the note is printed for a file that ends with \".\":\n%s", out)
			}
			if !strings.Contains(out, "Deleting e2e.gone") || !strings.Contains(out, "Deleting e2e.ctl") {
				t.Errorf("want Deleting lines for e2e.gone and e2e.ctl:\n%s", out)
			}
			if n := ne(t, db, "e2e.other"); n != 20 {
				t.Errorf("e2e.other: NE=%d, want 20\n%s", n, out)
			}
		})
	}

	// a "." as the very last byte (no line end after it) also ends the file: it was written whole
	t.Run("dot_end_no_eol", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.", "-g", "-D")
		have(t, db, out, []string{"e2e.keep", "e2e.other"}, []string{"e2e.gone", "e2e.ctl"})
		if strings.Contains(out, "not deleting") {
			t.Errorf("the note is printed for a file that ends with \".\":\n%s", out)
		}
	})

	// a "." in the middle ends the list: the lines after it are not read. e2e.gone is named only
	// after it, so it is deleted (not named before the "."), and e2e.late is not made.
	// Fails on main: both lines are read (e2e.gone kept, e2e.late made).
	for _, eol := range []struct{ name, e string }{{"dot_middle_lf", "\n"}, {"dot_middle_crlf", "\r\n"}} {
		t.Run(eol.name, func(t *testing.T) {
			s, db := setup(t)
			e := eol.e
			out := run(t, s, db, "e2e.keep 10 1 y"+e+"e2e.other 20 1 y"+e+"."+e+
				"e2e.gone 10 1 y"+e+"e2e.late 5 1 y"+e+"e2e.other 30 1 y"+e,
				"-g", "-NE", "-D")
			have(t, db, out, []string{"e2e.keep", "e2e.other"}, []string{"e2e.gone", "e2e.ctl", "e2e.late"})
			if !strings.Contains(out, "2 groups found") {
				t.Errorf("want 2 groups found (the lines after the \".\" are not read):\n%s", out)
			}
			if n := ne(t, db, "e2e.other"); n != 20 {
				t.Errorf("e2e.other: NE=%d, want 20 (the line after the \".\" says 30)\n%s", n, out)
			}
			if strings.Contains(out, "not deleting") {
				t.Errorf("the note is printed for a file with a \".\" line:\n%s", out)
			}
		})
	}
}
