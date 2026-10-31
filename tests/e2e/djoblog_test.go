package e2e

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"
)

// djobLine matches a line of a djoblog log: the time stamp as in the other diablo logs, the
// job and the process id of djoblog, and the text.
var djobLine = regexp.MustCompile(`^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} ([A-Za-z0-9._-]+)\[\d+\]: (.*)$`)

// djobRun runs "djoblog args..." in the site and returns the output and the exit status.
func djobRun(t *testing.T, s *Site, args ...string) (string, int) {
	t.Helper()
	out, err := s.TryRun("djoblog", args...)
	var ee *exec.ExitError
	switch {
	case err == nil:
		return out, 0
	case errors.As(err, &ee):
		return out, ee.ExitCode()
	}
	t.Fatalf("djoblog %v: %v\n%s", args, err, out)
	return "", -1
}

// djobText returns the texts of the log lines of job in file, failing on a line that is not in
// the format.
func djobText(t *testing.T, file, job string) []string {
	t.Helper()
	b, err := os.ReadFile(file)
	if err != nil {
		t.Fatalf("log of %s: %v", job, err)
	}
	var texts []string
	for _, line := range strings.Split(strings.TrimSuffix(string(b), "\n"), "\n") {
		m := djobLine.FindStringSubmatch(line)
		if m == nil || m[1] != job {
			t.Fatalf("line %q is not a %s log line", line, job)
		}
		texts = append(texts, m[2])
	}
	return texts
}

// TestDjoblogLogsLines: every line of the output, of stdout and of stderr, gets a time stamp
// and the job name; the log starts with BEGIN and ends with END and the exit status; a last
// line without a newline is logged; the log is <path_log>/<job>.log; nothing is printed.
func TestDjoblogLogsLines(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	out, rc := djobRun(t, s, "myjob", "sh", "-c", "echo one; echo two >&2; printf 'no newline'")
	if rc != 0 || out != "" {
		t.Errorf("exit status %d, output %q; want 0 and no output", rc, out)
	}
	texts := djobText(t, filepath.Join(s.Dir, "log", "myjob.log"), "myjob")
	if len(texts) != 5 || !strings.HasPrefix(texts[0], "BEGIN sh -c ") || texts[len(texts)-1] != "END (exit status 0)" {
		t.Fatalf("log texts: %q", texts)
	}
	got := strings.Join(texts[1:4], "|")
	if got != "one|two|no newline" && got != "two|one|no newline" {
		t.Errorf("output lines: %q", texts[1:4])
	}
}

// TestDjoblogExitStatus: the exit status is the command's, 128 plus the signal for a killed one,
// 127 for one that cannot be executed; the log says so.
func TestDjoblogExitStatus(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	for _, c := range []struct {
		name string
		args []string
		rc   int
		end  string
	}{
		{"fail", []string{"sh", "-c", "exit 3"}, 3, "END (exit status 3)"},
		{"killed", []string{"sh", "-c", "kill -9 $$"}, 137, "END (killed by signal 9)"},
		{"missing", []string{"/nonexistent/command"}, 127, "END (exit status 127)"},
	} {
		_, rc := djobRun(t, s, append([]string{c.name}, c.args...)...)
		if rc != c.rc {
			t.Errorf("%s: exit status %d, want %d", c.name, rc, c.rc)
		}
		texts := djobText(t, filepath.Join(s.Dir, "log", c.name+".log"), c.name)
		if texts[len(texts)-1] != c.end {
			t.Errorf("%s: last log line %q, want %q", c.name, texts[len(texts)-1], c.end)
		}
	}
	b, _ := os.ReadFile(filepath.Join(s.Dir, "log", "missing.log"))
	if !strings.Contains(string(b), "cannot execute /nonexistent/command") {
		t.Errorf("missing command not explained in the log:\n%s", b)
	}
}

// TestDjoblogUsage: a job name that is not a plain name, or no command, exits 2 and writes nothing.
func TestDjoblogUsage(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	for _, args := range [][]string{
		{"../evil", "true"}, {"a/b", "true"}, {".hidden", "true"}, {"bad name", "true"},
		{"onlyjob"}, {},
	} {
		if out, rc := djobRun(t, s, args...); rc != 2 {
			t.Errorf("djoblog %q: exit status %d, want 2\n%s", args, rc, out)
		}
	}
	if m, _ := filepath.Glob(filepath.Join(s.Dir, "log", "*.log")); len(m) != 0 {
		t.Errorf("logs written: %q", m)
	}
}

// TestDjoblogLines: control characters cannot break the format, a long line is split, the -f
// option names the file, and the options of the command are not taken for djoblog's.
func TestDjoblogLines(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	file := filepath.Join(t.TempDir(), "named.log")
	_, rc := djobRun(t, s, "-f", file, "ctl", "sh", "-c", `printf 'a\001b\n'; head -c 5000 /dev/zero | tr '\0' x; echo; echo "$1"`, "sh", "-C")
	if rc != 0 {
		t.Fatalf("exit status %d", rc)
	}
	texts := djobText(t, file, "ctl")
	if _, err := os.Stat(filepath.Join(s.Dir, "log", "ctl.log")); err == nil {
		t.Error("-f: the default log was written as well")
	}
	var long []int
	var seen []string
	for _, x := range texts[1 : len(texts)-1] {
		seen = append(seen, x)
		if strings.HasPrefix(x, "xxx") {
			long = append(long, len(x))
		}
	}
	if seen[0] != "a?b" {
		t.Errorf("control character: %q", seen[0])
	}
	if len(long) != 2 || long[0] != 4096 || long[1] != 904 {
		t.Errorf("5000 characters came as lines of %v, want 4096 and 904", long)
	}
	if seen[len(seen)-1] != "-C" {
		t.Errorf("the option of the command: %q", seen[len(seen)-1])
	}
}

// TestDjoblogRotation: the log is opened for each batch of lines, so renaming it while the job
// runs sends the rest to a new file.
func TestDjoblogRotation(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	logf := filepath.Join(s.Dir, "log", "slow.log")
	cmd := exec.Command(filepath.Join(s.Bin, "djoblog"), "slow", "sh", "-c", "echo before; sleep 3; echo after")
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 10*time.Second, "the first line in the log", func() bool {
		b, _ := os.ReadFile(logf)
		return strings.Contains(string(b), ": before")
	})
	if err := os.Rename(logf, logf+".0"); err != nil {
		t.Fatal(err)
	}
	if err := cmd.Wait(); err != nil {
		t.Fatalf("djoblog: %v", err)
	}
	old := djobText(t, logf+".0", "slow")
	cur := djobText(t, logf, "slow")
	if len(old) != 2 || old[1] != "before" {
		t.Errorf("rotated log: %q", old)
	}
	if len(cur) != 2 || cur[0] != "after" || cur[1] != "END (exit status 0)" {
		t.Errorf("new log: %q", cur)
	}
}

// TestDjoblogSyslog: -s logs to syslog and not to a file; the exit status is still the command's.
func TestDjoblogSyslog(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	out, rc := djobRun(t, s, "-s", "sysjob", "sh", "-c", "echo to syslog; exit 4")
	if rc != 4 || out != "" {
		t.Errorf("exit status %d, output %q; want 4 and no output", rc, out)
	}
	if m, _ := filepath.Glob(filepath.Join(s.Dir, "log", "sysjob*")); len(m) != 0 {
		t.Errorf("-s wrote files: %q", m)
	}
}

// TestAdmDailyAtrimJobLogs: adm/daily.atrim rotates the logs of the cron jobs (djoblog's
// <job>.log) too, and its own, which djoblog opens again for the end of the run.
func TestAdmDailyAtrimJobLogs(t *testing.T) {
	s := NewSite(t, SiteOptions{Setup: withAdm(admSource(t))})
	names := []string{"hourly.expire.log", "daily.reader.log", "weekly.reader.log", "biweekly.atrim.log"}
	for _, n := range names {
		if err := os.WriteFile(filepath.Join(s.Dir, "log", n), []byte(n+"\n"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	out, rc := djobRun(t, s, "daily.atrim", filepath.Join(s.Dir, "adm", "daily.atrim"))
	if rc != 0 {
		t.Fatalf("daily.atrim under djoblog: exit status %d\n%s", rc, out)
	}
	for _, n := range names {
		b, err := os.ReadFile(filepath.Join(s.Dir, "log", n+".0"))
		if err != nil || string(b) != n+"\n" {
			t.Errorf("%s.0 after the rotation: %q, %v", n, b, err)
		}
	}
	texts := djobText(t, filepath.Join(s.Dir, "log", "daily.atrim.log"), "daily.atrim")
	if texts[len(texts)-1] != "END (exit status 0)" {
		t.Errorf("the end of daily.atrim is not in the new daily.atrim.log: %q", texts)
	}
}
