package e2e

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// offerDated offers the site an article whose Date: is date and returns the final status of the
// IHAVE: 235 accepted, 437 rejected.
func offerDated(t *testing.T, s *Site, date string) int {
	t.Helper()
	a := newArticle(msgID(t), "e2e.test", "dated "+date, "body of an article")
	for i, l := range a {
		if strings.HasPrefix(l, "Date: ") {
			a[i] = "Date: " + date
		}
	}
	id := headerValue(a, "Message-ID")
	p := s.Peer(t)
	defer expect(t, p, 205, "QUIT")
	expect(t, p, 335, "IHAVE %s", id)
	send(t, p, wire(a))
	code, line, err := p.ReadStatus()
	if err != nil {
		t.Fatalf("IHAVE %s: %v", date, err)
	}
	if code != 235 && code != 437 {
		t.Fatalf("IHAVE with Date %q: %d %q, want 235 or 437", date, code, line)
	}
	return code
}

// TestFeederMaxAcceptAge: the age limit for incoming articles. By default (the harness has
// remember 7, which sets it) an article dated years ago or years ahead is rejected. A positive
// feedermaxacceptage is the limit; 0 turns the check off, as do diablo -r 0 and remember 0, which
// sets it. A date diablo cannot read at all is not checked either way.
//
// The harness writes its directives in alphabetical order, so a feedermaxacceptage line would come
// before the harness's remember line and be overridden by it ("this value will only override
// remember if placed later in this file"); the line is put after remember in the value of remember.
func TestFeederMaxAcceptAge(t *testing.T) {
	old := "Mon, 01 Jan 2001 12:00:00 +0000"
	future := "Fri, 01 Jan 2100 12:00:00 +0000"
	now := time.Now().UTC().Format("Mon, 02 Jan 2006 15:04:05 -0700")
	for _, c := range []struct {
		name   string
		config map[string]string
		args   []string
		oldOK  bool
	}{
		{"default", nil, nil, false},
		{"feedermaxacceptage 14", map[string]string{"remember": "7\nfeedermaxacceptage 14"}, nil, false},
		{"feedermaxacceptage 0", map[string]string{"remember": "7\nfeedermaxacceptage 0"}, nil, true},
		{"remember 0", map[string]string{"remember": "0"}, nil, true},
		{"remember 0, then feedermaxacceptage 14", map[string]string{"remember": "0\nfeedermaxacceptage 14"}, nil, false},
		{"-r 0", nil, []string{"-r", "0"}, true},
		{"-r 14", nil, []string{"-r", "14"}, false},
	} {
		t.Run(c.name, func(t *testing.T) {
			s := NewSite(t, SiteOptions{Config: c.config, Args: c.args})
			if code := offerDated(t, s, now); code != 235 {
				t.Errorf("an article dated now: %d, want 235", code)
			}
			want := 437
			if c.oldOK {
				want = 235
			}
			if code := offerDated(t, s, old); code != want {
				t.Errorf("an article dated 2001: %d, want %d", code, want)
			}
			if code := offerDated(t, s, future); code != want {
				t.Errorf("an article dated 2100: %d, want %d", code, want)
			}
			// the other formats are read as well (lib/parsedate.c, TestParseDate)
			for _, d := range []string{"Mon Jan  1 12:00:00 2001", "Monday, 01-Jan-01 12:00:00 GMT", "2001-01-01T12:00:00Z", "Jan 1, 2001"} {
				if code := offerDated(t, s, d); code != want {
					t.Errorf("an article dated %q: %d, want %d", d, code, want)
				}
			}
			// not a date: not checked
			if code := offerDated(t, s, "banana"); code != 235 {
				t.Errorf("an article with the unreadable Date banana: %d, want 235", code)
			}
		})
	}
}

// TestDhisexpireRemember: dhisexpire drops the history entries of expired articles that are
// older than remember (-r, default diablo.config), keeps those of articles still on the spool,
// and with remember 0 keeps every entry.
func TestDhisexpireRemember(t *testing.T) {
	for _, c := range []struct {
		name    string
		args    []string
		dropped string
		kept    string
	}{
		{"default", nil, "1 expired entries dropped", "2 entries kept"},
		{"-r 7d", []string{"-r", "7d"}, "1 expired entries dropped", "2 entries kept"},
		{"-r 60d", []string{"-r", "60d"}, "0 expired entries dropped", "3 entries kept"},
		{"-r 0", []string{"-r", "0"}, "0 expired entries dropped", "3 entries kept"},
	} {
		t.Run(c.name, func(t *testing.T) {
			s := NewSite(t, SiteOptions{NoStart: true})
			hist := strings.TrimSpace(s.Run(t, "dpath", "dhistory"))
			now := time.Now().Unix() / 60
			old := now - 30*24*60
			// an old expired entry, a new expired one, an old one still on the spool
			entries := fmt.Sprintf("DUMP 11111111.22222222.0 gm=%d ex=16384 boff=0 bsize=0 flags=\n"+
				"DUMP 33333333.44444444.0 gm=%d ex=16384 boff=0 bsize=0 flags=\n"+
				"DUMP 55555555.66666666.0 gm=%d ex=0 boff=0 bsize=0 flags=\n.\n", old, now, old)
			run := func(name string, stdin string, args ...string) string {
				t.Helper()
				cmd := exec.Command(filepath.Join(s.Bin, name), args...)
				cmd.Dir = s.Dir
				cmd.Env = append(os.Environ(), s.Env...)
				cmd.Stdin = strings.NewReader(stdin)
				out, err := cmd.CombinedOutput()
				if err != nil {
					t.Fatalf("%s %v: %v\n%s", name, args, err, out)
				}
				return string(out)
			}
			if out := run("diload", entries, "-f", hist); !strings.Contains(out, "3/3 entries loaded") {
				t.Fatalf("diload:\n%s", out)
			}
			out := run("dhisexpire", "", append(append([]string{"-a", "-v"}, c.args...), hist)...)
			for _, want := range []string{c.dropped, c.kept} {
				if !strings.Contains(out, want) {
					t.Errorf("dhisexpire %v: no %q in\n%s", c.args, want, out)
				}
			}
		})
	}
}
