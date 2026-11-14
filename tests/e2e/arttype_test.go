package e2e

import (
	"crypto/sha256"
	"embed"
	"encoding/base64"
	"fmt"
	"github.com/go-while/nntp-testkit/client"
	"math/rand"
	"mime/quotedprintable"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
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
	return b64Wrap(patchText(n*57-5), 76)
}

// patchText returns n bytes of a patch description with UTF-8 text in it.
func patchText(n int) []byte {
	var text strings.Builder
	for i := 0; text.Len() < n; i++ {
		text.WriteString("tcp: free saved_syn after an RCU grace period (caf\u00e9) " + strconv.Itoa(i) + "\n")
	}
	return []byte(text.String()[:n])
}

// b64Wrap returns data as one base64 stream in lines of w characters, the last one shorter
// and padded.
func b64Wrap(data []byte, w int) []string {
	enc := base64.StdEncoding.EncodeToString(data)
	var l []string
	for len(enc) > w {
		l = append(l, enc[:w])
		enc = enc[w:]
	}
	return append(l, enc)
}

// randBytes returns n bytes of random data, the same for the same seed.
func randBytes(n int, seed int64) []byte {
	b := make([]byte, n)
	rand.New(rand.NewSource(seed)).Read(b)
	return b
}

// edit returns the lines with f applied to each.
func edit(l []string, f func(string) string) []string {
	var e []string
	for _, x := range l {
		e = append(e, f(x))
	}
	return e
}

// joined returns the lines as one text with a newline after each.
func joined(l []string) []byte {
	return []byte(strings.Join(l, "\n") + "\n")
}

// qpLines returns at least n lines of quoted-printable UTF-8 Chinese text as a mail client
// writes it (mime/quotedprintable): =XX escapes in lines of up to 76 characters, each but the
// last ending in a soft line break. The lines decode to the whole text, valid UTF-8.
func qpLines(n int) []string {
	runes := []rune("今天天气很好，我们一起去公园散步，然后在湖边的小茶馆喝茶聊天。")
	for k := len(runes); ; k += len(runes) {
		var b strings.Builder
		w := quotedprintable.NewWriter(&b)
		for i := 0; i < k; i++ {
			w.Write([]byte(string(runes[i%len(runes)])))
		}
		w.Close()
		l := strings.Split(strings.TrimRight(b.String(), "\r\n"), "\r\n")
		if len(l) >= n {
			return l
		}
	}
}

// hdrQP is the MIME headers of a quoted-printable UTF-8 text.
var hdrQP = []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=utf-8", "Content-Transfer-Encoding: quoted-printable"}

// qpFew returns n lines of quoted-printable German without spaces (long compound words):
// 74 base64 characters with two escapes (=C3=A4, ä), so the escapes are a small part of
// the line.
func qpFew(n int) []string {
	var l []string
	for i := 0; i < n; i++ {
		l = append(l, "Donaudampfschifffahrtsgesellschaftskapit=C3=A4nswitwenrentenversicherung"+strconv.Itoa(10+i))
	}
	return l
}

// mpBody returns the body of a multipart with boundary b: each part after a "--b" line, then
// "--b--".
func mpBody(b string, parts ...[]string) []string {
	var l []string
	for _, p := range parts {
		l = append(append(l, "--"+b), p...)
	}
	return append(l, "--"+b+"--")
}

// b64TextPart is a part with base64 text in it; mpPart one that is itself a multipart.
func b64TextPart() []string {
	return append([]string{"Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...)
}

func mpPart(b string, parts ...[]string) []string {
	return append([]string{"Content-Type: multipart/mixed; boundary=" + b, ""}, mpBody(b, parts...)...)
}

// proseQuotingPart is a text body that quotes a MIME part with boundary b as an example.
func proseQuotingPart(b string) []string {
	return []string{"Here is a MIME example:", "--" + b, "Content-Type: text/plain", "Content-Transfer-Encoding: base64", "", "This is ordinary explanatory prose, not an encoded part."}
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
		// A text part sent as base64 (Content-Type text/..., or none) is decoded instead of
		// counting its lines as base64: it is text when it decodes to text that would be text
		// unencoded, and binary when a decoded byte is NUL, DEL or a control character other
		// than TAB, LF, FF, CR and ESC.
		{"base64 text", hdr64("text/plain; charset=UTF-8"), b64Text(30), atMIME, ""},
		{"base64 text, encoding first", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64", "Content-Type: text/plain; charset=UTF-8"}, b64Text(30), atMIME, ""},
		{"base64 html", hdr64("text/html; charset=UTF-8"), b64Text(30), atMIME | atHTML, ""},
		{"base64 text/x-patch", hdr64("text/x-patch"), b64Text(30), atMIME, ""},
		{"base64 text, ISO-2022-JP", hdr64("text/plain; charset=ISO-2022-JP"), []string{"GyRCJDMkcyRLJEEkTxsoQgo="}, atMIME, ""},
		{"base64 text, CRLF lines", hdr64("text/plain"), edit(b64Text(30), func(l string) string { return l + "\r" }), atMIME, ""},
		{"base64 text, 4 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 4), atMIME, ""},
		{"base64 text, 60 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 60), atMIME, ""},
		{"base64 text, 64 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 64), atMIME, ""},
		{"base64 text, 72 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 72), atMIME, ""},
		{"base64 text, 73 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 73), atMIME, ""},
		{"base64 text, 75 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 75), atMIME, ""},
		{"base64 text, 76 wide", hdr64("text/plain"), b64Wrap(patchText(1500), 76), atMIME, ""},
		// One line longer than dfeedtest's line buffer (64 KiB) is handed over in pieces, as a
		// long line is by diablo's (about 4 KiB): the pieces decode as one stream.
		{"base64 text, one line in pieces", hdr64("text/plain"), b64Wrap(patchText(60000), 80000), atMIME, ""},
		{"base64 random data, one line in pieces", hdr64("text/plain"), b64Wrap(randBytes(60000, 7), 80000), atMIME | atBinary | atBase64, ""},
		{"base64 text, then a plain signature", hdr64("text/plain"), append(b64Text(30), "-- ", "Max"), atMIME, "a \"-- \" signature separator ends the decoding: the signature is not decoded"},
		{"base64 text, two padded streams", hdr64("text/plain"), append(b64Text(5), b64Text(5)...), atMIME, ""},
		{"base64 signed text", hdr64("text/plain"), b64Wrap(joined(append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA512", "", "Signed text."}, pgp("SIGNATURE", 12)...)), 76), atMIME | atPGP, "the decoded lines get the armor rule of unencoded text"},
		// RFC 2045: a body without a Content-Type is text/plain. B64_NOCTYPE_IS_TEXT in
		// lib/arttype.c decides it; with it 0 the text case is atMIME | atBinary | atBase64.
		{"base64 text, no Content-Type", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64"}, b64Text(30), atMIME, ""},
		{"base64 random data, no Content-Type", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64"}, b64Wrap(randBytes(1140, 1), 76), atMIME | atBinary | atBase64, ""},
		// Base64 that does not decode to text.
		{"base64 random data in text/plain", hdr64("text/plain; charset=UTF-8"), atLines(3, 76, b64Alphabet, ""), atMIME | atBinary | atBase64, ""},
		{"base64 text, 12 lines", hdr64("text/plain; charset=UTF-8"), atLines(12, 76, b64Alphabet, ""), atMIME | atBinary | atBase64, "lines of base64 characters that do not decode to text"},
		{"base64 binary after text", hdr64("text/plain; charset=UTF-8"), b64Wrap(append(patchText(300), randBytes(171, 2)...), 76), atMIME | atBinary | atBase64, "one stream: text, then data"},
		{"base64 binary after padded text", hdr64("text/plain"), append(b64Text(5), b64Wrap(randBytes(171, 3), 76)...), atMIME | atBinary | atBase64, "data after the padding is decoded too"},
		{"base64 with NUL bytes", hdr64("text/plain"), []string{"AAAAAAAAAAAAAAAA"}, atMIME | atBinary | atBase64, ""},
		{"base64 with DEL", hdr64("text/plain"), b64Wrap(append(patchText(100), 0x7f), 76), atMIME | atBinary | atBase64, ""},
		{"base64 application/pdf", hdr64("application/pdf"), b64Text(30), atMIME | atBinary | atBase64, ""},
		// Characters that base64 decoders skip do not hide the data.
		{"base64 random data, trailing space", hdr64("text/plain"), edit(b64Wrap(randBytes(1140, 4), 76), func(l string) string { return l + " " }), atMIME | atBinary | atBase64, ""},
		{"base64 random data, leading =", hdr64("text/plain"), edit(b64Wrap(randBytes(1140, 5), 76), func(l string) string { return "=" + l }), atMIME | atBinary | atBase64, ""},
		{"base64 random data, leading !", hdr64("text/plain"), edit(b64Wrap(randBytes(1140, 6), 76), func(l string) string { return "!" + l }), atMIME | atBinary | atBase64, ""},
		{"uuencode under a base64 text header", hdr64("text/plain"), append([]string{"begin 644 a.bin"}, atLines(12, 60, uuAlphabet, "M")...), atMIME | atBinary | atBase64, ""},
		{"uuencode under a base64 header, no Content-Type", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64"}, append([]string{"begin 644 a.bin"}, atLines(12, 60, uuAlphabet, "M")...), atMIME | atBinary | atBase64, ""},
		// Double encoding: the decoded lines get the checks of unencoded text.
		{"base64 of a uuencoded file", hdr64("text/plain"), b64Wrap(joined(append([]string{"begin 644 a.bin"}, atLines(12, 60, uuAlphabet, "M")...)), 76), atMIME | atBinary | atBase64 | atUUencode, ""},
		{"base64 of base64 data", hdr64("text/plain"), b64Wrap(joined(atLines(12, 76, b64Alphabet, "")), 76), atMIME | atBinary | atBase64, ""},
		{"base64 of a yEnc block", hdr64("text/plain"), b64Wrap(joined(append(append([]string{"=ybegin line=128 size=384 name=a.bin"}, atLines(3, 128, b64Alphabet, "")...), "=yend size=384")), 76), atMIME | atBinary | atBase64 | atYenc, "the yEnc header is the first decoded line; the data has no control bytes"},
		{"base64 of text, then a yEnc block", hdr64("text/plain"), b64Wrap(joined(append(append([]string{"Here is the file.", "", "=ybegin part=1 line=128 size=384 name=a.bin"}, atLines(3, 128, b64Alphabet, "")...), "=yend size=384")), 76), atMIME | atBinary | atBase64 | atYenc | atPartial, ""},
		// The last decoded line has no newline: it gets the per-line checks at the end of the
		// article (ArticleTypeEnd), as at a MIME boundary and as unencoded.
		{"base64 text, last line a pgp marker without newline", hdr64("text/plain"), b64Wrap([]byte("Hello.\n-----BEGIN PGP MESSAGE-----"), 76), atMIME | atPGP, ""},
		{"base64 text, last line a pgp marker", hdr64("text/plain"), b64Wrap([]byte("Hello.\n-----BEGIN PGP MESSAGE-----\n"), 76), atMIME | atPGP, ""},
		{"base64 text part, last line a pgp marker without newline", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Wrap([]byte("Hello.\n-----BEGIN PGP MESSAGE-----"), 76)...), "--XX--"), atMIME | atMultipart | atPGP, ""},
		{"pgp marker last, unencoded", nil, []string{"Hello.", "-----BEGIN PGP MESSAGE-----"}, atPGP, ""},
		{"base64 of a uuencoded file, last line without newline", hdr64("text/plain"), b64Wrap([]byte(strings.Join(append([]string{"begin 644 a.bin"}, atLines(9, 60, uuAlphabet, "M")...), "\n")), 76), atMIME | atBinary | atBase64 | atUUencode, "the 9th uuencode line is the last"},
		{"uuencode, 9th line last, unencoded", nil, append([]string{"begin 644 a.bin"}, atLines(9, 60, uuAlphabet, "M")...), atBinary | atUUencode, ""},
		{"base64 text, last line with a control byte without newline", hdr64("text/plain"), b64Wrap([]byte("Hello.\nbye\x01"), 76), atMIME | atBinary | atBase64, ""},
		// A body that quotes MIME headers in its text has no MIME parts: only the article's
		// headers, or the lines after a "--boundary" of a multipart article (boundary from a
		// Content-Type), are a part's headers.
		{"text quoting a base64 encoding header", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8"},
			[]string{"The gateway adds this header:", "Content-Transfer-Encoding: base64", "", "and then the body is ordinary text that is not encoded at all, as you see here."}, atMIME, ""},
		{"text part quoting a base64 encoding header", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			[]string{"--XX", "Content-Type: text/plain", "", "The mail had these headers:", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", "", "and its body was ordinary text, not encoded at all.", "--XX--"}, atMIME | atMultipart, ""},
		{"text quoting a quoted-printable encoding header", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8"},
			append([]string{"Content-Transfer-Encoding: quoted-printable", ""}, edit(atLines(12, 75, b64Alphabet, ""), func(l string) string { return l + "=" })...), atMIME | atBinary | atBase64, "not a declaration: the soft line breaks alone do not make the lines quoted-printable"},
		{"base64 text part, folded quoted boundary", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed;", "\tboundary=\"=_a b\""},
			append(append([]string{"--=_a b", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...), "--=_a b--"), atMIME | atMultipart, ""},
		{"base64 image part, folded quoted boundary", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed;", "\tboundary=\"=_a b\""},
			append(append([]string{"--=_a b", "Content-Type: image/png", "Content-Transfer-Encoding: base64", ""}, b64...), "--=_a b--"), atMIME | atMultipart | atBinary | atBase64, ""},
		// The boundary parameter in its other forms (RFC 2045, RFC 822): a base64 text part
		// is text with each.
		{"boundary = \"XX\"", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary = \"XX\""}, mpBody("XX", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary= \"XX\"", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary= \"XX\""}, mpBody("XX", b64TextPart()), atMIME | atMultipart, ""},
		{"BOUNDARY=XX", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; BOUNDARY=XX"}, mpBody("XX", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary with a quoted pair", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"X\\\"Y\""}, mpBody("X\"Y", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary with a quoted pair of a plain character", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"part\\+one\""}, mpBody("part+one", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary folded before =", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary", "\t=XX"}, mpBody("XX", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary folded after =", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=", " \"XX\""}, mpBody("XX", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary of 70 characters", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"" + strings.Repeat("b", 70) + "\""}, mpBody(strings.Repeat("b", 70), b64TextPart()), atMIME | atMultipart, ""},
		{"boundary of 70 characters with a quoted pair", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"" + strings.Repeat("b", 69) + "\\\"\""}, mpBody(strings.Repeat("b", 69)+"\"", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary of 71 characters", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"" + strings.Repeat("b", 71) + "\""}, mpBody(strings.Repeat("b", 71), b64TextPart()), atMIME | atMultipart | atBinary | atBase64, "by design: not a valid boundary (RFC 2046), so its parts are not decoded, and 30 base64 lines are a binary as on main"},
		// Nested multiparts: a closing delimiter ends its multipart, so siblings do not push out
		// an outer boundary; past four levels the parts are not decoded (as on main).
		{"four nested multipart siblings, then a base64 text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			mpBody("XX", mpPart("Y1", []string{"", "a"}), mpPart("Y2", []string{"", "b"}), mpPart("Y3", []string{"", "c"}), mpPart("Y4", []string{"", "d"}), b64TextPart()), atMIME | atMultipart, ""},
		{"base64 text part four levels deep", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=B1"},
			mpBody("B1", mpPart("B2", mpPart("B3", mpPart("B4", b64TextPart())))), atMIME | atMultipart, ""},
		{"base64 text part five levels deep", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=B1"},
			mpBody("B1", mpPart("B2", mpPart("B3", mpPart("B4", mpPart("B5", b64TextPart()))))), atMIME | atMultipart | atBinary | atBase64, "by design: past four levels nothing is decoded, and 30 base64 lines are a binary as on main"},
		{"base64 image part five levels deep", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=B1"},
			mpBody("B1", mpPart("B2", mpPart("B3", mpPart("B4", mpPart("B5", append([]string{"Content-Type: image/png", "Content-Transfer-Encoding: base64", ""}, b64...)))))), atMIME | atMultipart | atBinary | atBase64, ""},
		{"epilogue quoting a part after the closing delimiter", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(mpBody("XX", []string{"Content-Type: text/plain", "", "Hello."}), "--XX", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", "", "and here the epilogue goes on in plain words, not encoded at all."), atMIME | atMultipart, "after --XX-- the boundary is closed"},
		// MIME parameters are read as such: "boundary=" in another parameter's quoted value
		// is not one, and a boundary parameter counts only on a multipart.
		{"text with a quoted name holding boundary=", []string{"Mime-Version: 1.0", "Content-Type: text/plain; name=\"guide boundary=OUT example.txt\""}, proseQuotingPart("OUT"), atMIME, ""},
		{"text with a boundary parameter", []string{"Mime-Version: 1.0", "Content-Type: text/plain; boundary=OUT"}, proseQuotingPart("OUT"), atMIME, ""},
		{"multipart, a quoted name holding boundary= first", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; name=\"guide boundary=WRONG note.txt\"; boundary=RIGHT"}, mpBody("RIGHT", b64TextPart()), atMIME | atMultipart, ""},
		{"multipart, a quoted notice before the boundary", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; name=\"notice boundary=other\"; boundary=right"}, mpBody("right", b64TextPart()), atMIME | atMultipart, ""},
		{"boundary folded inside its quoted value", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"part", " one\""}, mpBody("part one", b64TextPart()), atMIME | atMultipart, ""},
		// A part's encoding comes from its header block: an encoding header quoted at the end
		// of a text part does not carry over to the next part.
		{"encoding header quoted at the end of a text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=OUT"},
			mpBody("OUT", []string{"Content-Type: text/plain; charset=UTF-8", "", "Here is an example header:", "Content-Transfer-Encoding: base64"},
				[]string{"Content-Type: text/plain; charset=UTF-8", "", "This is ordinary second-part prose that was not sent as base64."}), atMIME | atMultipart, ""},
		// Only a delimiter of a multipart we are in ends a part; other lines that start with
		// "--" are text.
		{"quoted-printable part with a --- line", hdrQP, append([]string{"A text section follows:", "---"}, qpFew(12)...), atMIME, ""},
		{"quoted-printable part with a -- section line", hdrQP, append([]string{"-- section 2"}, qpFew(12)...), atMIME, ""},
		{"base64 text with a bare -- line in it", hdr64("text/plain; charset=UTF-8"), append(append(append([]string{}, b64Text(30)[:4]...), "--"), b64Text(30)[4:]...), atMIME, "decoders skip the '-'; only \"-- \" is a signature separator"},
		{"line starting with a closing delimiter", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			mpBody("XX", []string{"Content-Type: text/plain", "", "The next line is not a delimiter:", "--XX--not the end"}, b64TextPart()), atMIME | atMultipart, ""},
		{"multipart closed inside a part's headers", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			[]string{"--XX", "Content-Type: text/plain", "--XX--", "Content-Transfer-Encoding: base64", "", "and the epilogue goes on in plain words, not encoded at all."}, atMIME | atMultipart, "the closing delimiter ends the header block too"},
		// Quoted-printable lines are not base64 lines only in a text part: in an image or an
		// application part they count, declared or not. In a quoted-printable text part an
		// escape of a byte that is not text makes a binary, as in a base64 one.
		{"quoted-printable lines in image/png", []string{"Mime-Version: 1.0", "Content-Type: image/png"}, qpLines(12), atMIME | atBinary | atBase64, ""},
		{"quoted-printable lines in image/png, declared", []string{"Mime-Version: 1.0", "Content-Type: image/png", "Content-Transfer-Encoding: quoted-printable"}, qpLines(12), atMIME | atBinary | atBase64, ""},
		{"quoted-printable lines in application/octet-stream", []string{"Mime-Version: 1.0", "Content-Type: application/octet-stream"}, qpLines(12), atMIME | atBinary, ""},
		{"quoted-printable lines in application/octet-stream, declared", []string{"Mime-Version: 1.0", "Content-Type: application/octet-stream", "Content-Transfer-Encoding: quoted-printable"}, qpLines(12), atMIME | atBinary, ""},
		{"quoted-printable text with =00", hdrQP, []string{"Hello=00world, a NUL byte", "and more text"}, atMIME | atBinary, ""},
		{"quoted-printable text with =7F", hdrQP, []string{"Hello=7Fworld, a DEL byte"}, atMIME | atBinary, ""},
		{"quoted-printable text with =0C and =1B", hdrQP, []string{"Page one=0C", "=1B$B$3$s$K$A$O=1B(B"}, atMIME, "FF and ESC (ISO-2022-JP) are text"},
		// A line longer than dfeedtest's 64 KiB buffer comes in pieces: an escape cut at the end
		// of one is finished in the next.
		{"quoted-printable =00 cut after its =", hdrQP, []string{strings.Repeat("A", 65534) + "=00" + strings.Repeat("A", 100)}, atMIME | atBinary, ""},
		{"quoted-printable =00 cut after its =0", hdrQP, []string{strings.Repeat("A", 65533) + "=00" + strings.Repeat("A", 100)}, atMIME | atBinary, ""},
		{"quoted-printable text, an escape cut", hdrQP, []string{strings.Repeat("A", 65534) + "=C3=A4" + strings.Repeat("A", 100)}, atMIME, ""},
		{"quoted-printable lines escaping NUL, not declared", nil, edit(qpLines(12), func(l string) string { return "=00" + l[3:] }), atBinary | atBase64, "not text: the lines count as base64"},
		// MIME parts.
		{"base64 text part in multipart", []string{"Mime-Version: 1.0", "Content-Type: multipart/alternative; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...), "--XX--"), atMIME | atMultipart, ""},
		{"base64 text part, then plain text", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/x-patch", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...), "--XX", "Content-Type: text/plain", "", "Thanks.", "--XX--"), atMIME | atMultipart, ""},
		{"base64 image part after a base64 text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append(append([]string{"--XX", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Text(2)...), "--XX", "Content-Type: image/png", "Content-Transfer-Encoding: base64", ""), atLines(3, 40, b64Alphabet, "")...), atMIME | atMultipart | atBinary | atBase64, ""},
		{"base64 part without Content-Type after a text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append(append([]string{"--XX", "Content-Type: text/plain", ""}, "hello"), "--XX", "Content-Transfer-Encoding: base64", ""), b64Wrap(randBytes(171, 8), 76)...), atMIME | atMultipart | atBinary | atBase64, "a part has its own type: the earlier text/plain does not carry over, and random data is not text"},
		// Quoted-printable text is made of base64 characters, too: '=' inside a line is not
		// base64 (padding comes at the end of the data).
		{"quoted-printable Chinese text", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=utf-8", "Content-Transfer-Encoding: quoted-printable"}, qpLines(40), atMIME, ""},
		{"quoted-printable without headers", nil, qpLines(40), atDefault, ""},
		{"base64 attachment with padding", []string{"Mime-Version: 1.0", "Content-Type: image/png", "Content-Transfer-Encoding: base64"}, append(append([]string{}, b64...), "QUJDRA=="), atMIME | atBinary | atBase64, ""},
		{"base64 data without headers, padded at the end", nil, append(append([]string{}, b64...), "QUJDRA=="), atBinary | atBase64, ""},
		// Without the declaration only a line that is valid quoted-printable, at least half
		// =HH escapes, is not base64; in a part declared quoted-printable one escape or a soft
		// line break is enough, but lines without '=' still count.
		{"quoted-printable, few escapes, declared", hdrQP, qpFew(12), atMIME, ""},
		{"quoted-printable, few escapes, not declared", nil, qpFew(12), atBinary | atBase64, "a line with two escapes in 74 base64 characters looks more like base64"},
		{"quoted-printable, soft line breaks only, declared", hdrQP, edit(atLines(12, 75, b64Alphabet, ""), func(l string) string { return l + "=" }), atMIME, ""},
		{"quoted-printable, soft line breaks only, not declared", nil, edit(atLines(12, 75, b64Alphabet, ""), func(l string) string { return l + "=" }), atBinary | atBase64, ""},
		{"base64 lines in a quoted-printable part", hdrQP, b64, atMIME | atBinary | atBase64, "lines without '=' are counted as before"},
		{"base64 after a quoted-printable part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append(append([]string{"--XX", "Content-Type: text/plain; charset=utf-8", "Content-Transfer-Encoding: quoted-printable", ""}, qpLines(3)...), "--XX", "Content-Type: text/plain", ""), edit(b64, func(l string) string { return l[:70] + "=" })...), atMIME | atMultipart | atBinary | atBase64, "the declaration ends at the next boundary"},
		{"base64 with a stray '=' in each line", nil, edit(b64, func(l string) string { return l[:30] + "=" + l[30:] }), atBinary | atBase64, ""},
		{"base64 with a stray '=4F' in each line", nil, edit(b64, func(l string) string { return l[:30] + "=4F" + l[30:72] }), atBinary | atBase64, ""},
		{"base64 with a stray '=' in each line, image/png", hdr64("image/png"), edit(b64, func(l string) string { return l[:30] + "=" + l[30:] }), atMIME | atBinary | atBase64, ""},
		// Header parsing.
		{"pem certificate", nil, pem("CERTIFICATE", 24), atDefault, ""},
		{"pem certificate request", nil, pem("CERTIFICATE REQUEST", 14), atDefault, ""},
		{"pem rsa private key", nil, pem("RSA PRIVATE KEY", 25), atDefault, ""},
		{"pem public key", nil, pem("PUBLIC KEY", 14), atDefault, ""},
		{"pem openssh private key, 70 wide", nil, pemWide("OPENSSH PRIVATE KEY", 20, 70), atDefault, ""},
		{"pem beyond the armor limit", nil, pem("CERTIFICATE", 120), atBinary | atBase64, "by design: only the first 100 lines of an armored block are not counted"},
		{"base64 after a pem block", nil, append(pem("CERTIFICATE", 24), atLines(12, 64, b64Alphabet, "")...), atBinary | atBase64, ""},
		{"sha256 digests, 8 lines", nil, sha256Lines(8, ""), atDefault, ""},
		{"sha256 digests, 20 lines", nil, sha256Lines(20, ""), atDefault, ""},
		{"sha256sum output", nil, sha256Lines(20, "  file.bin"), atDefault, ""},
		{"quoted sha256 digests", nil, quoted(sha256Lines(12, "")), atDefault, ""},
		{"upper-case hex digests", nil, upper(sha256Lines(12, "")), atDefault, ""},
		{"digests then base64 data", nil, append(sha256Lines(12, ""), atLines(12, 64, b64Alphabet, "")...), atBinary | atBase64, ""},
		{"uppercase CONTENT-TYPE html", []string{"Mime-Version: 1.0", "CONTENT-TYPE: text/html"}, []string{"<p>Hi</p>"}, atMIME | atHTML, ""},
		{"uppercase CONTENT-TRANSFER-ENCODING", []string{"Mime-Version: 1.0", "CONTENT-TRANSFER-ENCODING: BASE64"}, atLines(3, 40, b64Alphabet, ""), atMIME | atBinary | atBase64, ""},
		{"uppercase CONTROL cancel", []string{"CONTROL: cancel <x@y>"}, []string{"cancel"}, atControl | atCancel, ""},
		{"Content-Type without space", []string{"Mime-Version: 1.0", "Content-Type:text/html"}, []string{"<p>Hi</p>"}, atMIME | atHTML, ""},
		{"Content-Type with a tab", []string{"Mime-Version: 1.0", "Content-Type:\ttext/html"}, []string{"<p>Hi</p>"}, atMIME | atHTML, ""},
		{"Content-Transfer-Encoding without space", []string{"Mime-Version: 1.0", "Content-Type:image/png", "Content-Transfer-Encoding:base64"}, atLines(3, 40, b64Alphabet, ""), atMIME | atBinary | atBase64, ""},
		{"mixed case content-type multipart", []string{"Mime-Version: 1.0", "cOnTeNt-TyPe:multipart/mixed; boundary=XX"}, []string{"--XX", "--XX--"}, atMIME | atMultipart, ""},
		{"uuencode ending on the 9th line", nil, append([]string{"begin 644 a.bin"}, atLines(9, 60, uuAlphabet, "M")...), atBinary | atUUencode, ""},
		{"base64 ending on the 9th line", nil, atLines(9, 64, b64Alphabet, ""), atBinary | atBase64, ""},
		{"empty body lines", nil, []string{"", "text", "", ""}, atDefault, ""},
		{"base64 CTE, empty body", []string{"Content-Type: image/png", "Content-Transfer-Encoding: base64"}, nil, atMIME | atBinary | atBase64, "the deferred base64 CTE must clear the default bit like the other binary paths"},
		{"folded multipart Content-Type", []string{"Mime-Version: 1.0", "Content-Type:", " multipart/mixed; boundary=XX"}, []string{"--XX", "--XX--"}, atMIME, "misdetection: the value on the next line is not seen; should add atMultipart"},
		// PGP: every armor form, short (4 lines) and the size of an RSA-4096 signature or key
		// (12 lines of 64 characters, which more than 8 in a row make a base64 binary).
		{"pgp message", nil, pgp("MESSAGE", 4), atPGP, ""},
		{"pgp message long", nil, pgp("MESSAGE", 12), atPGP, ""},
		{"pgp message beyond the armor limit", nil, pgp("MESSAGE", 120), atPGP, "encrypted PGP messages can exceed 100 armor lines"},
		{"pgp message large", nil, pgp("MESSAGE", 4096), atPGP, ""},
		{"pgp message part long", nil, pgp("MESSAGE, PART 01/02", 120), atPGP, ""},
		{"base64 binary after a long pgp message", nil, append(pgp("MESSAGE", 120), atLines(12, 64, b64Alphabet, "")...), atPGP | atBinary | atBase64, "the exemption ends with the armor"},
		{"unclosed pgp message before another MIME part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/plain", "", "-----BEGIN PGP MESSAGE-----", "", "--XX", "Content-Type: text/plain", ""}, atLines(12, 64, b64Alphabet, "")...), "--XX--"), atMIME | atMultipart | atPGP | atBinary | atBase64, "armor state ends at the MIME boundary"},
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

//go:embed testdata/arttype/review-246
var review246 embed.FS

// review246Article returns a sample article of testdata/arttype/review-246 as lines, and its
// MIME headers and body apart (for offerHeaders).
func review246Article(t *testing.T, name string) (lines, mime, body []string) {
	t.Helper()
	b, err := review246.ReadFile("testdata/arttype/review-246/" + name + ".txt")
	if err != nil {
		t.Fatal(err)
	}
	lines = strings.Split(strings.TrimRight(string(b), "\n"), "\n")
	for i, l := range lines {
		if l == "" {
			for _, h := range lines[:i] {
				if strings.HasPrefix(h, "Mime-Version:") || strings.HasPrefix(h, "Content-") {
					mime = append(mime, h)
				}
			}
			return lines, mime, lines[i+1:]
		}
	}
	t.Fatalf("%s: no blank line after the headers", name)
	return
}

// TestArticleTypeReviewSamples: the binary samples of a review of #246 (DeepSeek), in
// testdata/arttype/review-246, are binaries:
//   - quoted-printable whose escapes decode to control bytes, in a text part (NUL in each
//     line, all escapes) or in an image, PDF, zip or MPEG part, declared or not
//   - base64 of quoted-printable binary, base64 of BinHex and of uuencode with CR line ends
//   - a base64 text part whose encoding header is only quoted in a preamble, at the end of
//     the previous part, or after the multipart was closed, and a boundary given on
//     text/plain: none of them is a part's header, so the base64 is not decoded
func TestArticleTypeReviewSamples(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: GlobalLabel, NoStart: true})
	for name, want := range map[string]int{
		"qp-declared-text-allescapes":     atMIME | atBinary,
		"qp-declared-text-one-per-line":   atMIME | atBinary,
		"qp-declared-image-allescapes":    atMIME | atBinary | atBase64,
		"qp-declared-image-one-per-line":  atMIME | atBinary | atBase64,
		"qp-declared-image-png-binary":    atMIME | atBinary | atBase64,
		"qp-declared-pdf-binary":          atMIME | atBinary | atBase64,
		"qp-declared-zip-binary":          atMIME | atBinary | atBase64,
		"qp-declared-mpeg-binary":         atMIME | atBinary | atBase64,
		"qp-undeclared-allescapes":        atBinary | atBase64,
		"b64-of-qp-binary":                atMIME | atBinary | atBase64,
		"b64-of-binhex-cr":                atMIME | atBinary | atBase64 | atBinHex,
		"b64-of-uue-cr":                   atMIME | atBinary | atBase64 | atUUencode,
		"state-stale-cte-across-boundary": atMIME | atMultipart | atBinary | atBase64,
		"prose-cte-in-part-then-boundary": atMIME | atMultipart | atBinary | atBase64,
		"closing-in-header-block":         atMIME | atMultipart | atBinary | atBase64,
		"boundary-on-text-plain":          atMIME | atBinary | atBase64,
	} {
		lines, _, _ := review246Article(t, name)
		if got := artTypeOf(t, s, lines); got != want {
			t.Errorf("%s: ArtType 0x%06x, want 0x%06x", name, got, want)
		}
	}
}

// TestArticleTypesCRLF: an article with CRLF line ends throughout (dfeedtest -a, as a peer
// sends it) gets the same ArtType as with LF: its blank line ends the headers.
func TestArticleTypesCRLF(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: GlobalLabel, NoStart: true})
	crlf := func(l []string) []string { return edit(l, func(x string) string { return x + "\r" }) }
	for _, c := range []struct {
		name  string
		extra []string
		body  []string
		want  int
	}{
		{"base64 text", hdr64("text/plain; charset=UTF-8"), b64Text(30), atMIME},
		{"base64 text, last line a pgp marker", hdr64("text/plain"), b64Wrap([]byte("Hello.\n-----BEGIN PGP MESSAGE-----\n"), 76), atMIME | atPGP},
		{"base64 random data in text/plain", hdr64("text/plain"), b64Wrap(randBytes(1140, 12), 76), atMIME | atBinary | atBase64},
		{"uuencode", nil, append([]string{"begin 644 a.bin"}, atLines(12, 60, uuAlphabet, "M")...), atBinary | atUUencode},
	} {
		lf := artTypeOf(t, s, atArticle(c.extra, c.body))
		cr := artTypeOf(t, s, crlf(atArticle(c.extra, c.body)))
		if lf != c.want || cr != c.want {
			t.Errorf("%s: ArtType 0x%06x with LF, 0x%06x with CRLF, want 0x%06x", c.name, lf, cr, c.want)
		}
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
		p.ExpectCmd(335, "IHAVE %s", id)
		p.MustSend(client.Stuff(a))
		p.ExpectStatus(235, "IHAVE "+name)
		return id
	}
	text := send1("text", nil, []string{"Hello."})
	yenc := send1("yenc", nil, append(append([]string{"=ybegin line=128 size=384 name=a.bin"}, atLines(3, 128, b64Alphabet, "")...), "=yend size=384"))
	sig := send1("long signature", nil, append([]string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA512", "", "Signed text."}, pgp("SIGNATURE", 12)...))
	b64txt := send1("base64 text", hdr64("text/plain; charset=UTF-8"), b64Wrap(patchText(1500), 73))
	b64bin := send1("base64 random data", hdr64("text/plain; charset=UTF-8"), b64Wrap(randBytes(1140, 11), 76))
	p.ExpectCmd(205, "QUIT")

	bin := strings.Join(s.waitQueue(t, "bin", 2), "\n")
	txt := strings.Join(s.waitQueue(t, "txt", 3), "\n")
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
	// A text/plain body sent as base64 is text (here 73 characters a line); one that decodes
	// to random data is a binary.
	if !strings.Contains(txt, b64txt) || strings.Contains(bin, b64txt) {
		t.Errorf("dqueue/txt = %q, dqueue/bin = %q: want the base64 text article %s in txt only", txt, bin, b64txt)
	}
	if !strings.Contains(bin, b64bin) || strings.Contains(txt, b64bin) {
		t.Errorf("dqueue/txt = %q, dqueue/bin = %q: want the base64 random data article %s in bin only", txt, bin, b64bin)
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

// A remailer PGP MESSAGE with 110 data lines was refused as 020a00: the old
// 100-line armor limit exposed its payload tail to binary detection.
func TestLongPgpMessageNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	for _, n := range []int{99, 100, 110, 120, 4096} {
		t.Run(strconv.Itoa(n), func(t *testing.T) {
			body := append([]string{"**"}, pgp("MESSAGE", n)...)
			if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "235") {
				t.Errorf("IHAVE with %d PGP data lines: %q, want 235", n, line)
			}
		})
	}
	body := append(pgp("MESSAGE", 120), atLines(12, 64, b64Alphabet, "")...)
	if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "437") {
		t.Errorf("IHAVE with binary data after PGP armor: %q, want 437", line)
	}
	// An unmatched marker cannot exempt non-armor data for the rest of the
	// article. Raw base64 remains indistinguishable from encrypted PGP data.
	body = append([]string{"-----BEGIN PGP MESSAGE-----", "Version: Test", "", "begin 644 file.bin"}, atLines(12, 60, uuAlphabet, "M")...)
	if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "437") {
		t.Errorf("IHAVE with uuencoded data after unmatched PGP marker: %q, want 437", line)
	}
}

// offerGroupsBody offers an article with the given Newsgroups: and body and returns the first
// status line of the final IHAVE reply.
func offerGroupsBody(t *testing.T, s *Site, newsgroups string, body []string) string {
	t.Helper()
	id := msgID(t)
	p := s.Peer(t)
	defer p.ExpectCmd(205, "QUIT")
	p.ExpectCmd(335, "IHAVE %s", id)
	p.MustSend(client.Stuff(newArticle(id, newsgroups, "signed", body...)))
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
	defer p.ExpectCmd(205, "QUIT")
	p.ExpectCmd(335, "IHAVE %s", id)
	a := newArticle(id, group, "mime", body...)
	a = append(append(append([]string{}, a[:6]...), extra...), a[6:]...)
	p.MustSend(client.Stuff(a))
	line, err := p.ReadLine()
	if err != nil {
		t.Fatalf("IHAVE to %s: %v", group, err)
	}
	return line
}

// TestBase64TextNotRejectedAsBinary: a text/plain article sent with
// Content-Transfer-Encoding: base64 (some clients and mail gateways do that for non-ASCII text)
// passes the sample IFILTER, which rejects binaries outside *.bina* groups, at any line width
// and as one long line (which diablo reads in pieces). Before, the header alone made it a
// binary, then nine base64 lines in a row did. An image sent the same way, random data or a
// uuencoded file under a text header are still refused.
func TestBase64TextNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	text := hdr64("text/plain; charset=UTF-8")
	image := hdr64("image/png")
	for _, c := range []struct {
		name  string
		extra []string
		body  []string
		want  string
	}{
		{"base64 text/plain", text, b64Text(30), "235"},
		{"base64 text/plain, 73 wide", text, b64Wrap(patchText(1500), 73), "235"},
		{"base64 text/plain, one long line", text, b64Wrap(patchText(12000), 20000), "235"},
		{"base64 text, no Content-Type", []string{"Mime-Version: 1.0", "Content-Transfer-Encoding: base64"}, b64Text(30), "235"},
		{"base64 text part in multipart", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			append(append([]string{"--XX", "Content-Type: text/x-patch", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...), "--XX--"), "235"},
		{"text quoting an octet-stream header", []string{"Content-Type: text/plain"}, []string{"An example header:", "Content-Type: application/octet-stream", "", "Ordinary text."}, "235"},
		{"real octet-stream header", []string{"Content-Type: application/octet-stream"}, []string{"payload"}, "437"},
		{"base64 image", image, atLines(3, 40, b64Alphabet, ""), "437"},
		{"base64 random data in text/plain", text, b64Wrap(randBytes(1140, 9), 76), "437"},
		{"base64 random data in text/plain, one long line", text, b64Wrap(randBytes(12000, 10), 20000), "437"},
		{"uuencode under a base64 text header", text, append([]string{"begin 644 a.bin"}, atLines(12, 60, uuAlphabet, "M")...), "437"},
		{"text quoting a base64 encoding header", []string{"Mime-Version: 1.0", "Content-Type: text/plain; charset=UTF-8"}, []string{"The gateway adds this header:", "Content-Transfer-Encoding: base64", "", "and then the body is ordinary text that is not encoded at all, as you see here."}, "235"},
		{"base64 text part, boundary = \"XX\"", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary = \"XX\""}, mpBody("XX", b64TextPart()), "235"},
		{"base64 text part, boundary with a quoted pair", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"part\\+one\""}, mpBody("part+one", b64TextPart()), "235"},
		{"text with a quoted name holding boundary=", []string{"Mime-Version: 1.0", "Content-Type: text/plain; name=\"guide boundary=OUT example.txt\""}, proseQuotingPart("OUT"), "235"},
		{"base64 text part, a quoted name holding boundary= first", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; name=\"guide boundary=WRONG note.txt\"; boundary=RIGHT"}, mpBody("RIGHT", b64TextPart()), "235"},
		{"base64 text part, boundary folded inside its quoted value", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=\"part", " one\""}, mpBody("part one", b64TextPart()), "235"},
		{"encoding header quoted at the end of a text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=OUT"},
			mpBody("OUT", []string{"Content-Type: text/plain; charset=UTF-8", "", "Here is an example header:", "Content-Transfer-Encoding: base64"},
				[]string{"Content-Type: text/plain; charset=UTF-8", "", "This is ordinary second-part prose that was not sent as base64."}), "235"},
		{"base64 text part after a line starting with the closing delimiter", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			mpBody("XX", []string{"Content-Type: text/plain", "", "--XX--not the end"}, b64TextPart()), "235"},
		{"base64 text part after four nested multipart siblings", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"},
			mpBody("XX", mpPart("Y1", []string{"", "a"}), mpPart("Y2", []string{"", "b"}), mpPart("Y3", []string{"", "c"}), mpPart("Y4", []string{"", "d"}), b64TextPart()), "235"},
		{"base64 of a uuencoded file, last line without newline", text, b64Wrap([]byte(strings.Join(append([]string{"begin 644 a.bin"}, atLines(9, 60, uuAlphabet, "M")...), "\n")), 76), "437"},
	} {
		if line := offerHeaders(t, s, "comp.test", c.extra, c.body); !strings.HasPrefix(line, c.want) {
			t.Errorf("IHAVE with %s: %q, want %s", c.name, line, c.want)
		}
	}
}

// TestQuotedPrintableNotRejectedAsBinary: quoted-printable Chinese text (=E5=9C=A8... lines of
// base64 characters) passes the sample IFILTER, with and without its MIME headers; base64 data
// with a stray '=' in each line is still refused.
func TestQuotedPrintableNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	b64 := atLines(12, 76, b64Alphabet, "")
	for _, c := range []struct {
		name  string
		extra []string
		body  []string
		want  string
	}{
		{"quoted-printable Chinese", hdrQP, qpLines(40), "235"},
		{"quoted-printable Chinese without headers", nil, qpLines(40), "235"},
		{"base64 with a stray '='", nil, edit(b64, func(l string) string { return l[:30] + "=" + l[30:] }), "437"},
		{"quoted-printable with a --- line", hdrQP, append([]string{"A text section follows:", "---"}, qpFew(12)...), "235"},
		{"quoted-printable lines in image/png, declared", []string{"Mime-Version: 1.0", "Content-Type: image/png", "Content-Transfer-Encoding: quoted-printable"}, qpLines(12), "437"},
		// diablo reads a line longer than its buffer (about 4 KiB) in pieces: an escape cut
		// at the end of one is finished in the next, wherever the cut falls.
		{"quoted-printable text, a long line with escapes", hdrQP, []string{strings.Repeat("Stra=C3=9Fe ", 2000)}, "235"},
		{"quoted-printable =00 at 4026 in a long line", hdrQP, []string{strings.Repeat("A", 4026) + "=00" + strings.Repeat("A", 6000)}, "437"},
		{"quoted-printable =00 at 4027 in a long line", hdrQP, []string{strings.Repeat("A", 4027) + "=00" + strings.Repeat("A", 6000)}, "437"},
		{"quoted-printable =00 at 4028 in a long line", hdrQP, []string{strings.Repeat("A", 4028) + "=00" + strings.Repeat("A", 6000)}, "437"},
		{"quoted-printable =00 at 4029 in a long line", hdrQP, []string{strings.Repeat("A", 4029) + "=00" + strings.Repeat("A", 6000)}, "437"},
	} {
		if line := offerHeaders(t, s, "talk.test", c.extra, c.body); !strings.HasPrefix(line, c.want) {
			t.Errorf("IHAVE with %s: %q, want %s", c.name, line, c.want)
		}
	}
	// Binary samples of the review of #246 are refused too.
	for _, name := range []string{"qp-declared-text-one-per-line", "qp-declared-image-png-binary", "b64-of-qp-binary", "b64-of-uue-cr", "state-stale-cte-across-boundary"} {
		_, mime, body := review246Article(t, name)
		if line := offerHeaders(t, s, "talk.test", mime, body); !strings.HasPrefix(line, "437") {
			t.Errorf("IHAVE with %s: %q, want 437", name, line)
		}
	}
}

// TestArticleTypeStateNotCarriedOver: on one connection, the MIME state an article leaves
// behind (a quoted-printable declaration, a multipart boundary, a base64 text part still being
// decoded at its end) does not change the type of the next article.
func TestArticleTypeStateNotCarriedOver(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	p := s.Peer(t)
	defer p.ExpectCmd(205, "QUIT")
	soft := edit(atLines(12, 75, b64Alphabet, ""), func(l string) string { return l + "=" })
	quoted := append([]string{"--XX", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...)
	for _, c := range []struct {
		name  string
		extra []string
		body  []string
		want  string
	}{
		{"quoted-printable text", hdrQP, qpLines(3), "235"},
		{"soft line breaks, no headers, after it", nil, soft, "437"},
		{"multipart with a base64 text part", []string{"Mime-Version: 1.0", "Content-Type: multipart/mixed; boundary=XX"}, append(append([]string{}, quoted...), "--XX--"), "235"},
		{"text quoting that part, after it", []string{"Mime-Version: 1.0", "Content-Type: text/plain"}, quoted, "437"},
		{"base64 text ending in the middle of a line", hdr64("text/plain"), b64Wrap([]byte("Hello, no newline at the end"), 76), "235"},
		{"base64 lines of text, no headers, after it", nil, b64Text(30), "437"},
	} {
		id := msgID(t)
		p.ExpectCmd(335, "IHAVE %s", id)
		a := newArticle(id, "talk.test", "state", c.body...)
		a = append(append(append([]string{}, a[:6]...), c.extra...), a[6:]...)
		p.MustSend(client.Stuff(a))
		line, err := p.ReadLine()
		if err != nil {
			t.Fatalf("IHAVE %s: %v", c.name, err)
		}
		if !strings.HasPrefix(line, c.want) {
			t.Errorf("IHAVE with %s: %q, want %s", c.name, line, c.want)
		}
	}
}

// sha256Lines returns n SHA-256 digests in hex (64 characters), each followed by suffix.
func sha256Lines(n int, suffix string) []string {
	var l []string
	for i := 0; i < n; i++ {
		l = append(l, fmt.Sprintf("%x", sha256.Sum256([]byte{byte(i), 1}))+suffix)
	}
	return l
}

func quoted(l []string) []string {
	var q []string
	for _, x := range l {
		q = append(q, "> "+x)
	}
	return q
}

func upper(l []string) []string {
	var q []string
	for _, x := range l {
		q = append(q, strings.ToUpper(x))
	}
	return q
}

// TestChecksumListNotRejectedAsBinary: a text article that lists SHA-256 digests (64 hex
// characters a line, as in the announcement of a release) passes the sample IFILTER, which
// rejects binaries outside *.bina* groups. Before, nine digest lines in a row were counted as
// a base64 binary and the article was refused as IncomingFilter.
func TestChecksumListNotRejectedAsBinary(t *testing.T) {
	s := NewSite(t, SiteOptions{
		Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\nend\n",
	})
	offer := func(body []string) string {
		id := msgID(t)
		p := s.Peer(t)
		defer p.ExpectCmd(205, "QUIT")
		p.ExpectCmd(335, "IHAVE %s", id)
		p.MustSend(client.Stuff(newArticle(id, "comp.release", "release checksums", body...)))
		line, err := p.ReadLine()
		if err != nil {
			t.Fatalf("IHAVE: %v", err)
		}
		return line
	}
	for _, n := range []int{8, 9, 20} {
		body := append([]string{"SHA-256 checksums:"}, sha256Lines(n, "  release.tar.xz")...)
		body = append(body, sha256Lines(n, "")...)
		if line := offer(body); !strings.HasPrefix(line, "235") {
			t.Errorf("IHAVE with %d digest lines: %q, want 235", n, line)
		}
	}
	// A real base64 binary to the same group is still refused.
	if line := offer(atLines(12, 64, b64Alphabet, "")); !strings.HasPrefix(line, "437") {
		t.Errorf("IHAVE with base64 lines to a text group: %q, want 437", line)
	}
}

// Header-looking text is body data (RFC 5536 section 2; RFC 2045 section 3).
func TestArticleTypeBodyMimeLiterals(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: GlobalLabel, NoStart: true})
	for _, field := range []string{
		"Content-Type: application/octet-stream",
		"Content-Type: text/html",
		"Content-Type: multipart/encrypted; protocol=application/pgp-encrypted",
		"Content-Type: application/pgp-encrypted",
		"Content-Type: application/postscript",
		"Content-Type: message/partial",
		"Content-Transfer-Encoding: X-Bommanews",
		"Content-Transfer-Encoding: X-UnidataEncoding",
	} {
		t.Run(field, func(t *testing.T) {
			for _, multipart := range []bool{false, true} {
				extra := []string{"Content-Type: text/plain"}
				body := []string{"An example header follows:", field, "", "This is ordinary text."}
				want := atMIME
				if multipart {
					extra = []string{"Content-Type: multipart/mixed; boundary=XX"}
					body = append(append([]string{"--XX", "Content-Type: text/plain", ""}, body...), "--XX--")
					want |= atMultipart
				}
				if got := artTypeOf(t, s, atArticle(extra, body)); got != want {
					t.Errorf("multipart=%v: ArtType=%06x, want %06x", multipart, got, want)
				}
			}
		})
	}
}

func TestPgpArmorTrailingWhitespace(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	for _, suffix := range []string{" ", "\t", " \t "} {
		t.Run(fmt.Sprintf("suffix-%q", suffix), func(t *testing.T) {
			body := pgp("MESSAGE", 120)
			// One gateway-modified data line must not end the armor exemption.
			body[4] += suffix
			if got := artTypeOf(t, s, atArticle(nil, body)); got != atPGP {
				t.Errorf("ArtType=%06x, want %06x", got, atPGP)
			}
			if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "235") {
				t.Errorf("PGP with trailing whitespace: %q, want 235", line)
			}
			body = append(body, atLines(12, 64, b64Alphabet, "")...)
			if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "437") {
				t.Errorf("binary after PGP: %q, want 437", line)
			}
		})
	}
}

func TestLongPgpKeyAndSignature(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	for _, label := range []string{"PUBLIC KEY BLOCK", "PRIVATE KEY BLOCK", "SIGNATURE"} {
		t.Run(label, func(t *testing.T) {
			body := pgp(label, 120)
			if got := artTypeOf(t, s, atArticle(nil, body)); got != atPGP {
				t.Errorf("ArtType=%06x, want %06x", got, atPGP)
			}
			if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "235") {
				t.Errorf("long PGP armor: %q, want 235", line)
			}
			for _, tail := range [][]string{
				append(append([]string{}, body...), atLines(12, 64, b64Alphabet, "")...),
				append(append([]string{}, body[:4]...), append([]string{"not armor!"}, atLines(12, 64, b64Alphabet, "")...)...),
			} {
				if line := offerGroupsBody(t, s, "alt.anonymous.messages", tail); !strings.HasPrefix(line, "437") {
					t.Errorf("binary after END or malformed armor: %q, want 437", line)
				}
			}
		})
	}
}

func TestPgpSignatureWithinSignedMessage(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	for _, n := range []int{20, 90, 100, 120} {
		t.Run(fmt.Sprint(n), func(t *testing.T) {
			body := []string{"-----BEGIN PGP SIGNED MESSAGE-----", "Hash: SHA256", ""}
			for i := 0; i < n; i++ {
				body = append(body, "Ordinary signed text.")
			}
			body = append(body, pgp("SIGNATURE", 120)...)
			if got := artTypeOf(t, s, atArticle(nil, body)); got != atPGP {
				t.Errorf("ArtType=%06x, want %06x", got, atPGP)
			}
			if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "235") {
				t.Errorf("signed message: %q, want 235", line)
			}
			body = append(body, atLines(12, 64, b64Alphabet, "")...)
			if line := offerGroupsBody(t, s, "alt.anonymous.messages", body); !strings.HasPrefix(line, "437") {
				t.Errorf("binary after signature: %q, want 437", line)
			}
		})
	}
}

func TestBase64PgpArmorMime(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	for _, c := range []struct{ media, label string }{{"application/pgp-signature", "SIGNATURE"}, {"application/pgp-keys", "PUBLIC KEY BLOCK"}} {
		t.Run(c.media, func(t *testing.T) {
			body := b64Wrap(joined(pgp(c.label, 120)), 76)
			for _, hdr := range [][]string{hdr64(c.media), {"Content-Transfer-Encoding: base64", "Content-Type: " + c.media}} {
				if got := artTypeOf(t, s, atArticle(hdr, body)); got != atMIME|atPGP {
					t.Errorf("ArtType=%06x, want %06x", got, atMIME|atPGP)
				}
			}
			if line := offerHeaders(t, s, "alt.anonymous.messages", hdr64(c.media), body); !strings.HasPrefix(line, "235") {
				t.Errorf("MIME PGP: %q, want 235", line)
			}
			bad := b64Wrap(randBytes(1140, 12), 76)
			if line := offerHeaders(t, s, "alt.anonymous.messages", hdr64(c.media), bad); !strings.HasPrefix(line, "437") {
				t.Errorf("binary labeled PGP: %q, want 437", line)
			}
			multi := []string{"--pgp-test", "Content-Type: " + c.media, "Content-Transfer-Encoding: base64", ""}
			multi = append(multi, body...)
			multi = append(multi, "--pgp-test", "Content-Type: application/octet-stream", "Content-Transfer-Encoding: base64", "", "AAECAwQ=", "--pgp-test--")
			if got := artTypeOf(t, s, atArticle([]string{"Content-Type: multipart/mixed; boundary=pgp-test"}, multi)); got&atBinary == 0 {
				t.Errorf("binary sibling ArtType=%06x, want binary", got)
			}
		})
	}
}

func TestPaddedBase64GatewayFooter(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	hdr := hdr64("text/plain; charset=UTF-8")
	for _, text := range []string{"Hello.\n", "Hello!!\n"} {
		t.Run(text, func(t *testing.T) {
			body := append(b64Wrap([]byte(text), 76), "", "--", "Posted automatically by a mail-to-news gateway.")
			if got := artTypeOf(t, s, atArticle(hdr, body)); got != atMIME {
				t.Errorf("ArtType=%06x, want %06x", got, atMIME)
			}
			wire := atArticle(hdr, body)
			for i := range wire {
				wire[i] += "\r"
			}
			if got := artTypeOf(t, s, wire); got != atMIME {
				t.Errorf("CRLF ArtType=%06x, want %06x", got, atMIME)
			}
			if line := offerHeaders(t, s, "alt.test", hdr, body); !strings.HasPrefix(line, "235") {
				t.Errorf("gateway footer: %q, want 235", line)
			}
			for _, tail := range [][]string{atLines(12, 64, b64Alphabet, ""), append([]string{"begin 644 file.bin"}, atLines(12, 60, uuAlphabet, "M")...)} {
				if line := offerHeaders(t, s, "alt.test", hdr, append(append([]string{}, body...), tail...)); !strings.HasPrefix(line, "437") {
					t.Errorf("binary after footer: %q, want 437", line)
				}
			}
		})
	}
}
func TestUnpaddedBase64GatewayFooter(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	hdr := hdr64("text/plain; charset=UTF-8")
	for _, text := range []string{"Hello\n"} {
		t.Run(text, func(t *testing.T) {
			body := append(b64Wrap([]byte(text), 76), "", "--", "Posted automatically by a mail-to-news gateway.")
			if got := artTypeOf(t, s, atArticle(hdr, body)); got != atMIME {
				t.Errorf("ArtType=%06x, want %06x", got, atMIME)
			}
			wire := atArticle(hdr, body)
			for i := range wire {
				wire[i] += "\r"
			}
			if got := artTypeOf(t, s, wire); got != atMIME {
				t.Errorf("CRLF ArtType=%06x, want %06x", got, atMIME)
			}
			if line := offerHeaders(t, s, "alt.test", hdr, body); !strings.HasPrefix(line, "235") {
				t.Errorf("gateway footer: %q, want 235", line)
			}
			for _, tail := range [][]string{atLines(12, 64, b64Alphabet, ""), append([]string{"begin 644 file.bin"}, atLines(12, 60, uuAlphabet, "M")...)} {
				if line := offerHeaders(t, s, "alt.test", hdr, append(append([]string{}, body...), tail...)); !strings.HasPrefix(line, "437") {
					t.Errorf("binary after footer: %q, want 437", line)
				}
			}
		})
	}
}

func TestBase64IncompleteGatewaySeparator(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	for _, body := range [][]string{
		{"AAEC", "", "--", "AwQF"},
		{"SG", "", "--", "VsbG8KAA=="},
		{"SGVs", "--", "bG8KAA=="},
	} {
		if got := artTypeOf(t, s, atArticle(hdr64("text/plain"), body)); got&atBinary == 0 {
			t.Errorf("body %q: ArtType=%06x, want binary", body, got)
		}
		if line := offerHeaders(t, s, "alt.test", hdr64("text/plain"), body); !strings.HasPrefix(line, "437") {
			t.Errorf("body %q: %q, want 437", body, line)
		}
	}
}

func TestNestedMessageArticleTypes(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    arttypes binary\nend\n"})
	for _, outer := range []bool{false, true} {
		for _, binary := range []bool{false, true} {
			name := fmt.Sprintf("multipart=%v/binary=%v", outer, binary)
			t.Run(name, func(t *testing.T) {
				hdr := []string{"Content-Type: message/rfc822", "Content-Transfer-Encoding: 7bit"}
				inner := append([]string{"From: author@example.test", "Subject: forwarded text", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...)
				if binary {
					inner = append([]string{"From: author@example.test", "Content-Type: application/octet-stream", "Content-Transfer-Encoding: base64", ""}, b64Wrap(randBytes(1140, 12), 76)...)
				}
				body := inner
				if outer {
					hdr = []string{"Content-Type: multipart/mixed; boundary=outer"}
					body = append([]string{"--outer", "Content-Type: message/rfc822", "Content-Transfer-Encoding: 7bit", ""}, inner...)
					body = append(body, "--outer", "Content-Type: text/plain", "", "Content-Type: application/octet-stream", "--outer--")
				}
				got := artTypeOf(t, s, atArticle(hdr, body))
				if (got&atBinary != 0) != binary {
					t.Errorf("ArtType=%06x, want binary=%v", got, binary)
				}
				want := "235"
				if binary {
					want = "437"
				}
				if line := offerHeaders(t, s, "alt.test", hdr, body); !strings.HasPrefix(line, want) {
					t.Errorf("%q, want %s", line, want)
				}
			})
		}
	}
}

func TestNestedMessageHeaderScope(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	for _, enc := range []string{"", "7bit", "8bit", "binary"} {
		hdr := []string{"Content-Type: message/rfc822"}
		if enc != "" {
			hdr = append(hdr, "Content-Transfer-Encoding: "+enc)
		}
		// Two successive encapsulations exercise state reset without recursion.
		inner := append([]string{"Subject: text", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...)
		body := append([]string{"Subject: forwarded", "Content-Type: message/rfc822", ""}, inner...)
		if got := artTypeOf(t, s, atArticle(hdr, body)); got&atBinary != 0 {
			t.Errorf("encoding %q: ArtType=%06x, want text", enc, got)
		}
	}
	for _, media := range []string{"message/rfc822-extra", "message/partial"} {
		body := append([]string{"Subject: text", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...)
		if got := artTypeOf(t, s, atArticle([]string{"Content-Type: " + media}, body)); got&atBinary == 0 {
			t.Errorf("media %q: ArtType=%06x, want binary", media, got)
		}
	}
	body := append([]string{"--outer", "Content-Type: message/rfc822", "", "Subject: forwarded", "Content-Type: text/plain", "Content-Transfer-Encoding: base64", ""}, b64Text(30)...)
	body = append(body, "--outer", "Content-Type: application/octet-stream", "", "binary attachment", "--outer--")
	if got := artTypeOf(t, s, atArticle([]string{"Content-Type: multipart/mixed; boundary=outer"}, body)); got&atBinary == 0 {
		t.Errorf("outer binary sibling: ArtType=%06x, want binary", got)
	}
}

// offerLines offers an article to the site with IHAVE from the peer and returns the reply code.
func offerLines(t *testing.T, s *Site, lines []string) string {
	t.Helper()
	p := s.Peer(t)
	defer p.ExpectCmd(205, "QUIT")
	id := headerValue(lines[:slices.Index(lines, "")], "Message-ID")
	p.ExpectCmd(335, "IHAVE %s", id)
	p.MustSend(client.Stuff(lines))
	line, err := p.ReadLine()
	if err != nil {
		t.Fatalf("IHAVE: %v", err)
	}
	return line[:3]
}

// TestIncomingFilterTextBinary: with `textbinary <size>` in the IFILTER label, an article with
// text in its body before a binary of at most <size> bytes of encoded data (the lines of its
// uuencode, BinHex or base64 runs, without line ends) is not refused in a non-binary group, as
// in early groups where small files were posted with their discussion. An article that is only
// a binary (also one whose "text" is white space, part headers, a MIME delimiter or short runs
// of encoded lines), one with its text only after the binary, one whose binary is over the
// limit (also one cut into runs of 8 lines, or a base64 part that is not text), a yEnc binary
// and an article the label also filters for another type (html, in the article header or in a
// part after the binary) are refused as before.
func TestIncomingFilterTextBinary(t *testing.T) {
	ifilter := func(types, extra string) string {
		return DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes " + types + "\n" + extra + "end\n"
	}
	text := []string{"Here is the small program I mentioned, uuencoded.", "It runs on any model."}
	uu := func(n int) []string {
		return append(append([]string{"begin 644 prog.bin"}, atLines(n, 60, uuAlphabet, "M")...), "end")
	}
	const bhxAlphabet = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"
	binhex := append(append([]string{"(This file must be converted with BinHex 4.0)", ":" + strings.Repeat("!", 63)},
		atLines(12, 64, bhxAlphabet, "")...), "!!!!:")
	cat := func(parts ...[]string) []string {
		var l []string
		for _, p := range parts {
			l = append(l, p...)
		}
		return l
	}
	offer := func(s *Site, lines []string) string { return offerLines(t, s, lines) }
	art := func(body []string) []string { return newArticle(msgID(t), "comp.misc", "a program", body...) }
	html := func(body []string) []string {
		a := art(body)
		i := slices.Index(a, "")
		return slices.Insert(a, i, "Mime-Version: 1.0", "Content-Type: text/html")
	}
	mime := func(ctype string, body []string) []string {
		a := art(body)
		i := slices.Index(a, "")
		return slices.Insert(a, i, "Mime-Version: 1.0", "Content-Type: "+ctype)
	}
	plain := func(l ...string) []string { return append([]string{"Content-Type: text/plain", ""}, l...) }
	uuRaw := func(n int) []string { return atLines(n, 60, uuAlphabet, "M") }
	// 9 lines, then runs of 8 lines each ended by a line of text: a binary cut into short runs
	// after its first one, which counts as a run of 9 or more lines.
	cut := func(groups int) []string {
		l := uuRaw(9)
		for i := 0; i < groups; i++ {
			l = append(append(l, uuRaw(8)...), "x")
		}
		return l
	}
	b64Cut := func(groups int) []string {
		l := atLines(9, 76, b64Alphabet, "")
		for i := 0; i < groups; i++ {
			l = append(append(l, atLines(8, 76, b64Alphabet, "")...), atLines(1, 56, b64Alphabet, "")...)
		}
		return l
	}
	b64One := func(s string) []string { // a base64 text part of one line, no trailing newline in the data
		return []string{"Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: base64", "",
			base64.StdEncoding.EncodeToString([]byte(s))}
	}
	nestedEmpty := append([]string{"Content-Type: multipart/mixed; boundary=i", ""}, mpBody("i", plain())...)
	imagePart := func(data []string) []string {
		return append([]string{"Content-Type: image/jpeg", "Content-Transfer-Encoding: base64", ""}, data...)
	}

	// Without the option, a small binary in a text article is refused (the control).
	s0 := NewSite(t, SiteOptions{Newsfeeds: ifilter("binary", "")})
	if got := offer(s0, art(cat(text, uu(12)))); got != "437" {
		t.Errorf("no textbinary, text with a small uuencoded file: IHAVE %s, want 437", got)
	}

	s := NewSite(t, SiteOptions{Newsfeeds: ifilter("binary", "    textbinary 4k\n")})
	for _, c := range []struct {
		name string
		art  []string
		want string
	}{
		{"text with a small uuencoded file", art(cat(text, uu(12), text)), "235"},
		{"uuencoded file after the text", art(cat(text, uu(40))), "235"},
		{"text with a small BinHex file", art(cat(text, binhex)), "235"},
		{"text with base64 lines", art(cat(text, atLines(12, 64, b64Alphabet, ""))), "235"},
		{"text with a uuencoded file over the limit", art(cat(text, uu(100))), "437"},
		{"a uuencoded file alone", art(uu(12)), "437"},
		{"a uuencoded file with the text after it", art(cat(uu(12), text)), "437"},
		{"a uuencoded file with its last short lines, no text", art(append(append(uu(12)[:13], "&9F%L<V4*", "`"), "end")), "437"},
		{"text with a uuencoded file and its last short lines", art(cat(text, append(append(uu(12)[:13], "&9F%L<V4*", "`"), "end"))), "235"},
		// 1: after the binary every encoded line counts, whatever the length of its run
		{"text with a uuencoded file cut into runs of 8 lines", art(cat(text, cut(10))), "437"},
		{"text with a small cut uuencoded file", art(cat(text, cut(1))), "235"},
		{"text with an image part cut into runs of 8 lines",
			mime("multipart/mixed; boundary=b", mpBody("b", plain(text...), imagePart(b64Cut(10)))), "437"},
		{"text with a small image part", mime("multipart/mixed; boundary=b",
			mpBody("b", plain(text...), imagePart(atLines(12, 76, b64Alphabet, "")))), "235"},
		// 2, 3, 4: encoded runs of fewer than 9 lines, white space and a MIME delimiter are not text
		{"short runs of uuencode lines, no text", art(cat(uu(8), uu(9))), "437"},
		{"white space and a uuencoded file", art(cat([]string{" ", "\t"}, uu(12))), "437"},
		{"a closed multipart and a part with only a uuencoded file", mime("multipart/mixed; boundary=o",
			mpBody("o", nestedEmpty, plain(uu(12)...))), "437"},
		{"a closed multipart and a part with text and a uuencoded file", mime("multipart/mixed; boundary=o",
			mpBody("o", nestedEmpty, plain(cat(text, uu(12))...))), "235"},
		// review of chatgpt-1: the text of a base64 text part is its decoded text
		{"a base64 text part of white space, then a part with a uuencoded file", mime("multipart/mixed; boundary=b",
			mpBody("b", b64One(" \t\n"), plain(uu(12)...))), "437"},
		{"a base64 text part of a line end, then a part with a uuencoded file", mime("multipart/mixed; boundary=b",
			mpBody("b", b64One("\n"), plain(uu(12)...))), "437"},
		{"a base64 text part with text, then a part with a uuencoded file", mime("multipart/mixed; boundary=b",
			mpBody("b", b64One("Real discussion.\n"), plain(uu(12)...))), "235"},
		// 5, 6: a base64 text part after the binary is checked on its own
		{"text with a small uuencoded file, then a base64 text part", mime("multipart/mixed; boundary=b",
			mpBody("b", plain(cat(text, uu(12))...), b64TextPart())), "235"},
		{"text with a small uuencoded file, then a base64 text part that decodes to a yEnc header",
			mime("multipart/mixed; boundary=b", mpBody("b", plain(cat(text, uu(12))...),
				b64One("=ybegin line=128 size=6 name=a.bin"))), "437"},
		{"text with a small uuencoded file, then a base64 text part of one line without a newline",
			mime("multipart/mixed; boundary=b", mpBody("b", plain(cat(text, uu(12))...),
				b64One("Signed: Jesse"))), "235"},
		// 7: a type that comes after the binary is checked too, by a label that filters it
		{"text with a small uuencoded file, then an HTML part", mime("multipart/alternative; boundary=b",
			mpBody("b", plain(cat(text, uu(12))...), []string{"Content-Type: text/html", "", "<p>hi</p>"})), "235"},
		{"text with a small yEnc file", art(cat(text, []string{"=ybegin line=128 size=6 name=a.bin", "abcdef", "=yend size=6"})), "437"},
		{"text with a small uuencoded file and a yEnc file", art(cat(text, uu(12), []string{"=ybegin line=128 size=6 name=a.bin", "abcdef", "=yend size=6"})), "437"},
	} {
		if got := offer(s, c.art); got != c.want {
			t.Errorf("textbinary 4k, %s: IHAVE %s, want %s", c.name, got, c.want)
		}
	}

	// The limit is the bytes of the encoded lines: 12 lines of 61 are 732 bytes.
	sb := NewSite(t, SiteOptions{Newsfeeds: ifilter("binary", "    textbinary 732\n")})
	if got := offer(sb, art(cat(text, uu(12)))); got != "235" {
		t.Errorf("textbinary 732, 732 bytes of uuencode: IHAVE %s, want 235", got)
	}
	if got := offer(sb, art(cat(text, uu(13)))); got != "437" {
		t.Errorf("textbinary 732, 793 bytes of uuencode: IHAVE %s, want 437", got)
	}

	// A label that also filters html still refuses an HTML article with a small binary,
	// and lets a plain text one through.
	sh := NewSite(t, SiteOptions{Newsfeeds: ifilter("binary,html", "    textbinary 4k\n")})
	if got := offer(sh, html(cat(text, uu(12)))); got != "437" {
		t.Errorf("arttypes binary,html, HTML with a small uuencoded file: IHAVE %s, want 437", got)
	}
	if got := offer(sh, art(cat(text, uu(12)))); got != "235" {
		t.Errorf("arttypes binary,html, text with a small uuencoded file: IHAVE %s, want 235", got)
	}
	alt := mime("multipart/alternative; boundary=b", mpBody("b", plain(cat(text, uu(12))...),
		[]string{"Content-Type: text/html", "", "<p>hi</p>"}))
	if got := offer(sh, alt); got != "437" {
		t.Errorf("arttypes binary,html, a small uuencoded file in a text part, then an HTML part: IHAVE %s, want 437", got)
	}

	// 8: a limit of 2g or more is not off
	sg := NewSite(t, SiteOptions{Newsfeeds: ifilter("binary", "    textbinary 4g\n")})
	if got := offer(sg, art(cat(text, uu(12)))); got != "235" {
		t.Errorf("textbinary 4g, text with a small uuencoded file: IHAVE %s, want 235", got)
	}
}

// TestTextBinaryArtType: with `textbinary` in the IFILTER label the ArtType that diablo logs and
// feeds match on is the one the article has without it (frozen when the article became a
// binary), and the type the label is checked again with is the article's without its binary
// bits, including what comes after the binary; it is none for an article that is refused.
// dfeedtest -a does what LoadArticle() does.
func TestTextBinaryArtType(t *testing.T) {
	ifilter := func(extra string) string {
		return DefaultNewsfeeds + "label IFILTER\n    addgroup *\n    delgroup *.bina*\n    arttypes binary\n" + extra + "end\n"
	}
	off := NewSite(t, SiteOptions{Newsfeeds: ifilter(""), NoStart: true})
	on := NewSite(t, SiteOptions{Newsfeeds: ifilter("    textbinary 4k\n"), NoStart: true})
	feeds := NewSite(t, SiteOptions{Newsfeeds: textBinFeeds, NoStart: true})
	text := []string{"Here is the small program I mentioned, uuencoded."}
	uu := func(n int) []string {
		return append(append([]string{"begin 644 prog.bin"}, atLines(n, 60, uuAlphabet, "M")...), "end")
	}
	cat := func(parts ...[]string) []string {
		var l []string
		for _, p := range parts {
			l = append(l, p...)
		}
		return l
	}
	tbType := func(s *Site, lines []string) string {
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
			if v, ok := strings.CutPrefix(strings.TrimSpace(l), "TextBinaryType: "); ok {
				return v
			}
		}
		return "-"
	}
	html := []string{"Content-Type: text/html", "", "<p>hi</p>"}
	mix := func(ctype string, parts ...[]string) []string {
		return atArticle([]string{"Mime-Version: 1.0", "Content-Type: " + ctype}, mpBody("b", parts...))
	}
	txt := func(l ...string) []string { return append([]string{"Content-Type: text/plain", ""}, l...) }

	// matched returns the feed labels dfeedtest -a says the article goes to.
	matched := func(lines []string) []string {
		t.Helper()
		cmd := exec.Command(filepath.Join(feeds.Bin, "dfeedtest"), "-a")
		cmd.Dir = feeds.Dir
		cmd.Env = append(os.Environ(), feeds.Env...)
		cmd.Stdin = strings.NewReader(strings.Join(lines, "\n") + "\n")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("dfeedtest -a: %v\n%s", err, out)
		}
		var l []string
		for _, ln := range strings.Split(string(out), "\n") {
			if v, ok := strings.CutPrefix(ln, "Matched: "); ok {
				l = append(l, v)
			}
		}
		slices.Sort(l)
		return l
	}
	for _, c := range []struct {
		name  string
		lines []string
		want  []string // the feeds (arttypes !binary, none / 1k / 4k textbinary) that get the article
	}{
		{"plain text", atArticle(nil, text), []string{"fbig", "fnone", "fsmall"}},
		{"text, 732 bytes of uuencode", atArticle(nil, cat(text, uu(12))), []string{"fbig", "fsmall"}},
		{"text, 1830 bytes of uuencode", atArticle(nil, cat(text, uu(30))), []string{"fbig"}},
		{"text, 8 KB of uuencode", atArticle(nil, cat(text, uu(130))), nil},
		{"uuencode, no text", atArticle(nil, uu(12)), nil},
		{"text, small uuencode, yEnc", atArticle(nil, cat(text, uu(12), []string{"=ybegin line=128 size=6 name=a.bin", "abcdef", "=yend size=6"})), nil},
	} {
		if got := matched(c.lines); !slices.Equal(got, c.want) {
			t.Errorf("%s: feeds %q, want %q", c.name, got, c.want)
		}
	}

	for _, c := range []struct {
		name  string
		lines []string
		want  string // the type of the second check; "none" = the article is refused
	}{
		{"text, small uuencoded file", atArticle(nil, cat(text, uu(12))), fmt.Sprintf("%06x", atDefault)},
		{"text, uuencoded file over the limit", atArticle(nil, cat(text, uu(100))), "none"},
		{"uuencoded file only", atArticle(nil, uu(12)), "none"},
		{"multipart, text with a small uuencoded file, then an HTML part",
			mix("multipart/alternative; boundary=b", txt(cat(text, uu(12))...), html),
			fmt.Sprintf("%06x", atMIME|atMultipart|atHTML)},
		{"multipart, text with a small uuencoded file, then a yEnc part",
			mix("multipart/mixed; boundary=b", txt(cat(text, uu(12))...),
				txt("=ybegin line=128 size=6 name=a.bin", "abcdef", "=yend size=6")), "none"},
	} {
		got := artTypeOf(t, on, c.lines)
		if want := artTypeOf(t, off, c.lines); got != want {
			t.Errorf("%s: ArtType with textbinary %06x, without it %06x; it must not change", c.name, got, want)
		}
		if v := tbType(on, c.lines); v != c.want {
			t.Errorf("%s: TextBinaryType %s, want %s", c.name, v, c.want)
		}
		if v := tbType(off, c.lines); v != "-" {
			t.Errorf("%s: without textbinary TextBinaryType is shown (%s)", c.name, v)
		}
	}
}

// textBinFeeds: a peer and three outgoing feeds that take no binary (arttypes !binary), the
// first without textbinary, the others with 1k and 4k.
var textBinFeeds = func() string {
	feed := func(name, extra string) string {
		return "label " + name + "\n    hostname 127.0.0.1\n    alias e2e." + name +
			"\n    addgroup *\n    arttypes !binary\n" + extra + "end\n"
	}
	return GlobalLabel + "label peer\n    inhost " + PeerIP + "\n    alias e2e.peer\n    addgroup *\nend\n" +
		feed("fnone", "") + feed("fsmall", "    textbinary 1k\n") + feed("fbig", "    textbinary 4k\n")
}()

// TestTextBinaryOutgoingFeed: `textbinary <size>` in an outgoing feed makes its arttypes ignore
// the binary of an article with text before a binary of at most <size> bytes of encoded data
// (the feeds here have arttypes !binary). The article goes through the master as diablo
// takes it in, with no IFILTER label: a feed without the option, a binary over the feed's
// limit, a binary with no text before it and one with a yEnc file after it do not get it.
func TestTextBinaryOutgoingFeed(t *testing.T) {
	s := NewSite(t, SiteOptions{Newsfeeds: textBinFeeds})
	text := []string{"Here is the small program I mentioned, uuencoded."}
	uu := func(n int) []string {
		return append(append([]string{"begin 644 prog.bin"}, atLines(n, 60, uuAlphabet, "M")...), "end")
	}
	cat := func(parts ...[]string) []string {
		var l []string
		for _, p := range parts {
			l = append(l, p...)
		}
		return l
	}
	arts := []struct {
		name string
		body []string
		want []string // the feeds that get it
	}{
		{"plain text", text, []string{"fbig", "fnone", "fsmall"}},
		{"text, 732 bytes of uuencode", cat(text, uu(12)), []string{"fbig", "fsmall"}},
		{"text, 1830 bytes of uuencode", cat(text, uu(30)), []string{"fbig"}},
		{"text, 8 KB of uuencode", cat(text, uu(130)), nil},
		{"uuencode, no text", uu(12), nil},
		{"text, small uuencode, yEnc", cat(text, uu(12), []string{"=ybegin line=128 size=6 name=a.bin", "abcdef", "=yend size=6"}), nil},
	}
	ids := make([]string, len(arts))
	for i, a := range arts {
		lines := newArticle(msgID(t), "comp.misc", a.name, a.body...)
		ids[i] = headerValue(lines[:slices.Index(lines, "")], "Message-ID")
		if got := offerLines(t, s, lines); got != "235" {
			t.Fatalf("%s: IHAVE %s, want 235 (there is no IFILTER label)", a.name, got)
		}
	}
	// the plain article reaches every feed; the master writes the queues in the order it
	// got the articles, so when fnone has it the others were decided too
	s.waitQueue(t, "fnone", 1)
	for _, f := range []string{"fnone", "fsmall", "fbig"} {
		q := s.queue(t, f)
		for i, a := range arts {
			has := !slices.Contains(deepRevMissing(q, []string{ids[i]}), ids[i])
			if want := slices.Contains(a.want, f); has != want {
				t.Errorf("feed %s, %s: queued %v, want %v\ndqueue/%s: %q", f, a.name, has, want, f, q)
			}
		}
	}
}
