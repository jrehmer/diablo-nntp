package e2e

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// TestParseDate: the date formats parsedate() (lib/parsedate.c, used for the age check of
// incoming articles and by didate) reads, and what it makes of them, in UTC: the time zone of
// the date is applied. Shown by didate with TZ=UTC.
func TestParseDate(t *testing.T) {
	const illegal = "Illegal format"
	s := NewSite(t, SiteOptions{NoStart: true})
	for _, c := range []struct{ in, want string }{
		// RFC 5322 / RFC 822
		{"Mon, 01 Jan 2001 12:00:00 +0000", "Mon Jan  1 12:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00:00 -0500", "Mon Jan  1 17:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00:00 +0530", "Mon Jan  1 06:30:00 2001"},
		{"Mon, 1 Jan 2001 12:00:00 +0000 (UTC)", "Mon Jan  1 12:00:00 2001"},
		{"1 Jan 2001 12:00:00 GMT", "Mon Jan  1 12:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00 +0000", "Mon Jan  1 12:00:00 2001"},
		{"01 Jan 2001", "Mon Jan  1 00:00:00 2001"},
		{"Mon, 01 Jan 01 12:00:00 GMT", "Mon Jan  1 12:00:00 2001"},
		{"Thu, 01 Jan 76 12:00:00 GMT", "Thu Jan  1 12:00:00 1976"},
		{"Mon, 01 Jan 101 12:00:00 GMT", "Mon Jan  1 12:00:00 2001"},
		{"mon, 01 jan 2001 12:00:00 gmt", "Mon Jan  1 12:00:00 2001"},
		{"1 January 2001 12:00:00 GMT", "Mon Jan  1 12:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00:00 EST", "Mon Jan  1 17:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00:00 PDT", "Mon Jan  1 19:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00:00 CET", "Mon Jan  1 11:00:00 2001"},
		{"Mon, 01 Jan 2001 12:00:00 GMT+0100", "Mon Jan  1 11:00:00 2001"},
		{"Date: Mon, 01 Jan 2001 12:00:00 +0000", "Mon Jan  1 12:00:00 2001"},
		// the years of Usenet and beyond
		{"Tue, 01 Jan 1980 00:00:00 GMT", "Tue Jan  1 00:00:00 1980"},
		{"Wed, 01 Jan 1969 12:00:00 +0000", "Wed Jan  1 12:00:00 1969"},
		{"Mon, 01 Jan 1900 12:00:00 +0000", "Mon Jan  1 12:00:00 1900"},
		{"Fri, 01 Jan 2100 12:00:00 +0000", "Fri Jan  1 12:00:00 2100"},
		// RFC 850 / RFC 1036 (hyphens)
		{"Monday, 01-Jan-01 12:00:00 GMT", "Mon Jan  1 12:00:00 2001"},
		{"Monday, 01-Jan-2001 12:00:00 GMT", "Mon Jan  1 12:00:00 2001"},
		{"Wed, 1-Jan-86 14:05 EST", "Wed Jan  1 19:05:00 1986"},
		// ctime, asctime, date(1)
		{"Mon Jan  1 12:00:00 2001", "Mon Jan  1 12:00:00 2001"},
		{"Mon Jan  1 12:00:00 UTC 2001", "Mon Jan  1 12:00:00 2001"},
		{"Mon Jan  1 12:00:00 EST 2001", "Mon Jan  1 17:00:00 2001"},
		{"Sat Oct 31 05:40:12 2026", "Sat Oct 31 05:40:12 2026"},
		// US
		{"Jan 1, 2001 12:00:00", "Mon Jan  1 12:00:00 2001"},
		{"January 1, 2001", "Mon Jan  1 00:00:00 2001"},
		{"Jan 1, 2001 1:30:00 PM", "Mon Jan  1 13:30:00 2001"},
		{"Jan 1, 2001 12:00:00 AM", "Mon Jan  1 00:00:00 2001"},
		// ISO 8601
		{"2001-01-01", "Mon Jan  1 00:00:00 2001"},
		{"2001-01-01T12:00:00Z", "Mon Jan  1 12:00:00 2001"},
		{"2001-01-01 12:00:00 +0000", "Mon Jan  1 12:00:00 2001"},
		{"2001-01-01T12:00:00+01:00", "Mon Jan  1 11:00:00 2001"},
		{"2001-01-01T12:00:00.123Z", "Mon Jan  1 12:00:00 2001"},
		{"20010101T120000Z", "Mon Jan  1 12:00:00 2001"},
		{"2001/01/01 12:00:00", "Mon Jan  1 12:00:00 2001"},
		// European, Unix time
		{"01.01.2001 12:00", "Mon Jan  1 12:00:00 2001"},
		{"978350400", "Mon Jan  1 12:00:00 2001"},
		// month names of other languages
		{"lun, 01 mars 2001 12:00:00 +0100", "Thu Mar  1 11:00:00 2001"},
		{"Mo, 01 Mär 2001 12:00:00 +0100", "Thu Mar  1 11:00:00 2001"},
		{"1 déc 2001 12:00:00 GMT", "Sat Dec  1 12:00:00 2001"},
		{"1 ene 2001", "Mon Jan  1 00:00:00 2001"},
		// not a date, or not certain: no result
		{"banana", illegal},
		{"", illegal},
		{"12:00:00", illegal},
		{"01/01/2001", illegal}, // day and month in either order
		{"Oct 31 05:40:12", illegal}, // no year
		{"Mon, 32 Jan 2001 12:00:00 GMT", illegal},
		{"Mon, 00 Jan 2001 12:00:00 GMT", illegal},
		{"Mon, 01 Jan 2001 25:00:00 GMT", illegal},
		{"Mon, 01 Foo 2001 12:00:00 GMT", illegal},
	} {
		cmd := exec.Command(filepath.Join(s.Bin, "didate"), c.in)
		cmd.Dir = s.Dir
		cmd.Env = append(append(os.Environ(), s.Env...), "TZ=UTC")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Errorf("didate %q: %v\n%s", c.in, err, out)
			continue
		}
		if got := strings.TrimSpace(string(out)); got != c.want {
			t.Errorf("%q: %q, want %q", c.in, got, c.want)
		}
	}
}
