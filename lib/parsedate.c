
/*
 * LIB/PARSEDATE.C - approximate date parser
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 * parsedate() reads the date of a Date: header (or any date string) and
 * returns Unix time, or (time_t)-1 when it cannot make sense of it.  It
 * is meant to be liberal, as the Date: headers of 45 years of Usenet are:
 *
 *	RFC 5322/822	Mon, 01 Jan 2001 12:00:00 +0000
 *			1 Jan 2001 12:00 GMT (no weekday, no seconds, 01 Jan 01)
 *	RFC 850/1036	Monday, 01-Jan-01 12:00:00 GMT
 *	ctime/asctime	Mon Jan  1 12:00:00 2001
 *	date(1)		Mon Jan  1 12:00:00 UTC 2001
 *	US		Jan 1, 2001 12:00:00 PM  /  January 1, 2001
 *	ISO 8601	2001-01-01  /  2001-01-01T12:00:00Z  /  2001-01-01 12:00:00 +01:00
 *			20010101T120000Z  /  2001/01/01
 *	European	01.01.2001 12:00
 *	Unix time	978350400
 *
 * Weekday names are skipped, a comment in parentheses is ignored, the
 * month is an English name or abbreviation (any word that starts with
 * one of the three-letter abbreviations, so "mars" and "septembre" work)
 * or one of a few common German, French, Spanish, Italian and Dutch ones.
 * The time zone is applied: a numeric offset, UT, UTC, GMT, Z, the US
 * zones and a few European and Asian ones; any other name counts as UTC.
 * A year of two digits is 2000-2049 for 00-49 and 1950-1999 for 50-99
 * (RFC 5322); one of three digits (the Y2K "101") is 1900 plus the number.
 *
 * Not accepted, because they cannot be read with certainty: numeric dates
 * with the day and month in either order (01/02/2001), a date without a
 * year (the syslog "Oct 31 05:40:12"), and anything else.
 */

#include "defs.h"

Prototype time_t parsedate(char *buf);

#define PD_MAXTOK	64

enum { T_NUM = 1, T_ALPHA, T_SYM };

typedef struct PdTok {
    int		pt_Type;
    const char	*pt_Str;
    int		pt_Len;
    long long	pt_Val;		/* T_NUM: the value (saturated); T_SYM: the character */
} PdTok;

static int pdTokenize(const char *d, PdTok *tok);
static int pdMonth(const PdTok *t);
static int pdIsWeekday(const PdTok *t);
static int pdZone(const PdTok *t, long *off);
static int pdTime(const PdTok *tok, int n, int *pi, int *hour, int *min, int *sec);
static int pdZoneAt(const PdTok *tok, int n, int *pi, long *off);
static long pdYear(const PdTok *t);
static time_t pdMake(long year, int mon, int mday, int hour, int min, int sec, long off);

/*
 * Days from 1970-01-01 to the civil date y-m-d (proleptic Gregorian).
 */
static long long
pdDays(long long y, int m, int d)
{
    long long era;
    long long yoe, doy, doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return(era * 146097 + doe - 719468);
}

static int
pdIsSym(const PdTok *t, int c)
{
    return(t->pt_Type == T_SYM && t->pt_Val == c);
}

time_t
parsedate(char *d)
{
    PdTok tok[PD_MAXTOK];
    int n;
    int i = 0;
    int mon = -1;
    int mday = 0;
    long year = -1;
    int hour = 0;
    int min = 0;
    int sec = 0;
    long off = 0;
    int haveTime = 0;
    int zoneSeen = 0;

    if (d == NULL)
	return((time_t)-1);
    n = pdTokenize(d, tok);

    /*
     * "Date:" in front, then the weekday ("Mon," "Monday," "Mon ")
     */
    if (i + 1 < n && tok[i].pt_Type == T_ALPHA && tok[i].pt_Len == 4 &&
		strncasecmp(tok[i].pt_Str, "date", 4) == 0 && pdIsSym(&tok[i+1], ':'))
	i += 2;
    if (i < n && tok[i].pt_Type == T_ALPHA && pdMonth(&tok[i]) < 0) {
	if (pdIsWeekday(&tok[i]) || (i + 1 < n && pdIsSym(&tok[i+1], ','))) {
	    ++i;
	    if (i < n && pdIsSym(&tok[i], ','))
		++i;
	}
    }
    if (i >= n)
	return((time_t)-1);

    if (tok[i].pt_Type == T_NUM) {
	if (tok[i].pt_Len >= 9 && tok[i].pt_Len <= 10 && i + 1 == n) {
	    /* Unix time */
	    return((time_t)tok[i].pt_Val);
	}
	if (tok[i].pt_Len == 8 && i + 2 < n && tok[i+1].pt_Type == T_ALPHA &&
		tok[i+1].pt_Len == 1 && (tok[i+1].pt_Str[0] == 'T' || tok[i+1].pt_Str[0] == 't') &&
		tok[i+2].pt_Type == T_NUM && tok[i+2].pt_Len == 6) {
	    /* 20010101T120000Z */
	    long dv = (long)tok[i].pt_Val;
	    long tv = (long)tok[i+2].pt_Val;

	    year = dv / 10000;
	    mon = (int)(dv / 100 % 100) - 1;
	    mday = (int)(dv % 100);
	    hour = (int)(tv / 10000);
	    min = (int)(tv / 100 % 100);
	    sec = (int)(tv % 100);
	    haveTime = 1;
	    i += 3;
	} else if (tok[i].pt_Len == 4 && i + 4 < n &&
		(pdIsSym(&tok[i+1], '-') || pdIsSym(&tok[i+1], '/')) &&
		tok[i+2].pt_Type == T_NUM && pdIsSym(&tok[i+3], (int)tok[i+1].pt_Val) &&
		tok[i+4].pt_Type == T_NUM) {
	    /* ISO 2001-01-01, 2001/01/01 */
	    year = (long)tok[i].pt_Val;
	    mon = (int)tok[i+2].pt_Val - 1;
	    mday = (int)tok[i+4].pt_Val;
	    i += 5;
	} else if (tok[i].pt_Len <= 2 && i + 4 < n && pdIsSym(&tok[i+1], '.') &&
		tok[i+2].pt_Type == T_NUM && tok[i+2].pt_Len <= 2 && pdIsSym(&tok[i+3], '.') &&
		tok[i+4].pt_Type == T_NUM && tok[i+4].pt_Len == 4) {
	    /* European 01.01.2001 */
	    mday = (int)tok[i].pt_Val;
	    mon = (int)tok[i+2].pt_Val - 1;
	    year = (long)tok[i+4].pt_Val;
	    i += 5;
	} else {
	    /* 1 Jan 2001, 01-Jan-01, 1. Jan 2001 */
	    if (tok[i].pt_Len > 2)
		return((time_t)-1);
	    mday = (int)tok[i].pt_Val;
	    ++i;
	    while (i < n && (pdIsSym(&tok[i], '-') || pdIsSym(&tok[i], '.') || pdIsSym(&tok[i], ',')))
		++i;
	    if (i < n && tok[i].pt_Type == T_ALPHA)
		mon = pdMonth(&tok[i]);
	    if (mon < 0)
		return((time_t)-1);
	    ++i;
	    while (i < n && (pdIsSym(&tok[i], '-') || pdIsSym(&tok[i], '.') || pdIsSym(&tok[i], ',')))
		++i;
	    /* the year, not the hour of a time */
	    if (i >= n || tok[i].pt_Type != T_NUM || (i + 1 < n && pdIsSym(&tok[i+1], ':')))
		return((time_t)-1);
	    year = pdYear(&tok[i]);
	    ++i;
	}
    } else if (tok[i].pt_Type == T_ALPHA) {
	/* Jan 1, 2001   Jan  1 12:00:00 2001   January 1 2001 */
	mon = pdMonth(&tok[i]);
	if (mon < 0)
	    return((time_t)-1);
	++i;
	if (i < n && pdIsSym(&tok[i], '.'))
	    ++i;
	if (i >= n || tok[i].pt_Type != T_NUM || tok[i].pt_Len > 2)
	    return((time_t)-1);
	mday = (int)tok[i].pt_Val;
	++i;
	while (i < n && (pdIsSym(&tok[i], ',') || pdIsSym(&tok[i], '.')))
	    ++i;
	if (i < n && tok[i].pt_Type == T_NUM && !(i + 1 < n && pdIsSym(&tok[i+1], ':'))) {
	    year = pdYear(&tok[i]);	/* Jan 1, 2001 [12:00:00] */
	    ++i;
	} else {
	    /* ctime: the time, an optional zone, then the year */
	    if (!pdTime(tok, n, &i, &hour, &min, &sec))
		return((time_t)-1);
	    haveTime = 1;
	    if (pdZoneAt(tok, n, &i, &off))
		zoneSeen = 1;
	    if (i >= n || tok[i].pt_Type != T_NUM)
		return((time_t)-1);
	    year = pdYear(&tok[i]);
	    ++i;
	}
    } else {
	return((time_t)-1);
    }

    /*
     * The time ("T" or a comma before it), then the zone
     */
    if (!haveTime) {
	if (i < n && tok[i].pt_Type == T_ALPHA && tok[i].pt_Len == 1 &&
		(tok[i].pt_Str[0] == 'T' || tok[i].pt_Str[0] == 't'))
	    ++i;
	else if (i < n && pdIsSym(&tok[i], ','))
	    ++i;
	if (i + 1 < n && tok[i].pt_Type == T_NUM && pdIsSym(&tok[i+1], ':')) {
	    if (!pdTime(tok, n, &i, &hour, &min, &sec))
		return((time_t)-1);
	    haveTime = 1;
	}
    }
    if (haveTime && !zoneSeen)
	pdZoneAt(tok, n, &i, &off);

    if (year < 0 || mon < 0 || mon > 11 || mday < 1 || mday > 31 ||
		hour < 0 || hour > 24 || min < 0 || min > 59 || sec < 0 || sec > 60)
	return((time_t)-1);
    return(pdMake(year, mon, mday, hour, min, sec, off));
}

static time_t
pdMake(long year, int mon, int mday, int hour, int min, int sec, long off)
{
    long long t;

    if (year < 1 || year > 9999)
	return((time_t)-1);
    t = pdDays(year, mon + 1, mday) * 86400LL + hour * 3600LL + min * 60LL + sec - off;
    if ((long long)(time_t)t != t)
	return((time_t)-1);
    if (t == -1)
	return((time_t)0);	/* -1 is the failure value */
    return((time_t)t);
}

/*
 * Split into numbers, words and single punctuation characters; white space
 * and (comments) are dropped.
 */
static int
pdTokenize(const char *d, PdTok *tok)
{
    int n = 0;

    while (*d && n < PD_MAXTOK) {
	unsigned char c = (unsigned char)*d;

	if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
	    ++d;
	} else if (c == '(') {
	    int depth = 0;

	    do {
		if (*d == '(')
		    ++depth;
		else if (*d == ')')
		    --depth;
		++d;
	    } while (*d && depth > 0);
	} else if (isdigit(c)) {
	    const char *s = d;
	    long long v = 0;

	    while (isdigit((unsigned char)*d)) {
		if (v < 100000000000LL)
		    v = v * 10 + (*d - '0');
		++d;
	    }
	    tok[n].pt_Type = T_NUM;
	    tok[n].pt_Str = s;
	    tok[n].pt_Len = d - s;
	    tok[n].pt_Val = v;
	    ++n;
	} else if (isalpha(c) || c >= 0x80) {
	    const char *s = d;

	    while (isalpha((unsigned char)*d) || (unsigned char)*d >= 0x80)
		++d;
	    tok[n].pt_Type = T_ALPHA;
	    tok[n].pt_Str = s;
	    tok[n].pt_Len = d - s;
	    tok[n].pt_Val = 0;
	    ++n;
	} else {
	    tok[n].pt_Type = T_SYM;
	    tok[n].pt_Str = d;
	    tok[n].pt_Len = 1;
	    tok[n].pt_Val = c;
	    ++n;
	    ++d;
	}
    }
    return(n);
}

static int
pdWordIs(const PdTok *t, const char *w)
{
    return(t->pt_Len == (int)strlen(w) && strncasecmp(t->pt_Str, w, t->pt_Len) == 0);
}

/*
 * The month 0..11 of a word, or -1: the English abbreviation as the start of the word
 * ("Jan", "January", "mars"), or one of a few non-English names.
 */
static int
pdMonth(const PdTok *t)
{
    static const char *MonAry[12] = {
	"jan", "feb", "mar", "apr", "may", "jun",
	"jul", "aug", "sep", "oct", "nov", "dec"
    };
    static const struct { const char *name; int mon; } Other[] = {
	{ "janv", 0 }, { "ene", 0 }, { "gen", 0 },
	{ "f\xc3\xa9vr", 1 }, { "f\xe9vr", 1 }, { "fev", 1 }, { "febr", 1 },
	{ "m\xc3\xa4r", 2 }, { "m\xe4r", 2 }, { "mrz", 2 }, { "mrt", 2 },
	{ "avr", 3 }, { "abr", 3 },
	{ "mai", 4 }, { "mei", 4 }, { "mag", 4 },
	{ "juin", 5 }, { "giu", 5 },
	{ "juil", 6 }, { "lug", 6 },
	{ "ao\xc3\xbbt", 7 }, { "ao\xfbt", 7 }, { "aout", 7 }, { "ago", 7 },
	{ "sept", 8 }, { "set", 8 },
	{ "okt", 9 }, { "ott", 9 },
	{ "d\xc3\xa9" "c", 11 }, { "d\xe9" "c", 11 }, { "dez", 11 }, { "dic", 11 },
	{ NULL, 0 }
    };
    int i;

    if (t->pt_Type != T_ALPHA || t->pt_Len < 3 || t->pt_Len > 12)
	return(-1);
    for (i = 0; i < 12; ++i) {
	if (strncasecmp(t->pt_Str, MonAry[i], 3) == 0)
	    return(i);
    }
    for (i = 0; Other[i].name; ++i) {
	int l = strlen(Other[i].name);

	if (t->pt_Len >= l && t->pt_Len <= l + 6 && strncasecmp(t->pt_Str, Other[i].name, l) == 0)
	    return(Other[i].mon);
    }
    return(-1);
}

static int
pdIsWeekday(const PdTok *t)
{
    static const char *Days[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
    int i;

    if (t->pt_Type != T_ALPHA || t->pt_Len < 3 || t->pt_Len > 9)
	return(0);
    for (i = 0; i < 7; ++i) {
	if (strncasecmp(t->pt_Str, Days[i], 3) == 0)
	    return(1);
    }
    return(0);
}

/*
 * A year: 4 digits as they are; 2 digits as RFC 5322 has it (00-49 is 20xx, 50-99 is 19xx);
 * 3 digits are the years since 1900 ("101" is 2001).  Other lengths are not a year (-1).
 */
static long
pdYear(const PdTok *t)
{
    if (t->pt_Len == 4)
	return((long)t->pt_Val);
    if (t->pt_Len == 3)
	return((long)t->pt_Val + 1900);
    if (t->pt_Len <= 2)
	return((long)(t->pt_Val < 50 ? 2000 + t->pt_Val : 1900 + t->pt_Val));
    return(-1);
}

/*
 * hh:mm[:ss[.frac]] [AM|PM] at tok[*pi]
 */
static int
pdTime(const PdTok *tok, int n, int *pi, int *hour, int *min, int *sec)
{
    int i = *pi;

    if (i + 2 >= n || tok[i].pt_Type != T_NUM || !pdIsSym(&tok[i+1], ':') ||
		tok[i+2].pt_Type != T_NUM)
	return(0);
    *hour = (int)tok[i].pt_Val;
    *min = (int)tok[i+2].pt_Val;
    *sec = 0;
    i += 3;
    if (i + 1 < n && pdIsSym(&tok[i], ':') && tok[i+1].pt_Type == T_NUM) {
	*sec = (int)tok[i+1].pt_Val;
	i += 2;
	if (i + 1 < n && pdIsSym(&tok[i], '.') && tok[i+1].pt_Type == T_NUM)
	    i += 2;			/* fractions of a second */
    }
    if (i < n && tok[i].pt_Type == T_ALPHA && tok[i].pt_Len == 2) {
	if (pdWordIs(&tok[i], "am")) {
	    if (*hour == 12)
		*hour = 0;
	    ++i;
	} else if (pdWordIs(&tok[i], "pm")) {
	    if (*hour < 12)
		*hour += 12;
	    ++i;
	}
    }
    *pi = i;
    return(1);
}

/*
 * The zone name as seconds east of UTC (the time is local time of the zone, so UTC is the
 * time minus this)
 */
static int
pdZone(const PdTok *t, long *off)
{
    static const struct { const char *name; int hours; } Zones[] = {
	{ "ut", 0 }, { "utc", 0 }, { "gmt", 0 }, { "z", 0 }, { "wet", 0 },
	{ "est", -5 }, { "edt", -4 }, { "cst", -6 }, { "cdt", -5 },
	{ "mst", -7 }, { "mdt", -6 }, { "pst", -8 }, { "pdt", -7 },
	{ "bst", 1 }, { "cet", 1 }, { "met", 1 }, { "mez", 1 }, { "west", 1 },
	{ "cest", 2 }, { "mest", 2 }, { "mesz", 2 }, { "eet", 2 }, { "eest", 3 },
	{ "jst", 9 }, { "kst", 9 },
	{ NULL, 0 }
    };
    int i;

    if (t->pt_Type != T_ALPHA || t->pt_Len > 5)
	return(0);
    if (pdWordIs(t, "am") || pdWordIs(t, "pm") || pdIsWeekday(t) || pdMonth(t) >= 0)
	return(0);
    for (i = 0; Zones[i].name; ++i) {
	if (pdWordIs(t, Zones[i].name)) {
	    *off = Zones[i].hours * 3600L;
	    return(1);
	}
    }
    *off = 0;			/* a zone we do not know: UTC, as RFC 5322 says of the military ones */
    return(1);
}

/*
 * A zone at tok[*pi]: +hhmm, -hh:mm, +hh, a name, GMT+0100.  *pi moves past it when there
 * is one.
 */
static int
pdZoneAt(const PdTok *tok, int n, int *pi, long *off)
{
    int i = *pi;

    if (i < n && tok[i].pt_Type == T_ALPHA && pdZone(&tok[i], off)) {
	++i;
	if (i + 1 < n && (pdIsSym(&tok[i], '+') || pdIsSym(&tok[i], '-')) &&
		tok[i+1].pt_Type == T_NUM && (tok[i+1].pt_Len == 4 || tok[i+1].pt_Len == 2)) {
	    int sign = pdIsSym(&tok[i], '-') ? -1 : 1;
	    long v = (long)tok[i+1].pt_Val;

	    *off = sign * (tok[i+1].pt_Len == 4 ? (v / 100) * 3600L + (v % 100) * 60L : v * 3600L);
	    i += 2;
	}
	*pi = i;
	return(1);
    }
    if (i + 1 < n && (pdIsSym(&tok[i], '+') || pdIsSym(&tok[i], '-')) && tok[i+1].pt_Type == T_NUM) {
	int sign = pdIsSym(&tok[i], '-') ? -1 : 1;
	long v = (long)tok[i+1].pt_Val;

	if (tok[i+1].pt_Len == 4) {
	    *off = sign * ((v / 100) * 3600L + (v % 100) * 60L);
	    i += 2;
	} else if (tok[i+1].pt_Len <= 2) {
	    long m = 0;

	    i += 2;
	    if (i + 1 < n && pdIsSym(&tok[i], ':') && tok[i+1].pt_Type == T_NUM &&
			tok[i+1].pt_Len == 2) {
		m = (long)tok[i+1].pt_Val;
		i += 2;
	    }
	    *off = sign * (v * 3600L + m * 60L);
	} else {
	    return(0);
	}
	*pi = i;
	return(1);
    }
    return(0);
}
