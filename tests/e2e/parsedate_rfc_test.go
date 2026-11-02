package e2e

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestParseDateRFC(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	for _, tc := range []struct{ date, tz, want string }{
		{"Mon, 01 Jan 2001 12:00:00 +0000", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"01 Jan 2001 12:00 +1", "UTC", "Mon Jan  1 11:00:00 2001"},
		{"01 Jan 2001 12:00 GMT+1", "UTC", "Mon Jan  1 11:00:00 2001"},
		{"01 Jan 2001 12:00 GMT+05:30", "UTC", "Mon Jan  1 06:30:00 2001"},
		{"01 Jan 2001 12:00 +05:30", "UTC", "Mon Jan  1 06:30:00 2001"},
		{"01 Jan 2001 12:00 +530", "UTC", "Mon Jan  1 06:30:00 2001"},
		{"01 Jan 2001 12:00 -03:30", "UTC", "Mon Jan  1 15:30:00 2001"},
		{"01 Jan 2001 12:00 EST", "UTC", "Mon Jan  1 17:00:00 2001"},
		{"Fri, 5 May 95 13:23 BST-1", "UTC", "Fri May  5 14:23:00 1995"},
		{"01 Jan 2001 12:00 MEZT", "UTC", "Mon Jan  1 10:00:00 2001"},
		{"01 Jan 2001 12:00 unknown", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"01 Jan 2001 12:00", "XXX-3", "Mon Jan  1 12:00:00 2001"},
		{"28 Oct 2000", "UTC", "Sat Oct 28 00:00:00 2000"},
		{"31 Feb 2001", "UTC", "Illegal format"},
		{"01 Jan 2001 12:00 GMT", "XXX-3", "Mon Jan  1 15:00:00 2001"},
		{"01 Jan 49 12:00 GMT", "UTC", "Fri Jan  1 12:00:00 2049"},
		{"01 Jan 50 12:00 GMT", "UTC", "Sun Jan  1 12:00:00 1950"},
		{"29 Feb 2000 12:00 GMT", "UTC", "Tue Feb 29 12:00:00 2000"},
		{"29 Feb 1900 12:00 GMT", "UTC", "Illegal format"},
		{"29 Feb 2100 12:00 GMT", "UTC", "Illegal format"},
		{"31 Feb 2001 12:00 GMT", "UTC", "Illegal format"},
		{"01 Jan 2001 24:00 GMT", "UTC", "Illegal format"},
		{"01 Jan 2001 12:00 +05:99", "UTC", "Illegal format"},
		{"01 Jan 2001 12:00 GMT (comment)", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"01 Jan 2001 12:00 GMT trailing text", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"01 January 2001 1:2:3 GMT", "UTC", "Mon Jan  1 01:02:03 2001"},
		{"Mon Jan  1 12:00:00 2001", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"Mon Jan  1 12:00:00 UTC 2001", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"Mon Jan  1 12:00:00 EST 2001", "UTC", "Mon Jan  1 17:00:00 2001"},
		{"Mon Jan  1 12:00:00 2001", "XXX-3", "Mon Jan  1 12:00:00 2001"},
		{"Mon Jan  1 12:00:00 XYZ 2001", "UTC", "Illegal format"},
		{"Tue Jan  1 12:00:00 2001", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"Mon Jan 31 12:00:00 01", "UTC", "Illegal format"},
		{"Mon Feb 30 12:00:00 2001", "UTC", "Illegal format"},
		{"Mon Foo  1 12:00:00 2001", "UTC", "Illegal format"},
		{"Mon Jan  1 25:00:00 2001", "UTC", "Illegal format"},
		{"Sunday, 06-Nov-94 08:49:37 GMT", "UTC", "Sun Nov  6 08:49:37 1994"},
		{"Monday, 01-Jan-2001 12:00:00 EST", "UTC", "Mon Jan  1 17:00:00 2001"},
		{"1-Jan-01 12:00:00 GMT", "UTC", "Mon Jan  1 12:00:00 2001"},
		{"Monday, 31-Feb-01 12:00:00 GMT", "UTC", "Illegal format"},
		{"01-Jan-49 12:00:00 GMT", "UTC", "Fri Jan  1 12:00:00 2049"},
		{"01 Jan 49 12:00:00 GMT", "UTC", "Fri Jan  1 12:00:00 2049"},
		{"01-Jan-50 12:00:00 GMT", "UTC", "Sun Jan  1 12:00:00 1950"},
		{"01 Jan 50 12:00:00 GMT", "UTC", "Sun Jan  1 12:00:00 1950"},
		{"2001-01-01T12:00:00Z", "UTC", "Illegal format"},
		{"3/22/2005 1:04:26 PM", "UTC", "Illegal format"},
		{"7th May 1993", "UTC", "Illegal format"},
		{"978350400", "UTC", "Illegal format"},
	} {
		cmd := exec.Command(filepath.Join(s.Bin, "didate"), tc.date)
		cmd.Dir = s.Dir
		cmd.Env = append(append(os.Environ(), s.Env...), "TZ="+tc.tz, "LC_ALL=C")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Errorf("didate %q: %v\n%s", tc.date, err, out)
		} else if got := strings.TrimSpace(string(out)); got != tc.want {
			t.Errorf("didate %q TZ=%s: got %q, want %q", tc.date, tc.tz, got, tc.want)
		}
	}
}

func TestStrictDateConfigValues(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	for _, tc := range []struct {
		value string
		valid bool
	}{
		{"yes", true}, {"YES", true}, {"no", true}, {"on", true},
		{"off", true}, {"1", true}, {"0", true},
		{"", false}, {"maybe", false}, {"yees", false}, {"10", false},
	} {
		t.Run(tc.value, func(t *testing.T) {
			config := filepath.Join(t.TempDir(), "diablo.config")
			if err := os.WriteFile(config, []byte("version 2.0\nstrictdate "+tc.value+"\n"), 0600); err != nil {
				t.Fatal(err)
			}
			cmd := exec.Command(filepath.Join(s.Bin, "didate"), "01 Jan 2001 12:00 GMT")
			cmd.Env = append(os.Environ(), "DIABLO_CONFIG_PATH="+config, "TZ=UTC")
			out, err := cmd.CombinedOutput()
			if tc.valid && err != nil {
				t.Fatalf("valid strictdate %q: %v\n%s", tc.value, err, out)
			}
			if !tc.valid && (err == nil || !strings.Contains(string(out), "strictdate")) {
				t.Fatalf("invalid strictdate %q: err=%v output=%s", tc.value, err, out)
			}
		})
	}
}
