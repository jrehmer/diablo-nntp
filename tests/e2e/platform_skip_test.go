package e2e

import (
	_ "embed"
	"flag"
	"os"
	"runtime"
	"strings"
)

// openbsdSkip lists the tests that fail on OpenBSD only because it has no unified buffer
// cache (ISSUES.md D-171, won't fix).
//
//go:embed ci/openbsd-skip
var openbsdSkip string

// platformSkip runs before the tests (TestMain). On OpenBSD it sets -test.skip to the tests
// in ci/openbsd-skip, so that a new OpenBSD failure shows instead of hiding among the known
// ones. A -test.skip given on the command line, or E2E_OPENBSD_ALL=1, runs them anyway.
func platformSkip() {
	if runtime.GOOS != "openbsd" || os.Getenv("E2E_OPENBSD_ALL") == "1" {
		return
	}
	flag.Parse()
	if f := flag.Lookup("test.skip"); f == nil || f.Value.String() != "" {
		return
	}
	var names []string
	for _, l := range strings.Split(openbsdSkip, "\n") {
		if l = strings.TrimSpace(l); l != "" && !strings.HasPrefix(l, "#") {
			names = append(names, l)
		}
	}
	if len(names) > 0 {
		flag.Set("test.skip", "^("+strings.Join(names, "|")+")$")
	}
}
