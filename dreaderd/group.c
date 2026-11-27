
/*
 * DREADERD/GROUP.C
 *
 *	Group/Overview routines.  Group control and overview information is
 *	arranged in a two-level directory scheme by hash code under
 *	/news/spool/group.  The first level consists of 256 subdirectories.
 *	The group is stored as a KPDB database file (dactive.kp)
 *
 *	The header information is stored in 2 files:
 *		over.0.HASH		Overview index
 *		data.nnnn.HASH		Overview data
 *
 *	The overview data file is a list of NUL terminated article
 *	headers in no particular order.
 *
 *	The overview index file is binary file containing a header
 *	(struct OverInfo) and fixed length records (struct OverArt)
 *	with each article number placed in a slot based on the
 *	maximum number of possible articles in the group (ov_MaxArts).
 *
 *	Each header is temporarily stored in a memory reference list
 *	(struct OverData) linked from OverInfo->ov_HData.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"
#include <sys/resource.h>	/* getrlimit() in OverCacheLimit(); defs.h has it only with USE_PRCTL */

#define OVHSIZE		1024
#define OVHMASK		(OVHSIZE-1)

#define HMAPALIGN	(1024 * 64)

#define	GZ_MAGIC0	0x1F
#define	GZ_MAGIC1	0x8B
#define	GZ_EXTRA	0x04
#define	GZ_ORIGNAME	0x08
#define	GZ_COMMENT	0x10
#define	GZ_HEADCRC	0x02

#ifdef USE_ZLIB
/*
 * gzHeaderLen() - length of the gzip header at p (n bytes in the file), or 0
 * if it is not a deflate-compressed gzip header or runs past the end of the
 * file with no room left for data and the trailer.
 */
static size_t
gzHeaderLen(const unsigned char *p, size_t n)
{
    size_t o = 10;
    int flags;

    if (n < 18 || p[0] != GZ_MAGIC0 || p[1] != GZ_MAGIC1 || p[2] != Z_DEFLATED)
	return(0);
    flags = p[3];
    if (flags & GZ_EXTRA) {
	if (o + 2 > n)
	    return(0);
	o += 2 + (size_t)(p[o] | (p[o+1] << 8));
    }
    if (flags & GZ_ORIGNAME) {
	while (o < n && p[o])
	    ++o;
	++o;
    }
    if (flags & GZ_COMMENT) {
	while (o < n && p[o])
	    ++o;
	++o;
    }
    if (flags & GZ_HEADCRC)
	o += 2;
    if (o + 8 >= n)	/* data and the 8 byte trailer must follow */
	return(0);
    return(o);
}
#endif

Prototype void NNFeedOverview(Connection *conn);
Prototype void FlushOverCache(void);
Prototype int OverCacheLimit(void);
Prototype void OverCacheFdPressure(void);
Prototype OverInfo *GetOverInfo(const char *group);
Prototype void PutOverInfo(OverInfo *ov);
Prototype const char *GetOverRecord(OverInfo *ov, artno_t artno, int *plen, int *aleno, int *plines, TimeRestrict *tr, int *TimeRcvd);
Prototype int ReadOverviewRange(Connection *conn);
Prototype OverInfo *FindCanceledMsg(const char *group, const char *msgid, artno_t *partNo, int *pvalidGroups);
Prototype OverInfo *FindMsgInGroup(const char *group, const char *msgid, artno_t *partNo, artno_t maxScan);
Prototype int CancelOverArt(OverInfo *ov, artno_t artNo);
Prototype void OutputOverRange(OverInfo *ov, Connection *conn);

Prototype int NNTestOverview(Connection *conn);
Prototype const char *NNRetrieveHead(Connection *conn, int *povlen, const char **pmsgid, int *TimeRcvd, int *grpIter, artno_t *endNo);

const OverArt *GetOverArt(OverInfo *ov, artno_t artNo, off_t *ppos);
void AssignArticleNo(Connection *conn, ArtNumAss **pan, const char *group, const char *xref, int approved, const char *art, int artLen, const char *msgid);
static int ArtNumAssigned(const ArtNumAss *an, const char *group);
int WriteOverview(Connection *conn, ArtNumAss *an, const char *group, const char *xref, const char *art, int artLen, const char *msgid);
OverData *MakeOverHFile(OverInfo *ov, artno_t artNo, int create);
void FreeOverInfo(OverInfo *ov);
void FreeOverData(OverData *od);
static OverInfo *OpenOverInfo(const char *group, int create);

/* Overview records are fixed-size on disk.  Never publish a partial record. */
static int
OverWriteAt(int fd, const void *buf, size_t len, off_t pos)
{
    const char *ptr = buf;

    while (len) {
        ssize_t n = pwrite(fd, ptr, len, pos);

        if (n > 0) {
            ptr += n;
            pos += n;
            len -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            if (n == 0)
                errno = EIO;
            return(-1);
        }
    }
    return(0);
}

/*
 * writev() is allowed to complete only part of an overview header.  Issue
 * the whole record as one writev() and, on a short write, resume from the
 * exact byte where it stopped.
 */
static int
OverWriteV(int fd, const struct iovec *iov, int count)
{
    struct iovec vec[8];
    struct iovec *v = vec;
    int i;

    if (count < 0 || count > (int)(sizeof(vec) / sizeof(vec[0]))) {
        errno = EINVAL;
        return(-1);
    }
    for (i = 0; i < count; ++i)
        vec[i] = iov[i];

    for (;;) {
        ssize_t n;

        while (count > 0 && v->iov_len == 0) {
            ++v;
            --count;
        }
        if (count == 0)
            break;

        n = writev(fd, v, count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (n == 0)
                errno = EIO;
            return(-1);
        }
        while (count > 0 && (size_t)n >= v->iov_len) {
            n -= (ssize_t)v->iov_len;
            ++v;
            --count;
        }
        if (count > 0) {
            v->iov_base = (char *)v->iov_base + n;
            v->iov_len -= (size_t)n;
        }
    }
    return(0);
}

OverInfo *OvHash[OVHSIZE];
int	  NumOverInfo;

/*
 * The groups in OvHash, most recently used first.  When the cache is full,
 * OpenOverInfo() closes the least recently used group nobody holds.  Busy
 * feeds post across thousands of groups, so dropping half of the cache at
 * a time (as before) or a small cache reopens almost every group.
 */
static OverInfo *OvLruHead;
static OverInfo *OvLruTail;
static int	OvCacheMax;

/*
 * The descriptors left for the clients: an overview file that gets a
 * descriptor at or above OvFdHigh sets OvFdPressure, and the next
 * OpenOverInfo() lowers the cache (see OvCacheShrink()).  open() returns the
 * lowest free descriptor, so a number that high means every one below it
 * is in use, by whatever: a group keeps up to readerthreads+1 data files.
 */
static long	OvFdHigh;
static int	OvFdPressure;

static void
OvLruUnlink(OverInfo *ov)
{
    if (ov->ov_LruPrev)
	ov->ov_LruPrev->ov_LruNext = ov->ov_LruNext;
    else
	OvLruHead = ov->ov_LruNext;
    if (ov->ov_LruNext)
	ov->ov_LruNext->ov_LruPrev = ov->ov_LruPrev;
    else
	OvLruTail = ov->ov_LruPrev;
    ov->ov_LruPrev = ov->ov_LruNext = NULL;
}

static void
OvLruPushHead(OverInfo *ov)
{
    ov->ov_LruPrev = NULL;
    ov->ov_LruNext = OvLruHead;
    if (OvLruHead)
	OvLruHead->ov_LruPrev = ov;
    else
	OvLruTail = ov;
    OvLruHead = ov;
}

/* remove a cached group from the hash and the LRU list, and close it */
static void
OvCacheDrop(OverInfo *ov)
{
    OverInfo **pov = &OvHash[shash(ov->ov_Group) & OVHMASK];

    while (*pov != ov)
	pov = &(*pov)->ov_Next;
    *pov = ov->ov_Next;
    OvLruUnlink(ov);
    FreeOverInfo(ov);
    --NumOverInfo;
}

/* close the least recently used group nobody holds; 0 if there is none */
static int
OvCacheEvictOne(void)
{
    OverInfo *ov;

    for (ov = OvLruTail; ov; ov = ov->ov_LruPrev) {
	if (ov->ov_Refs == 0) {
	    OvCacheDrop(ov);
	    return(1);
	}
    }
    return(0);
}

/*
 * An open() failed with err.  If the process is out of descriptors, close
 * a quarter of the cached groups (at least one) so the caller can retry.
 * Returns the number closed, with errno still err.
 */
static int
OvCacheReclaim(int err)
{
    int n = NumOverInfo / 4;
    int freed = 0;

    if (err != EMFILE && err != ENFILE)
	return(0);
    if (n < 1)
	n = 1;
    while (freed < n && OvCacheEvictOne())
	++freed;
    if (freed)
	logit(LOG_NOTICE, "out of descriptors: closed %d cached overview groups",
	    freed);
    errno = err;
    return(freed);
}

/* an overview file got descriptor fd */
static void
OvFdNote(int fd)
{
    if (OvCacheMax == 0)
	(void)OverCacheLimit();
    if (fd >= OvFdHigh)
	OvFdPressure = 1;
}

/*
 * OverCacheFdPressure() - this fork is short of descriptors (an overview
 * file got one near the limit, or a new client's descriptor was dropped).
 */
void
OverCacheFdPressure(void)
{
    OvFdPressure = 1;
}

/*
 * The cached groups use the descriptors the clients need: lower this
 * fork's cache to three quarters of the groups it has now (not below
 * OV_CACHE_MAX) and close the least recently used ones.  The lower limit
 * stays, so a cache that does not fit is not refilled and shrunk again.
 * Called from OpenOverInfo(), where closing groups was always allowed.
 */
static void
OvCacheShrink(void)
{
    int n = NumOverInfo - NumOverInfo / 4;
    int freed = 0;

    OvFdPressure = 0;
    if (n < OV_CACHE_MAX)
	n = OV_CACHE_MAX;
    if (n < OvCacheMax) {
	logit(LOG_NOTICE, "readerovcache lowered to %d: the overview files use the descriptors left for clients",
	    n);
	OvCacheMax = n;
    }
    while (NumOverInfo > n && OvCacheEvictOne())
	++freed;
    if (freed)
	logit(LOG_NOTICE, "low on descriptors: closed %d cached overview groups",
	    freed);
}

/*
 * OverCacheLimit() - the number of groups a fork keeps open: readerovcache,
 * lowered so the descriptors fit.  Each cached group holds its over.* file
 * and usually one data.* file, sometimes more (OvCacheShrink() lowers it
 * further when they do not fit).  The forks select() on their clients, so
 * all descriptors must stay below FD_SETSIZE as well as below
 * RLIMIT_NOFILE; room is left for the clients, the spool servers and the
 * log files.  Computed once (dreaderd calls it at startup, before it forks).
 */
int
OverCacheLimit(void)
{
    if (OvCacheMax == 0) {
	struct rlimit rl;
	long lim = FD_SETSIZE;
	long cap;

	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 &&
	    rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < (rlim_t)lim)
	    lim = (long)rl.rlim_cur;
	OvFdHigh = lim - 2L * DOpts.ReaderThreads - 64;
	if (OvFdHigh < lim / 2)
	    OvFdHigh = lim / 2;
	cap = OvFdHigh / 3;
	if (cap < OV_CACHE_MAX)
	    cap = OV_CACHE_MAX;
	OvCacheMax = DOpts.ReaderOvCache;
	if (OvCacheMax < 1)
	    OvCacheMax = OV_CACHE_MAX;
	if (OvCacheMax > cap) {
	    logit(LOG_NOTICE, "readerovcache %d lowered to %ld (descriptor limit %ld)",
		OvCacheMax, cap, lim);
	    OvCacheMax = (int)cap;
	}
    }
    return(OvCacheMax);
}

void
NNFeedOverview(Connection *conn)
{
    int artLen;
    int l = 0;
    char *art = MBNormalize(&conn->co_ArtBuf, &artLen);
    char *line;
    int appr = 0;
    char ch;
    int err = 0;

    char *newsgroups = NULL;
    char *msgid = NULL;
    char *subject = NULL;
    char *date = NULL;
    char *xref = NULL;
    char *control = NULL;
    char *supers = NULL;	/* Supersedes */

    /*
     * Scan headers, look for Newsgroups: line, Subject:, Date:, From:, and
     * Message-Id:.  If any are missing, the article is bad.  If there is an
     * Xref: line, save that too and use it to calculate line numbers if 
     * Xref operation is enabled.
     *
     * We allow an LF-only line to terminate the headers as well as CR+LF,
     * because some news systems are totally broken.
     */

    for (line = art; line < art + artLen; line += l + 1) {
	for (l = line - art; l < artLen; ++l) {
	    if (art[l] == '\n') {
		if (l + 1 >= artLen || 		/* past end of article	*/
		    l == line - art || 		/* blank line		*/
		    (art[l+1] != ' ' && art[l+1] != '\t')  /* !header ext */
		) {
		    break;
		}
	    }
	}
	l -= line - art;

	ch = tolower((unsigned char)*line);

	if (l == 0 || (l == 1 && line[0] == '\r')) {
	    /* out of headers */
	    break;
	} else if (ch == 'n' && strncasecmp(line, "Newsgroups:", 11) == 0) {
	    zfreeStr(&conn->co_MemPool, &newsgroups);	/* a repeated header: the last one counts */
	    newsgroups = zallocStrTrim2(&conn->co_MemPool, ',', line + 11, l - 11);
	    /*
	     * White space between names separates them (D-206).  The plain
	     * list can be shorter, and zfreeStr() frees strlen() + 1, so it
	     * goes into a copy of its own size.
	     */
	    {
		int n = strlen(newsgroups) + 1;
		char *t;

		NormalizeNewsgroups(newsgroups);
		t = zallocStr(&conn->co_MemPool, newsgroups);
		zfree(&conn->co_MemPool, newsgroups, n);
		newsgroups = t;
	    }
	} else if (ch == 'm' && strncasecmp(line, "Message-ID:", 11) == 0) {
	    zfreeStr(&conn->co_MemPool, &msgid);
	    msgid = zallocStrTrim2(&conn->co_MemPool, 0, line + 11, l - 11);
	} else if (ch == 's' && strncasecmp(line, "Subject:", 8) == 0) {
	    zfreeStr(&conn->co_MemPool, &subject);
	    subject = zallocStrTrim2(&conn->co_MemPool, 0, line + 8, l - 8);
	} else if (ch == 'd' && strncasecmp(line, "Date:", 5) == 0) {
	    zfreeStr(&conn->co_MemPool, &date);
	    date = zallocStrTrim2(&conn->co_MemPool, 0, line + 5, l - 5);
	} else if (ch == 'x' && strncasecmp(line, "Xref:", 5) == 0) {
	    zfreeStr(&conn->co_MemPool, &xref);
	    xref = zallocStrTrim2(&conn->co_MemPool, ',', line + 5, l - 5);
	} else if (ch == 'c' && strncasecmp(line, "Control:", 8) == 0) {
	    zfreeStr(&conn->co_MemPool, &control);
	    control = zallocStrTrim2(&conn->co_MemPool, 0, line + 8, l - 8);
	} else if (ch == 's' && strncasecmp(line, "Supersedes:", 11) == 0) {
	    zfreeStr(&conn->co_MemPool, &supers);
	    supers = zallocStrTrim2(&conn->co_MemPool, 0, line + 11, l - 11);
	} else if (ch == 'a' && strncasecmp(line, "Approved:", 9) == 0) {
	    appr = 1;
	}
    }

    /*
     * no Control: header in the headers we scanned: not a control message,
     * whatever the lines before said
     */

    if (control == NULL)
	conn->co_Flags &= ~COF_WASCONTROL;

    if (conn->co_Flags & COF_POSTTOOBIG) {
	conn->co_Auth.dr_PostFailCount++;
	if (conn->co_Flags & COF_IHAVE) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "437 Rejected, too big\r\n");
	} else {
	    MBLogPrintf(conn, &conn->co_TMBuf, "439 %s too big\r\n",  conn->co_IHaveMsgId);
	}
	conn->co_Flags &= ~COF_POSTTOOBIG;
    } else if (newsgroups == NULL || msgid == NULL || subject == NULL || 
	date == NULL || strcmp(msgid, "<>") == 0
    ) {
	/*
	 * failure
	 */
	conn->co_Auth.dr_PostFailCount++;
	if (conn->co_Flags & COF_IHAVE) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "437 Rejected, headers missing\r\n");
	} else {
	    MBLogPrintf(conn, &conn->co_TMBuf, "439 %s\r\n",  conn->co_IHaveMsgId);
	}
    } else if (conn->co_ByteCounter == 0.0 && conn->co_BytesHeader == 0) {
	conn->co_Auth.dr_PostFailCount++;
	if (conn->co_Flags & COF_IHAVE) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "437 Rejected, Bytes header missing for header-only feed\r\n");
	} else {
	    MBLogPrintf(conn, &conn->co_TMBuf, "439 %s headerOnlyFeed requires Bytes header\r\n",  conn->co_IHaveMsgId);
	}
    } else if (FindCancelCache(msgid) == 0) {
	char logbuf[1024];
	conn->co_Auth.dr_PostFailCount++;
	snprintf(logbuf, sizeof(logbuf), "%s cancel cache", msgid);
	LogCmd(conn, '-', logbuf);
	if (DRIncomingLogPat != NULL)
	    LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	if (conn->co_Flags & COF_IHAVE) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "437 Article Already Cancelled\r\n");
	} else {
	    MBLogPrintf(conn, &conn->co_TMBuf, "439 %s Article Already Cancelled\r\n",  conn->co_IHaveMsgId);
	}
    } else {
	/*
	 * write out overview information
	 */
	char *group;
	char *ngroup = NULL;
	ArtNumAss	*ANABase = NULL;


	/*
	 * if it is a control message, we don't really care what the newsgroups
	 * line says.  instead, we cobble up "control.<type>" or just "control"
	 */

	if (conn->co_Flags & COF_WASCONTROL) {
	    char cmsgtype[64];
	    char *cptr;

	    cptr = control;
	    while (*cptr == ' ' || *cptr == '\t') {
		cptr++;
	    }
	    snprintf(cmsgtype, sizeof(cmsgtype), "control%s%s", *cptr ? "." : "", cptr);
	    if (((cptr = strchr(cmsgtype, ' '))) || ((cptr = strchr(cmsgtype, '\t')))) {
		*cptr = '\0';
	    }
	    zfreeStr(&conn->co_MemPool, &newsgroups);
	    newsgroups = zallocStr(&conn->co_MemPool, cmsgtype);
	}
	if (DebugOpt)
	    printf("Feed overview %s %s\n", msgid, newsgroups);

	/*
	 * pass 1 - assign article numbers
	 */

	for (group = newsgroups; *group; group = ngroup) {
	    char c;
	    char whspc = 0;
	    char *whspptr;

	    /* Strip leading spaces */
	    while (*group == ' ' || *group == '\t') {
		group++;
	    }
	    if (! *group) {
		break;
	    }

	    for (ngroup = group; *ngroup && *ngroup != ','; ++ngroup)
		;
	    c = *ngroup;
	    *ngroup = 0;

	    /*
	     * Skip groups with names that are too long
	     */
	    if (ngroup - group > MAXGNAME) {
		*ngroup = c;
		if (*ngroup == ',')
		    ++ngroup;
		continue;
	    }

	    /* Strip trailing space or tab from group name */
	    whspptr = strpbrk(group, " \t");
	    if (whspptr) {
		whspc = *whspptr;
		*whspptr = 0;
	    }

	    /*
	     * A group listed more than once gets one number and one
	     * overview record: the repeats are skipped here, and pass 2
	     * finds no number for them (it matches by pointer).
	     */
	    if (!ArtNumAssigned(ANABase, group))
		AssignArticleNo(conn, &ANABase, group, xref, appr, art, artLen, msgid);

	    /* Repair string back to its former state */
	    if (whspptr) {
		*whspptr = whspc;
	    }

	    *ngroup = c;

	    if (*ngroup == ',')
		++ngroup;
	}

	/*
	 * Supersedes is allowed on non-control messages.  We execute the
	 * cancel AND post the article.  Note: we do not allow supersedes
	 * on Control: messages. (XXX is this still true with the new logic? JG)
	 */

	if (supers) {
	    if (DebugOpt)
		printf("has Supersedes: %s %s\n", msgid, newsgroups);
	    ExecuteSupersedes(conn, supers, msgid, art, artLen);
	}

	err = 0;
	for (group = newsgroups; *group; group = ngroup) {
	    char c;
	    char whspc = 0;
	    char *whspptr;

	    for (ngroup = group; *ngroup && *ngroup != ','; ++ngroup)
		;
	    c = *ngroup;
	    *ngroup = 0;

	    /*
	     * Skip groups with names that are too long
	     */
	    if (ngroup - group > MAXGNAME) {
		*ngroup = c;
		if (*ngroup == ',')
		    ++ngroup;
		continue;
	    }

	    /* Strip trailing space or tab from group name */
	    whspptr = strpbrk(group, " \t");
	    if (whspptr) {
		whspc = *whspptr;
		*whspptr = 0;
	    }

	    err += WriteOverview(conn, ANABase, group, xref, art, artLen, msgid);

	    /* Repair string back to its former state */
	    if (whspptr) {
		*whspptr = whspc;
	    }

	    *ngroup = c;
	    if (*ngroup == ',')
		++ngroup;
	}
	while (ANABase) {
	    ArtNumAss *an = ANABase;
	    ANABase = an->an_Next;
	    zfree(&conn->co_MemPool, an, sizeof(ArtNumAss));
	}
	if (conn->co_Flags & COF_WASCONTROL) {
	    if (DebugOpt)
		printf("Control message: %s %s\n", msgid, newsgroups);
	    LogCmd(conn, 'c', control);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s c %s %s", conn->co_Auth.dr_Host,
							msgid, control);
	    ExecuteControl(conn, control, art, artLen);
	}

	if (!err) {
	    conn->co_Auth.dr_PostCount++;
	    if (conn->co_Flags & COF_IHAVE) {
		MBLogPrintf(conn, &conn->co_TMBuf, "235\r\n");
	    } else {
		MBLogPrintf(conn, &conn->co_TMBuf, "239 %s\r\n",  conn->co_IHaveMsgId);
	    }
	}
    }

    zfreeStr(&conn->co_MemPool, &newsgroups);
    zfreeStr(&conn->co_MemPool, &msgid);
    zfreeStr(&conn->co_MemPool, &subject);
    zfreeStr(&conn->co_MemPool, &date);
    zfreeStr(&conn->co_MemPool, &xref);
    zfreeStr(&conn->co_MemPool, &control);
    zfreeStr(&conn->co_MemPool, &supers);
    zfreeStr(&conn->co_MemPool, &conn->co_IHaveMsgId);

    MBFree(&conn->co_ArtBuf);
    NNCommand(conn);
}

/* group already has a number in the list (an earlier name in Newsgroups) */
static int
ArtNumAssigned(const ArtNumAss *an, const char *group)
{
    int len = strlen(group);

    for (; an; an = an->an_Next) {
	if (an->an_GroupLen == len && memcmp(an->an_GroupName, group, len) == 0)
	    return(1);
    }
    return(0);
}

void
AssignArticleNo(Connection *conn, ArtNumAss **pan, const char *group, const char *xref, int approved, const char *art, int artLen, const char *msgid)
{
    const char *rec;
    int recLen;
    int groupLen = strlen(group);
    artno_t activeArtBeg;
    artno_t activeArtEnd;
    int aabegchanged = 0;
    int foundXRef = 0;
    int ts;
    char aabegbuf[20];
    char aaendbuf[20];
    artno_t artNo;
    char logbuf[1024];

    if (DOpts.ReaderAutoAddToActive) {
	/*
	 * locate group in active file and lock, create if it does not exist
	 * You will have to manually add a GD, M, and fix S as appropriate
	 * through some external process, if you use this.
	 */
	if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) == NULL) {
	    if (ValidGroupName(group) < 0) {
		/* logit(LOG_ERR, "group %s illegal", group); */
	    } else {
		char tsBuf[64];

		/*
		 * Create if absent (D-254): the group may have been made, and
		 * numbered, since the read above.  Then it is left as it is.
		 */
		if (KPDBCreate(KDBActive, group, "NB", "0000000001", KP_LOCK) == 0) {
		    KPDBWrite(KDBActive, group, "NE", "0000000000", KP_LOCK_CONTINUE);
		    sprintf(tsBuf, "%08x", (int)time(NULL));
		    KPDBWrite(KDBActive, group, "CTS", tsBuf, KP_LOCK_CONTINUE);
		    KPDBWrite(KDBActive, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
		    KPDBWrite(KDBActive, group, "S", "y", KP_UNLOCK);
		} else {
		    KPDBWrite(KDBActive, group, NULL, NULL, KP_UNLOCK);
		}
	}
            if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) == NULL) {
		snprintf(logbuf, sizeof(logbuf), "%s %s not in dactive", msgid, group);
		LogCmd(conn, '-', logbuf);
		if (DRIncomingLogPat != NULL)
		    LogIncoming("%s - %s%s", conn->co_Auth.dr_Host,
							"", logbuf);
		return;
	    }
	}
    } else {
	/*
	 * locate group in active file and lock
	 */
	if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) == NULL) {
	    snprintf(logbuf, sizeof(logbuf), "%s %s not in dactive", msgid, group);
	    LogCmd(conn, '-', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    return;
	}
    }

    /*
     * silently drop postings to moderated groups that do not have an
     * approved header.
     */

    if (approved == 0) {
	int flagsLen;
	const char *flags = KPDBGetField(rec, recLen, "S", &flagsLen, "y");

	/* "=other.group" is an INN alias (dsyncgroups copies it), not flags */
	if (flagsLen > 0 && flags[0] == '=')
	    flagsLen = 0;
	while (flagsLen > 0) {
	    if (*flags == 'm') {
		KPDBUnlock(KDBActive, rec);
		snprintf(logbuf, sizeof(logbuf), "%s %s unapproved", msgid, group);
		LogCmd(conn, '-', logbuf);
		if (DRIncomingLogPat != NULL)
		    LogIncoming("%s - %s%s", conn->co_Auth.dr_Host,
							"", logbuf);
		return;
	    }
	    --flagsLen;
	    ++flags;
	}
    }

    /*
     * assign article number.  Locate Xref: line if Xref's are enabled
     */

    activeArtEnd = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL,10);
    activeArtBeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL,10);
    ts = (int)strtoul(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
    artNo = activeArtEnd + 1;

    if (xref) {
	const char *test;

	for (test = strchr(xref, ' '); test; test = strchr(test, ' ')) {
	    ++test;
	    if (strncmp(test, group, groupLen) == 0 && test[groupLen] == ':') {
		artNo = strtoll(test + groupLen + 1, NULL, 10);
		foundXRef = 1;
		break;
	    }
	}
    }

    /*
     * If we did not find an XRef entry and we are in xref-slave mode,
     * drop the newsgroup on the floor.
     */

    if (foundXRef == 0 && DOpts.ReaderXRefSlaveHost != NULL) {
	KPDBUnlock(KDBActive, rec);
	snprintf(logbuf, sizeof(logbuf), "%s %s no valid xref present in slave mode", msgid, group);
	LogCmd(conn, '-', logbuf);
	if (DRIncomingLogPat != NULL)
	    LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	return;
    }

    if (artNo < 1)
	artNo = 1;

    if (activeArtEnd < artNo) {
	activeArtEnd = artNo;
	if (activeArtBeg > activeArtEnd) {
	    activeArtBeg = activeArtEnd;
	    aabegchanged = 1;
	}
    } else if (activeArtBeg > artNo) {
	activeArtBeg = artNo;
	aabegchanged = 1;
	if (activeArtEnd < activeArtBeg)
	    activeArtEnd = activeArtBeg;
    } else if (activeArtBeg > activeArtEnd) {
	activeArtBeg = activeArtEnd = artNo;
	aabegchanged = 1;
    }

    {
	int nts = (int)time(NULL);
	if (nts != ts) {
	    char tsBuf[64];
	    sprintf(tsBuf, "%08x", nts);
	    KPDBWrite(KDBActive, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
	}
    }

    sprintf(aabegbuf, "%010lld", activeArtBeg);
    sprintf(aaendbuf, "%010lld", activeArtEnd);

    if (aabegchanged)
	KPDBWrite(KDBActive, group, "NB", aabegbuf, KP_LOCK_CONTINUE);	/* continuing lock */
    KPDBWrite(KDBActive, group, "NE", aaendbuf, KP_UNLOCK);		/* and unlock 	   */

    {
	ArtNumAss *an = zalloc(&conn->co_MemPool, sizeof(ArtNumAss));
	an->an_Next = *pan;
	*pan = an;
	an->an_GroupName = group;
	an->an_GroupLen = strlen(group);
	an->an_ArtNo = artNo;
    }
}

int
WriteOverview(Connection *conn, ArtNumAss *an, const char *group, const char *xref, const char *art, int artLen, const char *msgid)
{
    artno_t artNo = 0;
    const char *body;
    char *xtmp = NULL;
    const char *xrefhost = DOpts.ReaderXRefHost ? DOpts.ReaderXRefHost : "";
    int xtmpLen = strlen("Xref: ") + strlen(xrefhost) + strlen("\r\n") + 1;
    char logbuf[1024];
    int err = 0;

    /*
     * Locate article number assignment
     */
    {
	ArtNumAss *scan;

	for (scan = an; scan; scan = scan->an_Next) {
	    if (scan->an_GroupName == group) {
		artNo = scan->an_ArtNo;
	    }
	    /* ' ' + group + ':' + the most a %lld can print */
	    xtmpLen += 1 + scan->an_GroupLen + 1 + 20;
	}
    }

    if (artNo == 0)
	return(0);

    /*
     * XXX We should find some way to aggregate these into one log entry.
     */

    snprintf(logbuf, sizeof(logbuf), "%s %s:%lld", msgid, group, artNo);

    /*
     * Locate start of body (we may have to append our own Xref: line)
     */

    {
	int l;
	int lnl = 1;

	for (l = 0; l < artLen; ++l) {
	    /*
	     * blank line terminates headers
	     */
	    if (art[l] == '\r' && (l + 1 >= artLen || art[l+1] == '\n')) {
		if (lnl)
		    break;
	    }
	    lnl = 0;
	    if (art[l] == '\n')
		lnl = 1;
	}
	body = art + l;
    }

    /*
     * Write overview record.
     */

    {
	off_t pos;
	OverInfo *ov;
	const OverArt *oa;
	hash_t hv = msgidhash(msgid);
	int actLen = 0;
	int iovLen = 0;
	int prealloc_ov, prealloc_oh;
	struct iovec iov[3];

	if ((ov = OpenOverInfo(group, 1)) == NULL) {
	    logit(LOG_ERR, "Error in GetOverInfo(%s) msgid=%s", group, msgid);
	    LogCmd(conn, '-', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    MBLogPrintf(conn, &conn->co_TMBuf, "400 Error writing data\r\n");
	    NNTerminate(conn);
	    return(1);
	}
	prealloc_ov = (ov->ov_MaxArts * sizeof(OverArt)) / 8;
	prealloc_oh = (1024 * ov->ov_Head->oh_DataEntries) / 8;

	if (MakeOverHFile(ov, artNo, 1) == NULL) {
	    logit(LOG_ERR, "Error in MakeOverHFile() msgid=%s", msgid);
	    LogCmd(conn, '-', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    MBLogPrintf(conn, &conn->co_TMBuf, "400 Error writing data\r\n");
	    NNTerminate(conn);
	    PutOverInfo(ov);
	    return(1);
	}

	if (hflock(ov->ov_HCache->od_HFd, 0, XLOCK_EX) < 0) {
	    logit(LOG_ERR, "error locking overview data for %s: %s",
		group, strerror(errno));
	    LogCmd(conn, '-', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    MBLogPrintf(conn, &conn->co_TMBuf, "400 Error writing data\r\n");
	    NNTerminate(conn);
	    PutOverInfo(ov);
	    return(1);
	}
	pos = lseek(ov->ov_HCache->od_HFd, 0L, 2);

	errno = 0;

	if (xref) {
	    iov[0].iov_base = (void *)art;
	    iov[0].iov_len = artLen + 1;
	    iovLen = 1;
	} else {
	    ArtNumAss *scan;
	    int soff;
	    
	    xtmp = zalloc(&conn->co_MemPool, xtmpLen);
	    sprintf(xtmp, "Xref: %s", xrefhost);
	    soff = strlen(xtmp);
		
	    for (scan = an; scan; scan = scan->an_Next) {
		xtmp[soff++] = ' ';
		memcpy(xtmp + soff, scan->an_GroupName, scan->an_GroupLen);
		soff += scan->an_GroupLen;
		sprintf(xtmp + soff, ":%lld", scan->an_ArtNo);
		soff += strlen(xtmp + soff);
	    }
	    sprintf(xtmp + soff, "\r\n");
	    soff += 2;
	    iov[0].iov_base = (void *)art;
	    iov[0].iov_len = body - art;
	    iov[1].iov_base = xtmp;
	    iov[1].iov_len = soff;
	    iov[2].iov_base = (void *)body;
	    iov[2].iov_len = (art + artLen + 1) - body;
	    iovLen = 3;
	}

	if (art[0] == 0)
	    logit(LOG_ERR, "Warning: art[0] is NIL! %s", xtmp);
		
	{
	    int i;
	    for (i = 0; i < iovLen; ++i)
		actLen += iov[i].iov_len;
	}

	if (DOpts.ReaderXRefSlaveHost && 
	    (oa = GetOverArt(ov, artNo, NULL)) != NULL &&
	    oa->oa_MsgHash.h1 == hv.h1 &&
	    oa->oa_MsgHash.h2 == hv.h2 &&
	    OA_ARTNOEQ(artNo, oa->oa_ArtNo)
	) {
	    /*
	     * We can detect duplicate articles in XRef slave mode.  If 
	     * we see one, do not do anything.
	     */
	    LogCmd(conn, 'd', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s d %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    ; /* EMPTY */
	} else if (DOpts.ReaderXOverMode == 0) {
	    /*
	     * Do not write xover info at all.  This mode is not really
	     * supported by the reader but may eventually be supported
	     * in 100% nntp-cache mode if/when we develop it.
	     */
	    logit(LOG_INFO, "ReaderXOverMode0 (%lld:%s)", artNo, msgid);
	    LogCmd(conn, 'm', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s m %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    ; /* EMPTY */
	} else if (
	    DOpts.ReaderXOverMode == 2 ||
	    (pos >= 0 &&
	     FilePreAllocSpace(ov->ov_HCache->od_HFd, pos,
	     prealloc_oh, actLen) == 0 &&
	     OverWriteV(ov->ov_HCache->od_HFd, iov, iovLen) == 0)
	) {
	    /*
	     * Our write of the overview data succeeded or we were asked not
	     * to write out the overview data.  Write out the overview 
	     * article record.
	     */
	    OverArt ovart = { 0 };
	    off_t ovpos = 0;
	    int locked = (hflock(ov->ov_OFd, 0, XLOCK_EX) == 0);
	    int lockerr = errno;

	    if (locked)
		(void)GetOverArt(ov, artNo, &ovpos);
	    else
		ovpos = -1;	/* take the error path below */

	    ovart.oa_ArtNo = OA_ARTNOSET(artNo);
	    if (DOpts.ReaderXOverMode == 2)
		ovart.oa_SeekPos = -1;
	    else
		ovart.oa_SeekPos = pos;
	    ovart.oa_Bytes = actLen - 1;	/* do not include \0 */
	    ovart.oa_MsgHash = hv;
	    ovart.oa_TimeRcvd = (int)time(NULL);
	    ovart.oa_ArtSize = (conn->co_ByteCounter > 0.0 ? conn->co_ByteCounter : conn->co_BytesHeader);
	    /*
	     * D-174: the body line count, counted here on a full feed, or
	     * from dnewslink's "Bytes: <size> <lines>" on a header feed
	     * (MODE HEADFEED).  A Bytes: header on a full feed is the
	     * poster's and says nothing.
	     */
	    if ((conn->co_Flags & COF_HEADFEED) == 0)
		ovart.oa_Lines = conn->co_BodyLines + 1;
	    else if (conn->co_LinesHeader >= 0)
		ovart.oa_Lines = conn->co_LinesHeader + 1;

	    if (ovpos < 0 ||
		FilePreAllocSpace(ov->ov_OFd, ovpos, prealloc_ov,
		    sizeof(ovart)) != 0 ||
		OverWriteAt(ov->ov_OFd, &ovart, sizeof(ovart), ovpos) < 0) {
		int error = locked ? errno : lockerr;

		/* The data append is not visible without a valid index entry. */
		if (DOpts.ReaderXOverMode != 2 && ftruncate(ov->ov_HCache->od_HFd, pos) < 0)
		    logit(LOG_ERR, "error rolling back overview data for %s: %s",
			group, strerror(errno));
		if (locked)
		    hflock(ov->ov_OFd, 0, XLOCK_UN);
		logit(LOG_ERR, "error %s overview index for %s: %s",
		    locked ? "writing" : "locking", group, strerror(error));
		LogCmd(conn, '-', logbuf);
		if (DRIncomingLogPat != NULL)
		    LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
		MBLogPrintf(conn, &conn->co_TMBuf, "400 Error writing data\r\n");
		NNTerminate(conn);
		err = 1;
	    } else {
		hflock(ov->ov_OFd, 0, XLOCK_UN);
		LogCmd(conn, '+', logbuf);
		if (DRIncomingLogPat != NULL)
		    LogIncoming("%s + %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    }
	} else {
	    if (pos >= 0 && ftruncate(ov->ov_HCache->od_HFd, pos) < 0)
		logit(LOG_ERR, "error rolling back overview data for %s: %s",
		    group, strerror(errno));
	    logit(LOG_ERR, "error writing overview data file for %s", group);
	    LogCmd(conn, '-', logbuf);
	    if (DRIncomingLogPat != NULL)
		LogIncoming("%s - %s%s", conn->co_Auth.dr_Host, "", logbuf);
	    MBLogPrintf(conn, &conn->co_TMBuf, "400 Error writing data\r\n");
	    NNTerminate(conn);
	    err = 1;
	}
	hflock(ov->ov_HCache->od_HFd, 0, XLOCK_UN);
	PutOverInfo(ov);
    }
    if (xtmp)
	zfree(&conn->co_MemPool, xtmp, xtmpLen);
    return(err);
}

void FlushOverCache(void)
{
    OverInfo *ov;
    OverInfo *next;

    for (ov = OvLruHead; ov; ov = next) {
	next = ov->ov_LruNext;
	if (ov->ov_Refs == 0)
	    OvCacheDrop(ov);
    }
}

/*
 * GetOverInfo() - the overview index of group for the read and cancel
 *		   paths (GROUP, LISTGROUP, overview, article and cancel
 *		   lookups), or NULL when the group has no index file.  The
 *		   callers take NULL as a group without articles.  The feed
 *		   (WriteOverview(), the caller of OpenOverInfo() with
 *		   create) creates a missing index file, so
 *		   selecting a group that never had an article leaves no
 *		   over.* file behind.  An index file that exists but is empty
 *		   or damaged is still initialised here, under the exclusive
 *		   lock, as before (a feed may be creating it right now).
 *		   An index with a newer header version is refused.
 */
OverInfo *
GetOverInfo(const char *group)
{
    return(OpenOverInfo(group, 0));
}

/*
 * OpenOverInfo() - GetOverInfo(); with create the index (and its
 *		    directory) is made when it does not exist yet.
 */
static OverInfo *
OpenOverInfo(const char *group, int create)
{
    int oflags = O_RDWR | (create ? O_CREAT : 0);
    OverInfo **pov = &OvHash[shash(group) & OVHMASK];
    OverInfo *ov;

    if (OvFdPressure)
	OvCacheShrink();
    while ((ov = *pov) != NULL) {
	if (strcmp(group, ov->ov_Group) == 0)
	    break;
	pov = &ov->ov_Next;
    }
    if (ov != NULL && ov != OvLruHead) {
	OvLruUnlink(ov);
	OvLruPushHead(ov);
    }
    if (ov == NULL) {
	OverExpire save;
	struct stat st;
	char *path;
	size_t pathSize;
	int iter = 0;
	artno_t endNo = 0;

	bzero(&st, sizeof(st));

	/*
	 * If the cache is full, close the least recently used groups.
	 * Groups still held stay open, so it may grow past the limit for a
	 * while.
	 */

	while (NumOverInfo >= OverCacheLimit() && OvCacheEvictOne())
	    ;
	ov = zalloc(&SysMemPool, sizeof(OverInfo));
	ov->ov_Group = zallocStr(&SysMemPool, group);
	/*
	 * zalloc() zeroed ov, so ov_OFd is 0 (stdin) until it is opened
	 * below.  Mark it closed now so an early FreeOverInfo() on an error
	 * path does not close(0).
	 */
	ov->ov_OFd = -1;
	GetOverExpire(group, &save);
	ov->ov_LimitSecs = save.oe_LimitDays * 24.0 * 60.0 * 60.0;

	{
	    const char *rec;
	    int recLen;
	    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK_RO, &recLen)) == NULL) {
		FreeOverInfo(ov);
		return(NULL);
	    }
	    iter = strtol(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);
	    endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    KPDBUnlock(KDBActive, rec);
	}

	/* GFName() may return up to PATH_MAX bytes ('readergrouphash hier' about twice the group name) */
	pathSize = strlen(MyGroupHome) + PATH_MAX + 2;
	path = zalloc(&SysMemPool, pathSize);
again:
	{
	    const char *gfname = GFName(group, GRPFTYPE_OVER, 0, 1, iter,
						&DOpts.ReaderGroupHashMethod);

	    snprintf(path, pathSize, "%s/%s", MyGroupHome, gfname);
	    ov->ov_OFd = -1;
	    if (create && MakeGroupDirectory(path) == -1)
		logit(LOG_ERR, "Error on overview dir open/create: %s (%s)",
						path, strerror(errno));
	    else if ((ov->ov_OFd = xopen(oflags, 0644, "%s", path)) < 0 &&
		     OvCacheReclaim(errno))
		ov->ov_OFd = xopen(oflags, 0644, "%s", path);
	    if (ov->ov_OFd >= 0)
		OvFdNote(ov->ov_OFd);
	}
	if (ov->ov_OFd < 0 && !create && errno == ENOENT) {
	    /* no index yet: a group without articles, not an error */
	    FreeOverInfo(ov);
	    ov = NULL;
	} else if (ov->ov_OFd < 0) {
	    logit(LOG_ERR, "Error on overview open/create for group %s: %s (%s)",
						group, path, strerror(errno));
	    FreeOverInfo(ov);
	    ov = NULL;
	} else {
	    OverHead oh;
	    int r;

	    /*
	     * Leave a shared lock on the over.* file so expireover knows when
	     * it is OK to resize the file.  If the file was renamed-over,
	     * we have to re-open it.
	     */

	    if (hflock(ov->ov_OFd, 4, XLOCK_SH) < 0) {
		logit(LOG_ERR, "error locking overview index for %s: %s (%s)",
		    group, path, strerror(errno));
		FreeOverInfo(ov);
		zfree(&SysMemPool, path, pathSize);
		return(NULL);
	    }

	    if (fstat(ov->ov_OFd, &st) < 0 || st.st_nlink == 0) {
		hflock(ov->ov_OFd, 4, XLOCK_UN);
		close(ov->ov_OFd);
		ov->ov_OFd = -1;
		goto again;
	    }

	    /*
	     * check if new overview file or illegal overview file and size 
	     * accordingly
	     */
	    r = (read(ov->ov_OFd, &oh, sizeof(oh)) != sizeof(oh));
	    if (r == 0 && st.st_size == 0) {
		r = -1;
	    }
	    if (r == 0 && oh.oh_ByteOrder != OH_BYTEORDER) {
		logit(LOG_CRIT, "Incorrect overview byte order for %s (%s)",
								group, path);
		r = -1;
	    }
	    if (r == 0 && oh.oh_Version > OH_VERSION) {
		logit(LOG_CRIT, "Incorrect overview version for %s (%s)",
								group, path);
		FreeOverInfo(ov);
		zfree(&SysMemPool, path, pathSize);
		return(NULL);
	    }
	    if (r == 0 &&
		(off_t)st.st_size - (off_t)oh.oh_HeadSize < (off_t)sizeof(OverArt)) {
		/* header present but no room for even one OverArt: treat as a
		 * new/empty file so it is re-initialised, instead of computing
		 * ov_MaxArts 0 and taking a modulo by zero in GetOverArt(). */
		r = -1;
	    }
	    if (r != 0) {
		if (hflock(ov->ov_OFd, 0, XLOCK_EX) < 0) {
		    logit(LOG_ERR, "error locking overview index for %s: %s (%s)",
			group, path, strerror(errno));
		    FreeOverInfo(ov);
		    zfree(&SysMemPool, path, pathSize);
		    return(NULL);
		}
		/*
		 * we have to test again after we got the lock in case
		 * another process had a lock and adjusted the file.  Use the
		 * size as it is now, not the stale fstat from before the lock.
		 */
		if (fstat(ov->ov_OFd, &st) < 0)
		    st.st_size = 0;
		lseek(ov->ov_OFd, 0L, 0);
		r = (read(ov->ov_OFd, &oh, sizeof(oh)) != sizeof(oh));
		/* Another writer may have installed a newer header while we waited. */
		if (r == 0 && oh.oh_ByteOrder == OH_BYTEORDER &&
		    oh.oh_Version > OH_VERSION) {
		    logit(LOG_CRIT, "Incorrect overview version for %s (%s)",
								    group, path);
		    hflock(ov->ov_OFd, 0, XLOCK_UN);
		    FreeOverInfo(ov);
		    zfree(&SysMemPool, path, pathSize);
		    return(NULL);
		}
		if (r != 0 ||
		    oh.oh_ByteOrder != OH_BYTEORDER ||
		    (off_t)st.st_size - (off_t)oh.oh_HeadSize < (off_t)sizeof(OverArt)
		) {

		    /*
		     * If 'aInitArts' option not given in expireCtl
		     */

		    if (save.oe_InitArts <= 0)
			save.oe_InitArts = DEFARTSINGROUP;

		    if (ftruncate(ov->ov_OFd, 0) < 0) {
			logit(LOG_ERR, "error truncating overview index for %s: %s",
			    group, strerror(errno));
			hflock(ov->ov_OFd, 0, XLOCK_UN);
			FreeOverInfo(ov);
			zfree(&SysMemPool, path, pathSize);
			return(NULL);
		    }
		    st.st_size = sizeof(oh) + sizeof(OverArt) * save.oe_InitArts;
		    FileAllocSpace(ov->ov_OFd, 0, st.st_size);
		    fsync(ov->ov_OFd);
		    bzero(&oh, sizeof(oh));
		    oh.oh_Version = OH_VERSION;
		    oh.oh_ByteOrder = OH_BYTEORDER;
		    oh.oh_HeadSize = sizeof(oh);
		    oh.oh_MaxArts = save.oe_InitArts;
		    strncpy(oh.oh_Gname, group, sizeof(oh.oh_Gname) - 1);
		    oh.oh_DataEntries = save.oe_DataSize;
		    if (OverWriteAt(ov->ov_OFd, &oh, sizeof(oh), 0) < 0 ||
			fsync(ov->ov_OFd) < 0) {
			logit(LOG_ERR, "error initializing overview index for %s: %s",
			    group, strerror(errno));
			hflock(ov->ov_OFd, 0, XLOCK_UN);
			FreeOverInfo(ov);
			zfree(&SysMemPool, path, pathSize);
			return(NULL);
		    }
		}
		hflock(ov->ov_OFd, 0, XLOCK_UN);
	    }
	    if (oh.oh_Version < 3)
		oh.oh_DataEntries = OD_HARTS;
	    if ((oh.oh_DataEntries<=0) || ((oh.oh_DataEntries ^ (oh.oh_DataEntries - 1)) != (oh.oh_DataEntries << 1) - 1))
		oh.oh_DataEntries = OD_HARTS;
	    if (oh.oh_Version > 1 && strcmp(oh.oh_Gname, group) != 0) {
		hflock(ov->ov_OFd, 4, XLOCK_UN);
		close(ov->ov_OFd);
		ov->ov_OFd = -1;
		iter++;
		goto again;
	    }
	    if (iter > 0) {
		char tsBuf[64];
		sprintf(tsBuf,"%06d", iter);
		KPDBWrite(KDBActive, group, "ITER", tsBuf, 0);   
	    }
	    ov->ov_Iter = iter;
	    ov->ov_endNo = endNo;
	    ov->ov_Size = st.st_size;
	    ov->ov_MaxArts = (st.st_size - oh.oh_HeadSize) / sizeof(OverArt);
	    ov->ov_DataEntryMask = oh.oh_DataEntries - 1;
	    ov->ov_Head = xmap(NULL, ov->ov_Size, PROT_READ, MAP_SHARED, ov->ov_OFd, 0);
	    if (ov->ov_Head == NULL) {
		logit(LOG_ERR, "Error on overview mmap for group %s (%s)",
						group, strerror(errno));
		FreeOverInfo(ov);
		ov = NULL;
	    } else {
		xadvise(ov->ov_Head, ov->ov_Size, XADV_SEQUENTIAL);
		++NumOverInfo;
		pov = &OvHash[shash(group) & OVHMASK];
		ov->ov_Next = *pov;
		*pov = ov;
		OvLruPushHead(ov);
	    }
	}
	zfree(&SysMemPool, path, pathSize);
    }
    if (ov)
	++ov->ov_Refs;
    return(ov);
}

const OverArt *
GetOverArt(OverInfo *ov, artno_t artno, off_t *ppos)
{
    const OverArt *oa;
    off_t ovpos = ov->ov_Head->oh_HeadSize + 
	    ((artno & 0x7FFFFFFFFFFFFFFFLL) % ov->ov_MaxArts) * sizeof(OverArt);

    /*
     * memory map the overview data.  Check overview record to
     * see if we actually have the requested information.
     */

    oa = (const OverArt *)((const char *)ov->ov_Head + ovpos);

    if (ppos)
	*ppos = ovpos;

    if (DebugOpt > 2)
	printf("OA %08lx %d,%lld %s\n", (long)oa, oa->oa_ArtNo, artno, OA_ARTNOEQ(artno, oa->oa_ArtNo) ? "(match)" : "(MISMATCH)");
    return(oa);
}

const char *
GetOverRecord(OverInfo *ov, artno_t artno, int *plen, int *alen, int *plines, TimeRestrict *tr, int *TimeRcvd)
{
    int hvpos;
    int xpos;
    int xsize;
    const OverArt *oa;
    OverData *od;

    oa = GetOverArt(ov, artno, NULL);

    if (oa == NULL || ! OA_ARTNOEQ(artno, oa->oa_ArtNo) || oa->oa_Bytes > OVER_HMAPSIZE / 2) {
	if (plen)
	    *plen = 0;
	return(NULL);
    }

    if (ov->ov_LimitSecs > 0) {

	int dt = (int)(CurTime.tv_sec - oa->oa_TimeRcvd);
	if (dt > ov->ov_LimitSecs) {
	    if (plen)
		*plen = 0;
	    return(NULL);
	}
    }

    if (tr && tr->tr_Time > oa->oa_TimeRcvd)
	return(NULL);

    if (TimeRcvd)
	*TimeRcvd = oa->oa_TimeRcvd;

    if (alen)
	*alen = oa->oa_ArtSize;
    if (plines)
	*plines = OA_LINES(oa);

    if (plen == NULL) 
	return((const char *)1);

    if (oa->oa_SeekPos == -1)
	return(NULL);

    if ((od = MakeOverHFile(ov, artno, 0)) == NULL)
	return(NULL);

    /*
     * hvpos / oa->oa_Bytes.  Include the guard character(s) in our 
     * calculations.
     */

    hvpos = oa->oa_SeekPos;

    xsize = oa->oa_Bytes + 1;
    if ((xpos = hvpos) != 0) {
	--xpos;
	++xsize;
    }

    if (
	od->od_HMapBase == NULL || 
	xpos < od->od_HMapPos || 
	xpos + xsize > od->od_HMapPos + od->od_HMapBytes
    ) {
	struct stat st;

	if (od->od_HMapBase) {
	    xunmap((void *)od->od_HMapBase, od->od_HMapBytes);
	    od->od_HMapBase = NULL;
	    od->od_HMapBytes = 0;
	    od->od_HMapPos = 0;
	}

	st.st_size = 0;
	fstat(od->od_HFd, &st);

	/*
	 * Make sure the file is big enough to map requested header.  It
	 * is possible for it to not be.
	 */

	if (xpos + xsize > st.st_size)
	    return(NULL);

	od->od_HMapPos = xpos & ~(HMAPALIGN-1);
	od->od_HMapBytes = OVER_HMAPSIZE;
	if (od->od_HMapBytes + od->od_HMapPos > st.st_size)
	    od->od_HMapBytes = st.st_size - od->od_HMapPos;

	od->od_HMapBase = xmap(NULL, od->od_HMapBytes, PROT_READ, MAP_SHARED, od->od_HFd, od->od_HMapPos);
	if (od->od_HMapBase == NULL) {
	    logit(LOG_CRIT, "mmap() failed B %s", strerror(errno));
	    exit(1);
	}
    }

    /*
     * Return base of record, length in *plen.  But check for corruption...
     * if the overview starts with a nul we have a problem.
     */

    *plen = oa->oa_Bytes;
    {
	const char *r = od->od_HMapBase + hvpos - od->od_HMapPos;

	if (*r == 0)
	    return(NULL);
	if (xpos < hvpos && r[-1] != 0) {
	    logit(LOG_ERR, "corrupt overview entry for %s:%lld", ov->ov_Group, artno);
	    return(NULL);
	}
	if (r[oa->oa_Bytes] != 0) {
	    logit(LOG_ERR, "corrupt overview entry for %s:%lld", ov->ov_Group, artno);
	    return(NULL);
	}
	return(r);
    }
}

int
ProcessOverMmapNeed(OverInfo *ov, Connection *conn, TimeRestrict *tr, artno_t artend, size_t maxsize, int *beg, int *end)
{
    artno_t i;
    int nart=0;
    const OverArt *oa;

    for (i=conn->co_ListBegNo; i<=artend; i++) {
	oa = GetOverArt(ov, i, NULL);
	if ( (oa == NULL)
		|| (! OA_ARTNOEQ(i, oa->oa_ArtNo)) 
		|| (tr && tr->tr_Time > oa->oa_TimeRcvd)
		|| (oa->oa_SeekPos < 0)
	) {
	    /* move artno to first available article */
	    if (i==conn->co_ListBegNo) conn->co_ListBegNo++;
	    continue;
	}
	if (nart) {
	    if (*beg > oa->oa_SeekPos) {
		if (*end-oa->oa_SeekPos+2>OVER_HMAPSIZE) break;
		*beg = oa->oa_SeekPos;
	    } else if (*end < oa->oa_SeekPos+oa->oa_Bytes) {
		if (oa->oa_SeekPos+oa->oa_Bytes-*beg+2>OVER_HMAPSIZE) break;
		*end = oa->oa_SeekPos+oa->oa_Bytes;
	    }
	} else {
	    *beg = oa->oa_SeekPos;
	    *end = oa->oa_SeekPos+oa->oa_Bytes;
	}
	nart++;
    }
    return nart;
}

void
OutputOverRange(OverInfo *ov, Connection *conn)
{
    int hvpos;
    int xpos=0;
    int xsize=0;
    int nart=0;
    const OverArt *oa;
    OverData *od;
    artno_t artBase = conn->co_ListBegNo & ~ov->ov_DataEntryMask;
    artno_t artend = conn->co_ListEndNo;
    TimeRestrict *tr = NULL;

    if (conn->co_ArtMode == COM_NEWNEWS)
	tr = &conn->co_TimeRestrict;

    /* one datafile at a time */
    if (artend>artBase+ov->ov_DataEntryMask) {
	artend = artBase+ov->ov_DataEntryMask;
    }
    /* process mmap needs */
    if ((conn->co_ListBegNo>artBase) || (artend<artBase+ov->ov_DataEntryMask)) {
	nart = ProcessOverMmapNeed(ov, conn, tr, artend, OVER_HMAPSIZE, &xpos, &xsize);
	if (nart==0) return;
	if (xpos) xpos--;
	xsize -= xpos-1;
    }

    /* mmaping datafile */
    if ((od = MakeOverHFile(ov, artBase, 0)) == NULL) {
	conn->co_ListBegNo = artend+1;
	return;
    }

    if (
	od->od_HMapBase == NULL || 
	nart == 0 ||
	xpos < od->od_HMapPos || 
	xpos + xsize > od->od_HMapPos + od->od_HMapBytes
    ) {
	struct stat st;
	int advise=XADV_WILLNEED;

	if (od->od_HMapBase) {
	    xunmap((void *)od->od_HMapBase, od->od_HMapBytes);
	    od->od_HMapBase = NULL;
	    od->od_HMapBytes = 0;
	    od->od_HMapPos = 0;
	}

	st.st_size = 0;
	fstat(od->od_HFd, &st);

	/*
	 * Make sure the file is big enough to map requested header.  It
	 * is possible for it to not be.
	 */

	if (!st.st_size) {
	    if (xsize) {
		logit(LOG_CRIT, "Group %s data file is empty (%i/%i)", conn->co_GroupName, st.st_size, xsize);
	    }
	    conn->co_ListBegNo = artend+1;
	    return;
	}
	if (xpos > st.st_size) {
	    logit(LOG_CRIT, "Group %s data file is too small to be mmapped (%i/%i)", conn->co_GroupName, st.st_size, xpos);
	    conn->co_ListBegNo = artend+1;
	    return;
	} 

	if (nart==0) {
	    if (st.st_size>OVER_HMAPSIZE) {
		nart = ProcessOverMmapNeed(ov, conn, tr, artend, OVER_HMAPSIZE, &xpos, &xsize);
		if (nart==0) return;
		if (xpos) xpos--;
		xsize -= xpos-1;
	    } else {
		xpos = 0;
		xsize = st.st_size;
	    }
	}
	
	od->od_HMapPos = xpos;
	if (xpos + xsize > st.st_size) {
	    /* check if first article is inside the file */
    	    oa = GetOverArt(ov, conn->co_ListBegNo, NULL);
	    if (oa->oa_SeekPos + oa->oa_Bytes > st.st_size) {
		logit(LOG_CRIT, "Group %s data file is too small to contain header #%lld (%i+%i>%i)", conn->co_GroupName, conn->co_ListBegNo, oa->oa_SeekPos, oa->oa_Bytes, st.st_size);
		conn->co_ListBegNo++;
		return;
	    }
	    logit(LOG_CRIT, "Group %s data file is too small to be fully mmapped (%i+%i>%i)", conn->co_GroupName, xpos, xsize, st.st_size);
	    od->od_HMapBytes = st.st_size - xpos;
	} else {
	    od->od_HMapBytes= xsize;
	}
	
	od->od_HMapBase = xmap(NULL, od->od_HMapBytes, PROT_READ, MAP_SHARED, od->od_HFd, od->od_HMapPos);
	if (od->od_HMapBase == NULL) {
	    logit(LOG_CRIT, "mmap() failed C (%s) group %s (%lld:%lld@%i:%i)", strerror(errno), conn->co_GroupName, conn->co_ListBegNo, artend, od->od_HMapPos, od->od_HMapBytes);
	    exit(1);
	}
	xadvise(od->od_HMapBase, od->od_HMapBytes, advise);
    }

    for( ; conn->co_ListBegNo <= artend ; conn->co_ListBegNo++) {

	oa = GetOverArt(ov, conn->co_ListBegNo, NULL);

	if (oa==NULL || ! OA_ARTNOEQ(conn->co_ListBegNo, oa->oa_ArtNo)) continue;
	if (tr && tr->tr_Time > oa->oa_TimeRcvd) continue;
    	if (oa->oa_SeekPos == -1) continue;

	/* check mmap */
	if ( (oa->oa_SeekPos<od->od_HMapPos) 
		|| (oa->oa_SeekPos+oa->oa_Bytes-od->od_HMapPos > od->od_HMapBytes)
	) return;

	/*
	 * hvpos / oa->oa_Bytes.  Include the guard character(s) in our 
	 * calculations.
	 */

	hvpos = oa->oa_SeekPos;

	xsize = oa->oa_Bytes + 1;
	if ((xpos = hvpos) != 0) {
	    --xpos;
	    ++xsize;
	}


	/*
	 * Return base of record, length in *plen.  But check for corruption...
	 * if the overview starts with a nul we have a problem.
	 */

	{
	    const char *r = od->od_HMapBase + hvpos - od->od_HMapPos;

	    if (*r == 0)
		continue;
	    if (xpos < hvpos && r[-1] != 0) {
		logit(LOG_ERR, "corrupt overview entry for %s:%lld", ov->ov_Group, conn->co_ListBegNo);
		continue;
	    }
	    if (r[oa->oa_Bytes] != 0) {
		logit(LOG_ERR, "corrupt overview entry for %s:%lld", ov->ov_Group, conn->co_ListBegNo);
		continue;
	    }
	    OutputOverview(conn, r, oa->oa_Bytes, oa->oa_ArtSize, OA_LINES(oa));
	}
    }
}

/* Most overview records looked at in one pass before other work gets a turn. */
#define OVERVIEW_PASS_RECORDS	10000

/*
 * Hold the overview only for this pass. Return 0 if the group has no
 * readable overview, -1 for a pending write error, or 1 after a pass.
 * The caller finishes the response or continues from co_ListBegNo.
 */
int
ReadOverviewRange(Connection *conn)
{
    OverInfo *ov;
    int xpat_count = 0;
    int nrec = 0;

    if ((ov = GetOverInfo(conn->co_GroupName)) == NULL)
	return(0);
    if (conn->co_TMBuf.mh_WError) {
	PutOverInfo(ov);
	return(-1);
    }

    while (conn->co_TMBuf.mh_Bytes < MBUF_HIWAT &&
	    conn->co_ListBegNo <= conn->co_ListEndNo
    ) {
#ifndef USE_OVER_MADVISE
	int resLen;
	int artSize;
	int lines;
	const char *res;
	TimeRestrict *tr = NULL;
#endif

	/*
	 * Bound the work done in one pass. A range of missing articles
	 * produces no output, so the buffer test above never stops it and
	 * a very wide range (xref-slave mode) would hold the process.
	 */
	if (++nrec > OVERVIEW_PASS_RECORDS) {
	    FD_SET(conn->co_Desc->d_Fd, &WFds);
	    break;
	}
	if (conn->co_ArtMode == COM_XPAT && ++xpat_count > 50) {
	    FD_SET(conn->co_Desc->d_Fd, &WFds);
	    break;
	}
#ifdef USE_OVER_MADVISE
	OutputOverRange(ov, conn);
#else
	if (conn->co_ArtMode == COM_NEWNEWS)
	    tr = &conn->co_TimeRestrict;
	if ((res = GetOverRecord(ov, conn->co_ListBegNo, &resLen, &artSize, &lines, tr, NULL)) != NULL)
	    OutputOverview(conn, res, resLen, artSize, lines);
	++conn->co_ListBegNo;
#endif	/* USE_OVER_MADVISE */
    }
    PutOverInfo(ov);
    return(1);
}

/*
 * OverMsgIdCheck() - whether the article artNo of ov is the article msgid,
 *		      for an overview entry whose hash matches msgid's (D-211).
 *
 *	The overview index stores only the hash of the Message-ID, and two
 *	Message-IDs can have the same hash (D-210).  The overview data holds
 *	the article's header, so its last Message-ID: header (the one the
 *	index hash is made from) is compared with msgid byte for byte
 *	(CacheMsgIdMatches(), dreaderd/cache.c).
 *
 *	Returns 1 if the header is read and has msgid, 0 if the header is
 *	read and has another Message-ID (a collision: not this article), and
 *	-1 if the header cannot be read: no header kept (oa_SeekPos -1, which
 *	readerxover trackonly writes for every record), the article older
 *	than the group's age limit, the entry gone, or a corrupt record.
 *	The callers keep the hash match for -1: dreaderd starts with
 *	readerxover trackonly only under readercanlock all or none, a cancel
 *	is not checked under all, and under the checking modes
 *	canLockDecide() refuses a target whose header it cannot read for the
 *	group that decides (CtlCancel() asks it only while no earlier group
 *	has decided; once the deciding group's check has passed, the
 *	candidates of later groups are cancelled without a check, and after
 *	a refusal no later group is cancelled).
 */

#define OMC_MATCH	1
#define OMC_OTHER	0
#define OMC_UNKNOWN	-1

static int
OverMsgIdCheck(OverInfo *ov, artno_t artNo, const char *msgid)
{
    const char *data;
    int hlen = 0;
    int off;
    int nl;
    int last = -1;

    if ((data = GetOverRecord(ov, artNo, &hlen, NULL, NULL, NULL, NULL)) == NULL ||
	hlen <= 0)
	return(OMC_UNKNOWN);
    /*
     * An article fed with two Message-ID: headers is indexed by the hash
     * of the last one (NNFeedOverview()), so only the last Message-ID:
     * header, one that starts a line before the blank line, is compared:
     * the compare then matches the index, and a first header that equals
     * the Message-ID of another article does not make this one match.
     * CacheMsgIdMatches() reads the first header it is given, so it gets
     * the last one.  POST refuses such an article (post.c), a feed does
     * not.
     */
    for (off = 0; off < hlen; off = nl + 1) {
	for (nl = off; nl < hlen && data[nl] != '\n'; ++nl)
	    ;
	if (nl == off || (nl == off + 1 && data[off] == '\r'))
	    break;
	if (nl - off >= 11 && strncasecmp(data + off, "Message-ID:", 11) == 0)
	    last = off;
    }
    if (last >= 0 && CacheMsgIdMatches(data + last, hlen - last, msgid))
	return(OMC_MATCH);
    return(OMC_OTHER);
}

/*
 * FindCancelMsgId() - Locate cancel by message-id in group, return OverInfo
 *			and article number if found.
 *
 *			Increment *pvalidGroups if the group is valid, leave
 *			alone otherwise.  Whether or not the message-id could
 *			be located.
 *
 *			An entry with the hash of msgid is the article if its
 *			header has msgid (OverMsgIdCheck()); one whose header
 *			has another Message-ID is skipped and the scan goes
 *			on (D-211).  An entry whose header cannot be read
 *			still matches by its hash: see OverMsgIdCheck().
 *			NB and NE are read under the record lock; the scan
 *			(which reads headers) runs after the unlock.
 */

OverInfo * 
FindCanceledMsg(const char *group, const char *msgid, artno_t *partNo, int *pvalidGroups)
{
    const char *rec;
    int recLen;
    OverInfo *ov = NULL;

    /*
     * Only a group in the active file counts as valid and is looked up.
     * Don't lock the record, meaning that we have to re-read it after
     * calling GetOverInfo (otherwise someone else can update the numeric
     * fields in the record while we are trying to process them).
     */

    *partNo = -1;

    if ((rec = KPDBReadRecord(KDBActive, group, 0, &recLen)) != NULL) {

	++*pvalidGroups;

	if ((ov = GetOverInfo(group)) != NULL) {
	    hash_t hv = msgidhash(msgid);

	    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK_RO, &recLen)) != NULL) {
		artno_t artBeg;
		artno_t artEnd;

		artBeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
		artEnd = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
		KPDBUnlock(KDBActive, rec);

		if (artEnd - artBeg > ov->ov_MaxArts)
		    artBeg = artEnd - ov->ov_MaxArts;

		while (artEnd >= artBeg) {
		    off_t ovpos = 0;
		    const OverArt *oa = GetOverArt(ov, artEnd, &ovpos);

		    if (OA_ARTNOEQ(artEnd, oa->oa_ArtNo) && 
			bcmp(&oa->oa_MsgHash, &hv, sizeof(hv)) == 0 &&
			OverMsgIdCheck(ov, artEnd, msgid) != OMC_OTHER
		    ) {
			/* the slot can be reused while the header is read */
			oa = GetOverArt(ov, artEnd, &ovpos);
			if (OA_ARTNOEQ(artEnd, oa->oa_ArtNo) &&
			    bcmp(&oa->oa_MsgHash, &hv, sizeof(hv)) == 0
			) {
			    *partNo = artEnd;
			    break;
			}
		    }
		    --artEnd;
		} /* while */
	    }
	    if (*partNo == -1) {
		PutOverInfo(ov);
		ov = NULL;
	    }
	} /* if */
    }
    return(ov);
}

/*
 * FindMsgInGroup() - the article number of msgid in group, for a reader
 *		      command by message-id (XHDR/XOVER/HDR/OVER, D-193), or
 *		      NULL (*partNo -1).  Like FindCanceledMsg() it reads NB
 *		      and NE under the record lock and walks the overview index
 *		      after unlocking (an article added meanwhile is simply not
 *		      found).  An entry with the hash of msgid counts only if
 *		      its header has msgid (OverMsgIdCheck()); a collision
 *		      is skipped and the scan goes on, and an entry whose
 *		      header cannot be read counts as not found (D-211).  It
 *		      looks at no more than maxScan articles,
 *		      newest first, so a reader cannot make a reader process walk
 *		      a whole big group, or hold the record the feed side needs,
 *		      with an unknown message-id.
 */
OverInfo *
FindMsgInGroup(const char *group, const char *msgid, artno_t *partNo, artno_t maxScan)
{
    const char *rec;
    int recLen;
    OverInfo *ov;
    artno_t artBeg;
    artno_t artEnd;
    hash_t hv;

    *partNo = -1;
    if (KPDBReadRecord(KDBActive, group, 0, &recLen) == NULL)
	return(NULL);
    if ((ov = GetOverInfo(group)) == NULL)
	return(NULL);
    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK_RO, &recLen)) == NULL) {
	PutOverInfo(ov);
	return(NULL);
    }
    artBeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
    artEnd = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
    KPDBUnlock(KDBActive, rec);

    if (artEnd - artBeg > ov->ov_MaxArts)
	artBeg = artEnd - ov->ov_MaxArts;
    if (maxScan > 0 && artEnd - artBeg >= maxScan)
	artBeg = artEnd - maxScan + 1;
    hv = msgidhash(msgid);
    for (; artEnd >= artBeg; --artEnd) {
	off_t ovpos = 0;
	const OverArt *oa = GetOverArt(ov, artEnd, &ovpos);

	if (OA_ARTNOEQ(artEnd, oa->oa_ArtNo) &&
	    bcmp(&oa->oa_MsgHash, &hv, sizeof(hv)) == 0 &&
	    OverMsgIdCheck(ov, artEnd, msgid) == OMC_MATCH) {
	    *partNo = artEnd;
	    return(ov);
	}
    }
    PutOverInfo(ov);
    return(NULL);
}

/*
 * CancelOverMsgId() - cancel overview by message-id given article number.
 */

int 
CancelOverArt(OverInfo *ov, artno_t artNo)
{
    int r = 0;

    if (ov != NULL) { 
	off_t ovpos = 0;
	const OverArt *oa;

	if (hflock(ov->ov_OFd, 0, XLOCK_EX) < 0) {
	    logit(LOG_ERR, "error locking overview index for %s: %s",
		ov->ov_Group, strerror(errno));
	    return(0);
	}
	oa = GetOverArt(ov, artNo, &ovpos);
	if (OA_ARTNOEQ(artNo, oa->oa_ArtNo)) {
	    OverArt copy = *oa;

	    copy.oa_ArtNo = -1;		/* CANCELED! */
	    if (OverWriteAt(ov->ov_OFd, &copy, sizeof(copy), ovpos) == 0)
		r = 1;
	    else
		logit(LOG_ERR, "error canceling overview record for %s: %s",
		    ov->ov_Group, strerror(errno));
	}
	hflock(ov->ov_OFd, 0, XLOCK_UN);
    }
    return(r);
}

OverData *
MakeOverHFile(OverInfo *ov, artno_t artNo, int create)
{
    artno_t artBase = artNo & ~ov->ov_DataEntryMask;
    OverData **pod;
    OverData *od;
    int count = 0;
    int hfd;
#ifdef USE_ZLIB
    int tmpfd, zfd;
    unsigned char *zmap, *tmpmap, *zpos;
    size_t hl;
    char tmpfile[256];
    struct stat st;
    unsigned long zlen;
    z_stream z;
    int code;
#endif

    if (create)
	create = O_CREAT;

    if (ov->ov_HCache && artBase == ov->ov_HCache->od_ArtBase)
	return(ov->ov_HCache);
    for (pod = &ov->ov_HData; (od = *pod) != NULL; pod = &od->od_Next) {
	if (artBase == od->od_ArtBase)
	    break;
	++count;
    }
    if (od == NULL) {
	const char *gfname = GFName(ov->ov_Group, GRPFTYPE_DATA, artBase, 1,
				ov->ov_Iter, &DOpts.ReaderGroupHashMethod);

	*pod = od = zalloc(&SysMemPool, sizeof(OverData));
        errno = 0;
	hfd = xopen(O_RDWR|create, 0644, "%s/%s", MyGroupHome, gfname);
	/*
	 * Only the feed (create) closes other groups here: a reader may still
	 * use a record of a group it released (see PutOverInfo()), and
	 * OpenOverInfo() is where that was always allowed to go.
	 */
	if (hfd < 0 && create && OvCacheReclaim(errno))
	    hfd = xopen(O_RDWR|create, 0644, "%s/%s", MyGroupHome, gfname);
#ifdef USE_ZLIB
	/* a data file dexpireover -z compressed (not without zlib) */
	if (hfd < 0 && ! create) {
	    zfd = xopen(O_RDONLY, 0644, "%s/%s.gz", MyGroupHome, gfname);
	    if (! (zfd < 0)) {

		snprintf(tmpfile, sizeof(tmpfile), "/tmp/dr-gzover.XXXXXXXX");
		tmpfd = mkstemp(tmpfile);

		if (! (tmpfd < 0)) {
		    st.st_size = 0;
	    	    fstat(zfd, &st);

		    zmap = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, zfd, 0);
	    	    if (! zmap) {
		        logit(LOG_ERR, "Unable to xmap for gzcat %s/%s: %s",
			    MyGroupHome,
		            gfname,
		            strerror(errno)
		        );
			/* as on the other failures: the temporary file is not kept */
			hfd = -1;
			close(tmpfd);
			tmpfd = -1;
		    } else {
		        zpos = zmap + (st.st_size >= 18 ? st.st_size - 4 : 0);
		        zlen = st.st_size < 18 ? 0 :
			       (((unsigned)zpos[0] & 0xff) |
			       ((unsigned)zpos[1] & 0xff) << 8 |
			       ((unsigned)zpos[2] & 0xff) << 16 |
			       ((unsigned)zpos[3] & 0xff) << 24);

		        if (zlen < 256 || zlen > (64 * 1048576)) {
			    logit(LOG_ERR, "Bad zlen %d for gzcat %s/%s",
			        zlen,
			        MyGroupHome,
			        gfname
			    );
			    xunmap((void *)zmap, st.st_size);
			    hfd = -1;
			    close(tmpfd);
			    tmpfd = -1;
		        } else {
		            /* a mapping past the end of the file would fault */
		            if (ftruncate(tmpfd, zlen) < 0)
		                tmpmap = NULL;
		            else
		                tmpmap = xmap(NULL, zlen, PROT_READ|PROT_WRITE, MAP_SHARED, tmpfd, 0);
	    	            if (! tmpmap) {
		                logit(LOG_ERR, "Unable to xmap for gztmp %s/%s: %s",
			            MyGroupHome,
		                    gfname,
		                    strerror(errno)
		                );
			        xunmap((void *)zmap, st.st_size);
			        hfd = -1;
			        close(tmpfd);
				tmpfd = -1;
		            } else {
				// handle gzip headers
				hl = gzHeaderLen(zmap, st.st_size);
				bzero(&z, sizeof(z));
				if (hl == 0) {
				    z.msg = "bad gzip header";
				    code = Z_DATA_ERROR;
				} else {
				// begin uncompress
			        z.next_in = zmap + hl;
			        z.avail_in = st.st_size - hl;
    
			        z.next_out = tmpmap;
			        z.avail_out = zlen;

//				z.zalloc = zalloc;
//				z.zfree = zfree;
//				z.opaque = &SysMemPool;
    
				inflateInit2(&z, -MAX_WBITS);
			        code = inflate(&z, Z_FINISH);
				inflateEnd(&z);
				}
    
			        if (code != Z_STREAM_END) {
				    logit(LOG_ERR, "inflate error (%i) for gzcat %s/%s: %s",
				        code, 
			                MyGroupHome,
		                        gfname,
				        z.msg
				    );
			            xunmap((void *)zmap, st.st_size);
			            xunmap((void *)tmpmap, zlen);
				    hfd = -1;
				    close(tmpfd);
				    tmpfd = -1;
			        } else {
			            xunmap((void *)zmap, st.st_size);
		                    hfd = tmpfd;

//				    od->od_HMapBase = tmpmap;
//				    od->od_HMapBytes = zlen;
			            xunmap((void *)tmpmap, zlen);
			        }
			    }
			}
		    }

		    close(zfd);
		    if (unlink(tmpfile) < 0) {
		        logit(LOG_ERR, "Unable to remove gztmp %s: %s",
		            tmpfile,
		            strerror(errno)
		        );
		    }
		} else {
		    logit(LOG_ERR, "Unable to open/create gztmp %s: %s",
		        tmpfile,
		        strerror(errno)
		    );
		    close(zfd);
		}
	    }
	}
#endif

	od->od_HFd = hfd;

	if (od->od_HFd < 0) {
	    if (create) {
		logit(LOG_ERR, "Unable to open/create %s/%s: %s",
		    MyGroupHome,
		    gfname,
		    strerror(errno)
		);
	    }
	    FreeOverData(od);
	    *pod = od = NULL;
	} else {
	    OvFdNote(od->od_HFd);
	    od->od_ArtBase = artBase;
	    if (count > DOpts.ReaderThreads) {
		OverData *t = ov->ov_HData;
		ov->ov_HData = t->od_Next;
		FreeOverData(t);
	    }
	}
    }
    ov->ov_HCache = od;

    return(od);
}

/*
 * PutOverInfo() - release the ref count, but do not immediately unmap or 
 *		   free the data (other routines depend on this).
 */

void
PutOverInfo(OverInfo *ov)
{
    if (ov != NULL)
	--ov->ov_Refs;
}

void
FreeOverInfo(OverInfo *ov)
{
    OverData *od;

    while ((od = ov->ov_HData) != NULL) {
	ov->ov_HData = od->od_Next;
	FreeOverData(od);
    }
    if (ov->ov_Head)
	xunmap((void *)ov->ov_Head, ov->ov_Size);
    if (ov->ov_OFd >= 0) {
	/*
	 * remove shared lock and close
	 */
	hflock(ov->ov_OFd, 4, XLOCK_UN);
	close(ov->ov_OFd);
    }
    zfreeStr(&SysMemPool, &ov->ov_Group);

    bzero(ov, sizeof(OverInfo));
    zfree(&SysMemPool, ov, sizeof(OverInfo));
}

void
FreeOverData(OverData *od)
{
    if (od->od_HMapBase) {
	xunmap((void *)od->od_HMapBase, od->od_HMapBytes);
	od->od_HMapBase = NULL;
    }
    if (od->od_HFd >= 0) {
	close(od->od_HFd);
	od->od_HFd = -1;
    }
    zfree(&SysMemPool, od, sizeof(OverData));
}

int
NNTestOverview(Connection *conn)
{
    OverInfo *ov;
    int r = -1;

    if ((ov = GetOverInfo(conn->co_GroupName)) != NULL) {
	if (GetOverRecord(ov, conn->co_ArtNo, NULL, NULL, NULL, NULL, NULL) != NULL)
	    r = 0;
	PutOverInfo(ov);
    }
    return(r);
}

const char *
NNRetrieveHead(Connection *conn, int *povlen, const char **pmsgid, int *TimeRcvd, int *grpIter, artno_t *endNo)
{
    OverInfo *ov;
    const char *res = NULL;

    *povlen = 0;
    if (pmsgid != NULL)
	*pmsgid = "<>";

    if (conn->co_GroupName == NULL) {
	return(NULL);
    }

    if ((ov = GetOverInfo(conn->co_GroupName)) != NULL) {
	if (grpIter != NULL)
	    *grpIter = ov->ov_Iter;
	if (endNo != NULL)
	    *endNo = ov->ov_endNo;
	if ((res = GetOverRecord(ov, conn->co_ArtNo, povlen, NULL, NULL, NULL, TimeRcvd)) != NULL) {

	    const char *scan = res;
	    int scanLen = *povlen;

	    /*
	     * Locate and extract the Message-ID
	     */

	    while (scanLen > 0) {
		int i;
		char ch = tolower((unsigned char)scan[0]);
		for (i = 0; i < scanLen && scan[i] != '\n'; ++i)
		    ;
		if (pmsgid != NULL && ch == 'm' &&
				strncasecmp(scan, "Message-ID:", 11) == 0) {
		    int b = 11;
		    int e;
		    char buf[MAXMSGIDLEN];

		    while (b < scanLen && (scan[b] == ' ' || scan[b] == '\t'))
			++b;
		    e = b;
		    while (e < scanLen && (scan[e] != '>'))
			++e;
		    if (e < scanLen)
			++e;
		    if (e - b < MAXMSGIDLEN) {
			bcopy(scan + b, buf, e - b);
			buf[e-b] = 0;
			*pmsgid = MsgId(buf, NULL);
		    }
		    /*
		     * Stop scanning through headers
		     */
		    break;
		}

		if (i == 1 && scan[0] == '\r') {
		    *povlen -= scanLen;
		    break;
		}
		if (i < scanLen)
		    ++i;
		scanLen -= i;
		scan += i;
	    }
	} 
	PutOverInfo(ov);
    }
    if (pmsgid != NULL && strcmp(*pmsgid, "<>") == 0)
	res = NULL;
    return(res);
}

