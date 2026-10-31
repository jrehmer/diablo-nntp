
/*
 * CONTROL.C
 *
 * Control: cancel <message-id>
 *
 * Control: newgroup groupname [y/m[oderated]
 *	For your newsgroups file:
 *	groupname		comment
 *
 *	Group submission address: moderator-email
 *
 * Control: rmgroup groupname
 *
 * Control: checkgroups
 *	(message body contains)
 *	groupname		comment
 *	(missing groups are deleted?)
 *
 * Approved: 	(is a required header)
 * X-PGP-Sig: 	(is a required header for pgp-authentication)
 * X-DGPSig:	(is a required header for dgp-authentication)
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

#define MAXCMDARGS	8

Prototype void ExecuteControl(Connection *conn, const char *ctl, const char *art, int artLen);
Prototype void FreeControl(Connection *conn);
Prototype void ExecuteSupersedes(Connection *conn, const char *supers, const char *art, int artLen);

Control *ParseControl(Connection *conn, const char *cmd, const char *from, const char *newsgroup);
void SendMail(FILE *logf, long lpos, char *hostname, const char *cmd, const char *arg);
const char *PerformControl(Connection *conn, Control *ctl, const char **cmd, const char *art, const char *body, int bodyLen, char *groups, const char *from);
const char *CtlCancel(Connection *conn, Control *ctl, const char **cmd, const char *art, const char *body, int bodyLen, char *groups);
const char *CtlNewGroup(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen);
const char *CtlRmGroup(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen);
const char *CtlCheckGroups(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen, const char *from);
const char *locateXTrace(const char *art, int headLen, int *pxtLen);

void
ExecuteControl(Connection *conn, const char *ctlMsg, const char *art, int artLen)
{
    const char *body;
    const char *newsgroup = NULL;
    char *from = NULL;
    char *groups = NULL;
    char *cmdBuf;
    const char *cmd[MAXCMDARGS];
    int nargs = 0;
    int cmdBufLen;
#if DIABLO_PGP_SUPPORT
    int hasXPGPSig = 0;		/* PGP signature	*/
#endif
#if DIABLO_DGP_SUPPORT    
    int hasXDGPSig = 0;
#endif
    const char *badCmd = NULL;
    Control *ctl = NULL;
    int dupHeaders = 0;

    /*
     * control command and arguments
     */

    cmdBufLen = strlen(ctlMsg) + 1;
    cmdBuf = zalloc(&conn->co_MemPool, cmdBufLen);
    strcpy(cmdBuf, ctlMsg);

    {
	char *ptr = cmdBuf;

	while (nargs < MAXCMDARGS && (cmd[nargs] = parseword(&ptr, " \t")) != NULL)
	    ++nargs;
	while (nargs < MAXCMDARGS)
	    cmd[nargs++] = NULL;
    }

    if (cmd[0] == NULL) {
	if (DebugOpt)
	    printf("NULL control\n");
	zfree(&conn->co_MemPool, cmdBuf, cmdBufLen);
	return;
    }

    /*
     * PGP authenticate
     */

    if (DebugOpt)
	printf("Control Message received: %s %s\n", cmd[0], cmd[1]);

    /*
     * Scan headers for lines we need to locate the correct control.ctl line.
     *
     * Note that the Control: header stored in cmd[] may have been synthesized
     * (see ExecuteSupersedes), and thus not match the Control: header in the
     * article, or there may not even *be* a Control: header in the article.
     */

    {
	const char *line;
	int l = 0;
	int nControl = 0;
	int nFrom = 0;
	int nNewsgroups = 0;
	int nPGPSig = 0;

	for (line = art; line < art + artLen; line += l + 1) {
	    for (l = line - art; l < artLen; ++l) {
		if (art[l] == '\n') {
		    if (l + 1 >= artLen ||		/* end of article */
			l == line - art ||		/* blank line	  */
			(art[l+1] != ' ' && art[l+1] != '\t') /* !header ext */
		    ) {
			break;
		    }
		}
	    }
	    l -= line - art;

	    if (l == 0 || (l == 1 && line[0] == '\r')) {
		/* out of headers */
		break;
	    } else if (strncasecmp(line, "Control:", 8) == 0) {
		++nControl;
	    } else if (strncasecmp(line, "From:", 5) == 0) {
		++nFrom;
		from = zallocStrTrim2(&conn->co_MemPool, 0, line + 5, l - 5);
	    } else if (strncasecmp(line, "Newsgroups:", 11) == 0) {
		++nNewsgroups;
		groups = zallocStrTrim2(&conn->co_MemPool, ',', line + 11, l - 11);
#if DIABLO_PGP_SUPPORT
	    } else if (strncasecmp(line, "X-PGP-Sig:", 10) == 0) {
		++nPGPSig;
		hasXPGPSig = 1;
#endif
#if DIABLO_DGP_SUPPORT    
	    } else if (strncasecmp(line, "X-DGPSig:", 9) == 0) {
		hasXDGPSig = 1;
#endif

	    }

	}
	body = line;

	/*
	 * A control message with any of these twice is never executed: the
	 * article's Control: that is acted on, and the From: and Newsgroups:
	 * matched against dcontrol.ctl, are the last ones, while a signature
	 * covers the first.
	 */
	if (nControl > 1 || nFrom > 1 || nNewsgroups > 1 || nPGPSig > 1)
	    dupHeaders = 1;
    }

    /*
     * newsgroup - newgroup and rmgroup only
     * messageid - cancel only
     */

    if (strcasecmp(cmd[0], "newgroup") == 0) {
	if (cmd[1] == NULL)
	    badCmd = "no argument to newgroup\n";
	else
	    newsgroup = cmd[1];
    } else if (strcasecmp(cmd[0], "rmgroup") == 0) {
	if (cmd[1] == NULL)
	    badCmd = "no argument to rmgroup\n";
	else
	    newsgroup = cmd[1];
    } else if (strcasecmp(cmd[0], "cancel") == 0) {
	if (cmd[1] == NULL) {
	    badCmd = "no argument to cancel\n";
	} else {
	    cmd[1] = MsgId(cmd[1], NULL);
	    if (strcmp(cmd[1], "<>") == 0)
		badCmd = "bad message-id in cancel\n";
	}
    } else if (strcasecmp(cmd[0], "checkgroups") == 0) {
	;
    }
    if (badCmd == NULL && dupHeaders) {
	badCmd = "duplicate Control:, From:, Newsgroups: or X-PGP-Sig: header\n";
	logit(LOG_NOTICE, "control %s %s refused: duplicate Control:, From:, "
	    "Newsgroups: or X-PGP-Sig: header", cmd[0], cmd[1] ? cmd[1] : "");
    }

    /*
     * Locate match in dcontrol.ctl
     */

    if (badCmd == NULL && 
	(ctl = ParseControl(conn, cmd[0], from, newsgroup)) != NULL
    ) {
	/*
	 * log control message content
	 */

	if (ctl->ct_LogFo) {
	    int i;
	    int phead = 1;
	    time_t t = time(NULL);
	    struct tm *tp = localtime(&t);
	    char tbuf[64];

	    strftime(tbuf, sizeof(tbuf), "%c", tp);
	    fprintf(ctl->ct_LogFo, "\nCONTROL %s\n", tbuf);

	    for (i = 0; i < artLen; ++i) {
		if (art[i] == '\r')
		    continue;
		if (phead) {
		    fprintf(ctl->ct_LogFo, "<< ");
		    phead = 0;
		}
		fputc((int)(uint8)art[i], ctl->ct_LogFo);
		if (art[i] == '\n')
		    phead = 1;
	    }
	    fprintf(ctl->ct_LogFo, "\n");
	}

	/*
	 * options
	 */

	/****************************************************************
	 *		GLUE FOR PGP/DGP VERIFICATION			*
	 ****************************************************************
	 *
	 */

	if (ctl->ct_Flags & CTFF_VERIFY) {
	    /*
	     * On success, verification routine will clear other CTFF_VERIFY
	     * flags.
	     */

	    if (badCmd == NULL && (ctl->ct_Flags & CTF_DGPVERIFY)) {
#if DIABLO_DGP_SUPPORT
		if (hasXDGPSig)
		    badCmd = DGPVerify(ctl, art, artLen);
		else
		    badCmd = "dgp verification required, signature missing\n";
		if (badCmd == NULL && ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> dgp verification required, validated ok\n");
#else
		badCmd = "dgp verification required, not compiled in\n";
#endif
	    }
	    if (badCmd == NULL && (ctl->ct_Flags & CTF_PGPVERIFY)) {
#if DIABLO_PGP_SUPPORT
		if (hasXPGPSig)
		    badCmd = PGPVerify(ctl, art, artLen);
		else
		    badCmd = "pgp verification required, signature missing\n";
		if (badCmd == NULL && ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> pgp verification required, validated ok\n");
#else
		badCmd = "pgp verification required, not compiled in\n";
#endif
	    }
	} else {
	    if (ctl->ct_LogFo)
		fprintf(ctl->ct_LogFo, ">> dgp/pgp verification not requested\n");
	}

	if ((ctl->ct_Flags & CTF_EXECUTE)) {
	    if (ctl->ct_LogFo) {
		if (badCmd) {
		    fprintf(ctl->ct_LogFo, ">> unable to execute control (%s)\n", badCmd);
		} else {
		    fprintf(ctl->ct_LogFo, ">> executing control:\n");
		}
	    }
	    if (badCmd == NULL)
		badCmd = PerformControl(conn, ctl, cmd, art, body, artLen - (body - art), groups, from);
	} else {
	    if (ctl->ct_LogFo)
		fprintf(ctl->ct_LogFo, ">> control message dropped\n");
	    
	}
    } else {
	if (badCmd == NULL)
	    badCmd = "Control %s not in dcontrol.ctl\n";
    }

    /*
     * Final logging
     */

    if (ctl && ctl->ct_LogFo) {
	if (badCmd) {
	    fprintf(ctl->ct_LogFo, ">>");
	    fprintf(ctl->ct_LogFo, "%s", (newsgroup ? newsgroup : "?"));
	    fprintf(ctl->ct_LogFo, ">> processing failed\n");
	} else {
	    fprintf(ctl->ct_LogFo, ">> processing completed ok\n");
	}
    }

    /*
     * Mail (extract information from logfile or temporary file)
     */

    if (ctl && ctl->ct_LogFo) {
	if (ctl->ct_Flags & CTF_MAIL) {
	    fprintf(ctl->ct_LogFo, ">> mail requested\n");
	    fflush(ctl->ct_LogFo);
	    SendMail(ctl->ct_LogFo, ctl->ct_LogSeekPos,
		conn->co_Auth.dr_VServerDef->vs_HostName, cmd[0], cmd[1]);
	} else {
	    fprintf(ctl->ct_LogFo, ">> mail not requested\n");
	}
    }

    /*
     * Debugging
     */

    if (badCmd && DebugOpt && ctl && ctl->ct_LogFo) {
	char tmp[128];

	fflush(ctl->ct_LogFo);
	fseek(ctl->ct_LogFo, ctl->ct_LogSeekPos, 0);
	while (fgets(tmp, sizeof(tmp), ctl->ct_LogFo) != NULL)
	    fputs(tmp, stdout);
	fseek(ctl->ct_LogFo, 0L, 2);
    } else if (badCmd && DebugOpt) {
	printf(badCmd, (newsgroup ? newsgroup : "?"));
    }

    /*
     * Done
     */

    zfreeStr(&conn->co_MemPool, &from);
    zfreeStr(&conn->co_MemPool, &groups);
    zfree(&conn->co_MemPool, cmdBuf, cmdBufLen);
    FreeControl(conn);
}

Control *
ParseControl(Connection *conn, const char *cmd, const char *from, const char *newsgroup)
{
    FILE *fi = fopen(PatLibExpand(DControlCtlPat), "r");
    char buf[256];
    Control *ctl = NULL;

    if (DebugOpt) {
	printf(
	    "ParseControl '%s' '%s' '%s'\n",
	    ((cmd) ? cmd : "?"),
	    ((from) ? from : "?"),
	    ((newsgroup) ? newsgroup : "?")
	);
    }

    FreeControl(conn);

    if (fi == NULL)
	return(NULL);
    if (from == NULL)
	return(NULL);

    while (fgets(buf, sizeof(buf), fi) != NULL) {
	char *smsg;
	char *sfrom;
	char *sgroups;
	char *saction;

	if (buf[0] == '\n' || buf[0] == '#' ||
	    (smsg = strtok(buf, ":\n")) == NULL ||
	    smsg[0] == '#'
	) {
	    continue;
	}
	if ((sfrom = strtok(NULL, ":\n")) == NULL)
	    continue;
	if ((sgroups = strtok(NULL, ":\n")) == NULL)
	    continue;
	if ((saction = strtok(NULL, ":\n")) == NULL)
	    continue;

	/*
	 * control message type must match
	 */

	if (strcmp(smsg, "all") != 0 && strcasecmp(cmd, smsg) != 0)
	    continue;

	/*
	 * From: line must match.  Be a little fuzzy here
	 */
	if (WildOrCmp(sfrom, from) != 0) {
	    char tmp[512];
	    if (strlen(sfrom) > 128)
		continue;
	    sprintf(tmp, "*<%s>*|* %s|%s *", sfrom, sfrom, sfrom);
	    if (WildOrCmp(tmp, from) != 0)
		continue;
	}

	/*
	 * Newsgroups must match
	 */
	if (newsgroup && WildOrCmp(sgroups, newsgroup) != 0)
	    continue;

	if (DebugOpt)
	    printf("Control authenticated with %s:%s:%s:%s\n", smsg, sfrom, sgroups, saction);

	if (ctl) {
	    zfreeStr(&conn->co_MemPool, &ctl->ct_Msg);
	    zfreeStr(&conn->co_MemPool, &ctl->ct_From);
	    zfreeStr(&conn->co_MemPool, &ctl->ct_Groups);
	    ctl->ct_Flags &= ~(CTF_EXECUTE|CTF_MAIL|CTF_DGPVERIFY|CTF_PGPVERIFY|CTF_DROP); /* XXX */
	} else {
	    ctl = zalloc(&conn->co_MemPool, sizeof(Control));
	}
	ctl->ct_Msg = zallocStr(&conn->co_MemPool, smsg);
	ctl->ct_From = zallocStr(&conn->co_MemPool, sfrom);
	ctl->ct_Groups = zallocStr(&conn->co_MemPool, sgroups);

	for (saction = strtok(saction, ",\n"); saction; saction = strtok(NULL, ",\n")) {
	    char *logFile;

	    if ((logFile = strchr(saction, '=')) != NULL)
		*logFile++ = 0;

	    if (strcmp(saction, "doit") == 0) {
		ctl->ct_Flags |= CTF_EXECUTE;
	    } else if (strcmp(saction, "break") == 0) {
		ctl->ct_Flags |= CTF_BREAK;
	    } else if (strcmp(saction, "drop") == 0) {
		ctl->ct_Flags &= ~CTF_EXECUTE;
		ctl->ct_Flags |= CTF_DROP;
	    } else if (strcmp(saction, "log") == 0) {
		;
	    } else if (strcmp(saction, "mail") == 0) {
		ctl->ct_Flags |= CTF_MAIL;
	    } else if (strncmp(saction, "verify-", 7) == 0) {
		/*
		 * require PGP authentication
		 */
		ctl->ct_Flags |= CTF_PGPVERIFY;
		if ((ctl->ct_Flags & CTF_DROP) == 0)
		    ctl->ct_Flags |= CTF_EXECUTE;
		ctl->ct_Verify = zallocStr(&conn->co_MemPool, saction + 7);
	    } else if (strcmp(saction, "dgpverify") == 0) {
		/*
		 * require DGP authentication
		 */
		ctl->ct_Flags |= CTF_DGPVERIFY;
		if ((ctl->ct_Flags & CTF_DROP) == 0)
		    ctl->ct_Flags |= CTF_EXECUTE;
	    }
	    if (logFile) {
		int fd;
		if (ctl->ct_LogFo) {
		    fflush(ctl->ct_LogFo);
		    hflock(fileno(ctl->ct_LogFo), 0, XLOCK_UN);
		    fclose(ctl->ct_LogFo);
		}

		if (logFile[0] == '/')
		    fd = xopen(O_RDWR|O_CREAT, 0640, "%s", logFile);
		else
		    fd = xopen(O_RDWR|O_CREAT, 0640, "%s/%s", PatExpand(LogHomePat), logFile);

		if (DebugOpt)
		    printf("creating log file %s fd = %d\n", logFile, fd);

		if (fd >= 0) {
		    ctl->ct_LogFo = fdopen(fd, "r+");
		    hflock(fileno(ctl->ct_LogFo), 0, XLOCK_EX);
		    fseek(ctl->ct_LogFo, 0L, 2);
		    ctl->ct_LogSeekPos = ftell(ctl->ct_LogFo);
		    ctl->ct_Flags |= CTF_LOG;
		}
	    }
	}
	if (ctl && ctl->ct_Flags & CTF_BREAK)
	    break;
    }
    fclose(fi);

    /*
     * temporary file
     */

    if (ctl && ctl->ct_LogFo == NULL) {
	if (DebugOpt || (ctl->ct_Flags & (CTF_MAIL|CTF_LOG))) {
	    char path[256];
	    int fd;

	    sprintf(path, "/tmp/dreaderd.ctl%d", (int)getpid());
	    remove(path);

	    if (DebugOpt)
		printf("creating temporary log/copy file %s\n", path);

	    if ((fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0644)) >= 0) {
		ctl->ct_LogFo = fdopen(fd, "r+");
		ctl->ct_TmpFileName = zallocStr(&conn->co_MemPool, path);
		ctl->ct_LogSeekPos = 0;
	    }
	}
    }
    conn->co_Ctl = ctl;

    return(ctl);
}

void
FreeControl(Connection *conn)
{
    Control *ctl;

    if ((ctl = conn->co_Ctl) != NULL) {
	if (ctl->ct_LogFo) {
	    fflush(ctl->ct_LogFo);
	    hflock(fileno(ctl->ct_LogFo), 0, XLOCK_UN);
	    fclose(ctl->ct_LogFo);
	}
	if (ctl->ct_TmpFileName)
	    remove(ctl->ct_TmpFileName);

	zfreeStr(&conn->co_MemPool, &ctl->ct_Msg);
	zfreeStr(&conn->co_MemPool, &ctl->ct_From);
	zfreeStr(&conn->co_MemPool, &ctl->ct_Verify);
	zfreeStr(&conn->co_MemPool, &ctl->ct_Groups);
	zfreeStr(&conn->co_MemPool, &ctl->ct_TmpFileName);

	zfree(&conn->co_MemPool, ctl, sizeof(Control));
	conn->co_Ctl = NULL;
    }
}

/*
 * SendMail() - send mail to administrator
 *
 * Mail is sent to MAIL_ADMIN_ADDR.  This cannot block, so queue the mail with -odq
 */

void
SendMail(FILE *logf, long lpos, char *hostname, const char *cmd, const char *arg)
{
    pid_t pid;
    char *argv[] = { SENDMAIL_PATH, SENDMAIL_ARG0, "-t", "-odq", NULL };
    int fds[3];

    if ((pid = RunProgramPipe(fds, RPF_STDOUT, argv, NULL)) > 0) {
	FILE *fo = fdopen(fds[1], "w");
	char buf[128];

	fprintf(fo, "To: %s\n", safestr(DOpts.NewsMaster, "news"));
	fprintf(fo, "From: news@%s\n", hostname);
	fprintf(fo, "Subject: Control message %s %s\n", cmd, (arg ? arg : ""));
	fprintf(fo, "\n");
	fseek(logf, lpos, 0);
	while (fgets(buf, sizeof(buf), logf) != NULL)
	    fputs(buf, fo);
	fclose(fo);
	waitpid(pid, NULL, 0);
    } else {
	logit(LOG_ERR, "Unable to run sendmail (trying to email control message)");
    }
}

/*
 * PerformControl() - perform control function
 */

const char *
PerformControl(Connection *conn, Control *ctl, const char **cmd, const char *art, const char *body, int bodyLen, char *groups, const char *from)
{
    const char *badCmd = NULL;

    if (strcasecmp(cmd[0], "cancel") == 0 && cmd[1]) {
#ifndef	DIRTYSPOOL
	badCmd = CtlCancel(conn, ctl, cmd, art, body, bodyLen, groups);
#endif
    } else if (strcasecmp(cmd[0], "newgroup") == 0 && cmd[1]) {
	badCmd = CtlNewGroup(conn, ctl, cmd, body, bodyLen);
    } else if (strcasecmp(cmd[0], "rmgroup") == 0 && cmd[1]) {
	badCmd = CtlRmGroup(conn, ctl, cmd, body, bodyLen);
    } else if (strcasecmp(cmd[0], "checkgroups") == 0) {
	badCmd = CtlCheckGroups(conn, ctl, cmd, body, bodyLen, from);
    } else {
	badCmd = "Unknown control\n";
    }
    return(badCmd);
}

/*
 * CANCEL - cancel an article.  cmd[1] is the "<message-id>" of the article.
 *
 * Cancels are messy because dreaderd is oriented towards group/article-number
 * combinations.  message-id's are used to lookup articles in remote spools, but
 * dreaderd does not index overview information by message-id!
 *
 * What we do is take the newsgroup list and scan the overview data for the
 * message-id.
 *
 * 1) Find OverInfo (index) for group (no locking)
 * 2) Lock index
 * 3) Lookup article info (OverArt)
 * 4) Write cancelled article
 * 5) Unlock index
 */

/*
 * canLockDecide() - whether a cancel or supersede of the article artNo in ov
 * may be executed under readercanlock (RFC 8315).  ckey is the Cancel-Key
 * value of the cancelling article, or NULL.  Returns 1 to execute, -1 to
 * refuse, and sets *why for the log.
 */
static int
canLockDecide(OverInfo *ov, artno_t artNo, const char *ckey, const char **why)
{
    const char *data;
    int hlen = 0;
    char lock[1024];
    int r;

    switch (DOpts.ReaderCanLock) {
    case CANLOCK_ALL:
	*why = "not checked (readercanlock all)";
	return(1);
    case CANLOCK_NONE:
	*why = "refused (readercanlock none)";
	return(-1);
    }
    if ((data = GetOverRecord(ov, artNo, &hlen, NULL, NULL, NULL)) == NULL) {
	*why = "refused, headers of the target not available";
	return(-1);
    }
    r = CanLockFindHeader(data, hlen, "Cancel-Lock", lock, sizeof(lock));
    if (r < 0) {
	*why = "refused, Cancel-Lock of the target too long";
	return(-1);
    }
    if (r == 0) {
	if (DOpts.ReaderCanLock == CANLOCK_AUTH) {
	    *why = "target has no Cancel-Lock, executed (readercanlock auth)";
	    return(1);
	}
	*why = "refused, target has no Cancel-Lock";
	return(-1);
    }
    if (ckey == NULL) {
	*why = "refused, no Cancel-Key";
	return(-1);
    }
    if (CanLockVerify(ckey, lock)) {
	*why = "Cancel-Key verified";
	return(1);
    }
    *why = "refused, Cancel-Key does not match";
    return(-1);
}

const char *
CtlCancel(Connection *conn, Control *ctl, const char **cmd, const char *art, const char *body, int bodyLen, char *groups)
{
    char *ptr;
    int isValidCancel = 0;
    int validGroups = 0;
    int count = 0;
    char ckeyBuf[1024];
    const char *ckey = NULL;
    const char *why = NULL;

    /*
     * Cancel-Key of the cancelling (or superseding) article, if any.
     */

    if (CanLockFindHeader(art, body - art, "Cancel-Key", ckeyBuf, sizeof(ckeyBuf)) > 0)
	ckey = ckeyBuf;

    /*
     * Find a valid overview record; decide on the first one found (against
     * its Cancel-Lock, see readercanlock), then cancel in every group.
     */

    /*
     * A cancel is identified by its Control: message-id, but the target is
     * found by scanning the overview of the groups named in the article.  The
     * header check normally guarantees a Newsgroups: header, but we do not
     * index by message-id: with no groups there is nothing to scan, so this is
     * a safe no-op (not a NULL walk).
     */
    ptr = groups;

    while (ptr != NULL && *ptr) {
	int l;
	char c;
	OverInfo *ov;
	artno_t artNo;

	for (l = 0; ptr[l] && ptr[l] != ','; ++l)
	    ;
	c = ptr[l];
	ptr[l] = 0;
	ov = FindCanceledMsg(ptr, cmd[1], &artNo, &validGroups);

	if (ov != NULL) {
	    ++count;

	    if (isValidCancel == 0)
		isValidCancel = canLockDecide(ov, artNo, ckey, &why);
	    if (isValidCancel > 0)
		CancelOverArt(ov, artNo);
	    PutOverInfo(ov);
	}
	ptr += l;
	if ((*ptr = c) != 0)
	    ++ptr;
    }

    /*
     * If we could not find the article anywhere and if at least one of the
     * groups is valid, the cancel goes into the pre-cancel cache and the
     * article is cancelled if it comes in later on.  That skips the
     * Cancel-Lock check, so it is only done with readercanlock all; under
     * the other modes such early cancels are dropped, as INN does.
     */

    if (isValidCancel == 0) {
	if (validGroups && DOpts.ReaderCanLock == CANLOCK_ALL) {
	    EnterCancelCache(cmd[1]);
	    why = "target not here yet, pre-cancel cached";
	} else if (validGroups) {
	    why = "target not here yet, dropped (readercanlock)";
	} else {
	    why = "no valid groups, nothing canceled";
	}
    }
    if (DOpts.ReaderCanLock != CANLOCK_ALL)
	logit((isValidCancel > 0) ? LOG_INFO : LOG_NOTICE,
	    "cancel %s: %s, %d item(s)", cmd[1], why, (isValidCancel > 0) ? count : 0);
    if (DebugOpt)
	printf("CtlCancel %s: %s, %d items found: %s\n", cmd[1], why, count, safestr(groups, ""));
    if (ctl->ct_LogFo) {
	fprintf(ctl->ct_LogFo, ">> control cancel, %d items found, %s\n",
	    count, why);
    }
    return(NULL);
}

/*
 * NEWGROUP -
 */

const char *
CtlNewGroup(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen)
{
    const char *flags = "y";
    const char *rec;
    int recLen;
    char *moderator = NULL;
    char *description = NULL;
    int cmd1Len = strlen(cmd[1]);
    int isNew = 0;
    int hasCts = 0;

    if (ValidGroupName(cmd[1]) < 0) {
	if (ctl->ct_LogFo)
	    fprintf(ctl->ct_LogFo, ">> Newgroup: Illegal newsgroup name\n");
	return(NULL);
    }
    /*
     * cmd[2] may be NULL, 'y', or 'm'.
     */
    if (cmd[2] && cmd[2][0] == 'm')
	flags = "m";

    /*
     * Read and lock the record.  If the record does not exist, create a new
     * record (and lock that).
     */

    if ((rec = KPDBReadRecord(KDBActive, cmd[1], KP_LOCK, &recLen)) == NULL) {
	KPDBWrite(KDBActive, cmd[1], "NB", "0000000001", KP_LOCK);
	KPDBWrite(KDBActive, cmd[1], "NE", "0000000000", KP_LOCK_CONTINUE);
	isNew = 1;
    } else {
	if (KPDBGetField(rec, recLen, "CTS", NULL, NULL) != NULL)
	    hasCts = 1;
    }

    /*
     * Look for:  'For your newsgroups file:', next line: 'groupname comment'
     * Look for:  'Group submission address: moderator_email'
     */

    {
	int i;
	int l = 0;
	int lookFor = 0;

	for (i = 0; i < bodyLen; i = l) {
	    for (l = i; l < bodyLen && body[l] != '\n'; ++l)
		;
	    if (l < bodyLen)
		++l;
	    if (strncmp(body + i, "For your newsgroups file:", 25) == 0) 
		lookFor = 5;
	    if (moderator == NULL && strncmp(body + i, "Group submission address:", 25) == 0)
		moderator = zallocStrTrim(&conn->co_MemPool, body + i + 25, l - i - 25);
	    if (lookFor && description == NULL) {
		if (strncmp(body + i, cmd[1], cmd1Len) == 0) {
		    description = zallocStrTrim(&conn->co_MemPool, body + i + cmd1Len, l - i - cmd1Len);
		    lookFor = 0;
		} else {
		    --lookFor;
		}
	    }
	}
    }

    if (moderator) {
	SanitizeString(moderator);
	KPDBWriteEncode(KDBActive, cmd[1], "M", moderator, KP_LOCK_CONTINUE);
	zfreeStr(&conn->co_MemPool, &moderator);
    }
    if (description) {
	SanitizeDescString(description);
	KPDBWriteEncode(KDBActive, cmd[1], "GD", description, KP_LOCK_CONTINUE);
	zfreeStr(&conn->co_MemPool, &description);
    }

    {
	/*
	 * add creation-time-stamp and last-modified-time-stamp
	 * for group.
	 */
	char tsBuf[64];

	sprintf(tsBuf, "%08x", (int)time(NULL));
	if (hasCts == 0)
	    KPDBWrite(KDBActive, cmd[1], "CTS", tsBuf, KP_LOCK_CONTINUE);
	KPDBWrite(KDBActive, cmd[1], "LMTS", tsBuf, KP_LOCK_CONTINUE);
    }

    KPDBWrite(KDBActive, cmd[1], "S", flags, KP_UNLOCK);

    if (ctl->ct_LogFo) {
	if (isNew) {
	    fprintf(ctl->ct_LogFo, ">> Newgroup: Created new group %s flags=%s\n",
		cmd[1], 
		flags
	    );
	} else {
	    fprintf(ctl->ct_LogFo, 
		">> Newgroup: updated group %s flags=%s moderator=%s description=%s\n",
		cmd[1], 
		flags,
		(moderator ? moderator : "no chg"),
		(description ? description : "no chg")
	    );
	}
    }
    return(NULL);
}

const char *
CtlRmGroup(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen)
{
    const char *rec;
    int recLen;

    if (ValidGroupName(cmd[1]) < 0) {
	if (ctl->ct_LogFo)
	    fprintf(ctl->ct_LogFo, ">> Rmgroup: Illegal newsgroup name\n");
	return(NULL);
    }

    /*
     * Note that the record isn't locked in this case.
     */

    if ((rec = KPDBReadRecord(KDBActive, cmd[1], 0, &recLen)) != NULL) {
	KPDBDelete(KDBActive, cmd[1]);
	if (ctl->ct_LogFo)
	    fprintf(ctl->ct_LogFo, ">> RmGroup: Deleted group %s\n", cmd[1]);
    }
    return(NULL);
}

/*
 * checkgroups, as INN's docheckgroups with control.ctl "doit" does it.
 *
 *	The body has 'groupname description' lines (a description with "(Moderated)"
 *	is a moderated group).  Every group in it that matches the Control newsgroup
 *	wildcard of dcontrol.ctl is checked against the active file:
 *
 *	    - the description of a group that exists is updated;
 *	    - the moderated flag of a group that exists is corrected;
 *	    - a group that is missing is created (an addition);
 *	    - a group of a top-level hierarchy that the list has, that matches the
 *	      wildcard and is not in the list is removed (a removal).
 *
 *	The descriptions and the flags are always done.  As in INN, the additions and
 *	removals are only made when there are not more than "maxcheckgroups" of them
 *	(a line "maxcheckgroups:from:newsgroups:number" in dcontrol.ctl, INN's
 *	"/maxdocheckgroups/" works too; the last line that matches the sender and the
 *	first group of the message is used; default 10); beyond that they are written
 *	to the control log as the dgrpctl commands that make them, and not made.
 *
 *	A group that matches a line of the file "localgroups" (path_lib, one group or
 *	wildcard per line, '#' comments) is left alone: its description and flag are
 *	not changed, it is not created and never removed.  The descriptions of groups
 *	that the message does not list are never touched, removed or not.
 */

#define CG_MAXDOCHECKGROUPS	10
#define CG_MAXLINE		512

typedef struct CgEnt {
    char *ce_Group;
    char *ce_Desc;
    char  ce_Flags;		/* 'y' or 'm' */
} CgEnt;

static int
CgCompare(const void *a, const void *b)
{
    return(strcmp(((const CgEnt *)a)->ce_Group, ((const CgEnt *)b)->ce_Group));
}

/*
 * The limit of additions and removals: the last line "maxcheckgroups:from:newsgroups:number"
 * (or INN's "/maxdocheckgroups/") of dcontrol.ctl that matches the sender and the group.
 */
static int
CgMaxChanges(const char *from, const char *group)
{
    FILE *fi = fopen(PatLibExpand(DControlCtlPat), "r");
    char buf[256];
    int max = CG_MAXDOCHECKGROUPS;

    if (fi == NULL)
	return(max);
    while (fgets(buf, sizeof(buf), fi) != NULL) {
	char *smsg;
	char *sfrom;
	char *sgroups;
	char *snum;

	if (strncasecmp(buf, "maxcheckgroups:", 15) != 0 && strncasecmp(buf, "/maxdocheckgroups/:", 19) != 0)
	    continue;
	if ((smsg = strtok(buf, ":\n")) == NULL ||
	    (sfrom = strtok(NULL, ":\n")) == NULL ||
	    (sgroups = strtok(NULL, ":\n")) == NULL ||
	    (snum = strtok(NULL, ":\n")) == NULL
	)
	    continue;
	if (from != NULL && WildOrCmp(sfrom, from) != 0) {
	    char tmp[512];

	    if (strlen(sfrom) > 128)
		continue;
	    sprintf(tmp, "*<%s>*|* %s|%s *", sfrom, sfrom, sfrom);
	    if (WildOrCmp(tmp, from) != 0)
		continue;
	}
	if (group != NULL && WildOrCmp(sgroups, group) != 0)
	    continue;
	{
	    long v = strtol(snum, NULL, 10);

	    if (v >= 0 && v < 100000000)
		max = (int)v;
	}
    }
    fclose(fi);
    return(max);
}

/*
 * The patterns of the file "localgroups" (path_lib), as one '|' separated wildcard, or NULL.
 */
static char *
CgLocalGroups(void)
{
    FILE *fi = fopen(PatLibExpand("%s/localgroups"), "r");
    char buf[256];
    char *pat = NULL;
    size_t len = 0;

    if (fi == NULL)
	return(NULL);
    while (fgets(buf, sizeof(buf), fi) != NULL) {
	char *g = strtok(buf, " \t\r\n");
	char *np;

	if (g == NULL || g[0] == '#')
	    continue;
	if ((np = realloc(pat, len + strlen(g) + 2)) == NULL)
	    break;
	pat = np;
	len += sprintf(pat + len, "%s%s", len ? "|" : "", g);
    }
    fclose(fi);
    return(pat);
}

/*
 * A complete record for a group that did not exist, as CtlNewGroup() makes it.
 */
static void
CgCreate(const char *group, char flags, const char *description)
{
    char tsBuf[64];
    char fl[2];

    fl[0] = flags;
    fl[1] = 0;
    sprintf(tsBuf, "%08x", (int)time(NULL));
    KPDBWrite(KDBActive, group, "NB", "0000000001", KP_LOCK);
    KPDBWrite(KDBActive, group, "NE", "0000000000", KP_LOCK_CONTINUE);
    if (description[0])
	KPDBWriteEncode(KDBActive, group, "GD", description, KP_LOCK_CONTINUE);
    KPDBWrite(KDBActive, group, "CTS", tsBuf, KP_LOCK_CONTINUE);
    KPDBWrite(KDBActive, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
    KPDBWrite(KDBActive, group, "S", fl, KP_UNLOCK);
}

const char *
CtlCheckGroups(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen, const char *from)
{
    int i;
    int l = 0;
    CgEnt *ents = NULL;
    int nEnts = 0;
    int maxEnts = 0;
    char **hier = NULL;		/* top-level hierarchies of the list */
    int nHier = 0;
    char **gone = NULL;		/* groups to remove */
    int nGone = 0;
    int maxGone = 0;
    int nAdd = 0;
    int nFlags = 0;
    int maxChanges;
    int doit;
    int recOff;
    int recLen;
    char *local = CgLocalGroups();

    for (i = 0; i < bodyLen; i = l) {
	char tmp[CG_MAXLINE];
	char *newsgroup;
	char *description = NULL;

	for (l = i; l < bodyLen && body[l] != '\n'; ++l)
	    ;
	if (l < bodyLen)
	    ++l;
	if (l - i >= (int)sizeof(tmp))
	    continue;
	memcpy(tmp, body + i, l - i);
	tmp[l-i] = 0;

	if ((newsgroup = strtok(tmp, " \t\r\n")) == NULL)
	    continue;
	if (ValidGroupName(newsgroup) < 0)		/* valid group name	*/
	    continue;
	if (strlen(newsgroup) >= MAXGNAME)
	    continue;
	if (strchr(newsgroup, '.') == NULL)		/* sanity check 	*/
	    continue;
	if ((description = strtok(NULL, "\r\n")) == NULL)
	    continue;

	/*
	 * clean up the description
	 */
	SanitizeDescString(description);
	{
	    int dl = strlen(description);
	    while (*description == ' ' || *description == '\t') {
		++description;
		--dl;
	    }
	    while (dl > 0 && (description[dl-1] == ' ' || description[dl-1] == '\t')) {
		description[--dl] = 0;
	    }
	    if (dl > 255)
		description[255] = 0;
	}

	/*
	 * The group must match the ct_Groups field from the dcontrol.ctl
	 * file, and must not be a local group.
	 */
	if (WildOrCmp(ctl->ct_Groups, newsgroup) != 0)
	    continue;
	if (local && WildOrCmp(local, newsgroup) == 0)
	    continue;

	if (nEnts == maxEnts) {
	    CgEnt *ne;

	    maxEnts = maxEnts ? maxEnts * 2 : 256;
	    if ((ne = realloc(ents, maxEnts * sizeof(CgEnt))) == NULL)
		goto out;
	    ents = ne;
	}
	ents[nEnts].ce_Group = strdup(newsgroup);
	ents[nEnts].ce_Desc = strdup(description);
	ents[nEnts].ce_Flags = strstr(description, "(Moderated)") ? 'm' : 'y';
	if (ents[nEnts].ce_Group == NULL || ents[nEnts].ce_Desc == NULL)
	    goto out;
	++nEnts;
    }
    if (nEnts == 0)
	goto out;

    /*
     * The limit is looked up for the first group of the message.
     */
    maxChanges = CgMaxChanges(from, ents[0].ce_Group);

    /*
     * Sorted by name, a name twice in the list counts once (the first).
     */
    qsort(ents, nEnts, sizeof(CgEnt), CgCompare);
    {
	int k = 0;

	for (i = 0; i < nEnts; ++i) {
	    if (k > 0 && strcmp(ents[k-1].ce_Group, ents[i].ce_Group) == 0) {
		free(ents[i].ce_Group);
		free(ents[i].ce_Desc);
		continue;
	    }
	    ents[k++] = ents[i];
	}
	nEnts = k;
    }

    /*
     * The top-level hierarchies of the list: only they are checked for groups to remove.
     */
    for (i = 0; i < nEnts; ++i) {
	const char *g = ents[i].ce_Group;
	int hl = strcspn(g, ".");
	int h;

	for (h = 0; h < nHier; ++h) {
	    if ((int)strlen(hier[h]) == hl && strncmp(hier[h], g, hl) == 0)
		break;
	}
	if (h == nHier) {
	    char **nh = realloc(hier, (nHier + 1) * sizeof(char *));

	    if (nh == NULL)
		goto out;
	    hier = nh;
	    if ((hier[nHier] = malloc(hl + 1)) == NULL)
		goto out;
	    memcpy(hier[nHier], g, hl);
	    hier[nHier][hl] = 0;
	    ++nHier;
	}
    }

    /*
     * Count the additions and the flags to correct.
     */
    for (i = 0; i < nEnts; ++i) {
	const char *rec = KPDBReadRecord(KDBActive, ents[i].ce_Group, 0, &recLen);

	if (rec == NULL) {
	    ++nAdd;
	} else {
	    const char *p;
	    uint32 plen;

	    p = KPDBGetField(rec, recLen, "S", (int *)&plen, "n");
	    if (p == NULL || plen != 1 || p[0] != ents[i].ce_Flags)
		++nFlags;
	}
    }

    /*
     * The groups of those hierarchies that the list does not have.
     */
    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	recOff;
	recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
    ) {
	int groupLen;
	char grpbuf[MAXGNAME];
	CgEnt key;
	const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	int hl;
	int h;

	if (group == NULL || groupLen <= 0 || groupLen >= MAXGNAME)
	    continue;
	memcpy(grpbuf, group, groupLen);
	grpbuf[groupLen] = 0;
	hl = strcspn(grpbuf, ".");
	for (h = 0; h < nHier; ++h) {
	    if ((int)strlen(hier[h]) == hl && strncmp(hier[h], grpbuf, hl) == 0)
		break;
	}
	if (h == nHier || WildOrCmp(ctl->ct_Groups, grpbuf) != 0)
	    continue;
	if (local && WildOrCmp(local, grpbuf) == 0)
	    continue;
	key.ce_Group = grpbuf;
	if (bsearch(&key, ents, nEnts, sizeof(CgEnt), CgCompare) != NULL)
	    continue;
	if (nGone == maxGone) {
	    char **ng;

	    maxGone = maxGone ? maxGone * 2 : 64;
	    if ((ng = realloc(gone, maxGone * sizeof(char *))) == NULL)
		goto out;
	    gone = ng;
	}
	if ((gone[nGone] = strdup(grpbuf)) == NULL)
	    goto out;
	++nGone;
    }

    /*
     * Additions and removals only up to the limit; the descriptions and the flags always.
     */
    doit = (nAdd + nGone <= maxChanges);
    if (ctl->ct_LogFo && (nAdd + nGone || nFlags))
	fprintf(ctl->ct_LogFo, ">> Checkgroups: %d to add, %d to remove (limit %d): %s; %d moderated flags to correct\n",
	    nAdd, nGone, maxChanges, nAdd + nGone == 0 ? "nothing to do" : doit ? "made" : "NOT made, the commands follow", nFlags);

    for (i = 0; i < nEnts; ++i) {
	const char *g = ents[i].ce_Group;
	const char *rec = KPDBReadRecord(KDBActive, g, KP_LOCK, &recLen);

	if (rec == NULL) {
	    if (doit) {
		CgCreate(g, ents[i].ce_Flags, ents[i].ce_Desc);
		if (ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> Checkgroups: Added group %s flags=%c\n", g, ents[i].ce_Flags);
	    } else if (ctl->ct_LogFo) {
		fprintf(ctl->ct_LogFo, "dgrpctl newgroup %s %c \"\" \"%s\"\n", g, ents[i].ce_Flags, ents[i].ce_Desc);
	    }
	    continue;
	}
	{
	    const char *p;
	    uint32 plen;
	    int wrongFlags;
	    int wrongDesc;

	    p = KPDBGetField(rec, recLen, "S", (int *)&plen, "n");
	    wrongFlags = (p == NULL || plen != 1 || p[0] != ents[i].ce_Flags);
	    p = KPDBGetFieldDecode(rec, recLen, "GD", NULL, NULL);
	    wrongDesc = (p == NULL || strcmp(p, ents[i].ce_Desc) != 0);

	    if (!wrongDesc && !wrongFlags) {
		KPDBUnlock(KDBActive, rec);
		continue;
	    }
	    {
		char tsBuf[64];
		char fl[2];

		fl[0] = ents[i].ce_Flags;
		fl[1] = 0;
		sprintf(tsBuf, "%08x", (int)time(NULL));
		if (wrongDesc)
		    KPDBWriteEncode(KDBActive, g, "GD", ents[i].ce_Desc, KP_LOCK_CONTINUE);
		KPDBWrite(KDBActive, g, "LMTS", tsBuf, wrongFlags ? KP_LOCK_CONTINUE : KP_UNLOCK);
		if (wrongFlags) {
		    KPDBWrite(KDBActive, g, "S", fl, KP_UNLOCK);	/* releases the lock */
		    if (ctl->ct_LogFo)
			fprintf(ctl->ct_LogFo, ">> Checkgroups: Changed group %s flags=%c\n", g, ents[i].ce_Flags);
		}
	    }
	}
    }
    for (i = 0; i < nGone; ++i) {
	if (doit) {
	    if (KPDBReadRecord(KDBActive, gone[i], 0, &recLen) != NULL) {
		KPDBDelete(KDBActive, gone[i]);
		if (ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> Checkgroups: Deleted group %s\n", gone[i]);
	    }
	} else if (ctl->ct_LogFo) {
	    fprintf(ctl->ct_LogFo, "dgrpctl rmgroup %s\n", gone[i]);
	}
    }
out:
    for (i = 0; i < nEnts; ++i) {
	free(ents[i].ce_Group);
	free(ents[i].ce_Desc);
    }
    free(ents);
    for (i = 0; i < nHier; ++i)
	free(hier[i]);
    free(hier);
    for (i = 0; i < nGone; ++i)
	free(gone[i]);
    free(gone);
    free(local);
    return(NULL);
}

/*
 * ExecuteSupersedes(): cancel the article pointed to by the supersedes.
 * We dummy-up a cancel control.
 */

void
ExecuteSupersedes(Connection *conn, const char *supers, const char *art, int artLen)
{
    char *cmd = zalloc(&conn->co_MemPool, strlen(supers) + 32);

    sprintf(cmd, "cancel %s", supers);
    ExecuteControl(conn, cmd, art, artLen);
    zfree(&conn->co_MemPool, cmd, strlen(supers) + 32);
}

const char *
locateXTrace(const char *art, int headLen, int *pxtLen)
{
    while (headLen > 0) {
	int len;

	for (len = 0; len < headLen && art[len] != '\n'; ++len)
	    ;
	if (len < headLen)
	    ++len;
	if (len > 8 && strncasecmp(art, "X-Trace:", 8) == 0) {
	    art += 8;
	    len -= 8;
	    while (len && (*art == ' ' || *art == '\t')) {
		++art;
		--len;
	    }
	    for (headLen = 0; headLen < len; ++headLen) {
		if (art[headLen] == ' ' ||
		    art[headLen] == '\t' ||
		    art[headLen] == '\r' ||
		    art[headLen] == '\n'
		) {
		    break;
		}
	    }
	    break;
	}
	headLen -= len;
	art += len;
    }
    if (headLen <= 0) {
	headLen = 0;
	art = NULL;
    }
    *pxtLen = headLen;
    return(art);
}

