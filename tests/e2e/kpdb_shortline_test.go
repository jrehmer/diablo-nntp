package e2e

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	testkit "github.com/go-while/nntp-testkit"
)

// dkpBounded runs dkp with a deadline and waits for it to end; it returns the combined
// output and the error (a crash, or the kill when the deadline passed).
func dkpBounded(t *testing.T, args ...string) (string, error) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), testkit.Deadline())
	defer cancel()
	out, err := exec.CommandContext(ctx, filepath.Join(bin(t), "dkp"), args...).CombinedOutput()
	if ctx.Err() != nil {
		t.Logf("dkp %s: killed after the deadline", strings.Join(args, " "))
	}
	return string(out), err
}

// kpWithBlankLine writes records aa..dd with dkp and puts a blank line before the last one.
func kpWithBlankLine(t *testing.T) string {
	t.Helper()
	db := filepath.Join(t.TempDir(), "kp.db")
	for _, k := range []string{"aa", "bb", "cc", "dd"} {
		dkpRun(t, "", "-w", db, k, "x=1")
	}
	b, err := os.ReadFile(db)
	if err != nil {
		t.Fatal(err)
	}
	i := strings.LastIndex(strings.TrimRight(string(b), "\n"), "\n") + 1
	if err := os.WriteFile(db, []byte(string(b[:i])+"\n"+string(b[i:])), 0o644); err != nil {
		t.Fatal(err)
	}
	return db
}

// TestKPDBBlankLine (D-79): a blank or very short line in a KP database (a hand edit, or the
// debris of a crash) corrupts the sort: KPDBSort() writes the 8 hex digits of a record's
// offset at line start + 1 whatever the line's length, over the following line, and a
// lookup then recurses until the stack is gone (dkp dies with SIGSEGV).
func TestKPDBBlankLine(t *testing.T) {
	keys := []string{"aa", "bb", "cc", "dd"}

	// Plain lookups; whether the file is sorted first depends on its timestamps.
	t.Run("lookup", func(t *testing.T) {
		db := kpWithBlankLine(t)
		for _, k := range keys {
			if out, err := dkpBounded(t, "-r", db, k); err != nil || !strings.Contains(out, ":"+k+" x=1") {
				t.Errorf("dkp -r after a blank line was added to the KP file: record %s: err=%v output %q", k, err, out)
			}
		}
	})

	// A forced sort (dkp -s): no record line may lose its '+' to the offset digits.
	t.Run("forced sort", func(t *testing.T) {
		db := kpWithBlankLine(t)
		if out, err := dkpBounded(t, "-s", "-r", db); err != nil {
			t.Errorf("dkp -s -r: %v: %q", err, out)
		}
		b, err := os.ReadFile(db)
		if err != nil {
			t.Fatal(err)
		}
		for _, k := range keys {
			ok := false
			for _, l := range strings.Split(string(b), "\n") {
				if strings.HasSuffix(l, ":"+k+" x=1") && strings.HasPrefix(l, "+") {
					ok = true
				}
			}
			if !ok {
				t.Errorf("after dkp -s the record %s is damaged: the sort wrote offset digits over the line after the blank line\n%q", k, b)
			}
		}
		for _, k := range keys {
			if out, err := dkpBounded(t, "-r", db, k); err != nil || !strings.Contains(out, ":"+k+" x=1") {
				t.Errorf("dkp -r %s after dkp -s: err=%v output %q", k, err, out)
			}
		}
	})

	// A blank line at the very end of the file (page-sized files made the old code read
	// one byte past the map).
	t.Run("trailing blank line", func(t *testing.T) {
		db := kpWithBlankLine(t)
		b, err := os.ReadFile(db)
		if err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(db, append(b, '\n'), 0o644); err != nil {
			t.Fatal(err)
		}
		for _, k := range keys {
			if out, err := dkpBounded(t, "-r", db, k); err != nil || !strings.Contains(out, ":"+k+" x=1") {
				t.Errorf("dkp -r %s with a blank last line: err=%v output %q", k, err, out)
			}
		}
		if out, err := dkpBounded(t, "-s", "-r", db); err != nil {
			t.Errorf("dkp -s -r with a blank last line: %v: %q", err, out)
		}
		for _, k := range keys {
			if out, err := dkpBounded(t, "-r", db, k); err != nil || !strings.Contains(out, ":"+k+" x=1") {
				t.Errorf("dkp -r %s after the sort: err=%v output %q", k, err, out)
			}
		}
	})
}
