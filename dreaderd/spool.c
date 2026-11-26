
/*
 * SPOOL.C	- Spool server state machine
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void NNSpoolCommand1(Connection *conn);
Prototype void NNFinishInterruptedSpool(Connection *conn);

void NNSpoolResponse1(Connection *conn);
void NNSpoolResponse2(Connection *conn);
void NNSpoolResponse3(Connection *conn);
void NNSpoolResponseScrap(Connection *conn);
void NNSpoolHdrResponse(Connection *conn);
void NNGetLocal1(Connection *conn);

/* Bound a fallback HEAD as a whole, not just each backend response line. */
#define HDR_MSGID_MAX_BYTES (1024 * 1024)

/* D-195: room for an article's Xref and Newsgroups lines; the header read
 * of a local spool article for the groups check */
#define ACC_HDR_MAX	(16 * 1024)
#define ACC_LOCAL_MAX	(256 * 1024)

/*
 * accHeaderLine() - D-195: keep a header line (buf, len bytes without the
 *		     line end) for the groups check, if it is an Xref or
 *		     Newsgroups line or a continuation of one.  A line that
 *		     does not fit is left out whole, never cut, and
 *		     sr_AccCut set (also when there is no memory): the check
 *		     then treats the header as incomplete and refuses the
 *		     article unless an allowed group was kept.
 */
static void
accHeaderLine(ServReq *sreq, const char *buf, int len)
{
    if (*buf == ' ' || *buf == '\t') {
	if (!sreq->sr_AccIn)
	    return;
    } else {
	sreq->sr_AccIn = (len >= 5 && strncasecmp(buf, "Xref:", 5) == 0) ||
	    (len >= 11 && strncasecmp(buf, "Newsgroups:", 11) == 0);
	if (!sreq->sr_AccIn)
	    return;
    }
    if (sreq->sr_AccHdr == NULL && (sreq->sr_AccHdr = malloc(ACC_HDR_MAX)) == NULL) {
	sreq->sr_AccCut = 1;
	return;
    }
    if (sreq->sr_AccLen + len + 2 > ACC_HDR_MAX) {
	sreq->sr_AccCut = 1;
	return;
    }
    memcpy(sreq->sr_AccHdr + sreq->sr_AccLen, buf, len);
    sreq->sr_AccLen += len;
    sreq->sr_AccHdr[sreq->sr_AccLen++] = '\n';
}

void
NNSpoolCommand1(Connection *conn)
{
    char smaxage[32];

    ServReq *sreq = conn->co_SReq;

    if (DebugOpt)
	printf("NNSpoolCommand1: article %s\n", sreq->sr_MsgId);

    smaxage[0] = 0;
    if (sreq->sr_MaxAge)
	snprintf(smaxage, sizeof(smaxage), " a%d", sreq->sr_MaxAge);
    if (sreq->sr_HeadOnly) {
	/* A HEAD must not populate the full-article cache or enter the local
	 * ARTICLE/DFA path. Keep this flag on the request even if its client
	 * disconnects, so that the response is still drained correctly.
	 */
	MBPrintf(&conn->co_TMBuf, "head %s%s\r\n", sreq->sr_MsgId, smaxage);
	conn->co_ServerArticleRequestedCount++;
	NNSpoolResponse1(conn);
    } else if (conn->co_Desc->d_LocalSpool) {
	if (strcmp(conn->co_Desc->d_LocalSpool, "/") == 0)
	    MBPrintf(&conn->co_TMBuf, "whereis %s\r\n", sreq->sr_MsgId);
	else
	    MBPrintf(&conn->co_TMBuf, "whereis %s REL\r\n", sreq->sr_MsgId);
	NNGetLocal1(conn) ;
    } else {
	MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
	conn->co_ServerArticleRequestedCount++;
	NNSpoolResponse1(conn);
    }
}

/*
 * Retrieve location
 */
void
NNGetLocal1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    char *buf;
    char *ptr;
    char *filename;
    int len;
    long offset;
    long size;
    char smaxage[32];
    int wcode;

    conn->co_Func = NNGetLocal1;
    conn->co_State = "getlcl1";

    *smaxage = '\0';

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	conn->co_ServerByteCount += len;

	if (sreq->sr_MaxAge) {
	    snprintf(smaxage, sizeof(smaxage), " a%d", sreq->sr_MaxAge);
	}

	wcode = NntpCode(buf);
	if (wcode == 223) {
	    /*
	     * The client is gone (ended while it waited, e.g. by its idle
	     * timeout): nobody to send the article to.  The 223 line is
	     * the whole reply, so the request is just finished.  Going on
	     * crashed in NewDFA(), which uses sr_CConn.
	     */
	    if (sreq->sr_CConn == NULL) {
		NNFinishSReq(conn, NULL, 0);
		return;
	    }
	    /*
	     * sr_CConn may be NULL if client was terminated while
	     * server operation was still in progress.
	     */
	    if (conn->co_SReq->sr_CConn) {
		MBFree(&conn->co_SReq->sr_CConn->co_ArtBuf);
		switch(sreq->sr_CConn->co_ArtMode) {
		    case COM_BODYNOSTAT:
			break;
		    case COM_STAT:
		    case COM_HEAD:
		    case COM_BODY:
		    case COM_ARTICLE:
			MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d 0 %s %s\r\n",
						GoodRC(sreq->sr_CConn),
						sreq->sr_MsgId,
						GoodResId(sreq->sr_CConn)
			);
			break;
		    case COM_BODYWVF:
			MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d %lld %s %s\r\n",
						GoodRC(sreq->sr_CConn),
						artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
						sreq->sr_MsgId,
						GoodResId(sreq->sr_CConn)
			);
			break;
		    case COM_ARTICLEWVF: {
			const char *ovdata;
			const char *msgid;
			int ovlen;
			if ((ovdata = NNRetrieveHead(sreq->sr_CConn, &ovlen, &msgid, NULL, NULL, NULL)) != NULL) {
			    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_TMBuf, "%03d %lld %s %s\r\n",
						GoodRC(sreq->sr_CConn),
						artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
						sreq->sr_MsgId,
						GoodResId(sreq->sr_CConn)
);
			    DumpOVHeaders(sreq->sr_CConn, ovdata, ovlen);
			    MBPrintf(&sreq->sr_CConn->co_TMBuf, "\r\n"); 
			    sreq->sr_CConn->co_ArtMode = COM_BODYNOSTAT;
			} else {
			    /*
			     * The article went away (cancelled, expired) while
			     * the spool server answered.  Nothing follows a
			     * whereis reply, so there is no article to drop:
			     * answer the client, the stream is in step.
			     * NNSpoolResponseScrap() waited here for lines
			     * that never came.  No requeue: the record is
			     * gone here, another spool server cannot help.
			     */
			    NNFinishSReq(conn, "423 No such article number in this group\r\n", 0);
			    return;
			}
		    }
		    break;
		}
	    }
	    /*
	     * We have to get additionnal information from the buffer
	     */
	    strtok(buf, " ") ;
	    offset = -1;
	    size = -1;
	    filename = NULL;
	    while ((ptr = strtok(NULL, " ")) != NULL) {
		if (strcasecmp(ptr, "in") == 0) {
		    filename = strtok(NULL," ");
		} else if (strcasecmp("offset", ptr) == 0) {
		    ptr = strtok(NULL," ");
		    if (ptr) {
			offset = strtol(ptr, NULL, 10);
			if (offset == LONG_MIN || offset == LONG_MAX)
			    offset = -1;
		    } else {
			offset = -1;
		    }
		} else if (strcasecmp("length", ptr) == 0) {
		    ptr = strtok(NULL," ");
		    if (ptr) {
			size = strtol(ptr, NULL, 10);
			if (size == LONG_MIN || size == LONG_MAX)
			    size = -1;
		    } else {
			size = -1;
		    }
		}
	    }

	    /*
	     * The filename comes from the spool server's whereis reply and is
	     * handed to open() below, so its name is checked first.  This is
	     * lexical containment of the name a backend may ask for, not a full
	     * filesystem sandbox: a backend trusted to serve the local spool can
	     * still point open() through a symlink it plants inside the spool
	     * tree, so the spool tree itself remains a trust boundary (true
	     * containment would need openat()/O_NOFOLLOW resolution and is left
	     * as a follow-up).  The name checks:
	     *
	     *  - localspool must be an absolute path.  An empty or relative
	     *    value cannot contain a relative reply: "%s/%s" with an empty
	     *    root turns a relative name into an absolute one.
	     *  - localspool "/" is the documented full-path mode, where the
	     *    operator has declared this backend's absolute replies trusted,
	     *    so the name is used as given and is not contained.
	     *  - under a real spool directory the reply is relative to it and
	     *    must stay inside by name: no leading "/" and no ".." component.
	     *
	     * A refused reply falls through to the normal article fetch below.
	     */
	    if (filename != NULL) {
		const char *root = conn->co_Desc->d_LocalSpool;

		if (root == NULL || root[0] != '/') {
		    logit(LOG_ERR,
			"NNGetLocal1 : localspool is not an absolute path, refusing local fetch from %s",
			conn->co_Desc->d_Id);
		    filename = NULL;
		} else if (strcmp(root, "/") != 0 &&
			   (filename[0] == '/' || strstr(filename, "..") != NULL)) {
		    logit(LOG_ERR,
			"NNGetLocal1 : refusing spool path with traversal from %s: %s",
			conn->co_Desc->d_Id, filename);
		    filename = NULL;
		}
	    }

	    if (filename != NULL && offset >= 0 && size >= 0) {
		int lf;
		int n;
		int headered = 0;
		static char filepath[PATH_MAX];
		if (DebugOpt)
		    printf("NNGetLocal1 : whereis returned file %s offset %ld size %ld\n",
						filename, offset, size);
		if (strcmp(conn->co_Desc->d_LocalSpool, "/") == 0)
		    n = snprintf(filepath, sizeof(filepath), "%s", filename);
		else
		    n = snprintf(filepath, sizeof(filepath), "%s/%s",
					conn->co_Desc->d_LocalSpool, filename);
		if (n < 0 || n >= (int)sizeof(filepath)) {
		    /* a truncated path would name a different file: refuse it */
		    logit(LOG_ERR,
			"NNGetLocal1 : spool path too long, refusing fetch from %s",
			conn->co_Desc->d_Id);
		    lf = -1;
		} else {
		    lf = open(filepath, O_RDONLY);
		}
		/*
		 * D-233: the range must be article text.  One that starts with
		 * a spool record header (SpoolArtHdr) is not: a diablo spool
		 * server that names the whole record (an old one does, also
		 * for gzip and wire-format records) would have the client
		 * receive binary bytes before the headers, a compressed
		 * article or a dot-stuffed one.  An article never starts with
		 * 0xff 0x99, so fetch such a record over NNTP instead.
		 */
		if (lf >= 0 && size >= 2) {
		    unsigned char mg[2];

		    if (pread(lf, mg, 2, offset) == 2 &&
			    mg[0] == STORE_MAGIC1 && mg[1] == STORE_MAGIC2) {
			logit(LOG_NOTICE,
			    "NNGetLocal1 : %s in %s starts with a spool record header, not article text: fetching it from %s",
			    sreq->sr_MsgId, filepath, conn->co_Desc->d_Id);
			close(lf);
			lf = -1;
			headered = 1;
		    }
		}
		/*
		 * D-195: read the header first for the groups check.  If the
		 * reader may not read the article, answer that and send
		 * nothing of it (only co_ArtBuf holds its status line yet).
		 */
		if (lf >= 0 && sreq->sr_CConn && ArtAccessCheckNeeded(sreq->sr_CConn)) {
		    int want = (size < ACC_LOCAL_MAX) ? (int)size : ACC_LOCAL_MAX;
		    char *hb = malloc(want + 1);
		    const char *denied = NULL;
		    ssize_t got = -1;

		    if (hb != NULL)
			got = pread(lf, hb, want, offset);
		    if (got < 0)
			denied = ArtDeniedReply(sreq->sr_CConn);
		    else
			denied = ArtAccessDenied(sreq->sr_CConn, hb, (int)got, got == (ssize_t)size);
		    free(hb);
		    if (denied != NULL) {
			close(lf);
			MBFree(&sreq->sr_CConn->co_ArtBuf);
			NNFinishSReq(conn, denied, 0);
			return;
		    }
		}
		if (lf >= 0 && NewDFA(conn, lf, offset, size)) {
		    NNSendLocalArticle(conn);
		} else {
		    if (headered) {
			/* said so above */
		    } else if (lf == -1) {
			logit(LOG_ERR, "NNGetLocal1 : problem while openning file %s",
							filepath);
		    } else {
			logit(LOG_ERR, "NNGetLocal1 : problem while seeking file %s offset %i",
							filepath, offset);
		    }
		    close(lf);
		    MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
		    NNSpoolResponse1(conn);
		}
	    } else {
		logit(LOG_ERR, "NNGetLocal1 : error while getting whereis informations");
		MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
		NNSpoolResponse1(conn);
	    }
	} else if (wcode == 430 || wcode == 443 || wcode == 480 ||
		    wcode == 500 || wcode == 501 || wcode == 502 || wcode == 503) {
	    /*
	     * whereis request failed (or the server doesn't know it), try
	     * the normal way
	     */
	    MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
	    conn->co_ServerArticleRequestedCount++;
	    NNSpoolResponse1(conn);
	} else {
	    /*
	     * Not a reply whereis can get: lines may follow that the article
	     * request would read as its reply.  Handled as in
	     * NNSpoolResponse1(): the request goes elsewhere or is refused,
	     * and the connection is closed (COF_BROKEN, NNServerIdle()).
	     */
	    logit(LOG_ERR, "spool server %s: unexpected reply to whereis %s: %.80s, closing it",
		conn->co_Desc->d_Id, sreq->sr_MsgId, buf);
	    conn->co_Flags |= COF_BROKEN;
	    conn->co_ServerArticleMiscErrorCount++;
	    if (sreq->sr_CConn == NULL)
		NNFinishSReq(conn, NULL, 0);
	    else if (sreq->sr_CConn->co_ArtMode == COM_BODYNOSTAT)
		NNFinishSReq(conn, "(article not available)\r\n.\r\n", 1);
	    else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
		NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	    else
		NNFinishSReq(conn, "430 No such article\r\n", 1);
	}
    } else if (len < 0) {
	NNServerTerminate(conn);
    } else {
	/* else we haven't got the response yet */
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (1), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * Retrieve return code
 */

void
NNSpoolResponse1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    char *buf;
    int len;
    int code;
    int unexpected;

    conn->co_Func = NNSpoolResponse1;
    conn->co_State = "spres1";

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	conn->co_ServerByteCount += len;
	code = NntpCode(buf);
	if (sreq->sr_HeadOnly && code == 221) {
	    if (sreq->sr_CConn)
		MBFree(&sreq->sr_CConn->co_ArtBuf);
	    conn->co_BytesHeader = 0;
	    NNSpoolHdrResponse(conn);
	    return;
	}
	if (!sreq->sr_HeadOnly && code == 220) {
	    sreq->sr_AccLen = 0;
	    sreq->sr_AccIn = 0;
	    sreq->sr_AccCut = 0;
	    sreq->sr_Denied = 0;
	    /* We have a positive answer, we may cache article */
	    if (conn->co_Desc->d_Cache) {
		CreateCache(conn);
	    }
	    /*
	     * sr_CConn may be NULL if client was terminated while
	     * server operation was still in progress.
	     */
	    if (conn->co_SReq->sr_CConn) {
		MBFree(&conn->co_SReq->sr_CConn->co_ArtBuf);

		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_BODYNOSTAT:
		    break;
		case COM_STAT:
		case COM_HEAD:
		case COM_BODY:
		case COM_ARTICLE:
		    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d 0 %s %s\r\n",
			GoodRC(sreq->sr_CConn),
			sreq->sr_MsgId,
			GoodResId(sreq->sr_CConn)
		    );
		    break;
		case COM_BODYWVF:
		    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d %lld %s %s\r\n",
			GoodRC(sreq->sr_CConn),
			artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
			sreq->sr_MsgId,
			GoodResId(sreq->sr_CConn)
		    );
		    break;
		case COM_ARTICLEWVF:
		    {
			const char *ovdata;
			const char *msgid;
			int ovlen;

			if ((ovdata = NNRetrieveHead(sreq->sr_CConn, &ovlen, &msgid, NULL, NULL, NULL)) != NULL) {
			    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_TMBuf, "%03d %lld %s %s\r\n",
				GoodRC(sreq->sr_CConn),
				artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
				sreq->sr_MsgId,
				GoodResId(sreq->sr_CConn)
			    );
			    DumpOVHeaders(sreq->sr_CConn, ovdata, ovlen);
			    MBPrintf(&sreq->sr_CConn->co_TMBuf, "\r\n"); 
			    sreq->sr_CConn->co_ArtMode = COM_BODYNOSTAT;
			} else {
			    NNSpoolResponseScrap(conn);
			    return; /* bleh */
			}
		    }
		    break;
		}
	    }
	    NNSpoolResponse2(conn);
	    return;
	}
	else if (code == 430) {
	  conn->co_ServerArticleNotFoundErrorCount++;
	}
	else {
	  conn->co_ServerArticleMiscErrorCount++;
	}

	/*
	 * Only a one-line refusal leaves the stream in step.  Anything else
	 * (a malformed status line, or a code with a text block after it,
	 * such as a 220 to HEAD) leaves lines unread that the next request
	 * would take for its reply: the client gets its refusal, and the
	 * connection to the spool server is closed.
	 */
	/* 443: the Diablo feeder's "Bad Message-ID" / "Bad Command Options" */
	unexpected = !(code == 430 || code == 423 || code == 412 ||
		code == 420 || code == 443 || code == 480 || code == 500 ||
		code == 501 || code == 502 || code == 503);
	if (unexpected) {
	    logit(LOG_ERR, "spool server %s: unexpected reply to %s: %.80s, closing it",
		conn->co_Desc->d_Id, sreq->sr_MsgId, buf);
	    /* NNFinishSReq() goes on to NNServerIdle(), which closes it */
	    conn->co_Flags |= COF_BROKEN;
	}

	if (sreq->sr_CConn == NULL)
	    NNFinishSReq(conn, NULL, 0);
	else if (sreq->sr_CConn->co_ArtMode == COM_BODYNOSTAT)
	    NNFinishSReq(conn, "(article not available)\r\n.\r\n", 1);
	else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
            NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	else
            NNFinishSReq(conn, "430 No such article\r\n", 1);
    } else if (len < 0) {
	NNServerTerminate(conn);
    } else {
	/* else we haven't got the response yet */
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (2), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * HDR/XHDR message-id fallback. Buffer only a bounded HEAD, then use the same
 * case-insensitive/folded-header formatter and absent-field rules as overview.
 * No positive client response is sent until the backend terminator arrives;
 * transport failures can therefore use the normal spool failover unchanged.
 */
void
NNSpoolHdrResponse(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    char *buf;
    int len;

    conn->co_Func = NNSpoolHdrResponse;
    conn->co_State = "sphdr";
    while ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	Connection *client = sreq->sr_CConn;

	conn->co_ServerByteCount += len;
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    conn->co_ServerArticleCount++;
	    if (client == NULL) {
		NNFinishSReq(conn, NULL, 0);
	    } else if (conn->co_BytesHeader > HDR_MSGID_MAX_BYTES) {
		NNFinishSReq(conn, "503 Spool headers too large\r\n", 0);
	    } else {
		int size = client->co_ArtBuf.mh_Bytes;
		char *headers = zalloc(&client->co_MemPool, size + 1);
		const char *denied;

		MBRead(&client->co_ArtBuf, headers, size);
		headers[size] = 0;
		if ((denied = ArtAccessDenied(client, headers, size, 1)) != NULL) {
		    /* D-195: not in a group this reader may read */
		    zfree(&client->co_MemPool, headers, size + 1);
		    NNFinishSReq(conn, denied, 0);
		    return;
		}
		if (client->co_ListStrict)
		    MBLogPrintf(client, &client->co_TMBuf, "225 Headers follow\r\n");
		else
		    MBLogPrintf(client, &client->co_TMBuf, "221 %s data follows\r\n", client->co_ListHdrs);
		OutputOverview(client, headers, size, 0, -1);
		zfree(&client->co_MemPool, headers, size + 1);
		NNFinishSReq(conn, ".\r\n", 0);
	    }
	    return;
	}
	buf[len - 1] = '\n';
	if (len > 1 && buf[0] == '.') {
	    ++buf;
	    --len;
	}
	if (len <= HDR_MSGID_MAX_BYTES - conn->co_BytesHeader) {
	    conn->co_BytesHeader += len;
	    if (client)
		MBWrite(&client->co_ArtBuf, buf, len);
	} else {
	    conn->co_BytesHeader = HDR_MSGID_MAX_BYTES + 1;
	    if (client)
		MBFree(&client->co_ArtBuf);
	    /* Keep draining to the dot, preserving the backend command stream. */
	}
    }
    if (len < 0) {
	NNServerTerminate(conn);
    } else if (conn->co_Desc->d_Timeout &&
	       time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout) {
	logit(LOG_ERR, "Timeout waiting for %s HEAD %s, closing spool server",
	    conn->co_Desc->d_Id, sreq->sr_MsgId);
	NNServerTerminate(conn);
    }
}

/*
 * Retrieve headers
 */

void
NNSpoolResponse2(Connection *conn)
{
    char *buf;
    int len;
    ServReq *sreq = conn->co_SReq;
    char *vserver;
    int doneconn;
    char ch;

    conn->co_Func = NNSpoolResponse2;
    conn->co_State = "spres2";

    /*
     * We need to check that co_Auth.dr_VServerDef is defined
     * because it probably won't be for a connection to a backend
     * spool
     */
    if (sreq->sr_CConn && sreq->sr_CConn->co_Auth.dr_VServerDef)
	vserver = sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName;
    else
	vserver = "";

    while ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
#ifdef STATS_ART_AGE_DR
	if (! strncasecmp(buf, "Date:", 5)) {
	    logit(LOG_INFO, "articleage %s %d", sreq->sr_MsgId, (int)(time(NULL)) - parsedate(buf));
	}
#endif
	conn->co_ServerByteCount += len;
	if (len == 2 && strcmp(buf, "\r") == 0) {
	    if (sreq->sr_Cache)
		fwrite("\r\n", 1, 2, sreq->sr_Cache);

	    /*
	     * D-195: the whole header is in; an article in no group this
	     * reader may read is not answered (the status line and header
	     * lines so far are only in co_ArtBuf), its body is drained.
	     */
	    if (sreq->sr_CConn &&
		    ArtAccessDenied(sreq->sr_CConn, sreq->sr_AccHdr ? sreq->sr_AccHdr : "",
			sreq->sr_AccLen, !sreq->sr_AccCut) != NULL) {
		sreq->sr_Denied = 1;
		MBFree(&sreq->sr_CConn->co_ArtBuf);
	    }
	    if (sreq->sr_CConn && !sreq->sr_Denied) {
		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_ARTICLE:
		    MBPrintf(&sreq->sr_CConn->co_ArtBuf, "\r\n");
		    break;
		}
	    }
	    NNSpoolResponse3(conn);
	    return;
	}
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    if (sreq->sr_CConn == NULL)
		NNFinishSReq(conn, NULL, 0);
	    else if (sreq->sr_CConn->co_ArtMode == COM_BODYNOSTAT)
		NNFinishSReq(conn, "(article not available)\r\n.\r\n", 1);
	    else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
		NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	    else
		NNFinishSReq(conn, "430 No such article\r\n", 1);
	    return;
	}

	if (sreq->sr_CConn && ArtAccessCheckNeeded(sreq->sr_CConn) && len > 1)
	    accHeaderLine(sreq, buf, (len >= 2 && buf[len - 2] == '\r') ? len - 2 : len - 1);

	/*
	 * Only munge Xref: header if we asked for it, the wrong Xref:
	 * header can blow up news readers, so we need to be able to
	 * change it when viewing to match the Path:
	 *
	 * sr_CConn may be NULL if the client terminated or if an
	 * autonomous lookahead request was issued.
	 *
	 */

	doneconn = 0;
	ch = tolower((unsigned char)*buf);
	if (*vserver && sreq->sr_CConn &&
		!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate &&
		ch == 'x' && strncasecmp(buf, "Xref:", 5) == 0) {
	    char *ptr;
	    int l;

	    /*
	     * The len includes the trailing \n converted to a \0
	     */
	    l = len - 2;
	    while (l > 5 && (buf[l] == '\r' || buf[l] == '\n'))
		l--;

	    ptr = buf + 5;
	    while (isspace((unsigned char)*ptr))
		ptr++;
	    while (*ptr && !isspace((unsigned char)*ptr))
		ptr++;
	    while (isspace((unsigned char)*ptr))
		ptr++;
	    if (*ptr) {
		char line[8192];
		int e;

		l = (buf + l) - ptr + 1;
		if (l < 0)
		    l = 0;
		sprintf(line, "Xref: %s ", vserver);
		e = strlen(line);
		/* the header comes from the spool server: cut it to the buffer, as cache.c does */
		if (l > (int)sizeof(line) - 100)
		    l = (int)sizeof(line) - 100;
		memcpy(&line[e], ptr, l);
		line[e + l] = '\0';
		strcat(line, "\r\n");

		switch(sreq->sr_CConn->co_ArtMode) {
		    case COM_HEAD:
		    case COM_ARTICLE:
			MBWrite(&sreq->sr_CConn->co_ArtBuf, line, strlen(line));
			break;
		}
		doneconn = 1;
	    }
	}
	if (*vserver && sreq->sr_CConn &&
		!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoReadPath &&
			ch == 'p' && strncasecmp(buf, "Path:", 5) == 0) {
	    char *ptr;
	    int l;
	    int vsl = strlen(vserver);

	    /*
	     * The len includes the trailing \n converted to a \0
	     */
	    l = len - 2;
	    while (l > 5 && (buf[l] == '\r' || buf[l] == '\n'))
		l--;

	    ptr = buf + 5;
	    while (isspace((unsigned char)*ptr))
		ptr++;
	    if (*ptr && (strncmp(vserver, ptr, vsl) ||
			 ((ptr[vsl] != '\0') &&
			  (ptr[vsl] != '!')))) {
		char line[8192];
		int e;

		l = (buf + l) - ptr + 1;
		if (l < 0)
		    l = 0;
		sprintf(line, "Path: %s!", vserver);
		e = strlen(line);
		/* the header comes from the spool server: cut it to the buffer, as cache.c does */
		if (l > (int)sizeof(line) - 100)
		    l = (int)sizeof(line) - 100;
		memcpy(&line[e], ptr, l);
		line[e + l] = '\0';
		strcat(line, "\r\n");

		switch(sreq->sr_CConn->co_ArtMode) {
		    case COM_HEAD:
		    case COM_ARTICLE:
			MBWrite(&sreq->sr_CConn->co_ArtBuf, line, strlen(line));
			break;
		}
		doneconn = 1;
	    }
	}

	if (len) {
	    buf[len-1] = '\n';

	    if (sreq->sr_Cache)
		fwrite(buf, 1, len, sreq->sr_Cache);

	    /*
	     * sr_CConn may be NULL if the client terminated or if an
	     * autonomous lookahead request was issued.
	     */

	    if (!doneconn && sreq->sr_CConn) {
		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_HEAD:
		case COM_ARTICLE:
		    MBWrite(&sreq->sr_CConn->co_ArtBuf, buf, len);
		    break;
		}
	    }
	}

    }
    if (len < 0) {
	NNServerTerminate(conn);
    } else {
	/* else we haven't got the response yet */
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (3), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * Retrieve the body, place in client's ArtBuf
 */

void
NNSpoolResponse3(Connection *conn)
{
    char *buf;
    ServReq *sreq = conn->co_SReq;
    int len = 0;

    conn->co_Func = NNSpoolResponse3;
    conn->co_State = "spres3";

    for (;;) {
	len = MBReadLine(&conn->co_RMBuf, &buf);

	/*
	 * EOF: termination retries a request that has sent nothing to the
	 * client (the lines read so far sit only in the client's ArtBuf)
	 * and finishes one whose fast-copy output was published.
	 */

	if (len < 0) {
	    NNServerTerminate(conn);
	    return;
	}

	/*
	 * If len is 0, we have nothing to do
	 */

	if (len == 0)
	    break;


	conn->co_ServerByteCount += len + 1;
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    conn->co_ServerArticleCount++;
	    if (sreq->sr_CConn) {
		MBCopy(
		    &sreq->sr_CConn->co_ArtBuf,
		    &sreq->sr_CConn->co_TMBuf
		);
	    }
	    if (sreq->sr_Cache) {
		fflush(sreq->sr_Cache);
		if (ferror(sreq->sr_Cache))
		    AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
		else
		    CommitCache(conn, 0);
		fclose(sreq->sr_Cache);
		sreq->sr_Cache = NULL;
	    }
	    if (sreq->sr_CConn == NULL) {
		NNFinishSReq(conn, NULL, 0);
	    } else if (sreq->sr_Denied) {
		NNFinishSReq(conn, ArtDeniedReply(sreq->sr_CConn), 0);
	    } else {
		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_ARTICLE:
		case COM_BODY:
		case COM_BODYWVF:
		case COM_BODYNOSTAT:
		case COM_HEAD:
		    NNFinishSReq(conn, ".\r\n", 0);
		    break;
		case COM_STAT:
		    NNFinishSReq(conn, "", 0);
		    break;
		default:
		    NNFinishSReq(conn, "(something blew up in the spool code)\r\n.\r\n", 0);
		    break;
		}
	    }
	    return;
	}
	if (len > 0)
	    buf[len-1] = '\n';

	if (sreq->sr_CConn && !sreq->sr_Denied) {
	    switch(sreq->sr_CConn->co_ArtMode) {
	    case COM_ARTICLE:
	    case COM_BODY:
	    case COM_BODYWVF:
	    case COM_BODYNOSTAT:
		MBWrite(&sreq->sr_CConn->co_ArtBuf, buf, len);
		break;
	    }
	}
	if (sreq->sr_Cache)
	    fwrite(buf, 1, len, sreq->sr_Cache);
    }
    if (FastCopyOpt && sreq->sr_CConn && sreq->sr_CConn->co_ArtBuf.mh_Bytes) {
	sreq->sr_SpoolOutput = 1;
        MBCopy(
            &sreq->sr_CConn->co_ArtBuf,
            &sreq->sr_CConn->co_TMBuf
        );
    }
    /* still waiting for input */
    if (len == 0) {
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (4), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/* Use NNSpoolResponse3's interrupted-body ending (formerly spool.c:865-871).
 * STAT has already sent its single status line. */
void
NNFinishInterruptedSpool(Connection *conn)
{
    Connection *client = conn->co_SReq->sr_CConn;

    if (client->co_ArtBuf.mh_Bytes)
	MBCopy(&client->co_ArtBuf, &client->co_TMBuf);
    NNFinishSReq(conn, client->co_ArtMode == COM_STAT ? "" :
	"(spool server died prior to completion of the article dump)\r\n.\r\n", 0);
}

void
NNSpoolResponseScrap(Connection *conn)
{
    char *buf;
    int len;
    ServReq *sreq = conn->co_SReq;

    conn->co_Func = NNSpoolResponse3;
    conn->co_State = "spres3";

    while ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	conn->co_ServerByteCount += len + 1;
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    conn->co_ServerArticleCount++;
	    if (sreq->sr_Cache) {
		fflush(sreq->sr_Cache);
		if (ferror(sreq->sr_Cache))
		    AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
		else
		    CommitCache(conn, 0);
		fclose(sreq->sr_Cache);
		sreq->sr_Cache = NULL;
	    }
	    if (sreq->sr_CConn == NULL)
		NNFinishSReq(conn, NULL, 0);
	    else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
		NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	    else
		NNFinishSReq(conn, "430 No such article\r\n", 1);
	    return;
	}
    }
    if (len < 0) {
	NNServerTerminate(conn);
    } /* else we are still waiting for input */
}

