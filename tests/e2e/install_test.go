package e2e

// install-portable.sh with DESTDIR (no root needed): configuration files are real files
// that a reinstall never overwrites, adm scripts are executable, old config symlinks into
// samples/ become real copies.

import (
	"bytes"
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	testkit "github.com/go-while/nntp-testkit"
)

// sourceRoot returns the source tree of the build in DIABLO_BIN (its parent), or skips.
func sourceRoot(tb testing.TB) string {
	tb.Helper()
	root := filepath.Dir(bin(tb))
	if _, err := os.Stat(filepath.Join(root, "install-portable.sh")); err != nil {
		tb.Skipf("no install-portable.sh next to %s", bin(tb))
	}
	return root
}

// install runs install-portable.sh <mode> /news <stage> in the source tree.
func install(tb testing.TB, root, mode, stage string) string {
	tb.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), testkit.Deadline())
	defer cancel()
	cmd := exec.CommandContext(ctx, "/bin/sh", "./install-portable.sh", mode, "/news", stage)
	cmd.Dir = root
	out, err := cmd.CombinedOutput()
	if err != nil {
		tb.Fatalf("install-portable.sh %s: %v\n%s", mode, err, out)
	}
	return string(out)
}

func fileMode(tb testing.TB, path string) os.FileMode {
	tb.Helper()
	st, err := os.Lstat(path)
	if err != nil {
		tb.Fatal(err)
	}
	return st.Mode()
}

func TestInstallConfigFiles(t *testing.T) {
	root := sourceRoot(t)
	stage := t.TempDir()
	news := filepath.Join(stage, "news")
	install(t, root, "install", stage)

	// Real files, readable only by owner and group, with the sample's content.
	for _, name := range []string{"diablo.config", "dnewsfeeds", "dreader.access", "dspool.ctl"} {
		p := filepath.Join(news, name)
		m := fileMode(t, p)
		if !m.IsRegular() || m.Perm() != 0o640 {
			t.Errorf("%s: mode %v, want a regular file with 0640", name, m)
		}
		want, _ := os.ReadFile(filepath.Join(root, "samples", name))
		if got, _ := os.ReadFile(p); !bytes.Equal(got, want) {
			t.Errorf("%s: content differs from samples/%s", name, name)
		}
	}
	if st, err := os.Stat(filepath.Join(news, "dactive.kp")); err != nil || st.Size() != 0 {
		t.Errorf("dactive.kp: %v, size %v; want an empty file", err, st)
	}

	// Logs and run files name clients and users: the news user and group only.
	for _, dir := range []string{"log", "run"} {
		if m := fileMode(t, filepath.Join(news, dir)); !m.IsDir() || m.Perm() != 0o750 {
			t.Errorf("%s: mode %v, want a directory with 0750", dir, m)
		}
	}
	if m := fileMode(t, filepath.Join(news, "dqueue")); m.Perm() != 0o755 {
		t.Errorf("dqueue: mode %v, want 0755", m)
	}

	// adm scripts: executable where the source is; the reference copy keeps the bit too.
	adm, _ := filepath.Glob(filepath.Join(root, "samples", "adm", "*"))
	execs := 0
	for _, src := range adm {
		st, err := os.Stat(src)
		if err != nil || !st.Mode().IsRegular() {
			continue
		}
		name := filepath.Base(src)
		want := os.FileMode(0o644)
		if st.Mode()&0o111 != 0 {
			want = 0o755
			execs++
			if m := fileMode(t, filepath.Join(news, "samples", "adm", name)); m.Perm() != 0o555 {
				t.Errorf("samples/adm/%s: mode %v, want 0555", name, m.Perm())
			}
		}
		if m := fileMode(t, filepath.Join(news, "adm", name)); m.Perm() != want {
			t.Errorf("adm/%s: mode %v, want %v", name, m.Perm(), want)
		}
	}
	if execs == 0 {
		t.Errorf("no executable script in samples/adm: test can't check the execute bit")
	}

	// A reinstall keeps local changes: the config file as it is, a changed adm script with
	// the new version next to it.
	local := []byte("# local change\n")
	if err := os.WriteFile(filepath.Join(news, "diablo.config"), local, 0o640); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(news, "adm", "rot"), local, 0o755); err != nil {
		t.Fatal(err)
	}
	install(t, root, "install", stage)
	if got, _ := os.ReadFile(filepath.Join(news, "diablo.config")); !bytes.Equal(got, local) {
		t.Errorf("diablo.config overwritten by a reinstall: %q", got)
	}
	if got, _ := os.ReadFile(filepath.Join(news, "adm", "rot")); !bytes.Equal(got, local) {
		t.Errorf("changed adm/rot overwritten by a reinstall")
	}
	if _, err := os.Stat(filepath.Join(news, "adm", "rot.new")); err != nil {
		t.Errorf("no adm/rot.new next to the changed script: %v", err)
	}

	// Uninstall leaves the configuration.
	install(t, root, "uninstall", stage)
	if _, err := os.Stat(filepath.Join(news, "diablo.config")); err != nil {
		t.Errorf("uninstall removed diablo.config: %v", err)
	}
	if _, err := os.Stat(filepath.Join(news, "dbin", "diablo")); err == nil {
		t.Errorf("uninstall left dbin/diablo")
	}
}

// TestInstallConvertsConfigSymlinks: an earlier install's symlink /news/dnewsfeeds ->
// /news/samples/dnewsfeeds becomes a real file with the content in use, before samples/ is
// replaced; with install and with bininstall.
func TestInstallConvertsConfigSymlinks(t *testing.T) {
	root := sourceRoot(t)
	for _, mode := range []string{"install", "bininstall"} {
		t.Run(mode, func(t *testing.T) {
			stage := t.TempDir()
			news := filepath.Join(stage, "news")
			install(t, root, "install", stage)
			old := []byte("# the old sample this site ran with\n")
			sample := filepath.Join(news, "samples", "dnewsfeeds")
			if err := os.Chmod(sample, 0o644); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(sample, old, 0o444); err != nil {
				t.Fatal(err)
			}
			cfg := filepath.Join(news, "dnewsfeeds")
			if err := os.Remove(cfg); err != nil {
				t.Fatal(err)
			}
			if err := os.Symlink("/news/samples/dnewsfeeds", cfg); err != nil {
				t.Fatal(err)
			}

			out := install(t, root, mode, stage)
			m := fileMode(t, cfg)
			if !m.IsRegular() || m.Perm() != 0o640 {
				t.Fatalf("dnewsfeeds after %s: mode %v, want a regular file 0640\n%s", mode, m, out)
			}
			if got, _ := os.ReadFile(cfg); !bytes.Equal(got, old) {
				t.Errorf("dnewsfeeds after %s: %q, want the content it had (the old sample)", mode, got)
			}
			want, _ := os.ReadFile(filepath.Join(root, "samples", "dnewsfeeds"))
			if got, _ := os.ReadFile(sample); !bytes.Equal(got, want) {
				t.Errorf("samples/dnewsfeeds not updated by %s", mode)
			}
		})
	}
}

// installIn runs install-portable.sh in dir with the extra environment and returns the output
// and the error.
func installIn(tb testing.TB, dir, mode, stage string, env ...string) (string, error) {
	tb.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), testkit.Deadline())
	defer cancel()
	cmd := exec.CommandContext(ctx, "/bin/sh", "./install-portable.sh", mode, "/news", stage)
	cmd.Dir = dir
	cmd.Env = append(os.Environ(), env...)
	out, err := cmd.CombinedOutput()
	return string(out), err
}

// TestInstallKeepsDirectoryModes (D-142): a reinstall does not reset the modes of the directories
// the admin owns (the home, the spool, the queue) to 755; log and run, which are private, are
// only closed to others, never opened.
func TestInstallKeepsDirectoryModes(t *testing.T) {
	root := sourceRoot(t)
	stage := t.TempDir()
	news := filepath.Join(stage, "news")
	for dir, mode := range map[string]os.FileMode{
		"": 0o750, "spool": 0o700, "spool/news": 0o770, "dqueue": 0o775, "log": 0o755, "run": 0o770,
	} {
		p := filepath.Join(news, dir)
		if err := os.MkdirAll(p, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.Chmod(p, mode); err != nil {
			t.Fatal(err)
		}
	}
	install(t, root, "install", stage)
	for dir, want := range map[string]os.FileMode{
		"": 0o750, "spool": 0o700, "spool/news": 0o770, "dqueue": 0o775,
		"log": 0o750,  // 755 is closed to others
		"run": 0o770,  // group access the admin gave is kept
		"adm": 0o755,  // installer-managed directories are set
		"dbin": 0o755,
	} {
		if m := fileMode(t, filepath.Join(news, dir)); m.Perm() != want {
			t.Errorf("%s after a reinstall: mode %v, want %v", filepath.Join("news", dir), m, want)
		}
	}
	// a missing one is made as before
	if m := fileMode(t, filepath.Join(news, "spool", "group")); m.Perm() != 0o755 {
		t.Errorf("spool/group: mode %v, want 0755", m)
	}
}

// TestInstallNeedsBuiltPrograms (D-143): in a tree where nothing has been built the installer
// says so and installs nothing, instead of reporting "install complete" with an empty dbin.
func TestInstallNeedsBuiltPrograms(t *testing.T) {
	root := sourceRoot(t)
	src := t.TempDir()
	b, err := os.ReadFile(filepath.Join(root, "install-portable.sh"))
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(src, "install-portable.sh"), b, 0o755); err != nil {
		t.Fatal(err)
	}
	for _, d := range []string{"dbin", "samples", "man"} {
		if err := os.Mkdir(filepath.Join(src, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	stage := t.TempDir()
	out, err := installIn(t, src, "install", stage)
	if err == nil || !strings.Contains(out, "No programs in dbin") || strings.Contains(out, "complete") {
		t.Errorf("install without built programs: err %v, output %q; want a refusal", err, out)
	}
	if _, err := os.Stat(filepath.Join(stage, "news")); err == nil {
		t.Errorf("the installer made %s although nothing was built", filepath.Join(stage, "news"))
	}
}

// TestInstallStopsWhenFindFails (D-144): the source tree is read with find into a list first, so a
// find that fails stops the installer; before, the loop reading its output hid the failure and
// the installer said "install complete".
func TestInstallStopsWhenFindFails(t *testing.T) {
	root := sourceRoot(t)
	shim := t.TempDir()
	if err := os.WriteFile(filepath.Join(shim, "find"), []byte("#!/bin/sh\nexit 1\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	for _, mode := range []string{"install", "uninstall"} {
		stage := t.TempDir()
		if mode == "uninstall" {
			install(t, root, "install", stage)
		}
		out, err := installIn(t, root, mode, stage, "PATH="+shim+string(os.PathListSeparator)+os.Getenv("PATH"))
		if err == nil || strings.Contains(out, "complete") || strings.Contains(out, "Uninstalled") {
			t.Errorf("%s with a failing find: err %v, output %q; want a failure and no success message", mode, err, out)
		}
	}
}

// TestInstallReportsAdminUpdates checks that every advertised replacement exists,
// including after another reinstall, and that local maintenance files survive.
// Before the fix, the NOTE named <name>.new.new; the file written was <name>.new.
func TestInstallReportsAdminUpdates(t *testing.T) {
	root := sourceRoot(t)
	files, err := filepath.Glob(filepath.Join(root, "samples", "adm", "*"))
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, src := range files {
		st, err := os.Stat(src)
		if err != nil {
			t.Fatal(err)
		}
		if st.Mode().IsRegular() {
			names = append(names, filepath.Base(src))
		}
	}
	if len(names) == 0 {
		t.Fatal("no regular maintenance files in samples/adm")
	}
	for _, mode := range []string{"install", "bininstall"} {
		t.Run(mode, func(t *testing.T) {
			stage := t.TempDir()
			install(t, root, "install", stage)
			local := []byte("# local maintenance settings\n")
			for _, name := range names {
				path := filepath.Join(stage, "news", "adm", name)
				if err := os.WriteFile(path, local, 0o644); err != nil {
					t.Fatal(err)
				}
			}
			for pass := 0; pass < 2; pass++ {
				out := install(t, root, mode, stage)
				for _, name := range names {
					path := filepath.Join(stage, "news", "adm", name)
					note := "NOTE: " + path + " was changed locally; the new version is " + path + ".new\n"
					if !strings.Contains(out, note) {
						t.Errorf("pass %d: missing exact note %q in output:\n%s", pass, note, out)
					}
					if got, err := os.ReadFile(path); err != nil || !bytes.Equal(got, local) {
						t.Errorf("local %s changed: %q, %v", name, got, err)
					}
					want, err := os.ReadFile(filepath.Join(root, "samples", "adm", name))
					if err != nil {
						t.Fatal(err)
					}
					if got, err := os.ReadFile(path + ".new"); err != nil || !bytes.Equal(got, want) {
						t.Errorf("replacement %s.new: %q, %v; want sample content", name, got, err)
					}
					if _, err := os.Lstat(path + ".new.new"); !os.IsNotExist(err) {
						t.Errorf("unexpected %s.new.new: %v", name, err)
					}
				}
			}
		})
	}
}
