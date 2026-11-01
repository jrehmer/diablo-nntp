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
 *
 */

#include "defs.h"

Prototype void InitArticleType(void);
Prototype int ArtTypeConv(char *t);
Prototype int ArtTypeMatch(int articletype, ArtTypeList *acctypes);
Prototype int ArticleType(char *line, int len, int inheader);

#define	ARMOR_MAX	100	/* lines of one armored block (PGP, PEM) that are not counted */

static int lineHas(const char *line, int len, const char *needle);
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
int CTText = 0;	/* the last Content-Type seen is text/ */
int CTEBase64 = 0;	/* a base64 Content-Transfer-Encoding is waiting for the end of its header block */
int Armor = 0;	/* lines left to skip in an ASCII-armored block (PGP, PEM), 0 = not in one */

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

	if (len <= 0)
	    return(0);

	if (ptr[len - 1] == '\n')
	    len--;
	if (ptr[len - 1] == '\r')
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
    CTEBase64 = 0;
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

int
ArticleType(char *line, int len, int inheader)
{
    static char *ptr;
    static char first;

    if (Type != ARTTYPE_DEFAULT)
	Type = Type & ~ARTTYPE_DEFAULT;
    /*
     * The blank line at the end of a header block (of the article or of a
     * MIME part) settles a base64 Content-Transfer-Encoding: the part is a
     * binary unless its Content-Type is text/ (non-ASCII text is sent this
     * way).  The two headers can come in either order.
     */
    if (CTEBase64 && (len == 0 || (len == 1 && *line == '\r'))) {
	CTEBase64 = 0;
	if (!CTText)
	    Type |= ARTTYPE_BASE64 | ARTTYPE_BINARY;
    }
    /*
     * If we have found a binary, just keep it that way - saves CPU
     */
    if (Type & ARTTYPE_BINARY)
	return(Type);

    ptr = line;

    /*
     * Do some checks that could apply to headers and or body
     */
    first = tolower(*ptr);
    if (first == 'c' && tolower(*(ptr + 1)) == 'o') {
	const char *cv = NULL;	/* the value of a Content-Type: header */
	const char *ce = NULL;	/* the value of a Content-Transfer-Encoding: header */

	/* white space after the colon is optional ("Content-Type:text/html") */
	if (! strncasecmp(ptr, "Content-Type:", 13))
	    cv = ptr + 13;
	else if (! strncasecmp(ptr, "Content-transfer-encoding:", 26))
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
	    Type |= ARTTYPE_MIME;
	    CTText = (strncasecmp(cv, "text/", 5) == 0);
	    if (! strncasecmp(cv, "text/html", 9))
		Type |= ARTTYPE_HTML;
	    if (! strncasecmp(cv, "multipart", 9))
		Type |= ARTTYPE_MULTIPART;
	    if (! strncasecmp(cv, "application/postscript", 22))
		Type |= ARTTYPE_PS;
	    if (! strncasecmp(cv, "application/mac-binhex40", 24))
		BinHex = 1;
	    /*
	     * PGP/MIME (RFC 3156): application/pgp-encrypted, -signature and
	     * -keys parts are PGP data.  In an encrypted article the encrypted
	     * data is an octet-stream part, which is not a binary posting.
	     */
	    if (! strncasecmp(cv, "application/pgp-", 16))
		Type |= ARTTYPE_PGPMESSAGE;
	    if (! strncasecmp(cv, "multipart/", 10) &&
		    (lineHas(cv, len - (int)(cv - ptr), "application/pgp-encrypted") ||
		     lineHas(cv, len - (int)(cv - ptr), "application/pgp-signature")))
		Type |= ARTTYPE_PGPMESSAGE;
	    if (! strncasecmp(cv, "application/pgp-encrypted", 25) ||
		    (! strncasecmp(cv, "multipart/encrypted", 19) &&
		     lineHas(cv, len - (int)(cv - ptr), "application/pgp-encrypted")))
		PGPMime = 1;
	    if (! strncasecmp(cv, "application/octet-stream", 24) && !PGPMime)
		Type |= ARTTYPE_BINARY;
	    if (! strncasecmp(cv, "message/partial", 15))
		Type |= ARTTYPE_PARTIAL;
	}
	if (ce != NULL) {
	    if (! strncasecmp(ce, "base64", 6))
		CTEBase64 = 1;
	    if (! strncasecmp(ce, "X-Bommanews", 11))
		Type |= ARTTYPE_BOMMANEWS | ARTTYPE_BINARY;
	    if (! strncasecmp(ce, "X-UnidataEncoding", 17))
		Type |= ARTTYPE_UNIDATA | ARTTYPE_BINARY;
	}
    }
    if (first == '(' && strncasecmp(ptr, "(This file must be converted with BinHex 4.0)", 33) == 0)
	BinHex = 1;

    if (first == '=' && strncasecmp(ptr, "=ybegin part=", 13) == 0)
	Type |= ARTTYPE_BINARY|ARTTYPE_PARTIAL|ARTTYPE_YENC;
    if (first == '=' && strncasecmp(ptr, "=ybegin line=", 13) == 0)
	Type |= ARTTYPE_BINARY|ARTTYPE_YENC;

    if (inheader) {
	if (first == 'c' && tolower(*(ptr + 1)) == 'o') {
	    if (! strncasecmp(ptr, "Control: ", 9))
		Type |= ARTTYPE_CONTROL;
	    if (! strncasecmp(ptr, "Control: cancel ", 16))
		Type |= ARTTYPE_CANCEL;
	}
	if (first == 'm' && strncasecmp(ptr, "Mime-Version: ", 14) == 0)
		Type |= ARTTYPE_MIME;
    } else {
	int linetype;

	/* A MIME boundary starts a part: its Content-Type is not known yet */
	if (ptr[0] == '-' && ptr[1] == '-')
	    CTText = 0;

	/* Get quoted UUencode, etc. */
	if (*ptr == '>') {
	    ptr++;
	    len--;
	    if (*ptr == ' ') {
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
	 * skipping is bounded by ARMOR_MAX lines so that a long binary can't
	 * hide behind a "BEGIN" line.
	 */
	if (Armor > 0) {
	    if (strncmp(ptr, "-----END ", 9) == 0)
		Armor = 0;
	    else
		--Armor;
	    linetype = 0;
	} else {
	    linetype = classifyLineAsTypes(ptr, len);
	    if (*ptr == '-' && strncmp(ptr, "-----BEGIN ", 11) == 0) {
		/* PGP in any armor form: message (also a part), signed
		 * message, signature, public or private key block */
		if (strncmp(ptr + 11, "PGP ", 4) == 0)
		    Type |= ARTTYPE_PGPMESSAGE;
		Armor = ARMOR_MAX;
	    }
	}

	if (linetype & UUE) {
	    Base64 = 0;
	    BinHex = 0;
	    UUencode++;
	    if (UUencode > 8) {
		Type |= ARTTYPE_UUENCODE | ARTTYPE_BINARY;
		Type &= ~ARTTYPE_DEFAULT;
		return(Type);
	    }
	} else if (linetype & BHX) {
	    UUencode = 0;
	    Base64 = 0;
	    BinHex++;
	    if (BinHex > 8) {
		Type |= ARTTYPE_BINHEX | ARTTYPE_BINARY;
		Type &= ~ARTTYPE_DEFAULT;
		return(Type);
	    }
	} else if (linetype & B64) {
	    UUencode = 0;
	    BinHex = 0;
	    Base64++;
	    if (Base64 > 8) {
		Type |= ARTTYPE_BASE64 | ARTTYPE_BINARY;
		Type &= ~ARTTYPE_DEFAULT;
		return(Type);
	    }
	} else {
	    UUencode = 0;
	    Base64 = 0;
	    BinHex = 0;
	}
    }
    if (Type != ARTTYPE_DEFAULT)
	Type = Type & ~ARTTYPE_DEFAULT;
    return(Type);
}

