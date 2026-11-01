package e2e

import (
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// Article type bits, as in lib/defs.h.
const (
	atDefault   = 0x000001
	atControl   = 0x000002
	atCancel    = 0x000004
	atMIME      = 0x000100
	atBinary    = 0x000200
	atUUencode  = 0x000400
	atBase64    = 0x000800
	atMultipart = 0x001000
	atHTML      = 0x002000
	atPS        = 0x004000
	atBinHex    = 0x008000
	atPartial   = 0x010000
	atPGP       = 0x020000
	atYenc      = 0x040000
	atBommaNews = 0x080000
	atUnidata   = 0x100000
)

// artTypeOf returns the ArtType that dfeedtest -a (which feeds the article line by line to
// ArticleType(), as diablo does) reports for an article.
func artTypeOf(t *testing.T, s *Site, lines []string) int {
	t.Helper()
	cmd := exec.Command(filepath.Join(s.Bin, "dfeedtest"), "-a")
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	cmd.Stdin = strings.NewReader(strings.Join(lines, "\n") + "\n")
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("dfeedtest -a: %v\n%s", err, out)
	}
	for _, l := range strings.Split(string(out), "\n") {
		if v, ok := strings.CutPrefix(strings.TrimSpace(l), "ArtType: "); ok {
			n, err := strconv.ParseInt(v, 16, 64)
			if err != nil {
				t.Fatalf("dfeedtest -a: ArtType %q: %v", v, err)
			}
			return int(n)
		}
	}
	t.Fatalf("dfeedtest -a: no ArtType in\n%s", out)
	return 0
}

// atArticle builds an article with the usual headers, extra headers and a body.
func atArticle(extra []string, body []string) []string {
	a := newArticle("<arttype@e2e.test>", "e2e.test", "article type")
	a = a[:len(a)-1] // without the blank line
	a = append(a, extra...)
	a = append(a, "")
	return append(a, body...)
}

// atLines returns n lines of width w made of the characters in alphabet (varying from line
// to line, so they look like data).
func atLines(n, w int, alphabet string, first string) []string {
	var l []string
	for i := 0; i < n; i++ {
		var b strings.Builder
		b.WriteString(first)
		for j := 0; b.Len() < w+len(first); j++ {
			b.WriteByte(alphabet[(i*7+j*13+j*j)%len(alphabet)])
		}
		l = append(l, b.String())
	}
	return l
}

const (
	b64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
	uuAlphabet  = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
	hexAlphabet = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"
)

// binhexLines returns n BinHex 4.0 data lines of 64 characters: the data starts with ':' on
// the first line and ends with ':' on the last.
func binhexLines(n int) []string {
	l := atLines(n, 64, hexAlphabet, "")
	l[0] = ":" + l[0][1:]
	l[n-1] = l[n-1][:63] + ":"
	return l
}

// pgp returns an armored block: the begin line, headers, n lines of 64 characters, a CRC line
// and the end line.
func pgp(kind string, n int) []string {
	l := []string{"-----BEGIN PGP " + kind + "-----", "Version: GnuPG v2", ""}
	l = append(l, atLines(n, 64, b64Alphabet, "")...)
	return append(l, "=AbCd", "-----END PGP "+kind+"-----")
}

// TestArticleTypes: the ArtType that Diablo gives a sample of each kind of article
// (lib/arttype.c). The samples are synthetic, made to match the formats. Where the result is
// a misdetection, the case says so with bug, and want is what main gives today; the fix
// changes that one value (the case then fails on main until the fix is in).
func TestArticleTypes(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: GlobalLabel, NoStart: true})
	yenc := atLines(3, 128, b64Alphabet, "")
	b64 := atLines(12, 76, b64Alphabet, "")
	for _, c := range []struct {
		name  string
		extra []string
		body  []string
		want  int
		bug   string
	}{
		{"plain text", nil, []string{"Hello."}, atDefault, ""},
		{"quoted-printable", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: quoted-printable"}, []string{"Caf=C3=A9"}, atMIME, ""},
		{"html", []string{"Mime-Version: 1.0", "Content-Type: text/html; charset=UTF-8"}, []string{"<p>Hi</p>"}, atMIME | atHTML, ""},
		{"control cancel", []string{"Control: cancel <x@y>"}, []string{"cancel"}, atControl | atCancel, ""},
		{"control newgroup", []string{"Control: newgroup e2e.new"}, []string{"newgroup"}, atControl, ""},
		{"postscript", []string{"Mime-Version: 1.0", "Content-Type: application/postscript"}, []string{"%!PS"}, atMIME | atPS, ""},
		{"octet-stream", []string{"Mime-Version: 1.0", "Content-Type: application/octet-stream"}, []string{"data"}, atMIME | atBinary, ""},
		{"message/partial", []string{"Mime-Version: 1.0", "Content-Type: message/partial; id=x; number=1; total=2"}, []string{"data"}, atMIME | atPartial, ""},
		{"x-bommanews", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: X-Bommanews"}, []string{"data"}, atMIME | atBommaNews | atBinary, ""},
		{"x-unidata", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: X-UnidataEncoding"}, []string{"data"}, atMIME | atUnidata | atBinary, ""},
		{"multipart with base64 attachment", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain", "", "text", "--XX", "Content-Type: application/octet-stream", "Content-Transfer-Encoding: base64", ""}, b64...), "--XX--"),
			atMIME | atMultipart | atBinary, ""},
		{"base64 attachment", []string{"Mime-Version: 1.0", "Content-Type: image/png", "Content-Transfer-Encoding: base64"}, b64, atMIME | atBinary | atBase64, ""},
		{"uuencode", nil, append([]string{"begin 644 file.bin"}, atLines(12, 60, uuAlphabet, "M")...), atUUencode | atBinary, ""},
		{"binhex", []string{"Mime-Version: 1.0", "Content-Type: application/mac-binhex40"}, append([]string{"(This file must be converted with BinHex 4.0)"}, binhexLines(12)...), atMIME | atBinHex | atBinary, ""},
		// yEnc
		{"yenc line=", nil, append(append([]string{"=ybegin line=128 size=384 name=a.bin"}, yenc...), "=yend size=384"), atYenc | atBinary, ""},
		{"yenc part=", nil, append(append([]string{"=ybegin part=1 total=2 line=128 size=768 name=a.bin", "=ypart begin=1 end=384"}, yenc...), "=yend size=384 part=1"), atYenc | atBinary | atPartial, ""},
		// yEnc keywords in another order: the detector matches only "=ybegin part=" and
		// "=ybegin line=" at the start.
		{"yenc total= part= line=", nil, append(append([]string{"=ybegin total=2 part=1 line=128 size=768 name=a.bin", "=ypart begin=1 end=384"}, yenc...), "=yend size=384 part=1"), atDefault, "misdetection: not binary at all; should be atYenc | atBinary | atPartial"},
		{"yenc size= line=", nil, append(append([]string{"=ybegin size=384 line=128 name=a.bin"}, yenc...), "=yend size=384"), atDefault, "misdetection: not binary at all; should be atYenc | atBinary"},
		// Text that is taken for binary.
		{"base64 text", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64"}, atLines(3, 40, b64Alphabet, ""), atMIME, ""},
		{"base64 text, encoding first", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64", "Content-Type: text/plain; charset=UTF-8"}, atLines(3, 40, b64Alphabet, ""), atMIME, ""},
		{"base64 html", []string{"Mime-Version: 1.0", "Content-Type: text/html; charset=UTF-8", "Content-Transfer-Encoding: base64"}, atLines(3, 40, b64Alphabet, ""), atMIME | atHTML, ""},
		{"base64 text, 12 lines", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64"}, atLines(12, 76, b64Alphabet, ""), atMIME | atBinary | atBase64, "by design: the line count still makes a long base64 body a binary, so a binary can't pass as text/plain"},
		{"base64 without Content-Type", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64"}, atLines(3, 40, b64Alphabet, ""), atMIME | atBinary | atBase64, "by design: only an explicit text/ type is exempt"},
		{"base64 text part in multipart", []string{"Mime-Version: 1.0", "Content-Type: multipart/alternative; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64", ""}, atLines(3, 40, b64Alphabet, "")...), "--XX--"), atMIME | atMultipart, ""},
		{"base64 image part after a base64 text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append(append([]string{"--XX", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, atLines(2, 40, b64Alphabet, "")...), "--XX", "Content-Type: image/png", "Content-Transfer-Encoding: base64", ""), atLines(3, 40, b64Alphabet, "")...), atMIME | atMultipart | atBinary | atBase64, ""},
		{"base64 part without Content-Type after a text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append(append([]string{"--XX", "Content-Type: text/plain", ""}, "hello"), "--XX", "Content-Transfer-Encoding: base64", ""), atLines(3, 40, b64Alphabet, "")...), atMIME | atMultipart | atBinary | atBase64, "a part has its own type: the earlier text/plain does not carry over"},
		// Header parsing.
		{"pem certificate", nil, pem("CERTIFICATE", 24), atDefault, ""},
		{"pem certificate request", nil, pem("CERTIFICATE REQUEST", 14), atDefault, ""},
		{"pem rsa private key", nil, pem("RSA PRIVATE KEY", 25), atDefault, ""},
		{"pem public key", nil, pem("PUBLIC KEY", 14), atDefault, ""},
		{"pem openssh private key, 70 wide", nil, pemWide("OPENSSH PRIVATE KEY", 20, 70), atDefault, ""},
		{"pem beyond the armor limit", nil, pem("CERTIFICATE", 120), atBinary | atBase64, "by design: only the first 100 lines of an armored block are not counted"},
		{"base64 after a pem block", nil, append(pem("CERTIFICATE", 24), atLines(12, 64, b64Alphabet, "")...), atBinary | atBase64, ""},
		{"uppercase CONTENT-TYPE html", []string{"Mime-Version: 1.0", "CONTENT-TYPE: text/html"}, []string{"<p>Hi</p>"}, atMIME, "misdetection: the header is skipped (tolower(*(ptr + 1) == 'o') precedence bug); should add atHTML"},
		{"uppercase CONTENT-TRANSFER-ENCODING", []string{"Mime-Version: 1.0", "CONTENT-TRANSFER-ENCODING: BASE64"}, atLines(3, 40, b64Alphabet, ""), atMIME, "misdetection: same precedence bug; should be atMIME | atBinary | atBase64"},
		{"uppercase CONTROL cancel", []string{"CONTROL: cancel <x@y>"}, []string{"cancel"}, atDefault, "misdetection: same precedence bug; should be atControl | atCancel"},
		{"Content-Type without space", []string{"Mime-Version: 1.0", "Content-Type:text/html"}, []string{"<p>Hi</p>"}, atMIME, "misdetection: 'Content-Type: ' needs exactly one space; should add atHTML"},
		{"folded multipart Content-Type", []string{"Mime-Version: 1.0", "Content-Type:", " multipart/mixed; boundary=XX"}, []string{"--XX", "--XX--"}, atMIME, "misdetection: the value on the next line is not seen; should add atMultipart"},
		// PGP: every armor form, short (4 lines) and the size of an RSA-4096 signature or key
		// (12 lines of 64 characters, which more than 8 in a row make a base64 binary).
		{"pgp message", nil, pgp("MESSAGE", 4), atPGP, ""},
		{"pgp message long", nil, pgp("MESSAGE", 12), atPGP, ""},
		{"pgp message beyond the armor limit", nil, pgp("MESSAGE", 120), atPGP | atBinary | atBase64, "by design: only the first 100 lines of an armored block are not counted, so a long binary can't hide behind a BEGIN PGP line"},
		{"pgp message part", nil, append([]string{"-----BEGIN PGP MESSAGE, PART 01/02-----", "Version: 2.6.3i", "", "Part 1"}, pgp("MESSAGE", 4)[3:]...), atPGP, ""},
		{"pgp signed message", nil, append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA256", "", "Signed text."}, pgp("SIGNATURE", 4)...), atPGP, ""},
		{"pgp signed message long signature", nil, append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA512", "", "Signed text."}, pgp("SIGNATURE", 12)...), atPGP, ""},
		{"pgp public key", nil, pgp("PUBLIC KEY BLOCK", 4), atPGP, ""},
		{"pgp public key long", nil, pgp("PUBLIC KEY BLOCK", 30), atPGP, ""},
		{"base64 text after a pgp block", nil, append(pgp("SIGNATURE", 12), atLines(12, 64, b64Alphabet, "")...), atPGP | atBinary | atBase64, ""},
		{"pgp private key", nil, pgp("PRIVATE KEY BLOCK", 4), atPGP, ""},
		{"pgp/mime encrypted", []string{"Mime-Version: 1.0", "Content-Type: multipart/encrypted; protocol=\"application/pgp-encrypted\"; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: application/pgp-encrypted", "", "Version: 1", "--XX", "Content-Type: application/octet-stream", ""}, pgp("MESSAGE", 4)...), "--XX--"), atMIME | atMultipart | atPGP, ""},
		{"pgp/mime signed", []string{"Mime-Version: 1.0", "Content-Type: multipart/signed; protocol=\"application/pgp-signature\"; micalg=pgp-sha256; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain", "", "Signed.", "--XX", "Content-Type: application/pgp-signature", ""}, pgp("SIGNATURE", 4)...), "--XX--"), atMIME | atMultipart | atPGP, ""},
		{"pgp/mime encrypted, protocol on the next line", []string{"Mime-Version: 1.0", "Content-Type: multipart/encrypted;", "\tprotocol=\"application/pgp-encrypted\"; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: application/pgp-encrypted", "", "Version: 1", "--XX", "Content-Type: application/octet-stream", ""}, pgp("MESSAGE", 4)...), "--XX--"), atMIME | atMultipart | atPGP, "the protocol is on a folded line; the encrypted part's own Content-Type gives the PGP flag and the exemption of its octet-stream"},
		{"octet-stream attachment next to a pgp signature", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: application/octet-stream", "", "data", "--XX", "Content-Type: application/pgp-signature", ""}, pgp("SIGNATURE", 4)...), "--XX--"), atMIME | atMultipart | atBinary, "an octet-stream attachment is still a binary (and, as for any binary, the scan stops there, so the later pgp-signature part adds no flag)"},
	} {
		t.Run(c.name, func(t *testing.T) {
			got := artTypeOf(t, s, atArticle(c.extra, c.body))
			t.Logf("CASE %s 0x%06x", c.name, got)
			if got != c.want {
				t.Errorf("ArtType 0x%06x, want 0x%06x %s", got, c.want, c.bug)
			}
		})
	}
}

// TestArticleTypesRoutedByArttypes: diablo itself (diablo.c calls ArticleType() on the wire
// lines) sends each article to the queue of the label whose arttypes it matches: a yEnc
// article to the "arttypes binary" label and a text one to the "arttypes !binary" label.
// An article with a long PGP signature is a text article and is queued for the text label.
func TestArticleTypesRoutedByArttypes(t *testing.T) {
	feeds := GlobalLabel + `label peer
    inhost ` + PeerIP + `
    alias e2e.peer
    addgroup *
end
label bin
    hostname 127.0.0.1
    addgroup *
    arttypes binary
end
label txt
    hostname 127.0.0.1
    addgroup *
    arttypes !binary
end
`
	s := NewSite(t, SiteOptions{Newsfeeds: feeds})
	p := s.Peer(t)
	send1 := func(name string, extra, body []string) string {
		id := msgID(t)
		a := atArticle(extra, body)
		for i, l := range a {
			if strings.HasPrefix(l, "Message-ID:") {
				a[i] = "Message-ID: " + id
			}
		}
		expect(t, p, 335, "IHAVE %s", id)
		send(t, p, wire(a))
		status(t, p, 235, "IHAVE "+name)
		return id
	}
	text := send1("text", nil, []string{"Hello."})
	yenc := send1("yenc", nil, append(append([]string{"=ybegin line=128 size=384 name=a.bin"}, atLines(3, 128, b64Alphabet, "")...), "=yend size=384"))
	sig := send1("long signature", nil, append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA512", "", "Signed text."}, pgp("SIGNATURE", 12)...))
	expect(t, p, 205, "QUIT")

	bin := strings.Join(s.waitQueue(t, "bin", 1), "\n")
	txt := strings.Join(s.waitQueue(t, "txt", 2), "\n")
	if !strings.Contains(bin, yenc) || !strings.Contains(txt, text) {
		t.Errorf("dqueue/bin = %q, dqueue/txt = %q: want yEnc %s in bin and text %s in txt", bin, txt, yenc, text)
	}
	// And each is absent from the other queue.
	if strings.Contains(bin, text) {
		t.Errorf("dqueue/bin = %q, must not have the text article %s", bin, text)
	}
	if strings.Contains(txt, yenc) {
		t.Errorf("dqueue/txt = %q, must not have the yEnc article %s", txt, yenc)
	}
	// A text article with a 12-line signature (an RSA-4096 key) is not a binary.
	if !strings.Contains(txt, sig) || strings.Contains(bin, sig) {
		t.Errorf("dqueue/txt = %q, dqueue/bin = %q: want the long-signature article %s in txt only", txt, bin, sig)
	}
}

// TestSignedArticleNotRejectedAsBinary: a clear-signed text article with a long signature
// (9 or more armor lines, an RSA-4096 key or larger, as in the NoCeM notices) must pass the
// sample IFILTER, which rejects binaries outside *.bina* groups. Before, the signature lines
// were counted as a base64 binary and the article was refused as IncomingFilter.
func TestSignedArticleNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	for _, c := range []struct {
		name string
		n    int
		want string
	}{
		{"8 signature lines", 8, "235"},
		{"9 signature lines", 9, "235"},
		{"12 signature lines", 12, "235"},
		{"24 signature lines", 24, "235"},
	} {
		t.Run(c.name, func(t *testing.T) {
			body := append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA256", "", "A notice."}, pgp("SIGNATURE", c.n)...)
			line := offerGroupsBody(t, s, "news.lists.filters", body)
			if !strings.HasPrefix(line, c.want) {
				t.Errorf("IHAVE with a signed article (%d signature lines): %q, want %s", c.n, line, c.want)
			}
		})
	}
	// A real binary (yEnc) to the same group is still refused.
	yenc := append([]string{"=ybegin line=128 size=384 name=a.bin"}, atLines(3, 128, b64Alphabet, "")...)
	if line := offerGroupsBody(t, s, "news.lists.filters", append(yenc, "=yend size=384")); !strings.HasPrefix(line, "437") {
		t.Errorf("IHAVE with a yEnc article to a text group: %q, want 437", line)
	}
}

// offerGroupsBody offers an article with the given Newsgroups: and body and returns the first
// status line of the final IHAVE reply.
func offerGroupsBody(t *testing.T, s *Site, newsgroups string, body []string) string {
	t.Helper()
	id := msgID(t)
	p := s.Peer(t)
	defer expect(t, p, 205, "QUIT")
	expect(t, p, 335, "IHAVE %s", id)
	send(t, p, wire(newArticle(id, newsgroups, "signed", body...)))
	line, err := p.ReadLine()
	if err != nil {
		t.Fatalf("IHAVE to %s: %v", newsgroups, err)
	}
	return line
}

// pem returns a PEM block: the begin line, n lines of 64 base64 characters and the end line.
func pem(label string, n int) []string {
	return pemWide(label, n, 64)
}

func pemWide(label string, n, w int) []string {
	l := []string{"-----BEGIN " + label + "-----"}
	l = append(l, atLines(n, w, b64Alphabet, "")...)
	return append(l, "-----END "+label+"-----")
}

// TestPEMArticleNotRejectedAsBinary: a text article with a PEM certificate or key (a TLS or
// ssh help request) passes the sample IFILTER, which rejects binaries outside *.bina* groups.
// Before, the lines of a block of 9 or more were counted as a base64 binary.
func TestPEMArticleNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	for _, c := range []struct {
		name string
		body []string
	}{
		{"certificate", append([]string{"My certificate does not verify:"}, pem("CERTIFICATE", 24)...)},
		{"private key", append([]string{"My key:"}, pem("RSA PRIVATE KEY", 25)...)},
		{"public key", append([]string{"My key:"}, pem("PUBLIC KEY", 14)...)},
	} {
		line := offerGroupsBody(t, s, "comp.security.misc", c.body)
		if !strings.HasPrefix(line, "235") {
			t.Errorf("IHAVE with a PEM %s: %q, want 235", c.name, line)
		}
	}
	// A base64 binary behind a BEGIN line past the armor limit is still refused.
	if line := offerGroupsBody(t, s, "comp.security.misc", pem("CERTIFICATE", 120)); !strings.HasPrefix(line, "437") {
		t.Errorf("IHAVE with a 120-line block: %q, want 437", line)
	}
}

// offerHeaders offers an article in group with extra headers and a body, and returns the first
// status line of the final IHAVE reply.
func offerHeaders(t *testing.T, s *Site, group string, extra, body []string) string {
	t.Helper()
	id := msgID(t)
	p := s.Peer(t)
	defer expect(t, p, 205, "QUIT")
	expect(t, p, 335, "IHAVE %s", id)
	a := newArticle(id, group, "mime", body...)
	a = append(append(append([]string{}, a[:6]...), extra...), a[6:]...)
	send(t, p, wire(a))
	line, err := p.ReadLine()
	if err != nil {
		t.Fatalf("IHAVE to %s: %v", group, err)
	}
	return line
}

// TestBase64TextNotRejectedAsBinary: a short text/plain article sent with
// Content-Transfer-Encoding: base64 (some clients do that for non-ASCII text) passes the sample
// IFILTER, which rejects binaries outside *.bina* groups. Before, the header alone made it a
// binary. An image sent the same way is still refused.
func TestBase64TextNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	text := []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64"}
	image := []string{"Mime-Version: 1.0", "Content-Type: image/png", "Content-Transfer-Encoding: base64"}
	short := atLines(3, 40, b64Alphabet, "")
	if line := offerHeaders(t, s, "comp.test", text, short); !strings.HasPrefix(line, "235") {
		t.Errorf("IHAVE with a base64 text/plain article: %q, want 235", line)
	}
	if line := offerHeaders(t, s, "comp.test", image, short); !strings.HasPrefix(line, "437") {
		t.Errorf("IHAVE with a base64 image: %q, want 437", line)
	}
}
