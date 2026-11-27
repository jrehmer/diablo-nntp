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
// "." (without it dsyncgroups exits 1 before it writes anything); with -D a -G file must now
// show that it is whole too: its last line that is not blank must be a "." line (LF, CR LF or no
// line end; empty lines and lines of blanks after it do not matter), and it must have been read
// to its end without an error. Otherwise a note is printed, nothing is deleted, and the creates
// and updates go on; the exit status is 0.
// The whole file is read in every case, as before, and a "." line is no group: a "." in the
// middle does not end the read (without -D nothing differs from main), but any line that is not
// blank after it means the file does not end there, so with -D nothing is deleted. A "." in the
// middle and another at the end is fine: the last non-blank line is a ".", and the rows between
// count as listed. The "." is known by the length of the line, so ".", a NUL byte and more is not
// one: it is a line of its own, which has no group name and is skipped.
//
// Each subtest makes e2e.keep, e2e.gone, e2e.ctl and e2e.other first (no -D), then runs -G -D
// with a file that names e2e.keep, e2e.other (end number 20 in the file, 10 in the database) and
// e2e.new. On main (before D-328) these fail, because -D deletes and no note is printed:
// `no_dot*`, `empty`, `dot_middle_lf/crlf`, `dot_then_line`, `dot_first`, `dot_nul_last` and
// `read_error`. On 490fd4d1 (reading stopped at the first ".") these fail: `dot_middle_lf/crlf`
// (the rows after the "." were not read, e2e.late not made), `dot_middle_and_end` and
// `dot_then_blank_*` (they deleted nothing), `dot_then_line` (another note), `dot_first`,
// `appended_noD` (the rows after the "." not read, as without -D on main they are) and
// `read_error` (the note is about the "." line). The `dot_end*`, `no_dot_without_D` and
// `dot_nul_{junk,eol,crlf}` subtests are controls that pass on all three.
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

	// a "." in the middle and no "." at the end: the file does not end there (a second list, a
	// pasted file), what is named after the "." is not the whole of it. With -D nothing is deleted
	// and the note is printed. The rows after the "." are read as on main: e2e.late is made,
	// e2e.other gets the later end number (30), "5 groups found".
	// Fails on main (-D deletes e2e.ctl, no note) and on 490fd4d1 (the rows after the "." are
	// not read: e2e.late is not made, NE=20, "2 groups found").
	for _, eol := range []struct{ name, e string }{{"dot_middle_lf", "\n"}, {"dot_middle_crlf", "\r\n"}} {
		t.Run(eol.name, func(t *testing.T) {
			s, db := setup(t)
			e := eol.e
			out := run(t, s, db, "e2e.keep 10 1 y"+e+"e2e.other 20 1 y"+e+"."+e+
				"e2e.gone 10 1 y"+e+"e2e.late 5 1 y"+e+"e2e.other 30 1 y"+e,
				"-g", "-NE", "-D")
			have(t, db, out, append(local, "e2e.late"), nil)
			if !strings.Contains(out, "5 groups found") {
				t.Errorf("want 5 groups found (the lines after the \".\" are read):\n%s", out)
			}
			if n := ne(t, db, "e2e.other"); n != 30 {
				t.Errorf("e2e.other: NE=%d, want 30 (the line after the \".\" says 30, as on main)\n%s", n, out)
			}
			if !strings.Contains(out, note) {
				t.Errorf("want a note with %q:\n%s", note, out)
			}
			if strings.Contains(out, "Deleting") {
				t.Errorf("-D deleted with a \".\" that is not at the end:\n%s", out)
			}
		})
	}

	// a "." in the middle and a "." at the end: the last non-blank line is a ".", so the file is
	// whole and -D deletes what is absent from it. The rows between the two count as listed:
	// e2e.gone, named only there, is kept; e2e.late is made; e2e.ctl is deleted. This differs from
	// 490fd4d1, where the first "." ended the read and the file counted as not ending there.
	t.Run("dot_middle_and_end", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\ne2e.gone 10 1 y\ne2e.late 5 1 y\n.\n",
			"-g", "-NE", "-D")
		have(t, db, out, []string{"e2e.keep", "e2e.other", "e2e.gone", "e2e.late"}, []string{"e2e.ctl"})
		if !strings.Contains(out, "4 groups found") {
			t.Errorf("want 4 groups found:\n%s", out)
		}
		if strings.Contains(out, "not deleting") {
			t.Errorf("a note for a file whose last non-blank line is \".\":\n%s", out)
		}
		if !strings.Contains(out, "Deleting e2e.ctl") || strings.Contains(out, "Deleting e2e.gone") {
			t.Errorf("want a Deleting line for e2e.ctl and none for e2e.gone:\n%s", out)
		}
	})

	// a line after the "." (not blank) means the file does not end there
	t.Run("dot_then_line", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\nx", "-g", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want a note with %q:\n%s", note, out)
		}
		out = run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\nx\n", "-g", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, note) {
			t.Errorf("(x with a line end) want a note with %q:\n%s", note, out)
		}
	})

	// only blank lines after the last ".": empty lines and lines of blanks are no content, the
	// file ends with its "." and -D deletes
	for _, blank := range []struct{ name, tail string }{
		{"dot_then_blank_lf", ".\n\n\n \t\n"}, {"dot_then_blank_crlf", ".\r\n\r\n \t\r\n"},
	} {
		t.Run(blank.name, func(t *testing.T) {
			s, db := setup(t)
			out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\ne2e.new 5 1 y\n"+blank.tail,
				"-g", "-D")
			have(t, db, out, []string{"e2e.keep", "e2e.other", "e2e.new"}, []string{"e2e.gone", "e2e.ctl"})
			if strings.Contains(out, "not deleting") {
				t.Errorf("a note for a file that ends with \".\" and blank lines:\n%s", out)
			}
			if !strings.Contains(out, "Deleting e2e.gone") || !strings.Contains(out, "Deleting e2e.ctl") {
				t.Errorf("want Deleting lines for e2e.gone and e2e.ctl:\n%s", out)
			}
		})
	}

	// the "." is the first line and no "." ends the file: nothing is deleted, the note is
	// printed; the groups after the "." are read (e2e.new is made, "3 groups found")
	t.Run("dot_first", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, ".\ne2e.keep 10 1 y\ne2e.other 20 1 y\ne2e.new 5 1 y\n", "-g", "-D")
		have(t, db, out, append(local, "e2e.new"), nil)
		if !strings.Contains(out, "3 groups found") {
			t.Errorf("want 3 groups found:\n%s", out)
		}
		if !strings.Contains(out, note) {
			t.Errorf("want a note with %q:\n%s", note, out)
		}
		if strings.Contains(out, "Deleting") {
			t.Errorf("-D deleted with a file that does not end with \".\":\n%s", out)
		}
	})

	// without -D a "." in the middle is as on main: the rows after it are read (e2e.new is made),
	// no note, no deletion
	t.Run("appended_noD", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\ne2e.new 5 1 y\n", "-g", "-NE")
		have(t, db, out, append(local, "e2e.new"), nil)
		if !strings.Contains(out, "3 groups found") {
			t.Errorf("want 3 groups found:\n%s", out)
		}
		if strings.Contains(out, "not deleting") || strings.Contains(out, "Deleting") {
			t.Errorf("a note or a deletion without -D:\n%s", out)
		}
	})

	// a line of "." and a NUL byte is not the "." that ends the list: the line is read by its
	// length, not as the C string ".". It is a line of its own that has no group name and is
	// skipped (as on main), the rows after it are read (e2e.new is made), and the "." at the end
	// is the last non-blank line: e2e.gone and e2e.ctl are deleted, no note. Both forms: with
	// more bytes after the NUL, and NUL then the line end.
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

	// the same NUL line as the last line: it is not a "." line, so the file has no "." at its
	// end: nothing is deleted, the note. (Fails on 3892b341: ".\0" is taken for the "." and
	// e2e.gone and e2e.ctl are deleted.)
	t.Run("dot_nul_last", func(t *testing.T) {
		s, db := setup(t)
		out := run(t, s, db, "e2e.keep 10 1 y\ne2e.other 20 1 y\n.\x00junk", "-g", "-D")
		have(t, db, out, local, nil)
		if !strings.Contains(out, note) {
			t.Errorf("want a note with %q:\n%s", note, out)
		}
	})

	// a -G path that gives a read error: a directory opens with fopen() on Linux and read()
	// fails with EISDIR, so ferror() is set before any line. -D deletes nothing and the note
	// names the read error (not the missing "."); the creates and updates have nothing to do.
	// (A "." that is followed by a read error cannot be made with a file; the guard is the same
	// ferror() check for both.)
	t.Run("read_error", func(t *testing.T) {
		s, db := setup(t)
		dir := filepath.Join(s.Dir, "gfile.d")
		if err := os.Mkdir(dir, 0o755); err != nil {
			t.Fatal(err)
		}
		// The read error comes from read(2) on a directory (EISDIR); where a
		// directory can be read, there is no error to test.
		if _, err := os.ReadFile(dir); err == nil {
			t.Skip("read(2) of a directory does not fail on this platform")
		}
		out, err := s.TryRun("dsyncgroups", "-G", dir, "-f", db, "-D")
		if err != nil {
			t.Fatalf("dsyncgroups -G <dir>: %v\n%s", err, out)
		}
		have(t, db, out, local, nil)
		if !strings.Contains(out, "read error") || !strings.Contains(out, "not deleting groups (-D)") {
			t.Errorf("want a read error note:\n%s", out)
		}
		if strings.Contains(out, "Deleting") {
			t.Errorf("-D deleted after a read error:\n%s", out)
		}
	})
}
