/*-
 * Copyright (c) 2000 Russell Vincent
 * All rights reserved.
 * Algorithms Copyright (c) 2000 Joe Greco and sol.net Network Services
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 *
 *  Try and categorise an article into a range of types
 *
 *  We keep state info as the article is passed to us line by line
 *
 *  Once we have found an article type, we keep to that type unless
 *  we find a different type. We never reset.
 *
 *  Once we have found a binary, we stop scanning the article - save CPU
 *  (unless a binary in a text article is measured, see TextBinMax)
 *
 */

#include "defs.h"

Prototype void InitArticleType(void);
Prototype int ArtTypeConv(char *t);
Prototype int ArtTypeMatch(int articletype, ArtTypeList *acctypes);
Prototype int ArticleType(char *line, int len, int inheader);
Prototype int ArticleTypePiece(char *line, int len, int inheader);
Prototype int ArticleTypeEnd(void);
Prototype void ArticleTypeTextBinary(int max);
Prototype int ArticleTypeTextBinaryType(void);

#define	ARMOR_MAX	100	/* lines of other armor that are not counted */
#define	ARMOR_PGP_HEADERS -1	/* encrypted PGP message armor headers */
#define	ARMOR_PGP_DATA	-2	/* encrypted PGP message radix-64 data */

static int lineHas(const char *line, int len, const char *needle);
static int tbResult(int t);
int isBinHexLine(char *line, int len, int firstline);
int isUuencodedLine(char *buf, int len);

/*
 * Global state variables required for multi-line checking
 */
int Type = ARTTYPE_DEFAULT;
int UUencode = 0;
int BinHex = 0;
int Base64 = 0;
int PGPMime = 0;	/* in a PGP/MIME encrypted article: its octet-stream part is not a binary */
static int CTMessage = 0;	/* next body is an encapsulated message header block */
static int CTEIdentity = 1;	/* absent, 7bit, 8bit or binary transfer encoding */
static int CTArmorPGP = 0;	/* ASCII-armored PGP signature/key MIME part */
int CTText = 0;	/* the part's Content-Type is text/ */
int CTEBase64 = 0;	/* a base64 Content-Transfer-Encoding is waiting for the end of its header block */
int Armor = 0;	/* lines left, negative PGP armor state, or 0 outside armor */

/*
 * A base64 part with no Content-Type in its header block.  RFC 2045 makes
 * its type text/plain, so it is decoded and checked like a text/ part.
 * Set this to 0 to make such a part a binary at the end of its headers,
 * as before.  (The one place that decides it; tests/e2e/arttype_test.go
 * pins it in the "base64 text, no Content-Type" cases.)
 */
#define	B64_NOCTYPE_IS_TEXT	1

/*
 * A base64 text part is decoded instead of counting its lines as base64:
 * see b64Decode().  The base64 group (B64Bits, B64NBits, B64Quad, B64Pad)
 * carries across lines and calls, so the line width of the encoding or a
 * line handed over in pieces does not change what it decodes to.  The
 * decoded text is split into lines (DLine) and those get the same per-line
 * checks as an unencoded body, with their own counters (Dec*).
 */
#define	DLINE_MAX	1024	/* a longer decoded line is checked by its start */

static int CTSeen = 0;	/* the part has a Content-Type */
static int CTEQP = 0;	/* a quoted-printable Content-Transfer-Encoding is waiting for the end of its header block */
static int QPPart = 0;	/* in the body of a part declared quoted-printable */
static int QPEsc = 0;	/* an escape cut at the end of a piece: 1 = "=", 2 = "=H" */
static int QPEscHi;	/* the H of "=H" */
static int PieceMore = 0;	/* the last call was a piece: the line goes on */

/*
 * A binary in a text article (the 'textbinary' option of the IFILTER label
 * in dnewsfeeds).  With TextBinMax set, the text before the article
 * becomes a binary is counted: the lines of a body that is not MIME, or of
 * a text part, but blank lines, part headers, the begin and end lines of a
 * uuencoded or BinHex file and runs of encoded lines.  An article with such
 * text is read on to its end, to measure its encoded data: the lines of
 * each run of 9 or more uuencode, BinHex or base64 lines, from the first
 * line of the run, and the last short lines after it.  Text after the
 * binary does not count.  The type the caller gets stays the one the
 * article had when it became a binary, as without the option.  An article
 * with no text before its binary, and a binary of another kind (yEnc, an
 * application/octet-stream part, base64 or quoted-printable text that
 * decodes to binary data, X-Bommanews, X-UnidataEncoding), is not measured
 * (TBOther), and neither is one whose encoded data is over the limit: such
 * an article is not read on.
 */
static int TextBinMax = 0;	/* the limit in bytes, 0 = off */
static int TBFrozen = 0;	/* the type the article had when it became a binary */
static int TBOther = 0;		/* a binary that is not runs of encoded lines */
static long TBEnc = 0;		/* bytes of encoded data */
static long TBText = 0;		/* bytes of text before the article became a binary */
static long TBPending = 0;	/* bytes of the run being read, not yet 9 lines */
static int TBRunKind = 0;	/* UUE, BHX or B64: a run of 9 or more lines is being read */
static int TBTrail = 0;		/* the kind of the run that just ended, for its last lines */
static int TBTrailLeft = 0;	/* lines after a run that may still be its last short lines */

#define	TBACTIVE()	(TextBinMax > 0 && !TBOther && TBEnc <= TextBinMax)

/*
 * Where a part's headers are.  A header block is the article's headers, or
 * the lines after a delimiter of a multipart we are in up to the next blank
 * line, the boundary being the one a multipart Content-Type of a header
 * block gave.  Only a header block says a part's type (CTText, CTSeen) and
 * encoding (CTEBase64, CTEQP): a body that quotes "Content-Type:" or
 * "Content-Transfer-Encoding: base64" in its text is not a MIME part.  A
 * delimiter forgets all of it (partReset).
 */
#define	BND_MAX		70	/* the longest boundary (RFC 2046) */
#define	BND_NUM		4	/* multiparts nested deeper are not followed */

static int HdrBlock = 1;	/* in a header block */

/*
 * The value of a Content-Type header of a header block, its folded lines
 * unfolded, is gathered in CTBuf and read at the end of the header
 * (ctFinish): a multipart's boundary parameter, found by a MIME parameter
 * reader that skips quoted strings and comments (RFC 2045, RFC 822).
 */
#define	CTBUF_MAX	2048	/* a longer Content-Type gives no boundary */

static char CTBuf[CTBUF_MAX];
static int CTLen;
static int CTTrunc;	/* CTBuf overflowed */
static int CTCont = 0;	/* gathering a Content-Type header */

/*
 * The boundaries of the multiparts we are in, innermost on top.  A
 * closing "--boundary--" line pops its multipart (and any left open
 * inside it).  A multipart nested deeper than BND_NUM is not pushed: the
 * headers of its parts are not a header block and nothing in them is
 * decoded, as on main.
 */
static char Bnd[BND_NUM][BND_MAX];
static int BndLen[BND_NUM];
static int BndTop = 0;	/* entries in use */
static int B64Blank = 0;	/* previous whole body line was blank after complete base64 data */
static int B64Part = 0;	/* in the body of a base64 text part */
static int B64Bits;	/* bits of the current group not yet decoded */
static int B64NBits;	/* how many */
static int B64Quad;	/* base64 characters in the current group, 0-3 */
static int B64Pad;	/* 0, 1 = one '=' after two characters, 2 = group closed */
static char DLine[DLINE_MAX];
static int DLineLen;
static int DecUUencode, DecBinHex, DecBase64, DecArmor;
static int DLineCR;	/* the decoded line ended in CR: skip an LF after it */

#define	B64_SPACE	(-1)	/* white space: skipped */
#define	B64_EQ		(-2)	/* '=': padding */
#define	B64_OTHER	(-3)	/* not base64: skipped, the line is also checked as it came */
static signed char B64Val[256];	/* the value of a base64 character, or one of the above */
static int B64ValSet = 0;

/*
 * b64Decode() is not inlined: in ArticleType() its loop runs short of
 * registers and decodes about 40% slower (gcc 14, -O2).
 */
#if defined(__GNUC__)
#define	ARTTYPE_NOINLINE	__attribute__((noinline))
#else
#define	ARTTYPE_NOINLINE
#endif

/*
 * Extensible content type classification system
 *
 * (Derived from an earlier articletype.c, Copyright (C) 1998 Joe Greco
 * and sol.net Network Services.  This is a ground-up optimized rewrite.)
 *
 * This is a line-scanner that is designed to sniff out various types of
 * binary content that may or may not be well-delimited
 *
 * It scans a line, character at a time, updating a list of content types 
 * that the line might be, and that the line definitely is not.  The end
 * results are interpreted as the type(s) of content that the line may be.
 *
 * As this is basically a fancy character scan done with logical operations
 * and a table lookup, additional content types can be taught to this
 * function with a minimum of hassle, and up to five more types can be 
 * taught with ZERO additional overhead.
 *
 * The function is optimized to abort as soon as it has positively
 * identified the line isn't a known type, and since it only makes the
 * one pass through the line, it should be about as fast as one can get.
 * Optimization suggestions welcome of course.  <jgreco@ns.sol.net>
 * JG200102030148
 */

#define	UUE	0x01
#define	B64	0x02
#define	BHX	0x04
/* #define XXX	0x08	the next type to be learned */

#define	ALLTYPES	(UUE|B64|BHX)

unsigned char charactermap[256] = {
	0  |0  |0  ,	/* ASCII 0 */
	0  |0  |0  ,	/* ASCII 1 */
	0  |0  |0  ,	/* ASCII 2 */
	0  |0  |0  ,	/* ASCII 3 */
	0  |0  |0  ,	/* ASCII 4 */
	0  |0  |0  ,	/* ASCII 5 */
	0  |0  |0  ,	/* ASCII 6 */
	0  |0  |0  ,	/* ASCII 7 */
	0  |0  |0  ,	/* ASCII 8 */
	0  |0  |0  ,	/* ASCII 9 */
	UUE|B64|BHX,	/* ASCII 10 */
	0  |0  |0  ,	/* ASCII 11 */
	0  |0  |0  ,	/* ASCII 12 */
	0  |0  |0  ,	/* ASCII 13 */
	0  |0  |0  ,	/* ASCII 14 */
	0  |0  |0  ,	/* ASCII 15 */
	0  |0  |0  ,	/* ASCII 16 */
	0  |0  |0  ,	/* ASCII 17 */
	0  |0  |0  ,	/* ASCII 18 */
	0  |0  |0  ,	/* ASCII 19 */
	0  |0  |0  ,	/* ASCII 20 */
	0  |0  |0  ,	/* ASCII 21 */
	0  |0  |0  ,	/* ASCII 22 */
	0  |0  |0  ,	/* ASCII 23 */
	0  |0  |0  ,	/* ASCII 24 */
	0  |0  |0  ,	/* ASCII 25 */
	0  |0  |0  ,	/* ASCII 26 */
	0  |0  |0  ,	/* ASCII 27 */
	0  |0  |0  ,	/* ASCII 28 */
	0  |0  |0  ,	/* ASCII 29 */
	0  |0  |0  ,	/* ASCII 30 */
	0  |0  |0  ,	/* ASCII 31 */
	UUE|0  |0  ,	/* ASCII 32 */
	UUE|0  |BHX,	/* ASCII 33 */
	UUE|0  |BHX,	/* ASCII 34 */
	UUE|0  |BHX,	/* ASCII 35 */
	UUE|0  |BHX,	/* ASCII 36 */
	UUE|0  |BHX,	/* ASCII 37 */
	UUE|0  |BHX,	/* ASCII 38 */
	UUE|0  |BHX,	/* ASCII 39 */
	UUE|0  |BHX,	/* ASCII 40 */
	UUE|0  |BHX,	/* ASCII 41 */
	UUE|0  |BHX,	/* ASCII 42 */
	UUE|B64|BHX,	/* ASCII 43 */
	UUE|0  |BHX,	/* ASCII 44 */
	UUE|0  |BHX,	/* ASCII 45 */
	UUE|0  |0  ,	/* ASCII 46 */
	UUE|B64|0  ,	/* ASCII 47 */
	UUE|B64|BHX,	/* ASCII 48 */
	UUE|B64|BHX,	/* ASCII 49 */
	UUE|B64|BHX,	/* ASCII 50 */
	UUE|B64|BHX,	/* ASCII 51 */
	UUE|B64|BHX,	/* ASCII 52 */
	UUE|B64|BHX,	/* ASCII 53 */
	UUE|B64|BHX,	/* ASCII 54 */
	UUE|B64|0  ,	/* ASCII 55 */
	UUE|B64|BHX,	/* ASCII 56 */
	UUE|B64|BHX,	/* ASCII 57 */
	UUE|0  |0  ,	/* ASCII 58 */
	UUE|0  |0  ,	/* ASCII 59 */
	UUE|0  |0  ,	/* ASCII 60 */
	UUE|B64|0  ,	/* ASCII 61 */
	UUE|0  |0  ,	/* ASCII 62 */
	UUE|0  |0  ,	/* ASCII 63 */
	UUE|0  |BHX,	/* ASCII 64 */
	UUE|B64|BHX,	/* ASCII 65 */
	UUE|B64|BHX,	/* ASCII 66 */
	UUE|B64|BHX,	/* ASCII 67 */
	UUE|B64|BHX,	/* ASCII 68 */
	UUE|B64|BHX,	/* ASCII 69 */
	UUE|B64|BHX,	/* ASCII 70 */
	UUE|B64|BHX,	/* ASCII 71 */
	UUE|B64|BHX,	/* ASCII 72 */
	UUE|B64|BHX,	/* ASCII 73 */
	UUE|B64|BHX,	/* ASCII 74 */
	UUE|B64|BHX,	/* ASCII 75 */
	UUE|B64|BHX,	/* ASCII 76 */
	UUE|B64|BHX,	/* ASCII 77 */
	UUE|B64|BHX,	/* ASCII 78 */
	UUE|B64|0  ,	/* ASCII 79 */
	UUE|B64|BHX,	/* ASCII 80 */
	UUE|B64|BHX,	/* ASCII 81 */
	UUE|B64|BHX,	/* ASCII 82 */
	UUE|B64|BHX,	/* ASCII 83 */
	UUE|B64|BHX,	/* ASCII 84 */
	UUE|B64|BHX,	/* ASCII 85 */
	UUE|B64|BHX,	/* ASCII 86 */
	UUE|B64|0  ,	/* ASCII 87 */
	UUE|B64|BHX,	/* ASCII 88 */
	UUE|B64|BHX,	/* ASCII 89 */
	UUE|B64|BHX,	/* ASCII 90 */
	UUE|0  |BHX,	/* ASCII 91 */
	UUE|0  |0  ,	/* ASCII 92 */
	UUE|0  |0  ,	/* ASCII 93 */
	UUE|0  |0  ,	/* ASCII 94 */
	UUE|0  |0  ,	/* ASCII 95 */
	UUE|0  |BHX,	/* ASCII 96 */
	0  |B64|BHX,	/* ASCII 97 */
	0  |B64|BHX,	/* ASCII 98 */
	0  |B64|BHX,	/* ASCII 99 */
	0  |B64|BHX,	/* ASCII 100 */
	0  |B64|BHX,	/* ASCII 101 */
	0  |B64|BHX,	/* ASCII 102 */
	0  |B64|0  ,	/* ASCII 103 */
	0  |B64|BHX,	/* ASCII 104 */
	0  |B64|BHX,	/* ASCII 105 */
	0  |B64|BHX,	/* ASCII 106 */
	0  |B64|BHX,	/* ASCII 107 */
	0  |B64|BHX,	/* ASCII 108 */
	0  |B64|BHX,	/* ASCII 109 */
	0  |B64|0  ,	/* ASCII 110 */
	0  |B64|0  ,	/* ASCII 111 */
	0  |B64|BHX,	/* ASCII 112 */
	0  |B64|BHX,	/* ASCII 113 */
	0  |B64|BHX,	/* ASCII 114 */
	0  |B64|0  ,	/* ASCII 115 */
	0  |B64|0  ,	/* ASCII 116 */
	0  |B64|0  ,	/* ASCII 117 */
	0  |B64|0  ,	/* ASCII 118 */
	0  |B64|0  ,	/* ASCII 119 */
	0  |B64|0  ,	/* ASCII 120 */
	0  |B64|0  ,	/* ASCII 121 */
	0  |B64|0  ,	/* ASCII 122 */
	0  |0  |0  ,	/* ASCII 123 */
	0  |0  |0  ,	/* ASCII 124 */
	0  |0  |0  ,	/* ASCII 125 */
	0  |0  |0  ,	/* ASCII 126 */
	0  |0  |0  ,	/* ASCII 127 */
	0  |0  |0  ,	/* VALUE 128 */
	0  |0  |0  ,	/* VALUE 129 */
	0  |0  |0  ,	/* VALUE 130 */
	0  |0  |0  ,	/* VALUE 131 */
	0  |0  |0  ,	/* VALUE 132 */
	0  |0  |0  ,	/* VALUE 133 */
	0  |0  |0  ,	/* VALUE 134 */
	0  |0  |0  ,	/* VALUE 135 */
	0  |0  |0  ,	/* VALUE 136 */
	0  |0  |0  ,	/* VALUE 137 */
	0  |0  |0  ,	/* VALUE 138 */
	0  |0  |0  ,	/* VALUE 139 */
	0  |0  |0  ,	/* VALUE 140 */
	0  |0  |0  ,	/* VALUE 141 */
	0  |0  |0  ,	/* VALUE 142 */
	0  |0  |0  ,	/* VALUE 143 */
	0  |0  |0  ,	/* VALUE 144 */
	0  |0  |0  ,	/* VALUE 145 */
	0  |0  |0  ,	/* VALUE 146 */
	0  |0  |0  ,	/* VALUE 147 */
	0  |0  |0  ,	/* VALUE 148 */
	0  |0  |0  ,	/* VALUE 149 */
	0  |0  |0  ,	/* VALUE 150 */
	0  |0  |0  ,	/* VALUE 151 */
	0  |0  |0  ,	/* VALUE 152 */
	0  |0  |0  ,	/* VALUE 153 */
	0  |0  |0  ,	/* VALUE 154 */
	0  |0  |0  ,	/* VALUE 155 */
	0  |0  |0  ,	/* VALUE 156 */
	0  |0  |0  ,	/* VALUE 157 */
	0  |0  |0  ,	/* VALUE 158 */
	0  |0  |0  ,	/* VALUE 159 */
	0  |0  |0  ,	/* VALUE 160 */
	0  |0  |0  ,	/* VALUE 161 */
	0  |0  |0  ,	/* VALUE 162 */
	0  |0  |0  ,	/* VALUE 163 */
	0  |0  |0  ,	/* VALUE 164 */
	0  |0  |0  ,	/* VALUE 165 */
	0  |0  |0  ,	/* VALUE 166 */
	0  |0  |0  ,	/* VALUE 167 */
	0  |0  |0  ,	/* VALUE 168 */
	0  |0  |0  ,	/* VALUE 169 */
	0  |0  |0  ,	/* VALUE 170 */
	0  |0  |0  ,	/* VALUE 171 */
	0  |0  |0  ,	/* VALUE 172 */
	0  |0  |0  ,	/* VALUE 173 */
	0  |0  |0  ,	/* VALUE 174 */
	0  |0  |0  ,	/* VALUE 175 */
	0  |0  |0  ,	/* VALUE 176 */
	0  |0  |0  ,	/* VALUE 177 */
	0  |0  |0  ,	/* VALUE 178 */
	0  |0  |0  ,	/* VALUE 179 */
	0  |0  |0  ,	/* VALUE 180 */
	0  |0  |0  ,	/* VALUE 181 */
	0  |0  |0  ,	/* VALUE 182 */
	0  |0  |0  ,	/* VALUE 183 */
	0  |0  |0  ,	/* VALUE 184 */
	0  |0  |0  ,	/* VALUE 185 */
	0  |0  |0  ,	/* VALUE 186 */
	0  |0  |0  ,	/* VALUE 187 */
	0  |0  |0  ,	/* VALUE 188 */
	0  |0  |0  ,	/* VALUE 189 */
	0  |0  |0  ,	/* VALUE 190 */
	0  |0  |0  ,	/* VALUE 191 */
	0  |0  |0  ,	/* VALUE 192 */
	0  |0  |0  ,	/* VALUE 193 */
	0  |0  |0  ,	/* VALUE 194 */
	0  |0  |0  ,	/* VALUE 195 */
	0  |0  |0  ,	/* VALUE 196 */
	0  |0  |0  ,	/* VALUE 197 */
	0  |0  |0  ,	/* VALUE 198 */
	0  |0  |0  ,	/* VALUE 199 */
	0  |0  |0  ,	/* VALUE 200 */
	0  |0  |0  ,	/* VALUE 201 */
	0  |0  |0  ,	/* VALUE 202 */
	0  |0  |0  ,	/* VALUE 203 */
	0  |0  |0  ,	/* VALUE 204 */
	0  |0  |0  ,	/* VALUE 205 */
	0  |0  |0  ,	/* VALUE 206 */
	0  |0  |0  ,	/* VALUE 207 */
	0  |0  |0  ,	/* VALUE 208 */
	0  |0  |0  ,	/* VALUE 209 */
	0  |0  |0  ,	/* VALUE 210 */
	0  |0  |0  ,	/* VALUE 211 */
	0  |0  |0  ,	/* VALUE 212 */
	0  |0  |0  ,	/* VALUE 213 */
	0  |0  |0  ,	/* VALUE 214 */
	0  |0  |0  ,	/* VALUE 215 */
	0  |0  |0  ,	/* VALUE 216 */
	0  |0  |0  ,	/* VALUE 217 */
	0  |0  |0  ,	/* VALUE 218 */
	0  |0  |0  ,	/* VALUE 219 */
	0  |0  |0  ,	/* VALUE 220 */
	0  |0  |0  ,	/* VALUE 221 */
	0  |0  |0  ,	/* VALUE 222 */
	0  |0  |0  ,	/* VALUE 223 */
	0  |0  |0  ,	/* VALUE 224 */
	0  |0  |0  ,	/* VALUE 225 */
	0  |0  |0  ,	/* VALUE 226 */
	0  |0  |0  ,	/* VALUE 227 */
	0  |0  |0  ,	/* VALUE 228 */
	0  |0  |0  ,	/* VALUE 229 */
	0  |0  |0  ,	/* VALUE 230 */
	0  |0  |0  ,	/* VALUE 231 */
	0  |0  |0  ,	/* VALUE 232 */
	0  |0  |0  ,	/* VALUE 233 */
	0  |0  |0  ,	/* VALUE 234 */
	0  |0  |0  ,	/* VALUE 235 */
	0  |0  |0  ,	/* VALUE 236 */
	0  |0  |0  ,	/* VALUE 237 */
	0  |0  |0  ,	/* VALUE 238 */
	0  |0  |0  ,	/* VALUE 239 */
	0  |0  |0  ,	/* VALUE 240 */
	0  |0  |0  ,	/* VALUE 241 */
	0  |0  |0  ,	/* VALUE 242 */
	0  |0  |0  ,	/* VALUE 243 */
	0  |0  |0  ,	/* VALUE 244 */
	0  |0  |0  ,	/* VALUE 245 */
	0  |0  |0  ,	/* VALUE 246 */
	0  |0  |0  ,	/* VALUE 247 */
	0  |0  |0  ,	/* VALUE 248 */
	0  |0  |0  ,	/* VALUE 249 */
	0  |0  |0  ,	/* VALUE 250 */
	0  |0  |0  ,	/* VALUE 251 */
	0  |0  |0  ,	/* VALUE 252 */
	0  |0  |0  ,	/* VALUE 253 */
	0  |0  |0  ,	/* VALUE 254 */
	0  |0  |0  ,	/* VALUE 255 */
};

void cLATDebug(char *s)
{
	printf("%s", s);
}

int classifyLineAsTypes(char *buf, int len)
{
	register unsigned int istype = 0x00, isnttype = 0xff;
	register char *ptr = buf;
	int nonhex = 0;

	if (len <= 0)
	    return(0);

	if (ptr[len - 1] == '\n')
	    len--;
	if (len > 0 && ptr[len - 1] == '\r')
	    len--;

	/* UUENCODE-specific checks:  UUENCODE data is well-behaved */
	if (*ptr != 'M' || len != 61) {
		isnttype &= ~UUE;
	}

	/* BASE64-specific checks: BASE64 data isn't well-behaved */
	if (len < 60 || len > 77) {
		/*
		 * I can't actually find anything that specifies what the
		 * line length of base64 data is supposed to be.  RFC1113
		 * says 64, but isn't actually base64.  RFC1341 says "no
		 * more than 76 characters".  Typical.  Examination of
		 * actual base64 content shows lines to always be 74, for
		 * a relatively large sample size.  But better safe than
		 * sorry...  allow 60-76.
		 */

		isnttype &= ~B64;
	}

	/* BinHex-specific checks: BinHex just gives me shivers */
	if (len < 64 || len > 65) {
		isnttype &= ~BHX;
	}

	/*
	 * As much as I'd like to check BinHex CRC's, I don't think I
	 * want to write the code... 
	 */


	while (len > 0) {
		/*
		 * Short circuit: if we've already de-elected all possible
		 * types, then stop iterating
		 */

		if (! (isnttype & ALLTYPES)) {
			return(0);
		}

		/*
		 * If the character might legitimately be part of one
		 * of the component bit types, set that(those) bits
		 */
		if (!((*ptr >= '0' && *ptr <= '9') || (*ptr >= 'a' && *ptr <= 'f') ||
					(*ptr >= 'A' && *ptr <= 'F')))
		    nonhex = 1;
		istype |= charactermap[(unsigned char)*ptr];

		/*
		 * If the character is not part of one of the component
		 * bit types, clear that(those) bits
		 */
		isnttype &= charactermap[(unsigned char)*ptr];
		ptr++;
		len--;
	}

	/*
	 * A line of nothing but hex digits is a checksum or a hex dump, not
	 * encoded data: a SHA-256 digest is 64 hex characters, so nine of them
	 * in a list were taken for base64 (or BinHex) and the article for a
	 * binary.  Encoded binary data is never made of hex digits alone.
	 */
	if (!nonhex)
	    return(0);

	/*
	 * Now, what we have is istype, which contains a possible list of
	 * types the line might be, and isnttype, which contains zeroes in
	 * positions where the line certainly isn't that type.  To get the
	 * actual possible types that the line might be, AND them
	 */

	return(istype & isnttype);
}

void
InitArticleType(void)
{
    Type = ARTTYPE_DEFAULT;
    UUencode = 0;
    BinHex = 0;
    Base64 = 0;
    Armor = 0;
    PGPMime = 0;
    CTText = 0;
    CTArmorPGP = 0;
    CTMessage = 0;
    CTEIdentity = 1;
    CTEBase64 = 0;
    CTSeen = 0;
    B64Part = 0;
    B64Blank = 0;
    CTEQP = 0;
    QPPart = 0;
    HdrBlock = 1;
    CTCont = 0;
    BndTop = 0;
    QPEsc = 0;
    PieceMore = 0;
    TBFrozen = 0;
    TBOther = 0;
    TBEnc = 0;
    TBText = 0;
    TBPending = 0;
    TBRunKind = 0;
    TBTrail = 0;
    TBTrailLeft = 0;
}

/*
 * The 'textbinary' limit of the IFILTER label for the next articles, in
 * bytes; 0 turns the measuring off.  Set it before InitArticleType().
 */
void
ArticleTypeTextBinary(int max)
{
    TextBinMax = (max > 0) ? max : 0;
}

/*
 * After ArticleTypeEnd(): when the article is a binary in a text article
 * within the 'textbinary' limit (it has text, and encoded data of at most
 * that many bytes, and no binary of another kind), the article's type
 * without its binary bits (ARTTYPE_DEFAULT if no other bit is left), for
 * the IFILTER label to be checked again with.  Otherwise -1.
 */
int
ArticleTypeTextBinaryType(void)
{
    int t;

    if (TextBinMax <= 0 || !(TBFrozen & ARTTYPE_BINARY) || TBOther ||
	    TBEnc <= 0 || TBEnc > TextBinMax || TBText <= 0)
	return(-1);
    t = TBFrozen & ~(ARTTYPE_BINARY | ARTTYPE_UUENCODE | ARTTYPE_BINHEX |
							ARTTYPE_BASE64);
    if ((t & ~ARTTYPE_DEFAULT) == 0)
	t = ARTTYPE_DEFAULT;
    return(t);
}

/*
 * A run of encoded lines ended: one of fewer than 9 lines was text, after
 * one of 9 or more come its last short lines (tbLine).
 */
static void
tbRunEnd(void)
{
    if (TBPending > 0 && !TBFrozen && !HdrBlock && (!CTSeen || CTText))
	TBText += TBPending;
    TBPending = 0;
    if (TBRunKind) {
	TBTrail = TBRunKind;
	TBTrailLeft = 2;
    }
    TBRunKind = 0;
}

/*
 * Whether a line is made only of the characters of an encoding (kind is
 * UUE, BHX or B64), as the last short line of its data is.  BinHex data
 * ends with ':'.
 */
static int
tbAlphabetOnly(const char *ptr, int len, int kind)
{
    int i;

    if (len <= 0)
	return(0);
    for (i = 0; i < len; i++) {
	unsigned char c = ptr[i];

	if (c == '\r' && i == len - 1)
	    continue;
	if (kind == BHX && c == ':')
	    continue;
	if (kind == B64 && c == '=')
	    continue;
	if (!(charactermap[c] & kind) || c == '\n')
	    return(0);
    }
    return(1);
}

/*
 * The lines that frame an encoded file and are neither data nor text:
 * "begin <mode> <name>" and "end" of a uuencoded file, the BinHex note and
 * a BinHex line that starts or ends with ':'.
 */
static int
tbFraming(const char *ptr, int len)
{
    while (len > 0 && (ptr[len - 1] == ' ' || ptr[len - 1] == '\t' ||
							ptr[len - 1] == '\r'))
	--len;
    if (len >= 7 && strncmp(ptr, "begin ", 6) == 0 && ptr[6] >= '0' &&
							ptr[6] <= '7')
	return(1);
    if (len == 3 && strncmp(ptr, "end", 3) == 0)
	return(1);
    if (len >= 33 && strncasecmp(ptr, "(This file must be converted with BinHex", 33) == 0)
	return(1);
    if (len > 1 && (ptr[0] == ':' || ptr[len - 1] == ':') &&
					tbAlphabetOnly(ptr, len, BHX))
	return(1);
    return(0);
}

/*
 * Measure a body line as it came (not a decoded one): kind is the encoding
 * whose run it goes on (UUE, BHX or B64) and count the run's line count
 * with it, or both 0 for a line that is not encoded data.
 */
static void
tbLine(const char *ptr, int len, int kind, int count)
{
    if (!TBACTIVE())
	return;
    if (count == 0) {
	tbRunEnd();
	if (TBTrailLeft > 0 && tbAlphabetOnly(ptr, len, TBTrail)) {
	    --TBTrailLeft;
	    TBEnc += len;
	    return;
	}
	TBTrailLeft = 0;
	if (tbFraming(ptr, len))
	    return;
	if (!TBFrozen && !HdrBlock && (!CTSeen || CTText))
	    TBText += len;
	return;
    }
    if (count == 1) {
	tbRunEnd();
	TBTrailLeft = 0;
    }
    if (count < 9) {
	TBPending += len;
    } else if (count == 9) {
	TBEnc += TBPending + len;
	TBPending = 0;
	TBRunKind = kind;
    } else {
	TBEnc += len;
    }
}

int
ArtTypeConv(char *t)
{
    if (strcasecmp(t, "none") == 0)
	return(ARTTYPE_NONE);
    else if (strcasecmp(t, "default") == 0)
	return(ARTTYPE_DEFAULT);
    else if (strcasecmp(t, "control") == 0)
	return(ARTTYPE_CONTROL);
    else if (strcasecmp(t, "cancel") == 0)
	return(ARTTYPE_CANCEL);
    else if (strcasecmp(t, "mime") == 0)
	return(ARTTYPE_MIME);
    else if (strcasecmp(t, "binary") == 0 || strcasecmp(t, "binaries") == 0)
	return(ARTTYPE_BINARY);
    else if (strcasecmp(t, "uuencode") == 0)
	return(ARTTYPE_UUENCODE);
    else if (strcasecmp(t, "base64") == 0)
	return(ARTTYPE_BASE64);
    else if (strcasecmp(t, "yenc") == 0)
	return(ARTTYPE_YENC);
    else if (strcasecmp(t, "bommanews") == 0)
	return(ARTTYPE_BOMMANEWS);
    else if (strcasecmp(t, "unidata") == 0)
	return(ARTTYPE_UNIDATA);
    else if (strcasecmp(t, "multipart") == 0)
	return(ARTTYPE_MULTIPART);
    else if (strcasecmp(t, "html") == 0)
	return(ARTTYPE_HTML);
    else if (strcasecmp(t, "ps") == 0)
	return(ARTTYPE_PS);
    else if (strcasecmp(t, "binhex") == 0)
	return(ARTTYPE_BINHEX);
    else if (strcasecmp(t, "partial") == 0)
	return(ARTTYPE_PARTIAL);
    else if (strcasecmp(t, "pgp") == 0)
	return(ARTTYPE_PGPMESSAGE);
    else if (strcasecmp(t, "all") == 0)
	return(ARTTYPE_ALL);
    else {
	logit(LOG_ERR, "Unknown article type: %s", t);
	return(ARTTYPE_NONE);
    }
}

/*
 * Match a article type to a list of types that are acceptable
 *
 * Returns:  1 = Match
 *	     0 = No match
 *
 */
int
ArtTypeMatch(int articletype, ArtTypeList *acctypes)
{
    int res = 0;
    if (acctypes == NULL)
	return(1);
    if (acctypes->negate)
	res = 1;
    while (acctypes != NULL) {
	/*
	 * "none" (and an unknown name, which ArtTypeConv() logs) is 0:
	 * the test below is true for every article with it.
	 */
	if (acctypes->arttype == ARTTYPE_NONE) {
	    acctypes = acctypes->next;
	    continue;
	}
	if ((articletype & acctypes->arttype) == acctypes->arttype ||
					acctypes->arttype == ARTTYPE_ALL)
		res = !acctypes->negate;
	acctypes = acctypes->next;
    }
    return(res);
}

/*
 * Does the line (len bytes, not necessarily NUL terminated) contain
 * needle, ignoring case?
 */
static int
lineHas(const char *line, int len, const char *needle)
{
    int n = strlen(needle);
    int i;

    for (i = 0; i + n <= len; ++i) {
	if (strncasecmp(line + i, needle, n) == 0)
	    return(1);
    }
    return(0);
}

/*
 * len-bounded prefix tests.  A match requires the whole literal to fit
 * within the len bytes we were handed, so a line shorter than the literal
 * can never match by reading into whatever follows it in the buffer: an
 * article line is length-delimited, not NUL-terminated (see util/diablo.c).
 */
static int
hdrPrefix(const char *s, int len, const char *lit)
{
    int n = (int)strlen(lit);
    return (len >= n && strncasecmp(s, lit, n) == 0);
}

static int
linePrefix(const char *s, int len, const char *lit)
{
    int n = (int)strlen(lit);
    return (len >= n && strncmp(s, lit, n) == 0);
}

/*
 * Push the boundary of a multipart.  Past BND_NUM it is not pushed.
 */
static void
bndPush(const char *b, int n)
{
    if (BndTop >= BND_NUM || n < 1 || n > BND_MAX)
	return;
    memcpy(Bnd[BndTop], b, n);
    BndLen[BndTop] = n;
    ++BndTop;
}

/*
 * Gather the value of a Content-Type header (from its first line, more=0)
 * or one of its continuation lines (more=1, kept with the white space it
 * starts with, which is what unfolding leaves).
 */
static void
ctGather(const char *p, int len, int more)
{
    while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r'))
	--len;
    if (!more) {
	CTLen = 0;
	CTTrunc = 0;
	CTCont = 1;
    }
    if (len > CTBUF_MAX - CTLen) {
	CTTrunc = 1;
	len = CTBUF_MAX - CTLen;
    }
    memcpy(CTBuf + CTLen, p, len);
    CTLen += len;
}

/*
 * Skip white space and comments ("(...)", nested, with quoted pairs).
 */
static const char *
mimeSkip(const char *p, const char *e)
{
    int depth;

    for (;;) {
	while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
	    ++p;
	if (p >= e || *p != '(')
	    return(p);
	for (depth = 0; p < e; ++p) {
	    if (*p == '\\' && p + 1 < e)
		++p;
	    else if (*p == '(')
		++depth;
	    else if (*p == ')' && --depth == 0) {
		++p;
		break;
	    }
	}
    }
}

/*
 * Go to the next ';' that is not in a quoted string or a comment.
 */
static const char *
mimeNextParam(const char *p, const char *e)
{
    while (p < e && *p != ';') {
	if (*p == '"') {
	    for (++p; p < e && *p != '"'; ++p) {
		if (*p == '\\' && p + 1 < e)
		    ++p;
	    }
	    if (p < e)
		++p;
	} else if (*p == '(') {
	    p = mimeSkip(p, e);
	} else {
	    ++p;
	}
    }
    return(p);
}

/*
 * The end of a gathered Content-Type header.  Its type gives CTText.  For
 * a multipart, the first boundary parameter is pushed: the attribute is
 * matched without case, white space and comments may stand around '=',
 * and the value is a token or a quoted string with quoted pairs.  A value
 * longer than BND_MAX after unquoting is not a valid boundary (RFC 2046).
 * Nothing is pushed from a Content-Type longer than CTBUF_MAX.
 */
static void
ctFinish(void)
{
    const char *p = CTBuf, *e = CTBuf + CTLen, *a;
    char v[BND_MAX];
    int n, alen, ok;

    CTCont = 0;
    p = mimeSkip(p, e);
    CTText = (e - p >= 5 && strncasecmp(p, "text/", 5) == 0);
    /* RFC 3156 sections 2, 5 and 7: these MIME bodies contain ASCII armor.
     * Decode their transfer encoding before applying the armor checks.
     * Match the complete media type, not a similarly named subtype.
     */
    a = p;
    while (a < e && *a != ';' && *a != ' ' && *a != '\t' && *a != '\r' && *a != '(')
	++a;
    CTMessage = (a - p == 14 && strncasecmp(p, "message/rfc822", 14) == 0);
    CTArmorPGP = ((a - p == 25 && strncasecmp(p, "application/pgp-signature", 25) == 0) ||
	(a - p == 20 && strncasecmp(p, "application/pgp-keys", 20) == 0));

    if (CTTrunc || e - p < 10 || strncasecmp(p, "multipart/", 10) != 0)
	return;
    for (;;) {
	p = mimeNextParam(p, e);
	if (p >= e)
	    return;
	p = mimeSkip(p + 1, e);
	for (a = p; p < e && *p != '=' && *p != ';' && *p != '(' &&
		*p != ' ' && *p != '\t' && *p != '"'; ++p)
	    ;
	alen = (int)(p - a);
	p = mimeSkip(p, e);
	if (p >= e || *p != '=')
	    continue;
	p = mimeSkip(p + 1, e);
	n = 0;
	ok = 1;
	if (p < e && *p == '"') {
	    for (++p; p < e && *p != '"'; ++p) {
		if (*p == '\\' && p + 1 < e)
		    ++p;		/* quoted pair */
		if (n < BND_MAX)
		    v[n] = *p;
		++n;
	    }
	    if (p >= e)
		ok = 0;			/* no closing quote */
	    else
		++p;
	} else {
	    for (; p < e && *p != ';' && *p != '(' && *p != ' ' &&
						*p != '\t'; ++p) {
		if (n < BND_MAX)
		    v[n] = *p;
		++n;
	    }
	}
	if (alen == 8 && strncasecmp(a, "boundary", 8) == 0) {
	    if (ok && n >= 1 && n <= BND_MAX)
		bndPush(v, n);
	    return;
	}
    }
}

/*
 * Is this line (after its "--") the delimiter of a multipart we are in,
 * innermost first?  Returns 1 for one that starts a part, 2 for the
 * closing one ("--boundary--"), which pops that multipart and any left
 * open inside it, 0 for none.  Only white space may follow either.  A
 * part of an outer multipart also ends the ones inside it.
 */
static int
bndMatch(const char *p, int len)
{
    int i, n, k;

    for (i = BndTop - 1; i >= 0; i--) {
	n = BndLen[i];
	if (len < n || memcmp(p, Bnd[i], n) != 0)
	    continue;
	k = n;
	if (len >= n + 2 && p[n] == '-' && p[n + 1] == '-')
	    k = n + 2;
	/* only white space may follow (RFC 2046) */
	while (k < len && (p[k] == ' ' || p[k] == '\t' || p[k] == '\r'))
	    ++k;
	if (k < len)
	    continue;
	if (len >= n + 2 && p[n] == '-' && p[n + 1] == '-') {
	    BndTop = i;
	    return(2);
	}
	BndTop = i + 1;
	return(1);
    }
    return(0);
}

/*
 * Is this line quoted-printable rather than base64?  Every '=' must start
 * an =HH escape (two hex digits: RFC 2045 says upper case, decoders take
 * lower case too) or be the soft line break at the end; base64 has '=' only as padding at the end of the data,
 * and "==" or "=x" is not quoted-printable.  In a part declared
 * quoted-printable one such '=' is enough.  Without the declaration the
 * escapes must make up at least half of the line: the lines that look like
 * base64 are those without spaces, which in quoted-printable means text
 * in a non-Latin script, nearly all escapes (a line of Chinese in UTF-8 is
 * 25 escapes and a soft line break).  A stray '=' in base64 data, or a
 * few escapes in it, do not make it quoted-printable, and neither does an
 * escape of a byte that is not text (NOTTEXT).
 */
#define	QPHEX(c)	(((c) >= '0' && (c) <= '9') || ((c) >= 'A' && (c) <= 'F') || \
			 ((c) >= 'a' && (c) <= 'f'))
#define	QPVAL(c)	((c) <= '9' ? (c) - '0' : ((c) & ~0x20) - 'A' + 10)

/*
 * Is a byte not text: NUL, DEL or a control character other than TAB, LF,
 * FF, CR and ESC (ESC for ISO-2022)?  The rule for decoded base64 and
 * quoted-printable text.
 */
#define	NOTTEXT(b)	(((b) < 0x20 && (b) != '\t' && (b) != '\n' && \
			  (b) != '\f' && (b) != '\r' && (b) != 0x1b) || (b) == 0x7f)

/*
 * May a line of this part be taken for quoted-printable text?  Only in a
 * text/ part, or one without a Content-Type (B64_NOCTYPE_IS_TEXT): in an
 * image or application part quoted-printable-shaped lines still count as
 * base64 lines.
 */
#define	QPTEXT()	(CTText || (!CTSeen && B64_NOCTYPE_IS_TEXT))

static int
qpLine(const char *p, int len, int declared)
{
    int i, esc = 0, soft = 0;

    if (len > 0 && p[len - 1] == '\n')
	len--;
    if (len > 0 && p[len - 1] == '\r')
	len--;
    for (i = 0; i < len; i++) {
	if (p[i] != '=')
	    continue;
	if (i == len - 1) {
	    soft = 1;		/* soft line break */
	    break;
	}
	if (i + 2 >= len || !QPHEX(p[i + 1]) || !QPHEX(p[i + 2]) ||
		NOTTEXT(QPVAL(p[i + 1]) * 16 + QPVAL(p[i + 2])))
	    return(0);
	esc++;
	i += 2;
    }
    if (declared)
	return(esc > 0 || soft);
    return(esc * 3 * 2 >= len);
}

/*
 * Has a line of a part declared quoted-printable text an escape of a byte
 * that is not text (=00 to =1F but =09, =0A, =0C, =0D and =1B; =7F)?
 * Then the part is a binary, as decoded base64 text is (b64Decode).  An
 * escape cut at the end of a piece of a long line (ArticleTypePiece) is
 * finished in the next piece (QPEsc).
 */
static int
qpBad(const char *p, int len)
{
    int i, c;

    for (i = 0; i < len; i++) {
	c = (unsigned char)p[i];
	if (QPEsc == 2) {
	    QPEsc = 0;
	    if (QPHEX(c) && NOTTEXT(QPEscHi * 16 + QPVAL(c)))
		return(1);
	} else if (QPEsc == 1) {
	    QPEsc = 0;
	    if (QPHEX(c)) {
		QPEscHi = QPVAL(c);
		QPEsc = 2;
	    }
	}
	if (QPEsc == 0 && c == '=')
	    QPEsc = 1;
    }
    return(0);
}

/*
 * A yEnc header line: "=ybegin part=" or "=ybegin line=" (other keyword
 * orders are not recognised).  For every line, and for the decoded lines
 * of a base64 text part.
 */
static void
yencLine(const char *ptr, int len)
{
    if (len > 0 && *ptr == '=') {
	if (hdrPrefix(ptr, len, "=ybegin part=")) {
	    Type |= ARTTYPE_BINARY|ARTTYPE_PARTIAL|ARTTYPE_YENC;
	    TBOther = 1;
	}
	if (hdrPrefix(ptr, len, "=ybegin line=")) {
	    Type |= ARTTYPE_BINARY|ARTTYPE_YENC;
	    TBOther = 1;
	}
    }
}

/*
 * Only armor-shaped lines retain the unbounded PGP data exemption.
 * A missing END must not hide a following uuencoded file or other text.
 */
static int
pgpMessageLine(const char *p, int len, int *armor)
{
    int i;

    if (len > 0 && p[len - 1] == '\r')
	--len;
    /* Transport-added trailing SP/TAB is not Radix-64 data. */
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
	--len;
    if (linePrefix(p, len, "-----END "))
	return(1);
    if (len == 0) {
	*armor = ARMOR_PGP_DATA;
	return(1);
    }
    if (*armor == ARMOR_PGP_HEADERS) {
	for (i = 0; i < len; ++i) {
	    unsigned char c = p[i];
	    if (c == ':' && i > 0)
		return(1);
	    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		(c >= '0' && c <= '9') || c == '-'))
		break;
	}
	*armor = ARMOR_PGP_DATA;
    }
    /* Radix-64 data and its optional =XXXX CRC line. */
    if (len > 76)
	return(0);
    for (i = 0; i < len; ++i) {
	unsigned char c = p[i];
	if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	    (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '='))
	    return(0);
    }
    return(1);
}

/*
 * The per-line checks of a body line: runs of uuencode, BinHex and base64
 * lines, with ASCII-armored blocks not counted.  The counters are passed in
 * so that the decoded lines of a base64 text part are counted apart from
 * the lines as they came.  Returns 1 when the article is now a binary.
 */
static int
bodyLineTypes(char *ptr, int len, int *uu, int *bh, int *b64, int *armor)
{
    int linetype;

    /* Get quoted UUencode, etc. */
    if (len > 0 && *ptr == '>') {
	ptr++;
	len--;
	if (len > 0 && *ptr == ' ') {
	    ptr++;
	    len--;
	}
    }

    /*
     * We look for binary formats first, since these eat
     * up CPU like there is no tomorrow.  We may miss
     * some flags if we detect a binary file, the
     * alternative is to remove the return(Type)'s and
     * eat CPU for the entire binary.
     */

    /*
     * The lines of an ASCII-armored block, a PGP signature, key or message
     * or a PEM certificate or key (-----BEGIN label----- to -----END
     * label-----), are base64 text of 64 characters, not a binary posting:
     * do not count them as base64 or the other encodings.  The signature of
     * an RSA-4096 key has 12 lines and a certificate more, so a signed text
     * article or a post with a certificate was taken for a binary.  The
     * Other armor skipping is bounded by ARMOR_MAX lines. Encoded
     * PGP message/key/signature armor is already encoded data; legitimate
     * blocks exceed that limit: skip them until END rather than marking
     * the tail of the same message as a separate base64 binary. A line
     * outside armor syntax ends that exemption too.
     */
    /* A clear-signed preamble must not swallow its signature BEGIN. */
    if (*armor > 0 && linePrefix(ptr, len, "-----BEGIN PGP SIGNATURE-----"))
	*armor = 0;
    if (*armor < 0 && !pgpMessageLine(ptr, len, armor))
	*armor = 0;
    if (*armor != 0) {
	if (linePrefix(ptr, len, "-----END "))
	    *armor = 0;
	else if (*armor > 0)
	    --*armor;
	linetype = 0;
    } else {
	linetype = classifyLineAsTypes(ptr, len);

	/*
	 * '=' in base64 is padding at the end of the data; quoted-printable
	 * text in a non-Latin script (=E5=9C=A8..., 76 characters with a
	 * soft line break) is made of base64 characters too (qpLine)
	 */
	if ((linetype & B64) && QPTEXT() && qpLine(ptr, len, QPPart))
	    linetype &= ~B64;

	if (len > 0 && *ptr == '-' && linePrefix(ptr, len, "-----BEGIN ")) {
	    /* PGP in any armor form: message (also a part), signed
	     * message, signature, public or private key block */
	    if (linePrefix(ptr + 11, len - 11, "PGP "))
		Type |= ARTTYPE_PGPMESSAGE;
	    *armor = ARMOR_MAX;
	    if (linePrefix(ptr + 11, len - 11, "PGP MESSAGE-----") ||
		linePrefix(ptr + 11, len - 11, "PGP MESSAGE, PART ") ||
		linePrefix(ptr + 11, len - 11, "PGP PUBLIC KEY BLOCK-----") ||
		linePrefix(ptr + 11, len - 11, "PGP PRIVATE KEY BLOCK-----") ||
		linePrefix(ptr + 11, len - 11, "PGP SIGNATURE-----"))
		*armor = ARMOR_PGP_HEADERS;
	}
    }

    /* the lines as they came are measured, not decoded ones (tbLine) */
    if (uu == &UUencode) {
	if (linetype & UUE)
	    tbLine(ptr, len, UUE, *uu + 1);
	else if (linetype & BHX)
	    tbLine(ptr, len, BHX, *bh + 1);
	else if (linetype & B64)
	    tbLine(ptr, len, B64, *b64 + 1);
	else
	    tbLine(ptr, len, 0, 0);
    }

    if (linetype & UUE) {
	*b64 = 0;
	*bh = 0;
	if (++*uu > 8) {
	    Type |= ARTTYPE_UUENCODE | ARTTYPE_BINARY;
	    Type &= ~ARTTYPE_DEFAULT;
	    return(1);
	}
    } else if (linetype & BHX) {
	*uu = 0;
	*b64 = 0;
	if (++*bh > 8) {
	    Type |= ARTTYPE_BINHEX | ARTTYPE_BINARY;
	    Type &= ~ARTTYPE_DEFAULT;
	    return(1);
	}
    } else if (linetype & B64) {
	*uu = 0;
	*bh = 0;
	if (++*b64 > 8) {
	    Type |= ARTTYPE_BASE64 | ARTTYPE_BINARY;
	    Type &= ~ARTTYPE_DEFAULT;
	    return(1);
	}
    } else {
	*uu = 0;
	*b64 = 0;
	*bh = 0;
    }
    return(0);
}

/*
 * A decoded line of a base64 text part: the yEnc header check and the
 * per-line checks of a body line (bodyLineTypes), with the counters of the
 * decoded text.  Returns 1 when the article is now a binary.
 */
static int
decodedLineTypes(char *ptr, int len)
{
    yencLine(ptr, len);
    if ((Type & ARTTYPE_BINARY) ||
	    bodyLineTypes(ptr, len, &DecUUencode, &DecBinHex, &DecBase64,
								&DecArmor)) {
	TBOther = 1;
	Type |= ARTTYPE_BASE64;
	Type &= ~ARTTYPE_DEFAULT;
	return(1);
    }
    return(0);
}

/*
 * Start or end the body of a base64 text part.  At the end, a decoded line
 * without a newline is checked too.
 */
static void
b64Start(void)
{
    if (!B64ValSet) {
	static const char alpha[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int i;

	for (i = 0; i < 256; i++)
	    B64Val[i] = B64_OTHER;
	for (i = 0; i < 64; i++)
	    B64Val[(unsigned char)alpha[i]] = i;
	B64Val[' '] = B64Val['\t'] = B64Val['\r'] = B64Val['\n'] = B64_SPACE;
	B64Val['='] = B64_EQ;
	B64ValSet = 1;
    }
    B64Part = 1;
    B64Blank = 0;
    B64Bits = B64NBits = B64Quad = B64Pad = 0;
    DLineLen = 0;
    DLineCR = 0;
    DecUUencode = DecBinHex = DecBase64 = DecArmor = 0;
}

static void
b64End(void)
{
    if (B64Part && DLineLen > 0 && !(Type & ARTTYPE_BINARY))
	(void)decodedLineTypes(DLine, DLineLen);
    B64Part = 0;
    B64Blank = 0;
}

/*
 * Decode len bytes of the body of a base64 text part (a whole line or a
 * piece of one).  The part is a binary as soon as a decoded byte is not
 * text: NUL, DEL, or a control character other than TAB, LF, FF, CR and
 * ESC (ESC for ISO-2022).  Random data has one of those in all but about
 * 0.1% of its 57-byte lines.  Each decoded line also gets the yEnc header
 * check and the per-line checks (decodedLineTypes), so base64 of a yEnc,
 * uuencoded or base64 file is still a binary, while a signed text sent as
 * base64 is not.  Text in UTF-16 or
 * UTF-32 has NULs and is taken for a binary.
 *
 * White space is skipped.  '=' after two or three characters of a group
 * closes it; any other '=' is skipped.  Data after a closed group starts a
 * new one and is decoded too, so nothing can hide behind padding.  Any
 * other character that is not base64 is skipped as RFC 2045 decoders do;
 * the return value is then 1, and the caller also gives the line as it came
 * the per-line checks.
 */
static int ARTTYPE_NOINLINE
b64Decode(const char *buf, int len)
{
    int bits = B64Bits, nbits = B64NBits, quad = B64Quad, pad = B64Pad;
    int dlen = DLineLen, cr = DLineCR;
    int i, v, b;
    int unclean = 0;

    for (i = 0; i < len; i++) {
	v = B64Val[(unsigned char)buf[i]];
	if (v < 0) {
	    if (v == B64_EQ) {
		if (pad == 0 && quad == 2)
		    pad = 1;
		else if ((pad == 0 && quad == 3) || pad == 1) {
		    pad = 2;
		    bits = nbits = quad = 0;
		}
	    } else if (v == B64_OTHER) {
		unclean = 1;
	    }
	    continue;		/* B64_SPACE and the rest are skipped */
	}
	if (pad) {
	    /* data after padding: a new group */
	    pad = 0;
	    bits = nbits = quad = 0;
	}
	bits = (bits << 6) | v;
	nbits += 6;
	quad = (quad + 1) & 3;
	if (nbits < 8)
	    continue;
	nbits -= 8;
	b = (bits >> nbits) & 0xff;
	bits &= (1 << nbits) - 1;

	if (b >= 0x20 && b != 0x7f) {
	    /* the common case: a printable byte of the decoded line */
	    cr = 0;
	    if (dlen < DLINE_MAX)
		DLine[dlen++] = b;
	    continue;
	}
	if (NOTTEXT(b)) {
	    Type |= ARTTYPE_BASE64 | ARTTYPE_BINARY;
	    Type &= ~ARTTYPE_DEFAULT;
	    TBOther = 1;
	    break;
	}
	/* a decoded line ends at LF, CR or CRLF */
	if (b == '\n' && cr) {
	    cr = 0;
	} else if (b == '\n' || b == '\r') {
	    cr = (b == '\r');
	    if (decodedLineTypes(DLine, dlen))
		break;
	    dlen = 0;
	} else {
	    /* TAB, FF, ESC */
	    cr = 0;
	    if (dlen < DLINE_MAX)
		DLine[dlen++] = b;
	}
    }
    B64Bits = bits;
    B64NBits = nbits;
    B64Quad = quad;
    B64Pad = pad;
    DLineLen = dlen;
    DLineCR = cr;
    return(unclean);
}

/*
 * A delimiter line ends a part: what its headers said (type, pending
 * encodings, a base64 text part being decoded, a quoted-printable
 * declaration) is forgotten before the next part's headers.
 */
static void
partReset(void)
{
    if (B64Part)
	b64End();
    Armor = 0;
    CTText = 0;
    CTArmorPGP = 0;
    CTMessage = 0;
    CTEIdentity = 1;
    CTSeen = 0;
    CTEBase64 = 0;
    CTEQP = 0;
    QPPart = 0;
    QPEsc = 0;
    CTCont = 0;
}

/*
 * The end of the article (after its last line, before the "." line).  A
 * base64 text part can end with a decoded line that has no newline: it
 * gets the per-line checks here, as at a MIME boundary.  Returns the type.
 */
int
ArticleTypeEnd(void)
{
    if (TBFrozen && !TBACTIVE())
	return(TBFrozen);
    if (B64Part)
	b64End();
    if (TBACTIVE())
	tbRunEnd();
    if (Type != ARTTYPE_DEFAULT)
	Type = Type & ~ARTTYPE_DEFAULT;
    return(tbResult(Type));
}

/*
 * ArticleType() takes a whole line, without its end of line.  A line
 * longer than the caller's buffer comes in pieces: each but the last
 * through ArticleTypePiece(), the last through ArticleType().  Only state
 * that runs along a line (a quoted-printable escape cut at a piece's end)
 * needs to know; the base64 decoder goes on across lines anyway.
 */
static int articleType(char *line, int len, int inheader);

/*
 * The type the caller gets: once the article is a binary, the type it had
 * then, also while it is read on to be measured (TextBinMax).
 */
static int
tbResult(int t)
{
    if (TBFrozen)
	return(TBFrozen);
    if (TextBinMax > 0 && (t & ARTTYPE_BINARY)) {
	TBFrozen = t;
	/* no text before the binary: it is not measured, nor read on */
	if (TBText <= 0)
	    TBOther = 1;
    }
    return(t);
}

int
ArticleTypePiece(char *line, int len, int inheader)
{
    int t;

    if (TBFrozen && !TBACTIVE())
	return(TBFrozen);
    t = articleType(line, len, inheader);
    PieceMore = 1;
    return(tbResult(t));
}

int
ArticleType(char *line, int len, int inheader)
{
    int t;

    if (TBFrozen && !TBACTIVE())
	return(TBFrozen);
    t = articleType(line, len, inheader);
    PieceMore = 0;
    return(tbResult(t));
}

static int
articleType(char *line, int len, int inheader)
{
    static char *ptr;
    static char first;
    int b64blank = B64Blank;

    B64Blank = 0;

    /* a new line: no escape is cut at its start */
    if (!PieceMore)
	QPEsc = 0;

    if (Type != ARTTYPE_DEFAULT)
	Type = Type & ~ARTTYPE_DEFAULT;
    /*
     * The blank line at the end of a header block (of the article or of a
     * MIME part) settles a base64 Content-Transfer-Encoding: the part is a
     * binary unless its Content-Type is text/ (non-ASCII text is sent this
     * way).  The two headers can come in either order.
     */
    if (len == 0 || (len == 1 && *line == '\r')) {
	if (!HdrBlock && B64Part && !PieceMore && B64Quad == 0 && B64NBits == 0)
	    B64Blank = 1;
	if (CTCont)
	    ctFinish();
	if (HdrBlock && CTEBase64) {
	    if (CTText || CTArmorPGP || (!CTSeen && B64_NOCTYPE_IS_TEXT)) {
		/* a text part: decode it (b64Decode) */
		b64Start();
	    } else {
		Type |= ARTTYPE_BASE64 | ARTTYPE_BINARY;
		Type &= ~ARTTYPE_DEFAULT;
	    }
	}
	/* RFC 2046 section 5.2.1: the identity-encoded body starts
	 * with the encapsulated message's own headers. Keep the outer
	 * multipart boundary stack so a sibling still ends this message.
	 */
	if (HdrBlock && CTMessage && CTEIdentity) {
	    partReset();
	    HdrBlock = 1;
	    return(Type);
	}
	/* the same for a quoted-printable one (qpLine) */
	if (HdrBlock && CTEQP)
	    QPPart = 1;
	CTEBase64 = 0;
	CTEQP = 0;
	HdrBlock = 0;
    }
    /*
     * If we have found a binary, just keep it that way - saves CPU,
     * unless it is measured (TextBinMax)
     */
    if ((Type & ARTTYPE_BINARY) && !TBACTIVE())
	return(Type);

    /* an empty line carries no type to classify (and must not be read) */
    if (len <= 0)
	return(Type);

    ptr = line;

    /* a folded Content-Type header goes on, or it is done */
    if (CTCont) {
	if (HdrBlock && (*ptr == ' ' || *ptr == '\t'))
	    ctGather(ptr, len, 1);
	else
	    ctFinish();
    }

    /*
     * MIME fields belong to article or part headers, not header-like body text.
     * RFC 5536 section 2 and RFC 2045 section 3.
     */
    first = tolower((unsigned char)*ptr);
    if (HdrBlock && len >= 2 && first == 'c' &&
	    tolower((unsigned char)ptr[1]) == 'o') {
	const char *cv = NULL;	/* the value of a Content-Type: header */
	const char *ce = NULL;	/* the value of a Content-Transfer-Encoding: header */

	/* white space after the colon is optional ("Content-Type:text/html") */
	if (hdrPrefix(ptr, len, "Content-Type:"))
	    cv = ptr + 13;
	else if (hdrPrefix(ptr, len, "Content-transfer-encoding:"))
	    ce = ptr + 26;
	if (cv != NULL) {
	    while (cv < ptr + len && (*cv == ' ' || *cv == '\t'))
		++cv;
	}
	if (ce != NULL) {
	    while (ce < ptr + len && (*ce == ' ' || *ce == '\t'))
		++ce;
	}

	if (cv != NULL) {
	    int cvlen = len - (int)(cv - ptr);

	    Type |= ARTTYPE_MIME;
	    /*
	     * In a header block: the part's type (CTSeen, CTText) and,
	     * from the whole header, its boundary (ctFinish)
	     */
	    if (HdrBlock) {
		CTSeen = 1;
		CTText = hdrPrefix(cv, cvlen, "text/");
		ctGather(cv, cvlen, 0);
	    }
	    if (hdrPrefix(cv, cvlen, "text/html"))
		Type |= ARTTYPE_HTML;
	    if (hdrPrefix(cv, cvlen, "multipart"))
		Type |= ARTTYPE_MULTIPART;
	    if (hdrPrefix(cv, cvlen, "application/postscript"))
		Type |= ARTTYPE_PS;
	    if (hdrPrefix(cv, cvlen, "application/mac-binhex40"))
		BinHex = 1;
	    /*
	     * PGP/MIME (RFC 3156): application/pgp-encrypted, -signature and
	     * -keys parts are PGP data.  In an encrypted article the encrypted
	     * data is an octet-stream part, which is not a binary posting.
	     */
	    if (hdrPrefix(cv, cvlen, "application/pgp-"))
		Type |= ARTTYPE_PGPMESSAGE;
	    if (hdrPrefix(cv, cvlen, "multipart/") &&
		    (lineHas(cv, cvlen, "application/pgp-encrypted") ||
		     lineHas(cv, cvlen, "application/pgp-signature")))
		Type |= ARTTYPE_PGPMESSAGE;
	    if (hdrPrefix(cv, cvlen, "application/pgp-encrypted") ||
		    (hdrPrefix(cv, cvlen, "multipart/encrypted") &&
		     lineHas(cv, cvlen, "application/pgp-encrypted")))
		PGPMime = 1;
	    if (hdrPrefix(cv, cvlen, "application/octet-stream") && !PGPMime) {
		Type |= ARTTYPE_BINARY;
		TBOther = 1;
	    }
	    if (hdrPrefix(cv, cvlen, "message/partial"))
		Type |= ARTTYPE_PARTIAL;
	}
	if (ce != NULL) {
	    int celen = len - (int)(ce - ptr);

	    int elen = celen;
	    while (elen > 0 && (ce[elen - 1] == ' ' || ce[elen - 1] == '\t' || ce[elen - 1] == '\r'))
		--elen;
	    CTEIdentity = ((elen == 4 && (strncasecmp(ce, "7bit", 4) == 0 ||
		strncasecmp(ce, "8bit", 4) == 0)) ||
		(elen == 6 && strncasecmp(ce, "binary", 6) == 0));

	    /* a part's encoding is in its header block, not in its text */
	    if (HdrBlock && hdrPrefix(ce, celen, "base64"))
		CTEBase64 = 1;
	    if (HdrBlock && hdrPrefix(ce, celen, "quoted-printable"))
		CTEQP = 1;
	    if (hdrPrefix(ce, celen, "X-Bommanews")) {
		Type |= ARTTYPE_BOMMANEWS | ARTTYPE_BINARY;
		TBOther = 1;
	    }
	    if (hdrPrefix(ce, celen, "X-UnidataEncoding")) {
		Type |= ARTTYPE_UNIDATA | ARTTYPE_BINARY;
		TBOther = 1;
	    }
	}
    }
    if (first == '(' && len >= 33 &&
	    strncasecmp(ptr, "(This file must be converted with BinHex 4.0)", 33) == 0)
	BinHex = 1;

    yencLine(ptr, len);

    if (inheader) {
	if (len >= 2 && first == 'c' && tolower((unsigned char)ptr[1]) == 'o') {
	    if (hdrPrefix(ptr, len, "Control: "))
		Type |= ARTTYPE_CONTROL;
	    if (hdrPrefix(ptr, len, "Control: cancel "))
		Type |= ARTTYPE_CANCEL;
	}
	if (first == 'm' && hdrPrefix(ptr, len, "Mime-Version: "))
		Type |= ARTTYPE_MIME;
    } else {
	int dl = 0;

	/*
	 * A delimiter of a multipart we are in ends the part (and, closing,
	 * the multipart); an opening one starts the headers of the next
	 * part.  Other lines that start with "--" are text.
	 */
	if (len >= 2 && ptr[0] == '-' && ptr[1] == '-')
	    dl = bndMatch(ptr + 2, len - 2);
	if (dl) {
	    partReset();
	    HdrBlock = (dl == 1);
	    if (TBACTIVE()) {
		tbRunEnd();
		TBTrailLeft = 0;
	    }
	    if (Type & ARTTYPE_BINARY)
		return(Type);
	} else if (B64Part) {
	    int k = 3;
	    int bare = (len == 2 || (len == 3 && ptr[2] == '\r'));

	    /*
	     * A signature separator ("-- ") after base64 text: base64 has no
	     * '-', so the encoded text is over, and the signature is checked
	     * as it is, not decoded. Gateways sometimes strip that trailing
	     * space: accept bare "--" after complete base64 padding, or
	     * after a blank body line following a complete base64 group.
	     * Midstream "--" and other '-' lines are still decoded on.
	     */
	    while (k < len && (ptr[k] == ' ' || ptr[k] == '\t' || ptr[k] == '\r'))
		++k;
	    if (len >= 2 && ptr[0] == '-' && ptr[1] == '-' &&
		((len >= 3 && ptr[2] == ' ' && k == len) ||
		 (bare && (B64Pad == 2 || b64blank)))) {
		b64End();
		if (Type & ARTTYPE_BINARY)
		    return(Type);
	    } else if (!b64Decode(ptr, len) || (Type & ARTTYPE_BINARY)) {
		/*
		 * The body of a base64 text part is decoded, not counted
		 * as base64 lines.  A line with characters that are not
		 * base64 also gets the checks of a line as it came.
		 */
		if (TBACTIVE() && !TBFrozen)
		    TBText += len;
		return(Type);
	    }
	} else if (QPPart && QPTEXT() && qpBad(ptr, len)) {
	    Type |= ARTTYPE_BINARY;
	    Type &= ~ARTTYPE_DEFAULT;
	    TBOther = 1;
	    return(Type);
	}

	if (bodyLineTypes(ptr, len, &UUencode, &BinHex, &Base64, &Armor))
	    return(Type);
    }
    if (Type != ARTTYPE_DEFAULT)
	Type = Type & ~ARTTYPE_DEFAULT;
    return(Type);
}

