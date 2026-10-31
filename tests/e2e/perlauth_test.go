package e2e

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

// TestReaderPerlAuth: with PERL_ENABLED, an authdef with "perl <script>" calls the script's
// checkuser(user, pass) in an embedded perl; a result starting with 1 logs the user in and
// anything else is refused. Skipped when dreaderd is built without Perl support
// (./configure turns it on where perl can be embedded).
func TestReaderPerlAuth(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(bin(t), "dreaderd"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(b, []byte("PERL auth request")) {
		t.Skip("dreaderd is built without Perl support (PERL_ENABLED)")
	}
	script := filepath.Join(t.TempDir(), "auth.pl")
	if err := os.WriteFile(script, []byte(`sub checkuser {
    my ($user, $pass) = @_;
    return "100 Success" if $user eq "pat" && $pass eq "patspass";
    return "0";
}
1;
`), 0o644); err != nil {
		t.Fatal(err)
	}
	access := `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef viaperl
    perl ` + script + `
end
readerdef userpat
    read yes
    post yes
    auth viaperl
end
readerdef feed
    feed yes
end
access 127.0.0.1 userpat
access ` + PeerIP + ` feed
`
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access})
	feedReader(t, s, st, "e2e.test", newArticle(msgID(t), "e2e.test", "perl auth", "body"))

	// a refused login: dreaderd drops the connection (as it does for any failed AUTHINFO via
	// its DNS/auth worker), it does not answer 481
	bad := s.Reader(t)
	expect(t, bad, 381, "AUTHINFO USER pat")
	if code, line := bad.Cmd("AUTHINFO PASS wrong"); code == 281 {
		t.Errorf("AUTHINFO PASS with a wrong password: %s", line)
	}

	c := s.Reader(t)
	expect(t, c, 381, "AUTHINFO USER pat")
	expect(t, c, 281, "AUTHINFO PASS patspass")
	expect(t, c, 211, "GROUP e2e.test")
}
