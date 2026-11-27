
/*
 * XOVER.C	- general nntp reader commands
 *
 *	Overview-related NNTP commands
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"
#include "overstore.h"

/* articles a reader command by message-id looks at, newest first (D-193) */
#define READER_MSGID_SCAN	100000

Prototype void NNTPNewNews(Connection *conn, char **pptr);
Prototype void NNTPXHdr(Connection *conn, char **pptr);
Prototype void NNTPXOver(Connection *conn, char **pptr);
Prototype void NNTPHdr(Connection *conn, char **pptr);
Prototype void NNTPOver(Connection *conn, char **pptr);
Prototype void NNTPXZver(Connection *conn, char **pptr);
Prototype void NNTPXPat(Connection *conn, char **pptr);
Prototype void NNTPXPath(Connection *conn, char **pptr);
Prototype int OutputOverview(Connection *conn, const char *res, int resLen, int artSize, int lines);

void NNListNewNews(Connection *conn);
void NNStartListOverview(Connection *conn, const char *hdr, const char *ran, const char *pat, int mode);

void NNStartListOverviewRange(Connection *conn);
static void StartListOverviewMsgId(Connection *conn);
void NNListOverviewRange(Connection *conn);
static int StartListOverviewRangeRun(Connection *conn);
static int ListOverviewRangeRun(Connection *conn);

Prototype char *OverViewFmt;

char *OverViewFmt = OVERVIEW_FMT;

void
NNTPNewNews(Connection *conn, char **pptr)
{
    char *newsgroups = NULL;
    const char *yymmdd = NULL;
    const char *hhmmss = NULL;
    const char *gmt = NULL;
    const char *distributions = NULL;
    const char *extra = NULL;
    const char *opt = NULL;
 
    if (!conn->co_Auth.dr_ReaderDef->rd_AllowNewnews) {
	MBLogPrintf(conn, &conn->co_TMBuf, "500 \"newnews\" not implemented\r\n");
	NNCommand(conn);
	return;
    }

    if (pptr) {
	newsgroups = parseword(pptr, " \t");
	yymmdd = parseword(pptr, " \t");
	hhmmss = parseword(pptr, " \t");
	opt = parseword(pptr, " \t");
	if (opt && strcasecmp(opt, "GMT") == 0) {
	    gmt = opt;
	    distributions = parseword(pptr, " \t");
	} else {
	    distributions = opt;
	}
	extra = parseword(pptr, " \t");
    }
    /*
     * readernewnewsmaxlen: the wildmat is matched against every active
     * group (ListActiveGroups()), so its length bounds that work.  INN's
     * nnrpd refuses an argument over 497 bytes the same way.  0 (the
     * default) sets no limit.
     */
    if (newsgroups != NULL && DOpts.ReaderNewnewsMaxLen > 0 &&
		strlen(newsgroups) > (size_t)DOpts.ReaderNewnewsMaxLen) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 Argument too long\r\n");
	NNCommand(conn);
	return;
    }
    if (newsgroups == NULL || extra != NULL ||
	(distributions && (strlen(distributions) < 3 || distributions[0] != '<' ||
	 distributions[strlen(distributions) - 1] != '>')) ||
	SetTimeRestrict(&conn->co_TimeRestrict, yymmdd, hhmmss, gmt) < 0) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 newsgroups yymmdd hhmmss [GMT] [<distributions> (ignored)]\r\n");
	NNCommand(conn);
    } else {
	if (conn->co_Auth.dr_Flags & DF_GROUPLOG)
	    logit(LOG_INFO, "info %s%s%s%s%s newnews %s %s %s %s %s",
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",  
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		conn->co_Auth.dr_Host,
		newsgroups,
		yymmdd,
		hhmmss,
		gmt ? gmt : "-",
		distributions ? distributions : "-");

	MBLogPrintf(conn, &conn->co_TMBuf, "230 New news follows.\r\n");
	zfreeStr(&conn->co_MemPool, &conn->co_ListPat);
	zfreeStr(&conn->co_MemPool, &conn->co_ListHdrs);
	/* RFC 977 distributions are accepted for compatibility, not filtered. */
	conn->co_ListHdrs = zallocStr(&conn->co_MemPool, "Message-ID");
	conn->co_ArtMode = COM_NEWNEWS;
	conn->co_ListCacheGroups = NULL;
	ListActiveGroups(conn, newsgroups);
	/*
	 * The scan walks co_GroupName over the matching groups; the group the
	 * client selected is kept aside and put back at the end.
	 */
	conn->co_SavedGroupName = conn->co_GroupName;
	conn->co_GroupName = NULL;
	NNListNewNews(conn);
    }
}

/*
 * NEWNEWS lists the groups of the list one after the other.  A group with
 * nothing to list (no such group, no overview, no article in range or in the
 * time) must not call back into this function for the next one: a line of
 * some 110000 group names that hold nothing would nest one call per name
 * and overflow the stack of the reader process (4 MB on NetBSD), which
 * serves all its clients.  StartListOverviewRangeRun() and
 * ListOverviewRangeRun() return nonzero when the group is done and the next
 * one is to be taken, and the loop here takes it.
 */
void
NNListNewNews(Connection *conn)
{
    for (;;) {
	GroupList *gl;

	conn->co_Func = NNListNewNews;
	conn->co_State = "listnewnews";

	if (conn->co_ListCacheGroups == NULL) {
	    MBPrintf(&conn->co_TMBuf, ".\r\n");
	    if (conn->co_GroupName)
		zfreeStr(&conn->co_MemPool, &conn->co_GroupName);
	    conn->co_GroupName = conn->co_SavedGroupName;
	    conn->co_SavedGroupName = NULL;
	    NNCommand(conn);
	    return;
	}
	gl = conn->co_ListCacheGroups;
	if (conn->co_GroupName)
	    zfreeStr(&conn->co_MemPool, &conn->co_GroupName);
	conn->co_GroupName = gl->group;
	conn->co_ListBegNo = 0;
	conn->co_ListEndNo = LL_MAX;
	conn->co_ListCacheGroups = gl->next;
	zfree(&conn->co_MemPool, gl, sizeof(GroupList));
	if (!StartListOverviewRangeRun(conn))
	    return;
    }
}

void 
NNTPXHdr(Connection *conn, char **pptr)
{
    char *hdr;

    conn->co_ListStrict = 0;
    if ((hdr = parseword(pptr, " \t")) == NULL) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 header [range|MessageID]\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, hdr, parseword(pptr, " \t"), NULL, COM_XHDR);
    return;
}

void 
NNTPXOver(Connection *conn, char **pptr)
{
    conn->co_ListStrict = 0;
    NNStartListOverview(conn, OverViewFmt, parseword(pptr, " \t"), NULL, COM_XOVER);
    return;
}

/*
 * HDR and OVER (RFC 3977 8.3, 8.5): as XHDR and XOVER, but with the RFC's
 * reply codes: 225 for HDR, and 420/423 instead of an empty list when there
 * is no current article or no article in the range.  XHDR and XOVER keep
 * their old behaviour for existing clients.
 */
void
NNTPHdr(Connection *conn, char **pptr)
{
    char *hdr;

    conn->co_ListStrict = 1;
    if ((hdr = parseword(pptr, " \t")) == NULL) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 header [range|MessageID]\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, hdr, parseword(pptr, " \t"), NULL, COM_XHDR);
}

void
NNTPOver(Connection *conn, char **pptr)
{
    conn->co_ListStrict = 1;
    NNStartListOverview(conn, OverViewFmt, parseword(pptr, " \t"), NULL, COM_XOVER);
}

void 
NNTPXZver(Connection *conn, char **pptr)
{
    conn->co_ListStrict = 0;
#ifdef USE_ZLIB
    if (conn->co_Auth.dr_ReaderDef->rd_XzverLevel < 0) {
#else
    if (1) {	/* built without zlib */
#endif
	MBLogPrintf(conn, &conn->co_TMBuf, "500 \"xzver\" not enabled\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, OverViewFmt, parseword(pptr, " \t"), NULL, COM_XZVER);
    return;
}

void 
NNTPXPat(Connection *conn, char **pptr)
{
    conn->co_ListStrict = 0;
    char *hdr;
    char *ran;
    char *pat;

    if ((hdr = parseword(pptr, " \t")) == NULL ||
	(ran = parseword(pptr, " \t")) == NULL ||
	*(pat = *pptr) == '\0'
    ) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 header range|MessageID pat\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, hdr, ran, pat, COM_XPAT);
}

void 
NNTPXPath(Connection *conn, char **pptr)
{
    MBLogPrintf(conn, &conn->co_TMBuf, "500 What? (xpath not implemented yet)\r\n");
    NNCommand(conn);
}

void
FinishOverviewDotNewline(Connection *conn)
{
    if (conn->co_ArtMode == COM_XZVER) {
        MZPrintf(conn, ".\r\n");
        MZFinish(conn);
    } else {
        MBPrintf(&conn->co_TMBuf, ".\r\n");
    }
}


/*
 * NNStartListOverview() - list selected overview for specified article range
 *			   or message-id
 */

void
NNStartListOverview(Connection *conn, const char *hdr, const char *ran, const char *pat, int mode)
{
    zfreeStr(&conn->co_MemPool, &conn->co_ListPat);
    zfreeStr(&conn->co_MemPool, &conn->co_ListHdrs);
    zfreeStr(&conn->co_MemPool, &conn->co_ListMsgId);
    conn->co_ListPat = (pat) ? zallocStr(&conn->co_MemPool, pat) : NULL;
    conn->co_ListHdrs = zallocStr(&conn->co_MemPool, hdr);

    if (ran == NULL || ran[0] != '<') {
	if (conn->co_GroupName == NULL) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "412 Not in a newsgroup\r\n");
	    NNCommand(conn);
	    return;
	}
    }

    /*
     * HDR and OVER answer an error instead of an empty list: 420 without a
     * current article, 423 when the range holds no article, 501 for a range
     * that isn't one.  The message-id form is handled below.
     */
    if (conn->co_ListStrict) {
	if (ran == NULL) {
	    if (NoCurrentArticle(conn)) {
		MBLogPrintf(conn, &conn->co_TMBuf, "420 No current article selected\r\n");
		NNCommand(conn);
		return;
	    }
	} else if (ran[0] != '<') {
	    char *p;
	    char *q;
	    artno_t lo = strtoll(ran, &p, 10);
	    artno_t hi;
	    int bad = (p == ran || lo < 0);
	    artno_t lbeg = conn->co_ArtBeg;
	    artno_t lend = conn->co_ArtEnd;
	    const char *rec;
	    int recLen;

	    /* the group's range now, not as of GROUP */
	    if ((rec = KPDBReadRecord(KDBActive, conn->co_GroupName, KP_LOCK_RO, &recLen)) != NULL) {
		lbeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "0"), NULL, 10);
		lend = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "0"), NULL, 10);
		KPDBUnlock(KDBActive, rec);
	    }

	    if (!bad) {
		lo = artno_input(conn->co_ArtBeg, conn->co_ArtEnd, lo, conn->co_Numbering);
		if (lo < 0)		/* above the RFC 3977 limit */
		    bad = 1;
	    }
	    hi = lo;
	    if (!bad && *p == '-') {
		q = p + 1;
		if (*q == 0) {
		    hi = lend;		/* n-: to the end */
		    p = q;
		} else {
		    hi = strtoll(q, &p, 10);
		    if (p == q || hi < 0)
			bad = 1;
		    else {
			hi = artno_input(conn->co_ArtBeg, conn->co_ArtEnd, hi, conn->co_Numbering);
			if (hi < 0)
			    bad = 1;
		    }
		}
	    }
	    if (bad || *p != 0) {
		MBLogPrintf(conn, &conn->co_TMBuf, "501 Bad range %s\r\n", ran);
		NNCommand(conn);
		return;
	    }
	    if (lo > hi || lo > lend || hi < lbeg) {
		MBLogPrintf(conn, &conn->co_TMBuf, "423 No articles in that range\r\n");
		NNCommand(conn);
		return;
	    }
	}
    }

    conn->co_ArtMode = mode;

    /*
     * A message-id is resolved before the "follows" response, so an unknown
     * one answers 430 instead of an empty 221/224 list.  The found article
     * number and message-id are kept for StartListOverviewMsgId below.
     */
    if (ran != NULL && ran[0] == '<') {
	const char *mid = MsgId(ran, NULL);
	artno_t artNo = -1;

	/* Try the bounded current-group overview lookup first. */
	if (mid != NULL && strcmp(mid, "<>") != 0 && conn->co_GroupName != NULL)
	    artNo = OverStoreFindMessageId(conn->co_GroupName, mid, READER_MSGID_SCAN);
	if (artNo < 0) {
	    if (mode == COM_XHDR && mid != NULL && strcmp(mid, "<>") != 0) {
		/* HEAD is queued on the existing nonblocking spool machinery.
		 * Do not select a group or alter the current article for it.
		 * Overview metadata is unavailable on this fallback path.
		 */
		if (hdr[0] == ':') {
		    MBLogPrintf(conn, &conn->co_TMBuf, "503 Metadata unavailable by message-id outside this group\r\n");
		    NNCommand(conn);
		} else {
		    conn->co_ListMsgId = zallocStr(&conn->co_MemPool, mid);
		    conn->co_RequestFlags = ARTFETCH_MSGID;
		    NNServerRequest(conn, NULL, mid,
			conn->co_Auth.dr_ReaderDef->rd_MaxAgePolicy,
			SREQ_HDR, 0, -1, 0);
		    NNWaitThread(conn);
		}
		return;
	    }
	    MBLogPrintf(conn, &conn->co_TMBuf, "430 No such article\r\n");
	    NNCommand(conn);
	    return;
	}
	conn->co_ListBegNo = artNo;
	conn->co_ListEndNo = artNo;
	conn->co_ListMsgId = zallocStr(&conn->co_MemPool, mid);
	ran = mid;
    }

    switch(mode) {
    case COM_XPAT:
	MBLogPrintf(conn, &conn->co_TMBuf, "221 %s matches follow.\r\n", hdr);
	break;
    case COM_XHDR:
	if (conn->co_ListStrict)
	    MBLogPrintf(conn, &conn->co_TMBuf, "225 Headers follow\r\n");
	else
	    MBLogPrintf(conn, &conn->co_TMBuf, "221 %s data follows\r\n", hdr);
	break;
    case COM_XOVER:
	if (conn->co_ListStrict)
	    MBLogPrintf(conn, &conn->co_TMBuf, "224 Overview information follows\r\n");
	else
	    MBLogPrintf(conn, &conn->co_TMBuf, "224 data follows\r\n");
	break;
    case COM_XZVER:
	if (MZInit(conn, &conn->co_TMBuf, conn->co_Auth.dr_ReaderDef->rd_XzverLevel) < 0) {
	  MBLogPrintf(conn, &conn->co_TMBuf, "500 compress init error\r\n");
	  NNCommand(conn);
	  return;
	}
#ifdef USE_ZLIB
	MBLogPrintf(conn, &conn->co_TMBuf, "224 compressed data follows (zlib version %s)\r\n", zlibVersion());
#endif
	break;
    default:
	MBLogPrintf(conn, &conn->co_TMBuf, "500 software error\r\n");
	NNCommand(conn);
	return;
    }
    if (ran == NULL) {
	conn->co_ListBegNo = conn->co_ArtNo;
	conn->co_ListEndNo = conn->co_ArtNo;
	NNStartListOverviewRange(conn);
    } else if (ran[0] != '<') {
	char *p;
	conn->co_ListBegNo = artno_input(conn->co_ArtBeg, conn->co_ArtEnd, strtoll(ran, &p, 10), conn->co_Numbering);
	if (*p == '-') {
	    if (p[1] == 0)
		conn->co_ListEndNo = LL_MAX;
	    else
		conn->co_ListEndNo = artno_input(conn->co_ArtBeg, conn->co_ArtEnd, strtoll(p + 1, NULL, 10), conn->co_Numbering);
	} else {
	    conn->co_ListEndNo = conn->co_ListBegNo;
	}

	/* Protect against some overflow modes around 2^31 with RFC3977... */
	{
	    artno_t xlb = artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering);
	    artno_t xle = artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListEndNo, conn->co_Numbering);

	    if ((xle - xlb) < (conn->co_ListEndNo - conn->co_ListBegNo)) {
		conn->co_ListEndNo = conn->co_ListBegNo + (xle - xlb);
	    }
	}

	NNStartListOverviewRange(conn);
    } else {
	StartListOverviewMsgId(conn);
    }
}

void
NNStartListOverviewRange(Connection *conn)
{
    if (StartListOverviewRangeRun(conn))
	NNListNewNews(conn);
}

/*
 * Nonzero: NEWNEWS has nothing to list in this group, take the next one
 * (the caller does, see NNListNewNews()).
 */
static int
StartListOverviewRangeRun(Connection *conn)
{
    /*
     * trim list range
     */
    const char *rec;
    int recLen;
    artno_t lbeg;
    artno_t lend;

    /*
     * Handle user bogosity
     */

    if (conn->co_ListBegNo < 0)
	conn->co_ListBegNo = 0;
    if (conn->co_ListEndNo < 0)
	conn->co_ListEndNo = 0;

    if ((rec = KPDBReadRecord(KDBActive, conn->co_GroupName, KP_LOCK_RO, &recLen)) == NULL) {
	if (conn->co_ArtMode == COM_NEWNEWS)
	    return(1);			/* no such group: the next one */
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return(0);
    }
    lbeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "0"), NULL, 10);
    lend = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "0"), NULL, 10);
    KPDBUnlock(KDBActive, rec);

    /*
     * Handle some stupid case begin and end numbers
     */
    if ((conn->co_ListBegNo < lbeg && conn->co_ListEndNo < lbeg) ||
		conn->co_ListBegNo > lend ||
		conn->co_ListEndNo < lbeg ||
		conn->co_ListBegNo > conn->co_ListEndNo) {
	if (conn->co_ArtMode == COM_NEWNEWS)
	    return(1);			/* nothing in this group: the next one */
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return(0);
    }
    if (conn->co_ListBegNo < lbeg)
	conn->co_ListBegNo = lbeg;
    if (conn->co_ListEndNo > lend)
	conn->co_ListEndNo = lend;
    return(ListOverviewRangeRun(conn));
}

void
NNListOverviewRange(Connection *conn)
{
    if (ListOverviewRangeRun(conn))
	NNListNewNews(conn);
}

/* Bound each pass even when a range of holes produces no output. */
#define OVERVIEW_PASS_RECORDS	10000

typedef struct OverviewRangePass {
    Connection *conn;
    int writeError;
    int steps;
    int xpatSteps;
} OverviewRangePass;

static int
OverviewRangeCanContinue(void *context)
{
    OverviewRangePass *pass = context;
    Connection *conn = pass->conn;

    if (pass->writeError || conn->co_TMBuf.mh_Bytes >= MBUF_HIWAT)
	return(0);
    if (++pass->steps > OVERVIEW_PASS_RECORDS ||
	(conn->co_ArtMode == COM_XPAT && ++pass->xpatSteps > 50)) {
	FD_SET(conn->co_Desc->d_Fd, &WFds);
	return(0);
    }
    return(1);
}

static void
OverviewRangeEmit(void *context, artno_t number, const char *header,
		  int headerLen, int articleSize, int lines)
{
    OverviewRangePass *pass = context;
    Connection *conn = pass->conn;

    conn->co_ListBegNo = number;
    OutputOverview(conn, header, headerLen, articleSize, lines);
}

/*
 * Nonzero: the group of a NEWNEWS list is done, take the next one.
 */
static int
ListOverviewRangeRun(Connection *conn)
{
    OverviewRangePass pass = {
	.conn = conn,
	.writeError = conn->co_TMBuf.mh_WError
    };
    OverStoreRange range = {
	.group = conn->co_GroupName,
	.next = conn->co_ListBegNo,
	.last = conn->co_ListEndNo,
	.timeRestrict = conn->co_ArtMode == COM_NEWNEWS ? &conn->co_TimeRestrict : NULL,
	.canContinue = OverviewRangeCanContinue,
	.emit = OverviewRangeEmit,
	.context = &pass
    };
    int result;

    conn->co_Func = NNListOverviewRange;
    conn->co_State = "listover";

    result = OverStoreReadRange(&range);
    conn->co_ListBegNo = range.next;
    if (result == 0 && conn->co_ArtMode == COM_NEWNEWS)
	return(1);			/* no overview for this group: the next one */
    if (result == 0 || pass.writeError) {
	/* This also cleans up compression state after a write error. */
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return(0);
    }
    if (conn->co_ListBegNo > conn->co_ListEndNo) {
	/*
	 * NEWNEWS: only this group is done.  NNListNewNews() takes the next
	 * one and calls NNCommand() after the final ".".  NNCommand() here
	 * would read the next command the client sent and answer it inside
	 * the list, when this group took more than one pass.  The time stamp
	 * NNCommand() set here stays (for the idle timeout): it is refreshed
	 * at each group end, as before, not during one group.
	 */
	if (conn->co_ArtMode == COM_NEWNEWS) {
	    conn->co_LastActiveTime = CurTime.tv_sec;
	    return(1);
	}
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
    }
    return(0);
}

static void
StartListOverviewMsgId(Connection *conn)
{
    OverStorePoint point;

    /*
     * The message-id was already resolved in NNStartListOverview, which set
     * co_ListBegNo to the article and co_ListMsgId to the message-id (the
     * first field of the output).  Fetch the record and output it.
     */
    if (!OverStoreReadPoint(conn->co_GroupName, conn->co_ListBegNo, &point)) {
	MBLogPrintf(conn, &conn->co_TMBuf, "430 No such article\r\n");
	zfreeStr(&conn->co_MemPool, &conn->co_ListMsgId);
	NNCommand(conn);
	return;
    }
    OutputOverview(conn, point.header, point.headerLen, point.articleSize, point.lines);
    /* only this list uses it: no later list may print it as its index */
    zfreeStr(&conn->co_MemPool, &conn->co_ListMsgId);
    FinishOverviewDotNewline(conn);
    NNCommand(conn);
}

/*
 * The first field of an overview line.  For a message-id lookup: the
 * message-id for XHDR (RFC 2980 2.6), and 0 for HDR and OVER (RFC 3977 8.3.2
 * and 8.5.2: the article number is replaced by 0) and for XOVER, which RFC
 * 2980 gives no message-id form (INN answers it like OVER).  Else the article
 * number.
 */
static void
OutputListIndex(Connection *conn)
{
    if (conn->co_ListMsgId != NULL) {
	const char *f = (conn->co_ArtMode == COM_XHDR && !conn->co_ListStrict) ?
	    conn->co_ListMsgId : "0";

	if (conn->co_ArtMode == COM_XZVER)
	    MZWrite(conn, f, strlen(f));
	else
	    MBWrite(&conn->co_TMBuf, f, strlen(f));
    } else if (conn->co_ArtMode == COM_XZVER) {
	MZPrintf(conn, "%lld",
	    artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
    } else {
	MBPrintf(&conn->co_TMBuf, "%lld",
	    artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
    }
}

int
OutputOverview(Connection *conn, const char *res, int resLen, int artSize, int lines)
{
    const char *hdr = conn->co_ListHdrs;
    int didIndex = 0;

    while (*hdr) {
	int hlen;
	int hhlen;
	int rleft;
	int printHdr = 0;
	const char *rline;
	char hch;
	char ch;

	/*
	 * locate next header
	 */

	while (*hdr == '\r' || *hdr == '\n')
	    ++hdr;
	for (hlen = 0; hdr[hlen]; ++hlen) {
	    if (hdr[hlen] != '\n')
		continue;
	    if (hdr[hlen+1] == ' ' || hdr[hlen+1] == '\t')	/* multiline */
		continue;
	    ++hlen;
	    break;
	}
	if (hlen == 0)
	    break;

	/*
	 * locate header name non-inclusive of colon
	 */

	for (hhlen = 0; hhlen < hlen && hdr[hhlen] != ':'; ++hhlen)
	    ;

	/*
	 * scan overview info for header
	 */
	rline = res;
	rleft = resLen;

	hch = tolower((unsigned char)*hdr);

	/*
	 * D-174: Lines (the 8th OVER field) and HDR :lines are the :lines
	 * metadata, the body line count (RFC 3977 8.1.2, 8.4.2), when the feed
	 * gave one; otherwise the stored Lines: header as before.
	 */
	if (lines >= 0 &&
	    (conn->co_ArtMode == COM_XOVER || conn->co_ArtMode == COM_XZVER ||
	     conn->co_ArtMode == COM_XHDR) &&
	    ((hhlen == 5 && strncasecmp(hdr, "Lines", 5) == 0) ||
	     (hhlen == 0 && hlen >= 6 && strncasecmp(hdr, ":lines", 6) == 0))
	) {
	    char sep = (conn->co_ArtMode == COM_XHDR) ? ' ' : '\t';

	    if (didIndex == 0)
		OutputListIndex(conn);
	    if (conn->co_ArtMode == COM_XZVER)
		MZPrintf(conn, "%c%d", sep, lines);
	    else
		MBPrintf(&conn->co_TMBuf, "%c%d", sep, lines);
	    didIndex = 1;
	    hdr += hlen;
	    continue;
	}

	while (rleft) {
	    int rlen;
	    int rrlen;

	    /*
	     * get entire header
	     */

	    for (rlen = 0; rline[rlen]; ++rlen) {
		if (rline[rlen] != '\n')
		    continue;
		if (rline[rlen+1] == ' ' || rline[rlen+1] == '\t')/* multiln */
		    continue;
		++rlen;
		break;
	    }

	    /*
	     * no more headers, don't scan article (if article data passed)
	     */
	    if (rlen == 2 && rline[0] == '\r' && rline[1] == '\n')
		break;

	    /*
	     * This occurs if the map file containing the headers is boshed
	     */

	    if (rlen == 0)
		break;

	    /*
	     * get just header portion, non inclusive of colon
	     */

	    for (rrlen = 0; rrlen < rlen && rline[rrlen] != ':'; ++rrlen)
		;

	    ch = tolower((unsigned char)*rline);
	    if (rrlen == hhlen &&
			ch == hch && strncasecmp(rline, hdr, hhlen) == 0) {
		switch(conn->co_ArtMode) {
		case COM_XPAT:
		    {
			char hdr[128];
			char *p = hdr;
			int glen = rrlen;

			if (glen < rlen && rline[glen] == ':')
			    ++glen;
			while (glen < rlen && 
			    (rline[glen] == ' ' || rline[glen] == '\t')
			) {
			    ++glen;
			}

			if (rlen >= sizeof(hdr))
			    p = zalloc(&conn->co_MemPool, rlen + 1);
			memcpy(p, rline, rlen);
			/* Handle CR/LF in overview data */
			if (rlen > 2 && p[rlen - 2] == '\r' && p[rlen - 1] == '\n') {
			    p[rlen - 2] = 0;
			}
			p[rlen] = 0;
			if (wildmat(p + glen, conn->co_ListPat))
			    printHdr = ' ';
			if (p != hdr)
			    zfree(&conn->co_MemPool, p, rlen + 1);
		    }
		    break;
		case COM_XHDR:
		    printHdr =  ' ';
		    break;
		case COM_NEWNEWS:
		    printHdr = ' ';
		    didIndex = 1;
		    break;
		case COM_XOVER:
		    printHdr = '\t';
		    break;
		case COM_XZVER:
		    printHdr = '\t';
		    break;
		}
		if (printHdr) {
		    int b;
		    int i;
		    int doSpace = 0;

		    if (didIndex == 0) {
			OutputListIndex(conn);
			didIndex = 1;
		    }
		    if (conn->co_ArtMode != COM_NEWNEWS) {
			if (conn->co_ArtMode == COM_XZVER) {
			    MZPrintf(conn, "%c", printHdr);
			} else {
			    MBPrintf(&conn->co_TMBuf, "%c", printHdr);
			}
		    }

		    /*
		     * hack for overview format options, only deal with
		     * 'full' at the moment.
		     */

		    if (hhlen < hlen && strncmp(hdr + hhlen + 1, "full", 4) == 0) {
			if (conn->co_ArtMode == COM_XZVER) {
			    MZWrite(conn, hdr, hhlen + 1);
			    MZWrite(conn, " ", 1);
			} else {
			    MBWrite(&conn->co_TMBuf, hdr, hhlen + 1);
			    MBWrite(&conn->co_TMBuf, " ", 1);
			}
		    }

		    /*
		     * Compress whitespace if necessary. Mainly applies
		     * to folded lines.
		     */
		    for (b = i = rrlen + 1; i < rlen; ++i) {
			if (rline[i] == '\r' || rline[i] == '\n' ||
			    rline[i] == ' ' || rline[i] == '\t'
			) {
			    /*
			     * Skip first whitespace
			     */
			    if (b == i) {
				++b;
			    } else {
				/*
				 * Don't compress spaces
				 * We will compress space for OVER when it
				 * gets implemented
				 */
				if (rline[i] == ' ')
				    continue;
				/*
				 * Only compress tabs if doing XOVER
				 */
				if (conn->co_ArtMode != COM_XOVER &&
				    conn->co_ArtMode != COM_XZVER &&
							rline[i] == '\t')
				    continue;
				if (doSpace) {
				    if (conn->co_ArtMode == COM_XZVER) {
				        MZWrite(conn, " ", 1);
				    } else {
				        MBWrite(&conn->co_TMBuf, " ", 1);
				    }
				}
				if (conn->co_ArtMode == COM_XZVER) {
				    MZWrite(conn, rline + b, i - b);
				} else {
				    MBWrite(&conn->co_TMBuf, rline + b, i - b);
				}
				b = i + 1;
				doSpace = 1;
			    }
			}
		    }
		    if (b != i) {
			if (doSpace) {
			    if (conn->co_ArtMode == COM_XZVER) {
			        MZWrite(conn, " ", 1);
			    } else {
			        MBWrite(&conn->co_TMBuf, " ", 1);
			    }
			}
			if (conn->co_ArtMode == COM_XZVER) {
			    MZWrite(conn, rline + b, i - b);
			} else {
			    MBWrite(&conn->co_TMBuf, rline + b, i - b);
			}
		    }
		}
		break;
	    }
	    rleft -= rlen;
	    rline += rlen;
	} /* while */
	if (printHdr == 0 && (conn->co_ArtMode == COM_XOVER ||
			      conn->co_ArtMode == COM_XZVER)) {
	    if (didIndex == 0) {
		OutputListIndex(conn);
		didIndex = 1;
	    }
	    if (hhlen == 5 && hch == 'b' && strncasecmp(hdr, "Bytes", 5) == 0) {
		if (conn->co_ArtMode == COM_XZVER) {
		    MZPrintf(conn, "\t%d", artSize + 1);
		} else {
		    MBPrintf(&conn->co_TMBuf, "\t%d", artSize + 1);
		}
	    } else {
		if (conn->co_ArtMode == COM_XZVER) {
		    MZPrintf(conn, "\t");
		} else {
		    MBPrintf(&conn->co_TMBuf, "\t");
		}
	    }
	}

	hdr += hlen;
	conn->co_LastActiveTime = CurTime.tv_sec;
    }
    if (didIndex == 0 && conn->co_ArtMode == COM_XHDR && conn->co_ListStrict) {
	/* HDR: a missing header has the empty string as its value */
	OutputListIndex(conn);
	MBPrintf(&conn->co_TMBuf, " ");
	didIndex = 1;
    }
    if (didIndex == 0 && 
	(/* conn->co_ArtMode == COM_XPAT || */ conn->co_ArtMode == COM_XHDR)
    ) {
	OutputListIndex(conn);
	if (conn->co_ArtMode == COM_XZVER)
	    MZPrintf(conn, " (none)");
	else
	    MBPrintf(&conn->co_TMBuf, " (none)");
	didIndex = 1;
    }
    if (didIndex) {
	if (conn->co_ArtMode == COM_XZVER) {
	    MZPrintf(conn, "\r\n");
	} else {
	    MBPrintf(&conn->co_TMBuf, "\r\n");
	}
	return(0);
    }
    return(-1);
}

