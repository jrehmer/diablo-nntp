package e2e

import (
	"strings"
	"testing"
)

// TestConfigLongLine (D-82): LoadDiabloConfig() reads diablo.config with fgets() into a
// 256 byte buffer, so a comment line of 256 or more characters is read as two lines and
// the second half ("junk words ...", not a comment) makes every Diablo program exit with
// "Unknown diablo.config command".
func TestConfigLongLine(t *testing.T) {
	comment := "# " + strings.Repeat("a long comment, ", 26) + "tail words that are not a command"
	if len(comment) < 400 {
		t.Fatalf("test comment is only %d bytes", len(comment))
	}
	s := NewSite(t, SiteOptions{NoStart: true})
	// after the version line: commands before it are not looked at
	conf := strings.SplitN(s.ReadFile(t, "diablo.config"), "\n", 2)
	if !strings.HasPrefix(conf[0], "version") || len(conf) != 2 {
		t.Fatalf("unexpected diablo.config: %q", conf)
	}
	s.WriteFile(t, "diablo.config", conf[0]+"\n"+comment+"\n"+conf[1])

	// No server is running: once the whole configuration is read, dicmd tries its control
	// socket (from path_run) and fails there.
	out, _ := s.TryRun("dicmd", "info")
	if strings.Contains(out, "Unknown diablo.config command") || !strings.Contains(out, "udom-connect") {
		t.Errorf("dicmd with a %d character comment line in diablo.config: %q, want it to read the configuration and fail to connect (udom-connect)", len(comment), out)
	}
}
