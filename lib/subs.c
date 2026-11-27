
/*
 * LIB/SUBS.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype const char *PatExpand(const char *pat);
Prototype int AddrConfigFlag(const char *host);
Prototype char *ConfigGets(char *buf, int size, FILE *fi, int *tooLong);
Prototype const char *PatSubst(char *buf, size_t size, const char *pat, const char *dir);
Prototype void diablo_strlcpy(char *d, const char *s, int ssize, int dsize);
Prototype void NormalizeNewsgroups(char *ng);
Prototype int NewsgroupsSpaceInName(const char *ng);
Prototype void diablo_strlcpynl(char *d, const char *s, int ssize, int dsize);
Prototype void diablo_strlcpynl2(char *d, int trimwsafter, const char *s, int ssize, int dsize);
Prototype const char *PatExpand(const char *pat);
Prototype const char *PatLibExpand(const char *pat);
Prototype const char *PatDbExpand(const char *pat);
Prototype const char *PatLogExpand(const char *pat);
Prototype const char *PatSpoolExpand(const char *pat);
Prototype const char *PatRunExpand(const char *pat);
Prototype int CidrMatch(const char *cidr, const char *ip);
Prototype int IsIpAddr(char *a);
Prototype char *SanitiseAddr(char *addr);
Prototype int GetIPHostNoNameDefinite(void);
Prototype int GetIPHost(struct sockaddr *sa, char *hname, int hnamelen, char *ipname, int iplen);
Prototype int TestForwardLookup(const char *hostName, const char *ipst, const struct sockaddr *res);
Prototype char *NetAddrToSt(int fd, struct sockaddr *sa, int ip4conv, int port, int v6spec);
Prototype int ValidGroupName(const char *name);
Prototype void SanitizeString(char *name);
Prototype void SanitizeDescString(char *name);
Prototype char *StrnCpyNull(char *dst, const char *src, size_t maxlen);
Prototype const char *ftos(double d);
Prototype long bsizetol(char *p);
Prototype double bsizektod(char *p);
Prototype long btimetol(char *p);
Prototype char *dtlenstr(time_t t);
Prototype int MakeGroupDirectory(char *path);
Prototype char *strdupfree(char **st, char *val, char *empty);
Prototype char *safestr(char *st, char *noval);
Prototype int WriteAll(int fd, const void *buf, size_t bytes);
Prototype void WriteBestEffort(int fd, const void *buf, size_t bytes);
Prototype void NiceBestEffort(int inc);
Prototype int enabled(char *st);
Prototype HashFeed_MatchList *DiabHashFeedParse(MemPool **mp, char *hashconfig);
Prototype int DiabHashFeedBad(const HashFeed_MatchList *hf);
Prototype int MoveFile(char *from, char *to);
Prototype int TimeSpec(char *t, char *def);
Prototype int ParseIntRange(const char *str, int base, long min, long max, int *out);
Prototype int NntpCode(const char *line);

u_long ascToAddr(const char *str);
u_long ascToMask(const char *str);

void 
diablo_strlcpy(char *d, const char *s, int ssize, int dsize)
{
    while (ssize && dsize > 1 && *s) {
	*d = *s;
	--ssize;
	--dsize;
	++s;
	++d;
    }
    *d = 0;
}

void 
diablo_strlcpynl(char *d, const char *s, int ssize, int dsize)
{
    diablo_strlcpynl2(d, 0, s, ssize, dsize);
}

/*
 * NormalizeNewsgroups() - rewrite a Newsgroups: value in place as a plain
 * comma list: each run of commas, spaces, tabs, CRs and LFs becomes one
 * comma, with none at either end.  RFC 5536 3.1.4 allows white space only
 * around the commas; white space between two names is taken as a separator
 * too, so every name in the list is still seen (D-206).
 */
void
NormalizeNewsgroups(char *ng)
{
    char *d = ng;
    const char *s;
    int sep = 0;

    for (s = ng; *s; ++s) {
	if (*s == ',' || *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
	    sep = 1;
	    continue;
	}
	if (sep && d != ng)
	    *d++ = ',';
	sep = 0;
	*d++ = *s;
    }
    *d = 0;
}

/*
 * NewsgroupsSpaceInName() - 1 if a Newsgroups: value has white space
 * between two names, not next to a comma ("a b,c"); RFC 5536 3.1.4
 * allows white space only around the commas.
 */
int
NewsgroupsSpaceInName(const char *ng)
{
    const char *p;
    int prev = 0;	/* the last character that is not white space */

    for (p = ng; *p; ++p) {
	if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
	    const char *q = p;

	    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
		++q;
	    if (prev != 0 && prev != ',' && *q != 0 && *q != ',')
		return(1);
	    p = q - 1;
	    continue;
	}
	prev = *p;
    }
    return(0);
}

void 
diablo_strlcpynl2(char *d, int trimwsafter, const char *s, int ssize, int dsize)
{
    char *dold = d;
    int trimws = 0;

    while (ssize && *s == ' ') {
	++s;
	--ssize;
    }

    for (; ssize > 0 && dsize > 1 && *s; s++, ssize--) {
	if (*s == '\r' || *s == '\n')
	   continue;
	if (trimws && (*s == ' ' || *s == '\t'))
	   continue;
	trimws = (*s == trimwsafter);
	*d = *s;
	--dsize;
	++d;
    }
    *d = 0;

    /*
     * remove trailing tabs and spaces
     */
    while (d != dold && (d[-1] == '\t' || d[-1] == ' ')) {
	--d;
	*d = 0;
    }
}

/*
 * These hold the expanded path_* settings.  They were 128 bytes, and
 * PatSubst() truncates to fit, so a longer path silently named a different
 * file (a dumphist command cut mid-path, for one).  Callers that copy the
 * result into a buffer of their own bound that copy.
 */
static char PatPath1[PATH_MAX];
static char PatPath2[PATH_MAX];

/*
 * PatSubst() - expand a path pattern from diablo.config (path_*), the
 *		command line or dreader.access ("%s/dnewsfeeds"): the first
 *		%s becomes dir, %% a single %, and anything else, including
 *		any other % sequence, is copied as it is.  The pattern is
 *		never used as a printf format, so a stray %d or %n in a
 *		configured path can't read or write memory.
 */
const char *
PatSubst(char *buf, size_t size, const char *pat, const char *dir)
{
    size_t n = 0;
    int subst = 0;

    if (size == 0)
	return(buf);
    for (; *pat && n + 1 < size; ++pat) {
	if (pat[0] == '%' && pat[1] == 's' && !subst) {
	    const char *d;

	    for (d = dir ? dir : ""; *d && n + 1 < size; ++d)
		buf[n++] = *d;
	    subst = 1;
	    ++pat;
	} else if (pat[0] == '%' && pat[1] == '%') {
	    buf[n++] = '%';
	    ++pat;
	} else {
	    buf[n++] = *pat;
	}
    }
    buf[n] = 0;
    return(buf);
}

const char *
PatExpand(const char *pat)
{
    return(PatSubst(PatPath1, sizeof(PatPath1), pat, NewsHome));
}


const char *
PatLibExpand(const char *pat)
{
    return(PatSubst(PatPath2, sizeof(PatPath2), pat, PatExpand(LibHomePat)));
}


const char *
PatDbExpand(const char *pat)
{
    return(PatSubst(PatPath2, sizeof(PatPath2), pat, PatExpand(DbHomePat)));
}

const char *
PatLogExpand(const char *pat)
{
    return(PatSubst(PatPath2, sizeof(PatPath2), pat, PatExpand(LogHomePat)));
}

const char *
PatSpoolExpand(const char *pat)
{
    return(PatSubst(PatPath2, sizeof(PatPath2), pat, PatExpand(SpoolHomePat)));
}

const char *
PatRunExpand(const char *pat)
{
    return(PatSubst(PatPath2, sizeof(PatPath2), pat, PatExpand(RunHomePat)));
}

int 
IsIpAddr(char *a)
{
    const char *spanstr;

#ifdef INET6
    if (strchr(a, ':') != NULL)
	spanstr = "0123456789abcdefgh.:/*?[]";
    else
#endif
	spanstr = "0123456789./*?";
    if (strspn(a, spanstr) == strlen(a))
	return(1);
    else
	return(0);
}

/*
 * Sanitise an IP[v6] address by removing the '[]'
 */
char *
SanitiseAddr(char *addr)
{
    static char st[64];
    char *p;
    int len;

    if (addr == NULL)
	return(NULL);
    if (*addr == '[')
	addr++;
    len = strlen(addr);
    if ((p = strchr(addr, ']')) != NULL)
	len = p - addr;
    if (len >= sizeof(st))
	len = sizeof(st) - 1;
    strncpy(st, addr, len);
    st[len] = '\0';
    return(st);
}

u_long
ascToAddr(const char *str)
{
    char string[32];
    char *ptr;
    u_long i, a, b, c, d;

    snprintf(string, sizeof(string), "%s", str);
    if ((ptr = strchr(string, '/')))
	*ptr = '\0';

    /* Count the dots. */
    ptr = string;
    i = 0;
    while (*ptr)
	if (*ptr++ == '.')
	    i++;

    if (i == 0) {
	sscanf(string, "%ld", &a);
	return(a << 24);
    } else if (i == 1) {
	sscanf(string, "%ld.%ld", &a, &b);
	return((a << 24) + (b << 16));
    } else if (i == 2) {
	sscanf(string, "%ld.%ld.%ld", &a, &b, &c);
	return((a << 24) + (b << 16) + (c << 8));
    } else {
	sscanf(string, "%ld.%ld.%ld.%ld", &a, &b, &c, &d);
	return((a << 24) + (b << 16) + (c << 8) + d);
    }
}

u_long
ascToMask(const char *str)
{
    const char *ptr;
    u_long i;

    if ((ptr = strchr(str, '/')))
	ptr++;
    else
	return(0xffffffff);

    /* Accept either CIDR /nn or /xxx.xxx.xxx.xxx */
    if (strstr(ptr, ".")) {
	return(ntohl(inet_addr(ptr)));
    } else {
	i = atoi(ptr);
	if (!i)
	    return(0);
	else
	    return((u_long) 0xffffffff << (32 - i));
    }
}

/*
 * cidrBadOnce() - 1 the first time this process sees the bad pattern cidr.
 *		    CidrMatch() runs for every client and every rule: a bad
 *		    pattern is logged once, not at each match.  The first 32
 *		    patterns are remembered; past that each one is logged.
 */
static int
cidrBadOnce(const char *cidr)
{
    static char seen[32][64];
    static int nseen;
    int i;

    for (i = 0; i < nseen; ++i) {
	if (strncmp(seen[i], cidr, sizeof(seen[i]) - 1) == 0 &&
					strlen(cidr) < sizeof(seen[i]))
	    return(0);
    }
    if (nseen < 32 && strlen(cidr) < sizeof(seen[0]))
	snprintf(seen[nseen++], sizeof(seen[0]), "%s", cidr);
    return(1);
}

/*
 * cidrMaskBits() - the prefix length of "addr/nn": 0..max, max when there
 *		    is no "/nn", -1 when nn is not a number in that range.
 *
 *	atoi() made "/abc" and "/" a /0, an IPv4 "/33" shifted by a negative
 *	count and an IPv6 "/256" wrapped to 0 in a uint8: each of these
 *	matched every address.
 */
static int
cidrMaskBits(const char *cidr, int max)
{
    const char *ptr;
    int n = 0;

    if ((ptr = strchr(cidr, '/')) == NULL)
	return(max);
    if (*++ptr == 0)
	return(-1);
    for (; *ptr; ++ptr) {
	if (*ptr < '0' || *ptr > '9')
	    return(-1);
	if ((n = n * 10 + (*ptr - '0')) > max)
	    return(-1);
    }
    return(n);
}

/*
 * Match CIDR address - return 1 for sucess else 0
 *
 * This is really messy
 */
int
CidrMatch(const char *cidr, const char *ip)
{
#ifdef INET6
    char addr[256];
    char mask[256];
    int maskbits;
    char maskst[NI_MAXHOST];
    char *p;
    int i;

    if (strchr(ip, ':') != NULL) {
	uint8 b8;
	uint8 b;

	if (strchr(cidr, ':') == NULL)	/* Don't compare against non v6 CIDR */
	    return(0);
	strcpy(maskst, cidr);
	if ((p = strchr(maskst, '/')) != NULL)
	    *p = '\0';
	if ((maskbits = cidrMaskBits(cidr, 128)) < 0) {
	    if (cidrBadOnce(cidr))
		logit(LOG_ERR, "Invalid IPv6 mask: %s", cidr);
	    return(0);
	}
	if (inet_pton(AF_INET6, ip, addr) != 1) {
	    logit(LOG_ERR, "Invalid IPv6 address: %s", ip);
	    return(0);
	}
	if (inet_pton(AF_INET6, maskst, mask) != 1) {
	    logit(LOG_ERR, "Invalid IPv6 address: %s", cidr);
	    return(0);
	}
	
	for (i = 0; i * 8 < maskbits; i++) {
	    if (i < maskbits / 8)
		b8 = 255;
	    else
		for (b8 = 0, b = 0; b < maskbits % 8; b++)
		    b8 |= (1 << (7 - b));
	    if ((addr[i] & b8) != (mask[i] & b8))
		return(0);
	}
	return(1);
    } else
#endif
    {
	u_long net;
	u_long mask;
	u_long addr;
	const char *mp;
	int bad;

	if (strchr(cidr, ':') != NULL)	/* Don't compare against v6 CIDR */
	    return(0);
	/*
	 * A mask that is no mask matches nothing.  The /xxx.xxx.xxx.xxx
	 * form is converted by ascToMask() with inet_addr(); inet_aton()
	 * takes the same spellings (octal, shortened forms) and says
	 * whether it could, which inet_addr() cannot for an all-ones mask.
	 */
	if ((mp = strchr(cidr, '/')) != NULL && strchr(mp, '.') != NULL) {
	    struct in_addr ia;

	    bad = (inet_aton(mp + 1, &ia) == 0);
	} else
	    bad = (cidrMaskBits(cidr, 32) < 0);
	if (bad) {
	    if (cidrBadOnce(cidr))
		logit(LOG_ERR, "Invalid IPv4 mask: %s", cidr);
	    return(0);
	}
	addr = ascToAddr(ip);
	net = ascToAddr(cidr);
	mask = ascToMask(cidr);
	if ((addr & mask) != (net & mask))
	    return(0);
	return(1);
    }
}

/*
 * Returns 0 with the name in hname, -1 if there is definitely no name, -2 if
 * the lookup failed or may have failed temporarily, with hname empty.
 *
 * Only glibc's and illumos/Solaris' getnameinfo tell "no PTR record"
 * (EAI_NONAME) from a timeout or SERVFAIL (EAI_AGAIN).  The other libcs
 * (FreeBSD, the other BSDs, macOS, musl, uClibc) return EAI_NONAME for both,
 * so there EAI_NONAME counts as -2 as well.
 */
#if (defined(__GLIBC__) && !defined(__UCLIBC__)) || defined(__sun)
#define GNI_NONAME_IS_DEFINITE	1
#endif

/* 1 if GetIPHost can return -1 (a definite "no name") on this system */
int
GetIPHostNoNameDefinite(void)
{
#ifdef GNI_NONAME_IS_DEFINITE
    return(1);
#else
    return(0);
#endif
}

int
GetIPHost(struct sockaddr *sa, char *hname, int hnamelen, char *ipname, int iplen)
{
    strncpy(ipname, NetAddrToSt(0, sa, 1, 0, 0), iplen - 1);
    ipname[iplen - 1] = '\0';

    *hname = '\0';
    {
#ifdef INET6
	int error;

	if ((error = getnameinfo(sa, SA_LEN(sa), hname, hnamelen,
					NULL, 0, NI_NAMEREQD)) != 0) {
		*hname = '\0';
#ifdef GNI_NONAME_IS_DEFINITE
		if (error == EAI_NONAME)
		    return(-1);
#endif
		return(-2);
	}
#else
	struct hostent *he;
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;

	he = gethostbyaddr(
	    (char *)&sin->sin_addr,
	    sizeof(sin->sin_addr),
	    AF_INET
	);

	if (he != NULL) {
	    strncpy(hname, he->h_name, hnamelen - 1);
	    hname[hnamelen - 1] = '\0';
	} else {
	    return((h_errno == HOST_NOT_FOUND || h_errno == NO_DATA) ? -1 : -2);
	}
#endif /* INET6 */
    }
    return((*hname) ? 0 : -1);
}

/*
 * TestForwardLookup(). 
 *
 * Check that Fwd and Rev DNS entries match up.  Returns 0 if they do, -1 if
 * not (no such name, or no matching address), -2 if the forward lookup
 * failed otherwise (timeout, SERVFAIL, system error).
 */

int
TestForwardLookup(const char *hostName, const char *ipst, const struct sockaddr *sa)
{
#ifdef INET6
    struct addrinfo *ai0;
    struct addrinfo hints;
    int r = -1;

    stprintf("forward auth %s", hostName);

    /* Only do lookups for the family in ipst */
    memset(&hints, 0, sizeof(hints));
    if (strchr(ipst, ':'))
	hints.ai_family = AF_INET6;
    else if (strchr(ipst, '.'))
	hints.ai_family = AF_INET;

    if ((r = getaddrinfo(hostName, NULL, &hints, &ai0)) == 0) {
	struct addrinfo *ai;

	r = -1;

	for (ai = ai0; ai; ai = ai->ai_next) {
	    char *st = NetAddrToSt(0, ai->ai_addr, 1, 0, 0);
	    if (st != NULL && strcmp(ipst, st) == 0) {
		r = 0;
		break;
	    }
	}
	if (ai == NULL)
	    logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: %s/%s", hostName, ipst);
	freeaddrinfo(ai0);
    } else {
	logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: lookup of %s failed", hostName);
	/* only "no such name" is a definite answer */
#ifdef EAI_NODATA
	if (r == EAI_NODATA)
	    r = EAI_NONAME;
#endif
	r = (r == EAI_NONAME) ? -1 : -2;
    }
    return(r);
#else
    struct hostent *he;
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    int r = -1;

    if ((he = gethostbyname(hostName)) != NULL) {
	int i;

	for (i = 0; he->h_addr_list && he->h_addr_list[i]; ++i) {
	    const struct in_addr *haddr = (const void *)he->h_addr_list[i];
	    if (sin->sin_addr.s_addr == haddr->s_addr) {
		r = 0;
		break;
	    }
	}
	if (he->h_addr_list[i] == NULL)
	    logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: %s/%s", hostName, ipst);
    } else {
	logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: lookup of %s failed", hostName);
	if (h_errno != HOST_NOT_FOUND && h_errno != NO_DATA)
	    r = -2;
    }
    return(r);
#endif
}

int 
ValidGroupName(const char *name)
{
    int r = 0;

    if (name[0] == 0 || name[0] == '.') {
	r = -1;
    } else if (strlen(name) >= MAXGNAME) {
	/* longer names are skipped by LIST ACTIVE and can never get articles */
	r = -1;
    } else {
	for (; *name; ++name) {
	    if (*name == ' ' || *name == '/' || !isprint((unsigned char)*name)) {
		r = -1;
	    }
	    if (*name >= 'a' && *name <= 'z')
		continue;
	    if (*name >= 'A' && *name <= 'Z')
		continue;
	    if (*name >= '0' && *name <= '9')
		continue;
	    if (*name == '+')
		continue;
	    if (*name == '-')
		continue;
	    if (*name == '.') {
		if (name[-1] == '.' || name[1] == '.' || name[1] == 0)
		    r = -1;
	    }
	}
    }
    return(r);
}

void
SanitizeString(char *name)
{
    for (; *name; ++name) {
	if (*name >= 'a' && *name <= 'z')
	    continue;
	if (*name >= 'A' && *name <= 'Z')
	    continue;
	if (*name >= '0' && *name <= '9')
	    continue;
	if (*name == '\t')
	    *name = ' ';
	if (*name == '@' ||
	    *name == '-' ||
	    *name == '+' ||
	    *name == '_' ||
	    *name == '.' ||
	    *name == ',' ||
	    *name == ' '
	) {
	    continue;
	}
	*name = '?';
    }
}

/*
 * Sanitise a group description
 */
void
SanitizeDescString(char *name)
{
    for (; *name; ++name) {
	if (*name == '\t')
	    *name = ' ';
	if (!iscntrl((unsigned char)*name))
	    continue;
	*name = '?';
    }
}

/*
 * StrnCpyNull()
 *
 * Like strcpy, but gracefully handles null pointers and limits the length
 * of the string copied.
 */
char *
StrnCpyNull(char *dst, const char *src, size_t maxlen)
{
    if (dst) {
    	if (src) {
	    if (strlen(src) >= maxlen) {
		strncpy(dst, src, maxlen - 1);
		dst[maxlen - 1] = '\0';
	    }
	    else {
		strcpy(dst, src);
	    }
    	}
    	else {
	    *dst = '\0';
    	}
    }
    return dst;
}

#define ONE_K	1024.0	
#define ONE_M	(1024.0*1024.0)
#define ONE_G	(1024.0*1024.0*1024.0)
#define ONE_T	(1024.0*1024.0*1024.0*1024.0)

const char *
ftos(double d)
{
    static char FBuf[8][32];
    static int FCnt;
    char *p = FBuf[FCnt];

    if (d < 1024.0) {
	sprintf(p, "%d", (int)d);
    } else if (d < ONE_M) {
	sprintf(p, "%d.%03dK", (int)(d / ONE_K), ((int)d % (int)ONE_K) * 1000 / (int)ONE_K);
    } else if (d < ONE_G) {
	sprintf(p, "%d.%03dM", (int)(d / ONE_M), ((int)(d / ONE_K) % (int)ONE_K) * 1000 / (int)ONE_K);
    } else if (d < ONE_T) {
	sprintf(p, "%d.%03dG", (int)(d / ONE_G), ((int)(d / ONE_M) % (int)ONE_K) * 1000 / (int)ONE_K);
    } else {
	sprintf(p, "%d.%03dT", (int)(d / ONE_T), ((int)(d / ONE_G) % (int)(d / ONE_M) % (int)ONE_K) * 1000 / (int)ONE_K);
    }

    FCnt = (FCnt + 1) & 7;
    return(p);
}

/*
 * Convert a string value to a number of bytes. The string can be
 * specified in kb, mb or gb
 */
long
bsizetol(char *p)
{
    long n;

    n = strtol(p, &p, 0);
    switch(*p) {
    case 'g':
    case 'G':
	n *= 1024;
	/* fall through */
    case 'm':
    case 'M':
	n *= 1024;
	/* fall through */
    case 'k':
    case 'K':
	n *= 1024;
	break;
    }
    return(n);
}

/*
 * Convert a size string to a number of bytes. A plain number is in
 * kilobytes; the suffixes k, m, g and t (either case) mean kilo-, mega-,
 * giga- and terabytes.
 */
double
bsizektod(char *p)
{
    double n;

    n = strtol(p, &p, 0) * 1024.0;
    switch(*p) {
    case 't':
    case 'T':
	n *= 1024.0;
	/* fall through */
    case 'g':
    case 'G':
	n *= 1024.0;
	/* fall through */
    case 'm':
    case 'M':
	n *= 1024.0;
	break;
    }
    return(n);
}

/*
 * Convert a time specification in days, hours, mins and/or secs to a long
 * representing the number of secs. The specification can include
 * multiple time specifications.
 */
long
btimetol(char *p)
{
    long n;
    long res = 0;

    while (*p) {
	while (*p && !isdigit((unsigned char)*p))
	    p++;
	if (*p == '\0')
	    break;
	n = strtol(p, &p, 0);
	while (*p && isdigit((unsigned char)*p))
	    p++;
	if (*p == '\0') {
	    res += n;
	    break;
	}
	switch(*p) {
	    case 'd':
	    case 'D':
		n *= 24;
		/* fall through */
	    case 'h':
	    case 'H':
		n *= 60;
		/* fall through */
	    case 'm':
	    case 'M':
		n *= 60;
		break;
	    case 's':
	    case 'S':
	    case ' ':
		break;
	}
	p++;
	res += n;
    }
    return(res);
}

char *
dtlenstr(time_t t)
{
    int d = 0;
    int h = 0;
    int m = 0;
    static char tb[64];

    if (t > 24 * 60 * 60) {
	d = t / (24 * 60 * 60);
	t = t % (24 * 60 * 60);
    }
    if (t > 60 * 60) {
	h = t / (60 * 60);
	t = t % (60 * 60);
    }
    if (t > 60) {
	m = t / 60;
	t = t % 60;
    }
    sprintf(tb, "%d days %d hrs %d min %d sec", d, h, m, (int)t);
    return(tb);
}

#ifdef INET6
/*
 * A protocol independent conversion of network address to string
 *
 * Strips the initial portion of a IPv6 encoded IPv4 address if
 * requested
 *
 * If sa == NULL then the address is obtained from the fd passed
 * otherwise the fd is ignored.
 */
char *
NetAddrToSt(int fd, struct sockaddr *sa, int ip4conv, int port, int v6spec)
{
    static char st[NI_MAXHOST + 8];
    char addrst[NI_MAXHOST];
    char serv[16];
    int error;

    if (sa == NULL) {
	static struct sockaddr_storage res;
	socklen_t salen = sizeof(res);

	sa = (struct sockaddr *)&res;
	if (getpeername(fd, sa, &salen) != 0) {
	    logit(LOG_ERR, "%s", strerror(errno));
	    return(NULL);
	}
    }
    if (SA_LEN(sa) == 0)
	return(NULL);
    if ((error = getnameinfo(sa, SA_LEN(sa), addrst, sizeof(addrst),
					serv, sizeof(serv),
					NI_NUMERICHOST|NI_NUMERICSERV)) == 0) {
	if (v6spec && strchr(addrst, ':')) {
	    snprintf(st, sizeof(st), "[%s]", addrst);
	} else {
	    strncpy(st, addrst, sizeof(st) - 1);
	    st[sizeof(st) - 1] = '\0';
	}
	if (port) {
	    strncat(st, ":", sizeof(st) - 1);
	    strncat(st, serv, sizeof(st) - 1);
	}
	if (ip4conv && strncmp(st, "::ffff:", 7) == 0)
            return(st + 7);
	else
            return(st);
    } else {
            logit(LOG_ERR, "getnameinfo: %s\n", gai_strerror(error));
    }
    return(NULL);
}
#else
char *
NetAddrToSt(int fd, struct sockaddr *sa, int ip4conv, int port, int v6spec)
{
    static char st[128];
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;

    strncpy(st, inet_ntoa(sin->sin_addr), sizeof(st) - 1);
    st[sizeof(st) - 1] = '\0';
    if (port) {
	char port[16];
	snprintf(port, sizeof(port), ":%d", ntohs(sin->sin_port));
	strncat(st, port, sizeof(st) - 1);
    }
    return(st);
}

#endif

/*
 * Create the directories required for the file pathname specified in path
 *   path = the full path/filename which gets clobbered.
 */
int
MakeGroupDirectory(char *path)
{
    char dir[PATH_MAX];
    char *p;
    struct stat st;

    if (strlen(path) >= sizeof(dir)) {
	errno = ENAMETOOLONG;
	return(-1);
    }
    strcpy(dir, path);
    if (strchr(dir, '/') == NULL)
	return(0);
    if ((p = strrchr(dir, '/')) != NULL)
	*p = '\0';
    if (stat(dir, &st) == 0)
	return(0);
    if (MakeGroupDirectory(dir) == -1)
	return(-1);
    /*
     * Another process may have created the directory between the stat()
     * and here (two feed processes storing into the same new group
     * directory): that is what we wanted, not an error.
     */
    if (mkdir(dir, 0755) == -1 && errno != EEXIST)
	return(-1);
    return(0);
}

/*
 * If the string exists, free it
 * strdup the value
 * if value is "0", then set string to NULL
 */
char *
strdupfree(char **st, char *val, char *empty)
{
    if (*st != NULL)
	free(*st);
    if (val == NULL || strcmp(val, "0") == 0)
	*st = (empty == NULL) ? NULL : strdup(empty);
    else
	*st = strdup(val);
    return(*st);
}

/*
 * Return a string that isn't NULL
 */
char *
safestr(char *st, char *noval)
{
    if (st == NULL)
	return((noval != NULL) ? noval : "NONE");
    return(st);
}

int
enabled(char *st)
{
    if (st == NULL || !*st || *st == 'y' || *st == 'Y' ||
				strcasecmp(st, "on") == 0 || *st == '1')
	return(1);
    else
	return(0);
}

/*
 * DiabHashFeedBad() - the list DiabHashFeedParse() returns for a hashfeed
 *		it could not read (one entry that matches nothing), so that
 *		the caller can name its file and line.
 */
int
DiabHashFeedBad(const HashFeed_MatchList *hf)
{
    return(hf != NULL && hf->HM_Next == NULL && hf->HM_Type == HMTYPE_MD5 &&
	hf->HM_ModVal == 1 && hf->HM_Start == 2 && hf->HM_End == 2);
}

HashFeed_MatchList *
DiabHashFeedParse(MemPool **pool, char *hashconfig)
{
    char *ptr, sep;
    HashFeed_MatchList *new, *ret = NULL;
    HashFeed_MatchList **cfgnext = &ret;
    char cascade = 0;

    while (hashconfig && *hashconfig) {

	if ((ptr = strpbrk(hashconfig, ",|&"))) {
	    sep = *ptr;
	    *ptr = '\0';
	} else {
	    sep = 0;
	}

	if (! ((new = zalloc(pool, sizeof(HashFeed_MatchList))))) {
	    return(NULL);
	}
	int offloc = cascade ? 4 : 0;
	if (HM_ConfigNode_Sub(new, NULL, hashconfig, offloc) == NULL) {
	    /*
	     * No range, modulus 0 or missing ("1-2"), an offset outside
	     * 0..12, junk after a number: such an entry divided by zero
	     * when the first article was matched.  Without it the hashfeed
	     * would restrict nothing (all callers take NULL for "no
	     * hashfeed"), so a typo would feed a peer everything.  The
	     * whole hashfeed matches no article instead, as a bad address
	     * mask does.
	     */
	    logit(LOG_ERR, "bad hashfeed entry \"%s\": the hashfeed matches no article",
								hashconfig);
	    if (ptr)
		*ptr = sep;
	    bzero(new, sizeof(*new));
	    new->HM_Type = HMTYPE_MD5;
	    new->HM_ModVal = 1;		/* hash % 1 + 1 is 1, never in 2..2 */
	    new->HM_Start = 2;
	    new->HM_End = 2;
	    return(new);
	}
	new->HM_Cascade = cascade;
	*cfgnext = new;
	cfgnext = &(new->HM_Next);

	if (sep == '|' || sep == '&')
	    cascade = 1;
	else
	    cascade = 0;
		
	if (ptr)
	    *ptr++ = sep;
        hashconfig = ptr;
    }
    return ret;
}

int
MoveFile(char *from, char *to)
{
    int from_fd;
    int to_fd;
    static char buffer[32768];
    int rlen;
    struct stat from_st;
    struct stat to_st;

    if ((from_fd = open(from, O_RDONLY, 0)) < 0) {
	printf("ERROR: Unable to open input file: %s\n", from);
	return(-1);
    }
    /*
     * The file is already at its new name (#519 r2): the two names are one
     * path, or one file (the same inode: two methods that give a group the
     * same path, such as md5-32:30\1 and md5-32:40\1, or a hard link).  As
     * rename() does then, nothing is done and the move succeeds; unlinking
     * the destination or truncating it would remove the only copy.  A
     * symlink at the name is not this file (lstat()), it is replaced below.
     */
    if (strcmp(from, to) == 0 ||
	(fstat(from_fd, &from_st) == 0 && lstat(to, &to_st) == 0 &&
	 from_st.st_dev == to_st.st_dev && from_st.st_ino == to_st.st_ino)) {
	close(from_fd);
	return(0);
    }
    /*
     * The output file is created anew (D-332): whatever is at its name, a
     * file of an interrupted run or a symlink, is unlinked first, and the
     * open uses O_EXCL, so a symlink there is not followed and its target,
     * wherever it points, is not truncated or written.  Unlike O_TRUNC on
     * a file already there, the new file has mode 0644 (less the umask) and
     * the caller as owner, a hard link at the name is left with the old
     * contents, and the directory must be writable.  As createTempFile()
     * in dexpireover.
     */
    if ((unlink(to) < 0 && errno != ENOENT) ||
	(to_fd = open(to, O_CREAT | O_EXCL | O_WRONLY, 0644)) < 0) {
	printf("ERROR: Unable to create output file: %s\n", to);
	close(from_fd);
	return(-1);
    }
    while ((rlen = read(from_fd, buffer, 32768)) > 0)
	if (write(to_fd, buffer, rlen) != rlen) {
	    printf("ERROR: Unable to writing to file: %s\n", to);
	    close(from_fd);
	    close(to_fd);
	    unlink(to);
	    return(-1);
	}
    if (rlen < 0) {
	printf("ERROR: Unable to read from file: %s\n", from);
	close(from_fd);
	close(to_fd);
	unlink(to);
	return(-1);
    }
    close(from_fd);
    if (close(to_fd) != 0) {
	printf("ERROR: Unable to close file: %s\n", to);
	unlink(to);
	return(-1);
    }
    if (unlink(from) != 0) {
	printf("ERROR: Unable to remove input file: %s\n", from);
	return(-1);
    }
    return(0);
}

/*
 * Convert a string time specification in days, hours, minutes or seconds
 * into a integer of seconds.
 */
int
TimeSpec(char *t, char *def)
{
    char *endp;
    int tt;

    tt = strtol(t, &endp, 0);
    if (endp == NULL || !*endp)
	endp = def;
    switch (*endp) {
	case 's':
	    break;
	case 'm':
	    tt *= 60;
	    break;
	case 'h':
	    tt *= 60 * 60;
	    break;
	case 'd':
	    tt *= 24 * 60 * 60;
	    break;
	default:
	    fprintf(stderr, "Invalid time specification: %s \n", t);
	    tt = -1;
    }
    return(tt);
}

/*
 * WriteAll() - write all of buf, retrying after a short write or EINTR.
 *		Returns 0, or -1 with errno set.
 */
int
WriteAll(int fd, const void *buf, size_t bytes)
{
    const char *p = buf;

    while (bytes > 0) {
	ssize_t n = write(fd, p, bytes);

	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    return(-1);
	}
	if (n == 0) {
	    errno = EIO;
	    return(-1);
	}
	p += n;
	bytes -= n;
    }
    return(0);
}

/*
 * WriteBestEffort() - for messages whose loss changes nothing, such as
 *		an error line to a client that is disconnected next.
 */
void
WriteBestEffort(int fd, const void *buf, size_t bytes)
{
    if (WriteAll(fd, buf, bytes) < 0)
	return;
}

/*
 * NiceBestEffort() - change the priority; a refusal (lowering it needs
 *		root) is not an error for the callers.  Safe in a signal
 *		handler.
 */
void
NiceBestEffort(int inc)
{
    if (nice(inc) == -1)
	return;
}

/*
 * ParseIntRange() - a number that is all of str (white space around it is
 * allowed), between min and max.  Returns 0 and sets *out, or -1 and leaves
 * *out alone for an empty or non-numeric string, trailing junk, overflow or
 * a value out of range.
 */
int
ParseIntRange(const char *str, int base, long min, long max, int *out)
{
    char *end;
    long v;

    while (isspace((unsigned char)*str))
	str++;
    if (*str == 0)
	return(-1);
    errno = 0;
    v = strtol(str, &end, base);
    if (errno != 0 || end == str)
	return(-1);
    while (isspace((unsigned char)*end))
	end++;
    if (*end != 0 || v < min || v > max)
	return(-1);
    *out = (int)v;
    return(0);
}

/*
 * ConfigGets() - fgets() for configuration files.  A line longer than the
 * buffer is not split into two lines: the rest of it is read and dropped
 * and *tooLong is set, so the caller can skip a long comment or report the
 * line instead of parsing its tail as a line of its own.  A line that just
 * fits (its newline is the next character), or whose dropped rest is only
 * white space and a comment (a # after white space), is not too long.
 */
char *
ConfigGets(char *buf, int size, FILE *fi, int *tooLong)
{
    *tooLong = 0;
    if (fgets(buf, size, fi) == NULL)
	return(NULL);
    if (strchr(buf, '\n') == NULL) {
	int c = getc(fi);
	int comment = 0;
	/* a # starts a comment after white space, not inside a word */
	size_t n = strlen(buf);
	int prevSpace = n > 0 && isspace((unsigned char)buf[n - 1]);

	for (; c != EOF && c != '\n'; c = getc(fi)) {
	    if (!comment) {
		if (c == '#' && prevSpace)
		    comment = 1;
		else if (!isspace(c))
		    *tooLong = 1;
	    }
	    prevSpace = isspace(c);
	}
    }
    return(buf);
}

/*
 * AddrConfigFlag() - the AI_ADDRCONFIG getaddrinfo() hint for host, or 0
 *		      for a numeric address.  glibc 2.39 (EL10) applies
 *		      AI_ADDRCONFIG to numeric addresses too and refuses
 *		      "::1" on a machine without a global IPv6 address
 *		      (EAI_ADDRFAMILY, D-87/D-89); for a name or the
 *		      wildcard it still keeps unusable families out.
 */
int
AddrConfigFlag(const char *host)
{
#ifdef AI_ADDRCONFIG
    unsigned char buf[sizeof(struct in6_addr)];

    if (host != NULL && (inet_pton(AF_INET, host, buf) == 1
#ifdef AF_INET6
			 || inet_pton(AF_INET6, host, buf) == 1
#endif
	))
	return(0);
    return(AI_ADDRCONFIG);
#else
    return(0);
#endif
}

/*
 * NntpCode() - the reply code at the start of an NNTP reply line, or -1.
 *		Exactly three digits, the first one 1 to 5, then a space
 *		or the end of the line (CR LF, LF or the end of the string;
 *		a CR with text after it is no end).  No white space or sign
 *		in front, no more digits, no tab, no text glued to the code.
 *		The caller compares the whole number with the codes its
 *		command defines; any other code is an error, not a class.
 */
int
NntpCode(const char *line)
{
    if (line == NULL)
	return(-1);
    if (line[0] < '1' || line[0] > '5')
	return(-1);
    if (line[1] < '0' || line[1] > '9' || line[2] < '0' || line[2] > '9')
	return(-1);
    /* a space, or the end of the line (CR LF, LF, or the end of the string) */
    if (line[3] != ' ' && line[3] != 0 && line[3] != '\n' &&
	!(line[3] == '\r' && (line[4] == 0 || line[4] == '\n')))
	return(-1);
    return((line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0'));
}
