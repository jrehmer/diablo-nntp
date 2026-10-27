package e2e

import (
	"strings"
	"testing"
)

// TestDexpireCtlLongComment (D-82): dexpire.ctl is read in 512-byte pieces. A comment line
// longer than that was split, and its tail read as a rule of its own; here the tail is
// "*:a16", a rule for every group. Every matching rule applies and the last one wins, so the
// comment comes after the real rule: read as a rule, its tail would set the size of the
// group's overview index. The whole comment must be skipped.
func TestDexpireCtlLongComment(t *testing.T) {
	plain := overviewSize(t, "e2e.*:a4000\n")
	comment := "#" + strings.Repeat("c", 510) + "*:a16 is the tail of this comment\n"
	if got := overviewSize(t, "e2e.*:a4000\n"+comment); got != plain {
		t.Errorf("a %d character comment in dexpire.ctl changed the rules: overview file is %d bytes, %d without the comment", len(comment)-1, got, plain)
	}
}

// TestDexpireCtlLongPattern (D-82): a dexpire.ctl pattern is copied into oe_Wild[255]. A
// longer one ran over the fields behind it, and as the last rule over its list link
// (oe_Next), so a lookup that got past the rules before it followed a garbage pointer. Such
// a rule is now refused (logged and ignored); the rules before it still apply.
func TestDexpireCtlLongPattern(t *testing.T) {
	want := overviewSize(t, "e2e.*:a1000\n")
	ctl := "e2e.*:a1000\n" + strings.Repeat("x", 320) + ":a16\n"
	if got := overviewSize(t, ctl); got != want {
		t.Errorf("with a 320 character pattern as the last dexpire.ctl rule the overview file is %d bytes, want %d (the e2e.* rule before it)", got, want)
	}
}
