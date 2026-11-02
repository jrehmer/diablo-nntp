package e2e

import (
	_ "embed"
	"encoding/base64"
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

// hdr64 returns the MIME headers of a base64 body with the given Content-Type.
func hdr64(ctype string) []string {
	return []string{"Mime-Version: 1.0", "Content-Type: " + ctype, "Content-Transfer-Encoding: base64"}
}

// b64Text returns n lines of base64 (76 characters, the last one shorter and padded) that
// decode to a patch description with UTF-8 text in it.
func b64Text(n int) []string {
	var text strings.Builder
	for i := 0; text.Len() < n*57-5; i++ {
		text.WriteString("tcp: free saved_syn after an RCU grace period (caf\u00e9) " + strconv.Itoa(i) + "\n")
	}
	enc := base64.StdEncoding.EncodeToString([]byte(text.String()[:n*57-5]))
	var l []string
	for len(enc) > 76 {
		l = append(l, enc[:76])
		enc = enc[76:]
	}
	return append(l, enc)
}

// qpLines returns n lines of quoted-printable UTF-8 (Chinese text): 76 characters made of
// =XX and a soft line break "=" at the end, like a mail client writes non-Latin text.
func qpLines(n int) []string {
	var l []string
	for i := 0; i < n; i++ {
		var b strings.Builder
		for j := 0; b.Len() < 75; j++ {
			c := []rune("在天安门屠杀现场我目睹了中国人民")[(i+j)%16]
			for _, x := range []byte(string(c)) {
				if b.Len()+3 <= 75 {
					b.WriteString("=" + strings.ToUpper(strconv.FormatInt(int64(x), 16)))
				}
			}
		}
		l = append(l, b.String()+"=")
	}
	return l
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
		// Text that was sent as base64 is text when it decodes to text; base64 that does not,
		// or that is not a text part, is binary.
		{"base64 text", hdr64("text/plain; charset=UTF-8"), b64Text(30), atMIME, ""},
		{"base64 text, no Content-Type", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64"}, b64Text(30), atMIME, ""},
		{"base64 text/x-patch", hdr64("text/x-patch"), b64Text(30), atMIME, ""},
		{"base64 text, Content-Type after the encoding", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64", "Content-Type: text/plain; charset=UTF-8"}, b64Text(30), atMIME, ""},
		{"base64 text, ISO-2022-JP", hdr64("text/plain; charset=ISO-2022-JP"), []string{"GyRCJDMkcyRLJEEkTxsoQgo="}, atMIME, ""},
		{"base64 random data in text/plain", hdr64("text/plain; charset=UTF-8"), atLines(3, 76, b64Alphabet, ""), atMIME | atBinary | atBase64, ""},
		{"base64 binary after text", hdr64("text/plain; charset=UTF-8"), append(b64Text(5), atLines(3, 76, b64Alphabet, "")...), atMIME | atBinary | atBase64, ""},
		{"base64 with NUL bytes", hdr64("text/plain"), []string{"AAAAAAAAAAAAAAAA"}, atMIME | atBinary | atBase64, ""},
		{"base64 text/html", hdr64("text/html"), b64Text(30), atMIME | atHTML, ""},
		{"base64 application/pdf", hdr64("application/pdf"), b64Text(30), atMIME | atBinary | atBase64, ""},
		// Quoted-printable text is made of base64 characters, too: '=' inside a line is not
		// base64 (padding comes at the end of the data).
		{"quoted-printable Chinese text", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=utf-8", "Content-Transfer-Encoding: quoted-printable"}, qpLines(40), atMIME, ""},
		{"quoted-printable without headers", nil, qpLines(40), atDefault, ""},
		{"base64 attachment with padding", []string{"Mime-Version: 1.0", "Content-Type: image/png", "Content-Transfer-Encoding: base64"}, append(append([]string{}, b64...), "QUJDRA=="), atMIME | atBinary | atBase64, ""},
		{"base64 data without headers, padded at the end", nil, append(append([]string{}, b64...), "QUJDRA=="), atBinary | atBase64, ""},
		// Header parsing.
		{"uppercase CONTENT-TYPE html", []string{"Mime-Version: 1.0", "CONTENT-TYPE: text/html"}, []string{"<p>Hi</p>"}, atMIME, "misdetection: the header is skipped (tolower(*(ptr + 1) == 'o') precedence bug); should add atHTML"},
		{"uppercase CONTENT-TRANSFER-ENCODING", []string{"Mime-Version: 1.0", "CONTENT-TRANSFER-ENCODING: BASE64"}, atLines(3, 40, b64Alphabet, ""), atMIME, "misdetection: same precedence bug; should be atMIME | atBinary | atBase64"},
		{"uppercase CONTROL cancel", []string{"CONTROL: cancel <x@y>"}, []string{"cancel"}, atDefault, "misdetection: same precedence bug; should be atControl | atCancel"},
		{"Content-Type without space", []string{"Mime-Version: 1.0", "Content-Type:text/html"}, []string{"<p>Hi</p>"}, atMIME, "misdetection: 'Content-Type: ' needs exactly one space; should add atHTML"},
		{"folded multipart Content-Type", []string{"Mime-Version: 1.0", "Content-Type:", " multipart/mixed; boundary=XX"}, []string{"--XX", "--XX--"}, atMIME, "misdetection: the value on the next line is not seen; should add atMultipart"},
		// PGP: every armor form, short (4 lines) and the size of an RSA-4096 signature or key
		// (12 lines of 64 characters, which more than 8 in a row make a base64 binary).
		{"pgp message", nil, pgp("MESSAGE", 4), atPGP, ""},
		{"pgp message long", nil, pgp("MESSAGE", 12), atPGP | atBinary | atBase64, "misdetection: more than 8 armor lines are taken for base64 binary; should be atPGP only"},
		{"pgp message part", nil, append([]string{"-----BEGIN PGP MESSAGE, PART 01/02-----", "Version: 2.6.3i", "", "Part 1"}, pgp("MESSAGE", 4)[3:]...), atDefault, "misdetection: only the exact line BEGIN PGP MESSAGE is recognised; should be atPGP"},
		{"pgp signed message", nil, append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA256", "", "Signed text."}, pgp("SIGNATURE", 4)...), atDefault, "misdetection: not recognised; should be atPGP"},
		{"pgp signed message long signature", nil, append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA512", "", "Signed text."}, pgp("SIGNATURE", 12)...), atBinary | atBase64, "misdetection: not recognised, and the long signature is taken for base64 binary; should be atPGP only"},
		{"pgp public key", nil, pgp("PUBLIC KEY BLOCK", 4), atDefault, "misdetection: not recognised; should be atPGP"},
		{"pgp public key long", nil, pgp("PUBLIC KEY BLOCK", 30), atBinary | atBase64, "misdetection: not recognised, and taken for base64 binary; should be atPGP only"},
		{"pgp private key", nil, pgp("PRIVATE KEY BLOCK", 4), atDefault, "misdetection: not recognised; should be atPGP"},
		{"pgp/mime encrypted", []string{"Mime-Version: 1.0", "Content-Type: multipart/encrypted; protocol=\"application/pgp-encrypted\"; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: application/pgp-encrypted", "", "Version: 1", "--XX", "Content-Type: application/octet-stream", ""}, pgp("MESSAGE", 4)...), "--XX--"), atMIME | atMultipart | atBinary, "misdetection: binary because of the application/octet-stream part, no PGP flag; should be atMIME | atMultipart | atPGP, not binary"},
		{"pgp/mime signed", []string{"Mime-Version: 1.0", "Content-Type: multipart/signed; protocol=\"application/pgp-signature\"; micalg=pgp-sha256; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain", "", "Signed.", "--XX", "Content-Type: application/pgp-signature", ""}, pgp("SIGNATURE", 4)...), "--XX--"), atMIME | atMultipart, "misdetection: no PGP flag; should add atPGP"},
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

//go:embed testdata/arttype/base64-flowed-reply.txt
var base64FlowedReply string

// TestArticleTypeBase64FlowedReply: a German reply as news clients send it, text/plain with
// format=flowed in UTF-8, as base64 of 72 characters a line (testdata/arttype, rewritten from
// an article that was refused as binary: ArtType 000b00). It is text.
func TestArticleTypeBase64FlowedReply(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: GlobalLabel, NoStart: true})
	lines := strings.Split(strings.TrimRight(base64FlowedReply, "\n"), "\n")
	if got := artTypeOf(t, s, lines); got != atMIME {
		t.Errorf("ArtType 0x%06x, want 0x%06x (MIME only)", got, atMIME)
	}
}

// TestArticleTypesRoutedByArttypes: diablo itself (diablo.c calls ArticleType() on the wire
// lines) sends each article to the queue of the label whose arttypes it matches: a yEnc
// article to the "arttypes binary" label and a text one to the "arttypes !binary" label.
// An article with a long PGP signature is queued for the binary label today : that is a misdetection, pinned here like in TestArticleTypes.
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

	bin := strings.Join(s.waitQueue(t, "bin", 2), "\n")
	txt := strings.Join(s.waitQueue(t, "txt", 1), "\n")
	if !strings.Contains(bin, yenc) || !strings.Contains(txt, text) {
		t.Errorf("dqueue/bin = %q, dqueue/txt = %q: want yEnc %s in bin and text %s in txt", bin, txt, yenc, text)
	}
	// And each is absent from the other queue.
	if strings.Contains(bin, text) {
		t.Errorf("dqueue/bin = %q, must not have the text article %s", bin, text)
	}
	if strings.Contains(txt, yenc) || strings.Contains(txt, sig) {
		t.Errorf("dqueue/txt = %q, must not have the yEnc article %s or the long-signature article %s", txt, yenc, sig)
	}
	// Misdetection: a text article with a 12-line signature goes to the binary label; it should
	// go to txt.
	if !strings.Contains(bin, sig) {
		t.Errorf("dqueue/bin = %q, want the long-signature article %s (pinned misdetection)", bin, sig)
	}
}
