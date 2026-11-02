/*
 * LIB/PARSEDATE.C - RFC-style date parser
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 * the COPYRIGHT file in the base directory of this distribution
 * for specific rights granted.
 */

#include "defs.h"

Prototype time_t parsedate(char *buf);

/* Skip whitespace and nested RFC comments. */
static const char *
pdSpace(const char *p)
{
    while (*p) {
	if (isspace((unsigned char)*p)) {
	    ++p;
	} else if (*p == '(') {
	    int depth = 1;
	    ++p;
	    while (*p && depth) {
		if (*p == '\\' && p[1]) {
		    p += 2;
		    continue;
		}
		if (*p == '(')
		    ++depth;
		else if (*p == ')')
		    --depth;
		++p;
	    }
	    if (depth)
		return(NULL);
	} else {
	    break;
	}
    }
    return(p);
}

/* Read a bounded decimal field, without silently truncating long numbers. */
static int
pdNumber(const char **p, int min, int max, int *value)
{
    int n = 0, v = 0;

    while (isdigit((unsigned char)**p)) {
	if (++n > max)
	    return(0);
	v = v * 10 + *(*p)++ - '0';
    }
    if (n < min)
	return(0);
    *value = v;
    return(n);
}

static int
pdMonth(const char *p, size_t len)
{
    static const char *months[] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
    };
    int i;

    for (i = 0; i < 12; ++i) {
	if ((len == 3 || len == strlen(months[i])) &&
		strncasecmp(p, months[i], len) == 0)
	    return(i);
    }
    return(-1);
}

/* Legacy names; unknown names are treated as UTC. Offsets are in minutes. */
static int
pdZoneLookup(const char *p, size_t len, int *minutes)
{
    static const struct { const char *name; int minutes; } zones[] = {
	{ "UT", 0 }, { "UTC", 0 }, { "CUT", 0 }, { "GMT", 0 }, { "WET", 0 },
	{ "EST", -300 }, { "EDT", -240 }, { "CST", -360 }, { "CDT", -300 },
	{ "MST", -420 }, { "MDT", -360 }, { "PST", -480 }, { "PDT", -420 },
	{ "BST", 60 }, { "NDT", -150 }, { "NST", -210 }, { "ADT", -180 },
	{ "AST", -240 }, { "YDT", -480 }, { "YST", -540 }, { "AKDT", -480 },
	{ "AKST", -540 }, { "HADT", -540 }, { "HAST", -600 }, { "HST", -600 },
	{ "CES", 120 }, { "CEST", 120 }, { "MEZ", 60 }, { "MEZT", 120 },
	{ "CET", 60 }, { "MET", 60 }, { "EET", 120 }, { "MSK", 180 },
	{ "MSD", 240 }, { "WAST", 480 }, { "WADT", 540 }, { "HKT", 480 },
	{ "CCT", 480 }, { "JST", 540 }, { "KST", 540 }, { "KDT", 540 },
	{ "CAST", 570 }, { "CADT", 630 }, { "EAST", 600 }, { "EADT", 660 },
	{ "NZST", 720 }, { "NZDT", 780 },
	{ NULL, 0 }
    };
    int i;

    for (i = 0; zones[i].name; ++i) {
	if (strlen(zones[i].name) == len && strncasecmp(p, zones[i].name, len) == 0) {
	    *minutes = zones[i].minutes;
	    return(1);
	}
    }
    *minutes = 0;
    return(0);
}

/* A zone name this file does not know is read as GMT. */
static int
pdZone(const char *p, size_t len)
{
    int minutes;

    pdZoneLookup(p, len, &minutes);
    return(minutes);
}

/* Gregorian days before the given year. year is positive. */
static long long
pdBeforeYear(int year)
{
    long long y = year - 1;
    return(365 * y + y / 4 - y / 100 + y / 400);
}

time_t
parsedate(char *date)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const char *p, *word;
    int day, mon, year, hour = 0, minute = 0, second = 0;
    int n, zone = 0, haveZone = 0, leap, i;
    long long value, ndays;
    struct tm local;

    if (date == NULL || (p = pdSpace(date)) == NULL)
	return((time_t)-1);
    if (strncasecmp(p, "Date:", 5) == 0 && (p = pdSpace(p + 5)) == NULL)
	return((time_t)-1);
    /* An optional weekday precedes the day, with or without a comma. */
    if (isalpha((unsigned char)*p)) {
	while (isalpha((unsigned char)*p))
	    ++p;
	if (*p == ',')
	    ++p;
	if ((p = pdSpace(p)) == NULL)
	    return((time_t)-1);
    }
    /*
     * ctime(3) and date(1): "Mon Jan  1 12:00:00 2001", with an optional
     * zone name before the year ("Mon Jan  1 12:00:00 UTC 2001").  The
     * month comes first; the weekday was skipped above.
     */
    if (isalpha((unsigned char)*p)) {
	word = p;
	while (isalpha((unsigned char)*p))
	    ++p;
	if ((mon = pdMonth(word, p - word)) < 0 ||
		(p = pdSpace(p)) == NULL ||
		!pdNumber(&p, 1, 2, &day) || (p = pdSpace(p)) == NULL ||
		!pdNumber(&p, 1, 2, &hour) || *p++ != ':' ||
		!pdNumber(&p, 1, 2, &minute))
	    return((time_t)-1);
	if (*p == ':') {
	    ++p;
	    if (!pdNumber(&p, 1, 2, &second))
		return((time_t)-1);
	}
	if ((p = pdSpace(p)) == NULL)
	    return((time_t)-1);
	if (isalpha((unsigned char)*p)) {
	    word = p;
	    while (isalpha((unsigned char)*p))
		++p;
	    /* ctime has no numeric zone: a name we do not know is a bad date */
	    if (!pdZoneLookup(word, p - word, &zone))
		return((time_t)-1);
	    haveZone = 1;
	    if ((p = pdSpace(p)) == NULL)
		return((time_t)-1);
	}
	if (pdNumber(&p, 4, 4, &year) != 4)
	    return((time_t)-1);
	goto validate;
    }
    if (!pdNumber(&p, 1, 2, &day) || (p = pdSpace(p)) == NULL)
	return((time_t)-1);
    /* RFC 850: "Monday, 01-Jan-01 12:00:00 GMT", hyphens between the fields. */
    if (*p == '-' && (p = pdSpace(p + 1)) == NULL)
	return((time_t)-1);
    word = p;
    while (isalpha((unsigned char)*p))
	++p;
    mon = pdMonth(word, p - word);
    if (*p == '.' || *p == '-')
	++p;
    if (mon < 0 || (p = pdSpace(p)) == NULL)
	return((time_t)-1);
    n = pdNumber(&p, 2, 4, &year);
    if ((n != 2 && n != 4) || (p = pdSpace(p)) == NULL)
	return((time_t)-1);
    if (n == 2)
	year += (year < 50) ? 2000 : 1900;
    /* Retain the existing date-only behavior: midnight in local time. */
    if (*p == '\0')
	goto validate;
    if (!pdNumber(&p, 1, 2, &hour) || *p++ != ':' ||
	    !pdNumber(&p, 1, 2, &minute))
	return((time_t)-1);
    if (*p == ':') {
	++p;
	if (!pdNumber(&p, 1, 2, &second))
	    return((time_t)-1);
    }
    if ((p = pdSpace(p)) == NULL)
	return((time_t)-1);
    if (isalpha((unsigned char)*p)) {
	word = p;
	while (isalpha((unsigned char)*p))
	    ++p;
	zone = pdZone(word, p - word);
	haveZone = 1;
	if ((p = pdSpace(p)) == NULL)
	    return((time_t)-1);
    }
    if (*p == '+' || *p == '-') {
	int sign = (*p++ == '-') ? -1 : 1;
	int v, zh, zm = 0;

	n = pdNumber(&p, 1, 4, &v);
	if (!n)
	    return((time_t)-1);
	if (n >= 3) {
	    zh = v / 100;
	    zm = v % 100;
	} else {
	    zh = v;
	    if (*p == ':') {
		++p;
		if (pdNumber(&p, 2, 2, &zm) != 2)
		    return((time_t)-1);
	    }
	}
	if (zh > 23 || zm > 59)
	    return((time_t)-1);
	zone = sign * (zh * 60 + zm);
	haveZone = 1;
    }
validate:
    leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < 1 || day < 1 || day > days[mon] + (mon == 1 && leap) ||
	    hour > 23 || minute > 59 || second > 60)
	return((time_t)-1);
    if (!haveZone) {
	memset(&local, 0, sizeof(local));
	local.tm_year = year - 1900;
	local.tm_mon = mon;
	local.tm_mday = day;
	local.tm_hour = hour;
	local.tm_min = minute;
	local.tm_sec = second;
	local.tm_isdst = -1;
	return(mktime(&local));
    }
    ndays = pdBeforeYear(year) - pdBeforeYear(1970) + day - 1;
    for (i = 0; i < mon; ++i)
	ndays += days[i] + (i == 1 && leap);
    value = ndays * 86400 + hour * 3600 + minute * 60 + second - zone * 60;
    if ((long long)(time_t)value != value)
	return((time_t)-1);
    return((time_t)value);
}
