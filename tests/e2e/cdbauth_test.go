package e2e

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

// writeCDB writes a cdb file (D. J. Bernstein's constant database) with the given records.
func writeCDB(tb testing.TB, path string, recs map[string]string) {
	tb.Helper()
	hash := func(k string) uint32 {
		h := uint32(5381)
		for i := 0; i < len(k); i++ {
			h = (h<<5 + h) ^ uint32(k[i])
		}
		return h
	}
	le := binary.LittleEndian
	type slot struct{ h, pos uint32 }
	var tables [256][]slot
	body := &bytes.Buffer{}
	pos := uint32(2048)
	for k, v := range recs {
		var n [8]byte
		le.PutUint32(n[0:], uint32(len(k)))
		le.PutUint32(n[4:], uint32(len(v)))
		body.Write(n[:])
		body.WriteString(k)
		body.WriteString(v)
		h := hash(k)
		tables[h&255] = append(tables[h&255], slot{h, pos})
		pos += 8 + uint32(len(k)) + uint32(len(v))
	}
	var head [2048]byte
	hashes := &bytes.Buffer{}
	for i, t := range tables {
		n := uint32(len(t)) * 2
		le.PutUint32(head[i*8:], pos+uint32(hashes.Len()))
		le.PutUint32(head[i*8+4:], n)
		slots := make([]slot, n)
		for _, s := range t {
			j := (s.h >> 8) % n
			for slots[j].pos != 0 {
				j = (j + 1) % n
			}
			slots[j] = s
		}
		for _, s := range slots {
			var b [8]byte
			le.PutUint32(b[0:], s.h)
			le.PutUint32(b[4:], s.pos)
			hashes.Write(b[:])
		}
	}
	out := append(append(head[:], body.Bytes()...), hashes.Bytes()...)
	if err := os.WriteFile(path, out, 0o644); err != nil {
		tb.Fatal(err)
	}
}

// TestReaderCdbAuth: with CDB_ENABLED, an authdef "cdb <file>" looks the user up in a cdb file
// whose record is the password. The right password logs in, a wrong one and an unknown user do
// not. Skipped when dreaderd is built without CDB support (./configure turns it on where a cdb
// library is found).
func TestReaderCdbAuth(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(bin(t), "dreaderd"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(b, []byte("dns auth cdb")) {
		t.Skip("dreaderd is built without CDB support (CDB_ENABLED)")
	}
	db := filepath.Join(t.TempDir(), "users.cdb")
	writeCDB(t, db, map[string]string{"pat": "patspass", "sam": "samspass"})
	access := `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef viacdb
    cdb ` + db + `
end
readerdef userpat
    read yes
    post yes
    auth viacdb
end
readerdef feed
    feed yes
end
access 127.0.0.1 userpat
access ` + PeerIP + ` feed
`
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access})

	for _, c := range []struct {
		user, pass string
		ok         bool
	}{{"pat", "patspass", true}, {"sam", "samspass", true}, {"pat", "samspass", false},
		{"pat", "wrong", false}, {"nobody", "patspass", false}} {
		r := s.Reader(t)
		expect(t, r, 381, "AUTHINFO USER %s", c.user)
		code, line := r.Cmd("AUTHINFO PASS %s", c.pass)
		if (code == 281) != c.ok {
			t.Errorf("%s/%s: %s, want login %v", c.user, c.pass, line, c.ok)
		}
	}
}

// TestReaderCdbDenyList (D-169): an authdef "cdb !file" looks the user up in
// the deny cdb.  Before the fix the '!' was not stripped before open(), so the
// open failed and every login was refused, including the authdef's own
// user/pass; the found-user path also leaked the cdb descriptor.
//
// A user in the deny cdb is refused, the authdef's own user/pass (not in the
// cdb) logs in.  Base refuses both because the open fails; with the fix the
// own user succeeds.  Needs a CDB_ENABLED build (./configure finds libcdb);
// skips otherwise, or fails when E2E_REQUIRE_DB=1.
func TestReaderCdbDenyList(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(bin(t), "dreaderd"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(b, []byte("dns auth cdb")) {
		if os.Getenv("E2E_REQUIRE_DB") == "1" {
			t.Fatal("E2E_REQUIRE_DB=1 but dreaderd is built without CDB support (CDB_ENABLED)")
		}
		t.Skip("dreaderd is built without CDB support (CDB_ENABLED)")
	}
	db := filepath.Join(t.TempDir(), "deny.cdb")
	writeCDB(t, db, map[string]string{"deny": ""})
	access := `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef bycdb
    cdb !` + db + `
    user allowed
    pass secret
end
readerdef usercdb
    read yes
    post yes
    auth bycdb
end
readerdef feed
    feed yes
end
access 127.0.0.1 usercdb
access ` + PeerIP + ` feed
`
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access})

	// A listed user is refused (and the old found-user return leaked fd).
	for i := 0; i < 10; i++ {
		c := s.Reader(t)
		expect(t, c, 381, "AUTHINFO USER deny")
		code, line := c.Cmd("AUTHINFO PASS secret")
		if code == 281 {
			t.Fatalf("listed user logged in: %s", line)
		}
		c.Close()
	}

	// The authdef's own user/pass is not in the deny cdb and must log in.
	c := s.Reader(t)
	expect(t, c, 381, "AUTHINFO USER allowed")
	expect(t, c, 281, "AUTHINFO PASS secret")
	expect(t, c, 205, "QUIT")
}
