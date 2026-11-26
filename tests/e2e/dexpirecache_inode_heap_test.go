package e2e

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

// inodeCacheEntry is one empty file or symbolic link made for the -i tests.
type inodeCacheEntry struct {
	name string // relative to the cache directory, as dexpirecache prints it after "./"
	sec  int64  // the ctime second (a new file's mtime is its ctime)
}

// makeInodeCache makes the cache directories "00".."03" in the site's spool/cache with two
// cache files with content (an hour old: -i never removes them) and then len(sizes) groups of
// empty files and symbolic links, each made within about one second (a slow machine may cross
// a second's end inside a group), a group gap after the one before (a multiple of a second). The names are not in the order they are made in and have
// different lengths ("e10" sorts before "e9"). It returns the entries oldest first, as -i
// must remove them: by ctime (the second of the entry's mtime), then by name.
func makeInodeCache(t *testing.T, cache string, sizes []int, gap time.Duration) []inodeCacheEntry {
	t.Helper()
	for _, d := range []string{"00", "01", "02", "03"} {
		if err := os.MkdirAll(filepath.Join(cache, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	old := time.Now().Add(-time.Hour)
	for _, n := range []string{"00/f1", "01/f2"} {
		p := filepath.Join(cache, n)
		if err := os.WriteFile(p, []byte(strings.Repeat("article line\r\n", 600)), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.Chtimes(p, old, old); err != nil {
			t.Fatal(err)
		}
		// dexpirecache counts a regular file with no blocks as an empty one
		if st, err := os.Lstat(p); err != nil || st.Sys().(*syscall.Stat_t).Blocks == 0 {
			t.Skipf("%s shows no blocks on this file system (%v)", p, err)
		}
	}
	total := 0
	for _, n := range sizes {
		total += n
	}
	var entries []inodeCacheEntry
	id := 0
	for _, n := range sizes {
		now := time.Now()
		time.Sleep(now.Truncate(time.Second).Add(gap + 100*time.Millisecond).Sub(now))
		first := len(entries)
		for j := 0; j < n; j++ {
			// 7919 is prime: the ids are all different and in no useful order
			k := (id*7919 + 13) % (total * 3)
			id++
			name := fmt.Sprintf("%02d/e%d", k%4, k)
			p := filepath.Join(cache, name)
			var err error
			if k%3 == 0 {
				err = os.Symlink("f1", p)
			} else {
				err = os.WriteFile(p, nil, 0o644)
			}
			if err != nil {
				t.Fatal(err)
			}
			entries = append(entries, inodeCacheEntry{name: name})
		}
		for i := first; i < len(entries); i++ {
			st, err := os.Lstat(filepath.Join(cache, entries[i].name))
			if err != nil {
				t.Fatal(err)
			}
			entries[i].sec = st.ModTime().Unix()
		}
	}
	slices.SortFunc(entries, func(a, b inodeCacheEntry) int {
		if a.sec != b.sec {
			if a.sec < b.sec {
				return -1
			}
			return 1
		}
		return strings.Compare("./"+a.name, "./"+b.name)
	})
	return entries
}

// inodeRemoved returns the names dexpirecache -v says it removes, in order.
func inodeRemoved(out string) []string {
	var removed []string
	for _, l := range strings.Split(out, "\n") {
		if r, ok := strings.CutPrefix(l, "Removing ./"); ok {
			removed = append(removed, r)
		}
	}
	return removed
}

// TestDexpirecacheInodeSet (D-305): which empty files and links -i removes, for several N (the
// inodes still needed): the N oldest by ctime, those of one ctime second by name (strcmp(), so
// "e10" before "e9"), oldest first in the -v output; all of them when N is larger than their
// number; nothing else. -i keeps only the N oldest entries while it scans (a bounded
// structure: before, a sorted list that each entry walked, which took N squared steps), so
// this checks the set against an order made here from the entries' times. The cache has 90
// entries made in three seconds (30 each, names in no order, in four directories, every third
// a symbolic link) and two cache files with content that stay. A preloaded library reports no
// free inodes. Passes before and after the change (the speed is not tested here: D-305).
func TestDexpirecacheInodeSet(t *testing.T) {
	so := preloadShim(t, "statfszero", statfsZeroSource)
	for _, n := range []int{1, 2, 29, 30, 31, 45, 59, 60, 61, 89, 90, 91, 1000} {
		t.Run("n"+strconv.Itoa(n), func(t *testing.T) {
			t.Parallel()
			s := NewSite(t, SiteOptions{NoStart: true})
			cache := filepath.Join(s.Dir, "spool", "cache")
			order := makeInodeCache(t, cache, []int{30, 30, 30}, time.Second)

			mark := filepath.Join(t.TempDir(), "statfs")
			base := s.Env
			s.Env = append(slices.Clone(base), lockFaultEnv(so), "STATFSZERO_MARK="+mark, "STATFSZERO_FFREE=0")
			args := []string{"-d", cache, "-v", "-i", strconv.Itoa(n)}
			out, err := s.TryRun("dexpirecache", args...)
			s.Env = base
			if b, rerr := os.ReadFile(mark); rerr != nil || len(b) == 0 {
				t.Fatalf("the shim was never asked (%v):\n%s", rerr, out)
			}
			if err != nil {
				t.Errorf("dexpirecache %s: %v", strings.Join(args, " "), err)
			}

			var want []string
			for _, e := range order[:min(n, len(order))] {
				want = append(want, e.name)
			}
			if removed := inodeRemoved(out); !slices.Equal(removed, want) {
				t.Errorf("removed %q, want %q", removed, want)
			}
			gone := func(p string) bool { _, err := os.Lstat(p); return err != nil }
			for _, e := range order {
				if g, w := gone(filepath.Join(cache, e.name)), slices.Contains(want, e.name); g != w {
					t.Errorf("%s removed: %v, want %v", e.name, g, w)
				}
			}
			for _, f := range []string{"00/f1", "01/f2"} {
				if gone(filepath.Join(cache, f)) {
					t.Errorf("cache file %s removed, want kept", f)
				}
			}
			if line := fmt.Sprintf("* %7d empty files and links removed\n", len(want)); !strings.Contains(out, line) {
				t.Errorf("no line %q", strings.TrimSuffix(line, "\n"))
			}
			if t.Failed() {
				t.Logf("dexpirecache %s: err=%v\n%s", strings.Join(args, " "), err, out)
			}
		})
	}
}

// TestDexpirecacheInodeTwoDays (D-305): the entries that the two-day rule of addSLink()
// removes during the scan (an empty file or link whose ctime is more than two days old) still
// take their places among the N that -i keeps, and removeLinks() cannot unlink them again. That
// is right and is checked here: those inodes are free already, so the N entries -i keeps are
// the N oldest of all, and a run frees the larger of N and the number of two-day entries, not
// their sum (D-303). Six entries are made, then eight five seconds later, and the clock the
// program sees (time(), moved by a preloaded library) is set so that the first six are more
// than two days old and the other eight are not. -i 3 and -i 6: the six go, the eight stay;
// -i 9: the six and the three oldest of the eight (by name); -i 1000: all fourteen. The count
// of empty files and links removed counts each once. Passes before and after the change.
func TestDexpirecacheInodeTwoDays(t *testing.T) {
	so := preloadShim(t, "statfstime", statfsZeroSource+withMark(timeShiftSource))
	for _, n := range []int{3, 6, 9, 1000} {
		t.Run("n"+strconv.Itoa(n), func(t *testing.T) {
			t.Parallel()
			s := NewSite(t, SiteOptions{NoStart: true})
			cache := filepath.Join(s.Dir, "spool", "cache")
			order := makeInodeCache(t, cache, []int{6, 8}, 5*time.Second)

			// the clock the program sees is set to two days and two seconds after the six were
			// made: they are more than two days old, the eight (five seconds later) are not
			tmp := t.TempDir()
			shift := filepath.Join(tmp, "shift")
			secs := order[0].sec + 2
			for i, e := range order {
				if (i < 6) != (e.sec < secs) || (i >= 6 && e.sec < secs+2) {
					t.Fatalf("setup: the entries are not six, and eight at least four seconds later: %v", order)
				}
			}
			if err := os.WriteFile(shift, []byte(strconv.FormatInt(2*24*3600+secs-time.Now().Unix(), 10)+"\n"), 0o644); err != nil {
				t.Fatal(err)
			}
			mark := filepath.Join(tmp, "statfs")
			base := s.Env
			s.Env = append(slices.Clone(base), lockFaultEnv(so), "STATFSZERO_MARK="+mark, "STATFSZERO_FFREE=0", "TIMESHIFT_FILE="+shift)
			args := []string{"-d", cache, "-v", "-i", strconv.Itoa(n)}
			out, err := s.TryRun("dexpirecache", args...)
			s.Env = base
			if b, rerr := os.ReadFile(mark); rerr != nil || len(b) == 0 {
				t.Fatalf("the shim was never asked (%v):\n%s", rerr, out)
			}
			if err != nil {
				t.Errorf("dexpirecache %s: %v", strings.Join(args, " "), err)
			}

			want := order[:min(max(n, 6), len(order))] // the six, or the N oldest
			gone := func(p string) bool { _, err := os.Lstat(p); return err != nil }
			for _, e := range order {
				w := slices.Contains(want, e)
				if g := gone(filepath.Join(cache, e.name)); g != w {
					t.Errorf("%s removed: %v, want %v", e.name, g, w)
				}
			}
			if line := fmt.Sprintf("* %7d empty files and links removed\n", len(want)); !strings.Contains(out, line) {
				t.Errorf("no line %q", strings.TrimSuffix(line, "\n"))
			}
			if t.Failed() {
				t.Logf("dexpirecache %s: err=%v\n%s", strings.Join(args, " "), err, out)
			}
		})
	}
}
