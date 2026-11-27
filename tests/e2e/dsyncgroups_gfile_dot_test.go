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
// after the "." are not read, and with -D groups are deleted only when the file ENDS with a "."
// line (LF, CR LF or no line end), with nothing at all after it. Without one, or with anything
// after the first ".", a note is printed, nothing is deleted, and the creates and updates go
// on; the exit status is 0. Without -D nothing is different, with or without a ".". The "." is
// known by the length of the line, so ".", a NUL byte and more is not one.
//
// Each subtest makes e2e.keep, e2e.gone, e2e.ctl and e2e.other first (no -D), then runs -G -D
// with a file that names e2e.keep, e2e.other (end number 20 in the file, 10 in the database) and
// e2e.new. Fails on main: `no_dot` (everything absent is deleted, no note), `dot_middle*`,
// `dot_then_byte` and `dot_nul_last`; fails on 3892b341 (the first "." was enough): `dot_middle*`,
// `dot_then_byte`, `dot_nul_junk`, `dot_nul_eol`, `dot_nul_crlf`, `dot_nul_last`. The `dot_end*`
// and `dot_nul_{junk,eol,crlf}` subtests are controls that pass on main too.
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

	// a "." in the middle ends the list and the lines after it are not read, but the file does
	// not end there: it may be two lists or a pasted one, and what is named after the "." is
	// not known. With -D nothing is deleted (e2e.gone and e2e.ctl are named in no row before the
	// "."), a note says so, e2e.late is not made, and "2 groups found" shows that the rows
	// after the "." were not read.
	// Fails on 3892b341 (-D deleted e2e.gone and e2e.ctl, no note) and on main (the rows after
	// the "." are read: e2e.late is made, "5 groups found" or so).
	const midNote = "has lines after its \".\" line: not deleting groups (-D)"
	for _, eol := range []struct{ name, e string }{{"dot_middle_lf", "\n"}, {"dot_middle_crlf", "\r\n"}} {
		t.Run(eol.name, func(t *testing.T) {
			s, db := setup(t)
			e := eol.e
			out := run(t, s, db, "e2e.keep 10 1 y"+e+"e2e.other 20 1 y"+e+"."+e+
				"e2e.gone 10 1 y"+e+"e2e.late 5 1 y"+e+"e2e.other 30 1 y"+e,
				"-g", "-NE", "-D")
			have(t, db, out, local, []string{"e2e.late"})
			if !strings.Contains(out, "2 groups found") {
				t.Errorf("want 2 groups found (the lines after the \".\" are not read):\n%s", out)
			}
			if n := ne(t, db, "e2e.other"); n != 20 {
				t.Errorf("e2e.other: NE=%d, want 20 (the line after the \".\" says 30)\n%s", n, out)
			}
			if !strings.Contains(out, midNote) {
				t.Errorf("want a note with %q:\n%s", midNote, out)
			}
			if strings.Contains(out, "Deleting") {
				t.Errorf("-D deleted with a \".\" that is not at the end:\n%s", out)
			}
		})
	}

	// a "." in the middle and a "." at the end: the first "." ends the list, so the one at the
	// end is not read and the first is not the end of the input: nothing is deleted (the strict
	// reading; the second "." is just more text after the first)
	t.Run("dot_middle_and_end", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\ne2e.gone 10 1 y\ne2e.late 5 1 y\n.\n",
			"-g", "-NE", "-D")
		have(t, db, out, local, []string{"e2e.late"})
		if !strings.Contains(out, "2 groups found") {
			t.Errorf("want 2 groups found:\n%s", out)
		}
		if !strings.Contains(out, midNote) {
			t.Errorf("want a note with %q:\n%s", midNote, out)
		}
		if strings.Contains(out, "Deleting") {
			t.Errorf("-D deleted with a \".\" that is not at the end:\n%s", out)
		}
	})

	// ".\n" and then one more byte (not even a line end) is something after the "." too
	t.Run("dot_then_byte", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\nx", "-g", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, midNote) {
			t.Errorf("want a note with %q:\n%s", midNote, out)
		}
	})

	// a line of "." and a NUL byte is not the "." that ends the list: the line is read by its
	// length, not as the C string ".". It is skipped (no group name, no numbers), the rows after
	// it are read (e2e.new is made), and the "." at the end is the end: e2e.gone and e2e.ctl are
	// deleted, no note. Both forms: with more bytes after the NUL, and NUL then the line end.
	// Fails on 3892b341 (strcmp() takes ".\0junk" for "."): the list ends there, e2e.new is not
	// made, and "2 groups found".
	for _, nul := range []struct{ name, line string }{
		{"dot_nul_junk", ".\x00junk\n"}, {"dot_nul_eol", ".\x00\n"}, {"dot_nul_crlf", ".\x00\r\n"},
	} {
		t.Run(nul.name, func(t *testing.T) {
			s, db := setup(t)
			out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n"+nul.line+"e2e.new 5 1 y\n.\n",
				"-g", "-NE", "-D")
			have(t, db, out, []string{"e2e.keep", "e2e.other", "e2e.new"}, []string{"e2e.gone", "e2e.ctl"})
			if !strings.Contains(out, "3 groups found") {
				t.Errorf("want 3 groups found (the rows after the NUL line are read):\n%s", out)
			}
			if strings.Contains(out, "not deleting") {
				t.Errorf("a note for a file that ends with \".\":\n%s", out)
			}
		})
	}

	// the same NUL line as the last line: it is not a "." at the end of the input, so the file
	// has no "." line at all: nothing is deleted, the "no" note. (Fails on 3892b341: ".\0" is taken
	// for the "." and e2e.gone and e2e.ctl are deleted.)
	t.Run("dot_nul_last", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\x00junk", "-g", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want a note with %q:\n%s", note, out)
		}
	})
}
