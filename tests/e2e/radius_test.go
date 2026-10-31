package e2e

import (
	"bytes"
	"crypto/hmac"
	"crypto/md5"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/go-while/nntp-testkit/client"
)

// fakeRadius is a RADIUS server for the authdef "radius" tests: it checks the shared secret
// (by decrypting User-Password) and answers Access-Accept, with a Filter-Id for a user that has
// one, or Access-Reject. It keeps the attributes of every request.
type fakeRadius struct {
	conn   *net.UDPConn
	secret string
	users  map[string]radiusUser
	mu     sync.Mutex
	reqs   []radiusReq
}

type radiusUser struct{ pass, filterID string }

type radiusReq struct {
	user, pass, callingStation, connectInfo string
}

func startFakeRadius(tb testing.TB, secret string, users map[string]radiusUser) *fakeRadius {
	tb.Helper()
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		tb.Fatal(err)
	}
	f := &fakeRadius{conn: conn, secret: secret, users: users}
	tb.Cleanup(func() { conn.Close() })
	go f.serve()
	return f
}

func (f *fakeRadius) port() int { return f.conn.LocalAddr().(*net.UDPAddr).Port }

func (f *fakeRadius) requests() []radiusReq {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]radiusReq(nil), f.reqs...)
}

func (f *fakeRadius) serve() {
	buf := make([]byte, 4096)
	for {
		n, from, err := f.conn.ReadFromUDP(buf)
		if err != nil {
			return
		}
		if resp := f.answer(buf[:n]); resp != nil {
			f.conn.WriteToUDP(resp, from)
		}
	}
}

// answer returns the reply to an Access-Request, or nil.
func (f *fakeRadius) answer(p []byte) []byte {
	if len(p) < 20 || p[0] != 1 || int(p[2])<<8|int(p[3]) != len(p) {
		return nil
	}
	id, reqAuth := p[1], p[4:20]
	var req radiusReq
	var wantMA bool
	for a := p[20:]; len(a) >= 2 && int(a[1]) >= 2 && int(a[1]) <= len(a); a = a[a[1]:] {
		v := a[2:a[1]]
		switch a[0] {
		case 1:
			req.user = string(v)
		case 2:
			req.pass = f.decryptPassword(v, reqAuth)
		case 31:
			req.callingStation = string(v)
		case 77:
			req.connectInfo = string(v)
		case 80:
			wantMA = true // Message-Authenticator: newer radcli sends it and wants it back
		}
	}
	f.mu.Lock()
	f.reqs = append(f.reqs, req)
	f.mu.Unlock()

	code := byte(3) // Access-Reject
	var attrs []byte
	if u, ok := f.users[req.user]; ok && u.pass == req.pass {
		code = 2
		if u.filterID != "" {
			attrs = append([]byte{11, byte(2 + len(u.filterID))}, u.filterID...)
		}
	}
	if wantMA {
		// Message-Authenticator (BlastRADIUS), which radcli 1.4 and later wants as the first
		// attribute: HMAC-MD5 over the reply with the request's authenticator in the header
		// and zeros in the attribute itself
		attrs = append([]byte{80, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, attrs...)
	}
	n := 20 + len(attrs)
	resp := append([]byte{code, id, byte(n >> 8), byte(n)}, reqAuth...)
	resp = append(resp, attrs...)
	if wantMA {
		mac := hmac.New(md5.New, []byte(f.secret))
		mac.Write(resp)
		copy(resp[22:38], mac.Sum(nil))
	}
	sum := md5.Sum(append(append([]byte(nil), resp...), f.secret...))
	copy(resp[4:20], sum[:])
	return resp
}

func (f *fakeRadius) decryptPassword(enc, reqAuth []byte) string {
	out := make([]byte, 0, len(enc))
	prev := reqAuth
	for i := 0; i+16 <= len(enc); i += 16 {
		h := md5.Sum(append([]byte(f.secret), prev...))
		for j := 0; j < 16; j++ {
			out = append(out, enc[i+j]^h[j])
		}
		prev = enc[i : i+16]
	}
	return string(bytes.TrimRight(out, "\x00"))
}

// radiusAccess is a dreader.access where 127.0.0.1 has to log in through the RADIUS file conf.
// A user whose Filter-Id is "readonly" gets the readerdef of that name (no posting).
func radiusAccess(conf string) string {
	return `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef viaradius
    radius ` + conf + `
end
readerdef poster
    read yes
    post yes
    auth viaradius
end
readerdef readonly
    read yes
    post no
end
readerdef feed
    feed yes
end
access 127.0.0.1 poster
access ` + PeerIP + ` feed
`
}

// radiusBuild tells what the dreaderd in DIABLO_BIN was built with: ok is false without RADIUS
// support (RADIUS_ENABLED), radcli is true for the radcli adapter, false for libradius.
func radiusBuild(tb testing.TB) (ok, radcli bool) {
	tb.Helper()
	b, err := os.ReadFile(filepath.Join(bin(tb), "dreaderd"))
	if err != nil {
		tb.Fatal(err)
	}
	return bytes.Contains(b, []byte("dns auth radius")), bytes.Contains(b, []byte("rc_read_config"))
}

func radiusSite(tb testing.TB, conf string) *Site {
	tb.Helper()
	_, spool := spoolStore(tb)
	return NewReaderSite(tb, ReaderOptions{Spool: spool, Access: radiusAccess(conf)})
}

// TestReaderRadiusAuth: an authdef "radius <file>" asks the RADIUS server of the file (BSD
// radius.conf format, the same file for libradius on FreeBSD and SmartOS and for the radcli
// adapter on Linux). The right password logs in, a wrong one or an unknown user does not, a
// Filter-Id in the Access-Accept selects the readerdef of that name, and the request carries
// the user, the password (encrypted with the shared secret) and the client address in
// Calling-Station-Id. Skipped when dreaderd is built without RADIUS_ENABLED.
func TestReaderRadiusAuth(t *testing.T) {
	if ok, _ := radiusBuild(t); !ok {
		t.Skip("dreaderd is built without RADIUS support (RADIUS_ENABLED)")
	}
	srv := startFakeRadius(t, "testing123", map[string]radiusUser{
		"pat": {pass: "patspass"},
		// a password longer than one 16-byte block of the encryption
		"long": {pass: "a-password-of-more-than-sixteen-bytes"},
		"vip":  {pass: "vipspass", filterID: "readonly"},
	})
	conf := filepath.Join(t.TempDir(), "radius.conf")
	if err := os.WriteFile(conf, []byte(fmt.Sprintf("# the RADIUS server\nauth 127.0.0.1:%d testing123 2 1\n", srv.port())), 0o644); err != nil {
		t.Fatal(err)
	}
	s := radiusSite(t, conf)

	login := func(user, pass string) (*client.Conn, int) {
		c := s.Reader(t)
		expect(t, c, 381, "AUTHINFO USER %s", user)
		code, _ := c.Cmd("AUTHINFO PASS %s", pass)
		return c, code
	}

	c, code := login("pat", "patspass")
	if code != 281 {
		t.Fatalf("pat with the right password: %d, want 281", code)
	}
	expect(t, c, 200, "MODE READER") // the poster readerdef: posting allowed

	if _, code = login("long", "a-password-of-more-than-sixteen-bytes"); code != 281 {
		t.Errorf("a password of more than 16 bytes: %d, want 281", code)
	}
	if _, code = login("pat", "wrong"); code == 281 {
		t.Error("pat with a wrong password logged in")
	}
	if _, code = login("nobody", "x"); code == 281 {
		t.Error("an unknown user logged in")
	}

	c, code = login("vip", "vipspass")
	if code != 281 {
		t.Fatalf("vip: %d, want 281", code)
	}
	expect(t, c, 201, "MODE READER") // the readerdef named by the Filter-Id: no posting

	var sawPat bool
	for _, r := range srv.requests() {
		if r.user == "pat" && r.pass == "patspass" {
			sawPat = true
			if r.callingStation != "127.0.0.1" {
				t.Errorf("Calling-Station-Id %q, want 127.0.0.1", r.callingStation)
			}
			if r.connectInfo != "" {
				t.Errorf("Connect-Info %q on a client that is not behind a proxy", r.connectInfo)
			}
		}
	}
	if !sawPat {
		t.Errorf("the server did not get pat's request: %+v", srv.requests())
	}
}

// TestReaderRadiusServerDown: a RADIUS server that does not answer (or a file that does not
// exist) refuses the login and does not hang or kill dreaderd: the next connection is served.
func TestReaderRadiusServerDown(t *testing.T) {
	if ok, _ := radiusBuild(t); !ok {
		t.Skip("dreaderd is built without RADIUS support (RADIUS_ENABLED)")
	}
	// a UDP port nobody answers on
	dead, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer dead.Close()
	deadPort := dead.LocalAddr().(*net.UDPAddr).Port

	dir := t.TempDir()
	downConf := filepath.Join(dir, "down.conf")
	if err := os.WriteFile(downConf, []byte(fmt.Sprintf("auth 127.0.0.1:%d testing123 1 1\n", deadPort)), 0o644); err != nil {
		t.Fatal(err)
	}
	for name, conf := range map[string]string{"no answer": downConf, "missing file": filepath.Join(dir, "no-such.conf")} {
		s := radiusSite(t, conf)
		c := s.Reader(t)
		expect(t, c, 381, "AUTHINFO USER pat")
		if code, line := c.Cmd("AUTHINFO PASS patspass"); code == 281 {
			t.Errorf("%s: logged in: %s", name, line)
		}
		c2 := s.Reader(t) // dreaderd and its auth worker are still there
		expect(t, c2, 381, "AUTHINFO USER pat")
	}
}

// TestReaderRadiusNativeConfig (radcli only): a radcli radiusclient.conf ("authserver ...")
// is read as it is.
func TestReaderRadiusNativeConfig(t *testing.T) {
	ok, radcli := radiusBuild(t)
	if !ok || !radcli {
		t.Skip("dreaderd is not built with the radcli adapter (RADIUS_RADCLI)")
	}
	// the dictionary of the radcli package: /etc/radcli on Debian, Ubuntu and Fedora,
	// /usr/share/radcli on Arch, elsewhere on others
	dict := ""
	out, _ := exec.Command("find", "/etc", "/usr/etc", "/usr/local/etc", "/opt/local/etc",
		"/usr/share", "/usr/local/share", "-maxdepth", "3", "-path", "*/radcli/dictionary").Output()
	if f := strings.Fields(string(out)); len(f) > 0 {
		dict = f[0]
	}
	if dict == "" {
		t.Skip("no radcli dictionary found")
	}
	srv := startFakeRadius(t, "testing123", map[string]radiusUser{"pat": {pass: "patspass"}})
	conf := filepath.Join(t.TempDir(), "radiusclient.conf")
	body := fmt.Sprintf("authserver 127.0.0.1:%d:testing123\ndictionary %s\nradius_timeout 2\nradius_retries 1\n", srv.port(), dict)
	if err := os.WriteFile(conf, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	s := radiusSite(t, conf)
	c := s.Reader(t)
	expect(t, c, 381, "AUTHINFO USER pat")
	expect(t, c, 281, "AUTHINFO PASS patspass")
}
