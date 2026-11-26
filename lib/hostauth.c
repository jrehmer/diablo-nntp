
/*
 * LIB/HOSTAUTH.C
 *
 * (c)Copyright 1997-1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int LoadHostAccess(time_t t, int force, int rebuildtime);
Prototype char *Authenticate(int fd, struct sockaddr *res, char *ipst, char *label);
Prototype void DumpHostCache(const char *fname);

#define	TESTTYPE_NONE	0
#define	TESTTYPE_ANY	1
#define	TESTTYPE_IP	2
#define	TESTTYPE_FQDN	4

typedef struct HostAccess {
    char		entry[256];
    int			testtype;
    char 		addrst[NI_MAXHOST];
    char		label[256];
} HostAccess;

time_t HATDSec = 0;
time_t HAMTime = 0;
time_t HACMTime = 0;
time_t DNMTime = 0;

/*
 * The cache being replaced, open for reading in the rebuild only: the
 * addresses of a name that does not resolve for now are taken from it.
 */
static FILE *HAOldCache = NULL;

/*
 * The (label, entry) pairs that got records in this rebuild, from a lookup
 * or kept from the old cache: a later line of the same pair whose lookup
 * fails writes nothing (no old address next to fresh ones, none twice).
 */
typedef struct HAKept {
    struct HAKept	*next;
    char		label[256];
    char		entry[256];
} HAKept;

static HAKept *HAKeptList = NULL;

int convHost(char *host, HostAccess *HA, FILE *fo);
static int keepOldHost(const char *host, HostAccess *HA, FILE *fo, const char *why);
static int haWritten(const HostAccess *HA);
static void haTerminate(HostAccess *HA);
void loadDiabloHosts(FILE *fo, int t, int *count);
void loadDnewsFeeds(FILE *fo, int t, int *count);

/*
 * LoadAccess() - [re]load diablo.hosts file
 *
 * This load is to create a cache file of the diablo.hosts file
 * that contains the IP addresses of all the hosts found. This
 * makes authentication a lot quicker and easier.
 *
 * If a time of '0' is passed, we don't fork and don't use syslog
 * 
 */

int
LoadHostAccess(time_t t, int force, int rebuildtime)
{
    struct stat st = { 0 };
    pid_t pid;

    /*
     * check for diablo.hosts file modified once a minute
     */

    if (!force  && (t / 10 <= HATDSec))
	return(0);

    HATDSec = t / 10;

    /* Don't do this if diablo.hosts hasn't been modified  or we are
     * within the rebuild time limit
     */
    if (!force && (t - HACMTime) < rebuildtime &&
		stat(PatDbExpand(DHostsCachePat), &st) == 0) {
	int ok = 1;
	if (stat(PatLibExpand(DiabloHostsPat), &st) == 0) {
	    if (st.st_mtime != HAMTime)
		ok = 0;
	} else {
	    if (HAMTime != 0)	/* File deleted */
		ok = 0;
	}
	if (stat(PatLibExpand(DNewsfeedsPat), &st) == 0) {
	    if (st.st_mtime != DNMTime)
		ok = 0;
	} else {
	    if (DNMTime != 0)	/* File deleted */
		ok = 0;
	}
	if (ok)
	    return(0);
    }

    if (stat(PatLibExpand(DiabloHostsPat), &st) == 0)
	HAMTime = st.st_mtime;
    else
	HAMTime = 0;
    if (stat(PatLibExpand(DNewsfeedsPat), &st) == 0)
	DNMTime = st.st_mtime;
    else
	DNMTime = 0;

    HACMTime = t;

    /*
     * Do the DNS lookups in the background
     * The main loop does a wait to cleanup the children
     */
    if (t != 0)
	pid = fork();
    else
	pid = 0;
    if (pid == 0) {
	FILE *fo;
        int lockfd;
	char buf[PATH_MAX];
	int count = 0;

	if ((lockfd = open(PatDbExpand(DHostsLockPat), O_RDWR|O_CREAT, 0644)) < 0) {
	    if (t != 0)
		logit(LOG_INFO, "%s : Unable to create lockfile (%s)",
				PatDbExpand(DHostsLockPat), strerror(errno));
	    else
		printf("%s : Unable to create lockfile (%s)\n",
				PatDbExpand(DHostsLockPat), strerror(errno));
	    exit(0);
	    
	}
	if (xflock(lockfd, XLOCK_EX|XLOCK_NB) == -1) {
	    if (t != 0)
		logit(LOG_INFO, "%s : Rebuild already in progress",
						PatDbExpand(DHostsCachePat));
	    else
		printf("%s : Rebuild already in progress\n",
						PatDbExpand(DHostsCachePat));
	    close(lockfd);
	    exit(0);
	}
	sprintf(buf, "%d", (int)getpid());
	WriteBestEffort(lockfd, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "%s.new", PatDbExpand(DHostsCachePat));

	fo = PrivateFOpen(buf, "w", 0640);
	if (fo == NULL) {
	    if (t != 0)
		logit(LOG_CRIT, "%s: %s", buf, strerror(errno));
	    else
		fprintf(stderr, "%s: %s\n", buf, strerror(errno));
	    xflock(lockfd, XLOCK_UN);
	    exit(1);
	}

	if (t != 0)
	    logit(LOG_INFO, "Rebuilding host cache");
	else
	    printf("Rebuilding host cache\n");

	HAOldCache = fopen(PatDbExpand(DHostsCachePat), "r");
	loadDnewsFeeds(fo, t, &count);
	loadDiabloHosts(fo, t, &count);
	if (HAOldCache != NULL) {
	    fclose(HAOldCache);
	    HAOldCache = NULL;
	}
	while (HAKeptList != NULL) {
	    HAKept *k = HAKeptList;

	    HAKeptList = k->next;
	    free(k);
	}
	if (count == 0)
	    logit(LOG_INFO, "No incoming hosts found");
	else if (DebugOpt)
	    printf("Found %d incoming hosts\n", count);

	/*
	 * An empty cache is valid when the last incoming host is removed.
	 * Never retain stale authorization entries just because the rebuild
	 * produced zero records.  Only publish a successfully written file.
	 */
	{
	    int writeError = ferror(fo);

	    if (fclose(fo) == EOF)
		writeError = 1;
	    if (writeError) {
		logit(LOG_CRIT, "%s: unable to write host cache", buf);
		unlink(buf);
		xflock(lockfd, XLOCK_UN);
		close(lockfd);
		exit(1);
	    }
	}

	/* Atomically replace the previous cache, including with an empty one. */
	if (rename(buf, PatDbExpand(DHostsCachePat)) < 0) {
	    logit(LOG_CRIT, "Cannot replace host cache %s: %s",
			PatDbExpand(DHostsCachePat), strerror(errno));
	    unlink(buf);
	    xflock(lockfd, XLOCK_UN);
	    close(lockfd);
	    exit(1);
	}
	xflock(lockfd, XLOCK_UN);
	close(lockfd);
	if (t != 0)
	    exit(0);
    }
    return(pid);
}

int
convHost(char *host, HostAccess *HA, FILE *fo)
{
    int found = 0;

    /*
     * Handle the forcing of entry type
     */
    HA->testtype = TESTTYPE_NONE;
    if (strncmp(host, "IP:", 3) == 0) {
	HA->testtype |= TESTTYPE_IP;
	host += 3;
    } else if (strncmp(host, "FQDN:", 5) == 0) {
	HA->testtype |= TESTTYPE_FQDN;
	host += 5;
    }

    /*
     * If this entry is an IP, then only match against an IP
     * otherwise only match against the hostname
     */
    if (HA->testtype == TESTTYPE_NONE) {
	if (IsIpAddr(host))
	    HA->testtype |= TESTTYPE_IP;
	else
	    HA->testtype |= TESTTYPE_FQDN;
    }

    if (strlen(host) >= sizeof(HA->entry)) {
	logit(LOG_ERR, "%s: host entry too long (at most %d characters): %.40s...",
		PatLibExpand(DNewsfeedsPat), (int)sizeof(HA->entry) - 1, host);
	return(0);
    }
    strcpy(HA->entry, host);
    HA->addrst[0] = 0;

    /*
     * Don't lookup IP for wildcards/CIDR
     */
    if (strchr(host, '*') != NULL || strchr(host, '?') != NULL ||
						strchr(host, '/') != NULL) {
	fwrite(HA, sizeof(*HA), 1, fo);
	return(1);
    }

#ifdef INET6
    {
	struct addrinfo hints, *ai0;

	/*
	 * Explicit hints: with NULL hints glibc adds AI_ADDRCONFIG, and
	 * glibc 2.39 (EL10) then drops even a numeric IPv6 address on a
	 * host without a global IPv6 address, so the entry was lost.
	 */
	int gai;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	if ((gai = getaddrinfo(host, NULL, &hints, &ai0)) == 0) {
	    struct addrinfo *ai;

	    for (ai = ai0; ai; ai = ai->ai_next) {
		if (getnameinfo(ai->ai_addr, ai->ai_addrlen, HA->addrst,
					sizeof(HA->addrst), NULL,
					0, NI_NUMERICHOST) == 0) {
		    HA->testtype |= TESTTYPE_IP;
		    fwrite(HA, sizeof(*HA), 1, fo);
		    found++;
		}
	    }
	    freeaddrinfo(ai0);
	    if (found)
		(void)haWritten(HA);
	} else if (gai == EAI_AGAIN || gai == EAI_MEMORY
#ifdef EAI_SYSTEM
			|| gai == EAI_SYSTEM
#endif
	) {
	    /*
	     * No answer for now (resolver down or timed out, no memory, a
	     * system error): keep the addresses the name had.  Any other
	     * error (no such name, no address, a failed answer) drops the
	     * name as before.
	     */
	    found = keepOldHost(host, HA, fo, gai_strerror(gai));
	}
    }
#else
    {
	struct hostent *he;
	int i;

	if ((he = gethostbyname(host)) != NULL) {
	    for (i = 0; he->h_addr_list && he->h_addr_list[i] != NULL; ++i) {
		struct in_addr *haddr = (void *)he->h_addr_list[i];
		strcpy(HA->addrst, inet_ntoa(*haddr));
		HA->testtype |= TESTTYPE_IP;
		fwrite(HA, sizeof(*HA), 1, fo);
		found++;
	    }
	    if (found)
		(void)haWritten(HA);
	} else if (h_errno == TRY_AGAIN) {
	    found = keepOldHost(host, HA, fo, "temporary lookup failure");
	}
    }
#endif
    return(found);
}

/*
 * haWritten() - note that HA's (label, entry) gets records in this
 * rebuild.  Returns 1 when it already had, 0 when it is new, -1 when it
 * could not be noted (no memory).
 */
static int
haWritten(const HostAccess *HA)
{
    HAKept *k;

    for (k = HAKeptList; k != NULL; k = k->next) {
	if (strcmp(k->label, HA->label) == 0 &&
					strcmp(k->entry, HA->entry) == 0)
	    return(1);
    }
    if ((k = malloc(sizeof(*k))) == NULL)
	return(-1);
    strcpy(k->label, HA->label);
    strcpy(k->entry, HA->entry);
    k->next = HAKeptList;
    HAKeptList = k;
    return(0);
}

/*
 * keepOldHost() - the lookup of host gave no answer this time: write the
 * addresses the previous cache had for the same label and entry, each
 * once, unless that label and entry already got records in this rebuild
 * (from a lookup or kept).  Returns the number of records written (0
 * without a previous cache or when it had none: the entry is dropped as
 * before).
 */
static int
keepOldHost(const char *host, HostAccess *HA, FILE *fo, const char *why)
{
    HostAccess old;
    char **addrs = NULL;
    int naddrs = 0;
    int found = 0;
    int i;

    /* already has records in this rebuild, or no memory to note it */
    if (haWritten(HA) != 0)
	return(0);

    if (HAOldCache != NULL) {
	rewind(HAOldCache);
	while (fread(&old, sizeof(old), 1, HAOldCache) == 1) {
	    old.entry[sizeof(old.entry) - 1] = 0;
	    old.label[sizeof(old.label) - 1] = 0;
	    old.addrst[sizeof(old.addrst) - 1] = 0;
	    if (old.addrst[0] == 0 || !(old.testtype & TESTTYPE_IP) ||
				strcmp(old.entry, HA->entry) != 0 ||
				strcmp(old.label, HA->label) != 0)
		continue;
	    for (i = 0; i < naddrs; ++i) {
		if (strcmp(addrs[i], old.addrst) == 0)
		    break;
	    }
	    if (i < naddrs)
		continue;
	    {
		char **na = realloc(addrs, (naddrs + 1) * sizeof(*addrs));

		if (na == NULL || (na[naddrs] = strdup(old.addrst)) == NULL) {
		    if (na != NULL)
			addrs = na;
		    break;
		}
		addrs = na;
		naddrs++;
	    }
	    strcpy(HA->addrst, old.addrst);
	    HA->testtype |= TESTTYPE_IP;
	    fwrite(HA, sizeof(*HA), 1, fo);
	    found++;
	}
    }
    for (i = 0; i < naddrs; ++i)
	free(addrs[i]);
    free(addrs);
    logit(LOG_ERR, "host cache: %s: %s, %d previous address(es) kept",
						host, why, found);
    return(found);
}

void
loadDiabloHosts(FILE *fo, int t, int *count)
{
    FILE *fi;
    char buf[PATH_MAX];
    HostAccess HA;
    char *s;
    char *l;
    char *p;

    if (DebugOpt)
	printf("Search for hosts in %s\n", PatLibExpand(DiabloHostsPat));

    fi = fopen(PatLibExpand(DiabloHostsPat), "r");
    if (fi == NULL)
	return;

    bzero(&HA, sizeof(HA));
    while (fgets(buf, sizeof(buf), fi) != NULL) {
	p = buf;
	if (*p == '\n' || *p == '#')
	    continue;
	HA.testtype = TESTTYPE_NONE;
	if (strncmp(p, "IP:", 3) == 0) {
	    HA.testtype |= TESTTYPE_IP;
	    p += 3;
	} else if (strncmp(p, "FQDN:", 5) == 0) {
	    HA.testtype |= TESTTYPE_FQDN;
	    p += 5;
	}
	/*
	 * Fields are separated by white space, so that the host can be an
	 * IPv6 address.  A name with one trailing ':' ("host: label") and
	 * the old "host:label" form are still understood.
	 */
	s = strtok(p, " \t\r\n");	/* hostname	*/
	if (s == NULL || !*s)
	    continue;
        l = strtok(NULL, " \t\r\n");	/* label	*/
	if (l == NULL || !*l) {
	    if ((l = strrchr(s, ':')) == NULL || l == s || !l[1])
		continue;
	    *l++ = 0;
	} else if ((p = strchr(s, ':')) != NULL && p[1] == 0 && p != s) {
	    *p = 0;
	}

	if (strlen(s) > 255 || strlen(l) > 255)
	    continue;

	diablo_strlcpy(HA.label, l, sizeof(HA.label), sizeof(HA.label));
	if (convHost(s, &HA, fo))
	    (*count)++;
	bzero(&HA, sizeof(HA));
    }
    fclose(fi);
}

void
loadDnewsFeeds(FILE *fo, int t, int *count)
{
    FILE *fi;
    char buf[512];
    HostAccess HA;
    char label[255];

    label[0] = 0;
    if (DebugOpt)
	printf("Search for hosts in %s\n", PatLibExpand(DNewsfeedsPat));

    fi = iopen(PatLibExpand(DNewsfeedsPat), "r");
    if (fi == NULL) {
	if (t != 0)
	    logit(LOG_CRIT, "%s: %s", PatLibExpand(DNewsfeedsPat),
							strerror(errno));
	else
	    fprintf(stderr, "%s: %s\n", PatLibExpand(DNewsfeedsPat),
							strerror(errno));
	exit(1);
    }

    bzero(&HA, sizeof(HA));
    while (igets(buf, sizeof(buf), fi) != NULL) {
	char *cmd = strtok(buf, " \t\n");
	char *p = (cmd != NULL) ? strtok(NULL, " \t\n") : NULL;
	if (IgetsTooLong) {	/* skipped whole (D-94) */
	    logit(LOG_ERR, "%s: a line is too long (at most %d characters), ignored",
		PatLibExpand(DNewsfeedsPat), (int)sizeof(buf) - 2);
	    continue;
	}
	if (cmd == NULL || *cmd == '#')
	    continue;

	/* "end" has no argument */
	if (strcmp(cmd, "end") == 0) {
	    label[0] = 0;
	    continue;
	}
	if (p == NULL)
	    continue;
	if (strcmp(cmd, "label") == 0) {
	    if (strlen(p) >= sizeof(label)) {
		/* too long to be a label of ours; its hosts are not entered */
		label[0] = 0;
		continue;
	    }
	    strcpy(label, p);
	    continue;
	}
	if (!label[0])
	    continue;

	if (strcmp(cmd, "inhost") == 0 || strcmp(cmd, "host") == 0) {
	    strcpy(HA.label, label);
	    if (convHost(p, &HA, fo))
		(*count)++;
	    bzero(&HA, sizeof(HA));
	    continue;
	}
    }
    iclose(fi);
}

/*
 * The longest a feeder session waits for the name of a connecting address
 * that no IP entry matches (D-242).
 */
#define HOSTAUTH_DNS_SECS	10

/*
 * peerName() - the name of the address res (ipst, pname as text) whose
 *	reverse (PTR) lookup a forward lookup of that name confirms, or NULL.
 *
 *	getnameinfo() and getaddrinfo() have no time limit of their own, and
 *	whoever answers the PTR query of an address can make them wait.  A
 *	lookup cut short by a signal would leave the resolver's state undefined
 *	in a process that goes on to serve the peer, so both lookups run in a
 *	child process that writes the name to a pipe; after secs seconds it is
 *	killed and the address has no name.
 */
static char *
peerName(struct sockaddr *res, char *ipst, const char *pname, int secs)
{
    char buf[NI_MAXHOST];
    size_t got = 0;
    int done = 0;
    int p[2];
    pid_t pid;
    time_t end;

    if (pipe(p) < 0) {
	logit(LOG_ERR, "DNS lookup of %s: pipe: %s", pname, strerror(errno));
	return(NULL);
    }
    if ((pid = fork()) < 0) {
	logit(LOG_ERR, "DNS lookup of %s: fork: %s", pname, strerror(errno));
	close(p[0]);
	close(p[1]);
	return(NULL);
    }
    if (pid == 0) {
	char hostst[NI_MAXHOST];
	int ok = 0;

	close(p[0]);
#ifdef INET6
	if (getnameinfo(res, SA_LEN(res), hostst, sizeof(hostst),
					NULL, 0, NI_NAMEREQD) == 0 &&
	    TestForwardLookup(hostst, (const char *)ipst, res) >= 0)
	    ok = 1;
#else
	{
	    struct sockaddr_in *sin = (struct sockaddr_in *)res;
	    struct hostent *he;

	    he = gethostbyaddr((char *)&sin->sin_addr, sizeof(sin->sin_addr),
								AF_INET);
	    if (he != NULL && strlen(he->h_name) < sizeof(hostst)) {
		strcpy(hostst, he->h_name);
		if (TestForwardLookup(hostst, (const char *)ipst, res) >= 0)
		    ok = 1;
	    }
	}
#endif
	if (ok)
	    (void)write(p[1], hostst, strlen(hostst));
	_exit(0);
    }
    close(p[1]);
    end = time(NULL) + secs;
    for (;;) {
	struct pollfd pfd;
	time_t left = end - time(NULL);
	ssize_t n;

	if (left <= 0)
	    break;
	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	if ((n = poll(&pfd, 1, (int)left * 1000)) < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	if (n == 0)
	    continue;
	if ((n = read(p[0], buf + got, sizeof(buf) - 1 - got)) < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	if (n == 0) {
	    done = 1;
	    break;
	}
	got += n;
	if (got == sizeof(buf) - 1) {	/* more than a name can be */
	    got = 0;
	    done = 1;
	    break;
	}
    }
    if (!done) {
	kill(pid, SIGKILL);
	logit(LOG_NOTICE, "DNS lookup of %s took longer than %d s, "
			"its name is not used", pname, secs);
	got = 0;
    }
    close(p[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
	;
    if (got == 0)
	return(NULL);
    buf[got] = 0;
    return(zallocStr(&SysMemPool, buf));
}

/*
 * A record read from the cache file is not trusted to hold terminated
 * strings.
 */
static void
haTerminate(HostAccess *HA)
{
    HA->entry[sizeof(HA->entry) - 1] = 0;
    HA->addrst[sizeof(HA->addrst) - 1] = 0;
    HA->label[sizeof(HA->label) - 1] = 0;
}

/*
 * AUTHENTICATION()	- authenticate a new connection
 *
 *   We follow the following procedure (D-242: no DNS before the cheap
 *   checks):
 *
 *	- first the entries that match an address: IP entries, CIDR and
 *	  wildcard-IP entries, and name entries by the addresses the host
 *	  cache has for them; no lookup is done for these
 *	- only when none matches and an entry can match by name alone (a
 *	  wildcard name, or a name the cache has no address for) are the
 *	  reverse and forward lookups done, in a child, at most
 *	  HOSTAUTH_DNS_SECS seconds; a name only counts when the forward
 *	  lookup confirms it
 *	- an address no entry matches gets no lookup at all
 *
 *	Returns the host name: the entry's name for a name entry matched by
 *	its cached address, the confirmed name for a match by name, the IP
 *	address for an IP, CIDR or wildcard-IP entry.  NULL: no match.
 *
 *	label: at least sizeof(HostAccess.label) (256) bytes; set to the
 *	label of the matching entry, or "".
 */

char *
Authenticate(int fd, struct sockaddr *res, char *ipst, char *label)
{
    char *hname = NULL;		/* the host name returned, or NULL */
    char *pname = NULL;		/* IP address (as string)	  */
    int isValid = 0;

#ifdef INET6
    {
	char *st;

	st = NetAddrToSt(0, res, 1, 0, 0);
	if (st != NULL) {
	    pname = zallocStr(&SysMemPool, st);
	} else {
	    logit(LOG_ERR, "getnameinfo: Unable to get numeric address");
	    return(NULL);
	}
	st[0]= 0;
    }
#else
    {
	struct sockaddr_in *sin = (struct sockaddr_in *)res;

	pname = zallocStr(&SysMemPool, inet_ntoa(sin->sin_addr));
    }
#endif /* INET6 */

    /*
     * Check IP against cache file of diablo.hosts. 
     */
    {
	FILE *fi = fopen(PatDbExpand(DHostsCachePat), "r");

	if (fi) {
	    HostAccess HA;
	    int byName = 0;	/* an entry can match by name alone */

	    while (fread(&HA, sizeof(HA), 1, fi) == 1) {
		haTerminate(&HA);
		if (HA.addrst[0] && (HA.testtype & TESTTYPE_IP) && strcmp(ipst, HA.addrst) == 0) {
		    isValid = 1;
		    break;
		} else if ((strchr(HA.entry, '/') != NULL) && (HA.testtype & TESTTYPE_IP) && CidrMatch(HA.entry, pname)) {
		    isValid = 2;
		    break;
		} else if ((HA.testtype & TESTTYPE_IP) && !(HA.testtype & TESTTYPE_FQDN) && WildCaseCmp(HA.entry, pname) == 0) {
		    isValid = 3;
		    break;
		} else if ((HA.testtype & TESTTYPE_FQDN) &&
			   (!(HA.testtype & TESTTYPE_IP) ||
			    strchr(HA.entry, '*') != NULL ||
			    strchr(HA.entry, '?') != NULL)) {
		    byName = 1;
		}
	    }
	    if (!isValid && byName &&
			(hname = peerName(res, ipst, pname, HOSTAUTH_DNS_SECS)) != NULL) {
		rewind(fi);
		while (fread(&HA, sizeof(HA), 1, fi) == 1) {
		    haTerminate(&HA);
		    if ((HA.testtype & TESTTYPE_FQDN) && strcasecmp(HA.entry, hname) == 0) {
			isValid = 4;
			break;
		    } else if ((HA.testtype & TESTTYPE_FQDN) && WildCaseCmp(HA.entry, hname) == 0) {
			isValid = 5;
			break;
		    }
		}
	    }
	    if (isValid) {
		char *method = "UNKNOWN";
		if (isValid == 1)
		    method = "Exact IP match";
		if (isValid == 2)
		    method = "CIDR match";
		if (isValid == 3)
		    method = "Wildcmp IP match";
		if (isValid == 4)
		    method = "Exact Hostname match";
		if (isValid == 5)
		    method = "Wildcmp hostname match";
		/* a name entry matched by its cached address: its name */
		if (isValid == 1 && (HA.testtype & TESTTYPE_FQDN))
		    hname = zallocStr(&SysMemPool, HA.entry);
		logit(LOG_DEBUG, "Matched %s (%s) to %s (label:%s) using %s",
					hname ? hname : "NOTFOUND", pname,
					HA.entry, HA.label,
					method);
		if (HA.label[0])
		    snprintf(label, sizeof(HA.label), "%s", HA.label);
		else
		    label[0] = 0;
	    }
	    fclose(fi);
	} else {
	    logit(LOG_ERR, "%s file not found", PatDbExpand(DHostsCachePat));
	}
    }

    /*
     * A valid connection without a name (an IP, CIDR or wildcard-IP
     * entry) has the IP address as its name.
     */
    if (isValid) {
	if (hname == NULL) {
	    hname = pname;
	    pname = NULL;
	}
    } else {
	if (hname)
	    zfreeStr(&SysMemPool, &hname);
    }
    zfreeStr(&SysMemPool, &pname);
    return(hname);
}

void
DumpHostCache(const char *fname)
{
   FILE *fi = fopen(fname, "r");

    if (fi) {
	HostAccess HA;
        char tt[20];

	while (fread(&HA, sizeof(HA), 1, fi) == 1) {
	    haTerminate(&HA);
	    strcpy(tt, "");
	    if (HA.testtype & TESTTYPE_NONE)
		strcat(tt, "NONE:");
	    if (HA.testtype & TESTTYPE_IP)
		strcat(tt, "IP:");
	    if (HA.testtype & TESTTYPE_FQDN)
		strcat(tt, "H:");
	    if (HA.testtype & TESTTYPE_ANY)
		strcat(tt, "ANY:");
	    printf("%s label=%s type=%s ip=%s\n", HA.entry, HA.label, tt,
				(HA.testtype & TESTTYPE_IP) ? HA.addrst : "NONE");
	}
    } else {
	fprintf(stderr, "Unable to open %s\n", fname);
    }
}
