package e2e

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// printHeld runs prog with args and stdin under the output shim of TestGroupCreateDsyncgroupsLock
// (a printf() of a text that contains on stops the process there, as a stdout that does not
// drain would), and, while it is stopped, runs "dgrpctl adjust <group> NX +5" (it locks the
// record, reads NX, writes NX+5, as the feeder's GenerateXRef() and dreaderd's AssignArticleNo()
// do). It reports whether that was done within 2 s, i.e. whether the record was free while
// prog was stuck on its output, and prog's output.
func printHeld(t *testing.T, s *Site, stdin, on, group, prog string, args ...string) (out string, early bool) {
	t.Helper()
	r := newCreateRaceFrom(t, "dgrpctlprint", groupCreateOutPauseSource)
	type result struct {
		out string
		err error
	}
	r.arm(t)
	aDone := make(chan result, 1)
	go func() {
		cmd := exec.Command(filepath.Join(s.Bin, prog), args...)
		cmd.Dir = s.Dir
		cmd.Env = append(append(os.Environ(), s.Env...), append(append([]string{}, r.env...), "GROUPCREATE_PAUSE_ON="+on)...)
		cmd.Stdin = strings.NewReader(stdin)
		cmd.WaitDelay = 3 * time.Second
		o, err := cmd.CombinedOutput()
		aDone <- result{string(o), err}
	}()
	if !r.waitHeld(60 * time.Second) {
		r.release()
		a := <-aDone
		t.Fatalf("%s did not print %q (no .held file); it ended with %v and printed:\n%s", prog, on, a.err, a.out)
	}
	bDone := make(chan result, 1)
	go func() {
		o, err := s.run("dgrpctl", "adjust", group, "NX", "+5")
		bDone <- result{o, err}
	}()
	var b result
	select {
	case b = <-bDone:
		early = true
	case <-time.After(2 * time.Second):
	}
	r.release()
	a := <-aDone
	if !early {
		b = <-bDone
	}
	if a.err != nil {
		t.Fatalf("%s %s: %v\n%s", prog, strings.Join(args, " "), a.err, a.out)
	}
	if b.err != nil {
		t.Fatalf("dgrpctl adjust %s: %v\n%s", group, b.err, b.out)
	}
	if f := kpFields(t, s, group); f["NX"] != "0000000005" {
		t.Errorf("%s has NX=%s after the adjust, want 0000000005", group, f["NX"])
	}
	return a.out, early
}

// TestDgrpctlCheckgroupsPrintsAfterUnlock (D-321): "dgrpctl checkgroups" without exec read the
// record of a group with KP_LOCK and printed its "dgrpctl modgroup" line before it released it,
// so a stdout that does not drain kept the record locked and every writer of the group (the
// feeder's GenerateXRef(), dreaderd's AssignArticleNo()) waited. A shim stops dgrpctl in the
// printf() of that line; "dgrpctl adjust e2e.test NX +5" must then be done within 2 s. The text
// and the order of the output are those of before.
//
// The exec subtests are controls: the messages of "checkgroups exec" ("Modified group", "Added
// group") were printed after the write that releases the lock already, so they pass on a9d79551
// too. Subtest modgroup fails on a9d79551: adjust waits until the shim lets dgrpctl go.
func TestDgrpctlCheckgroupsPrintsAfterUnlock(t *testing.T) {
	const list = "e2e.test\tA description that differs.\ne2e.fresh\tA new group of the list.\n"
	for _, tc := range []struct {
		name, on, group string
		args            []string
		want            []string
	}{
		{"modgroup", "dgrpctl modgroup e2e.test", "e2e.test", []string{"checkgroups"},
			[]string{"dgrpctl modgroup e2e.test y \"\" \"A description that differs.\"\n",
				"dgrpctl newgroup e2e.fresh y \"\" \"A new group of the list.\"\n"}},
		{"exec-modified", ">> Checkgroups: Modified group e2e.test", "e2e.test", []string{"checkgroups", "exec"},
			[]string{">> Checkgroups: Modified group e2e.test\n", ">> Checkgroups: Added group e2e.fresh\n"}},
		{"exec-added", ">> Checkgroups: Added group e2e.fresh", "e2e.fresh", []string{"checkgroups", "exec"},
			[]string{">> Checkgroups: Modified group e2e.test\n", ">> Checkgroups: Added group e2e.fresh\n"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s := NewReaderSite(t, ReaderOptions{SpoolAddr: "127.0.0.1:1", Groups: []string{"e2e.test"}, NoStart: true})
			file := filepath.Join(s.Dir, "checkgroups.txt")
			if err := os.WriteFile(file, []byte(list), 0o644); err != nil {
				t.Fatal(err)
			}
			out, early := printHeld(t, s, "", tc.on, tc.group, "dgrpctl", append(append([]string{}, tc.args...), file)...)
			if !early {
				t.Errorf("dgrpctl adjust %s waited while dgrpctl was stuck on its output: the record was locked", tc.group)
			}
			prev := -1
			for _, w := range tc.want {
				i := strings.Index(out, w)
				if i < 0 {
					t.Errorf("no %q in the output:\n%s", w, out)
				} else if i < prev {
					t.Errorf("%q is out of order in the output:\n%s", w, out)
				} else {
					prev = i
				}
			}
		})
	}
}

// TestDgrpctlMasscancelPrintsAfterUnlock (D-321): "dgrpctl masscancel" holds the record of the
// group (KP_LOCK) while it walks the overview, and printed ">> cancelled <group>:<n>" (and its
// two failure lines) inside that window. The lines are collected and printed after the unlock.
// A shim stops dgrpctl in the printf() of the cancel line; "dgrpctl adjust e2e.test NX +5" must
// be done within 2 s. Fails on a9d79551: adjust waits. The line, and the cancel itself, are
// as before.
func TestDgrpctlMasscancelPrintsAfterUnlock(t *testing.T) {
	s, id, _ := overReaderSite(t)
	out, early := printHeld(t, s, id+"\n", ">> cancelled e2e.test:1", "e2e.test", "dgrpctl", "masscancel", "e2e.test")
	if !early {
		t.Errorf("dgrpctl adjust waited while masscancel was stuck on its output: the record was locked")
	}
	if !strings.Contains(out, ">> cancelled e2e.test:1\n") {
		t.Errorf("no cancel line in the output of masscancel:\n%s", out)
	}
	// the article is cancelled: a second run finds nothing to cancel
	if again, err := runWith(t, s, id+"\n", nil, "dgrpctl", "masscancel", "e2e.test"); err != nil || strings.Contains(again, ">> cancelled") {
		t.Errorf("a second masscancel of the same id (err %v) cancelled it again:\n%s", err, again)
	}
}
