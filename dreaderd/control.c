
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
Prototype void ExecuteSupersedes(Connection *conn, const char *supers, const char *msgid, const char *art, int artLen);

Control *ParseControl(Connection *conn, const char *cmd, const char *from, const char *newsgroup);
void SendMail(FILE *logf, long lpos, char *hostname, const char *cmd, const char *arg);
const char *PerformControl(Connection *conn, Control *ctl, const char **cmd, const char *art, const char *body, int bodyLen, char *groups, const char *from);
const char *CtlCancel(Connection *conn, Control *ctl, const char **cmd, const char *art, const char *body, int bodyLen, char *groups);
const char *CtlNewGroup(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen);
const char *CtlRmGroup(Connection *conn, Control *ctl, const char **cmd, const char *body, int bodyLen);
const char *CgRun(Connection *conn, Control *ctl, const char *art, int artLen, const char *body, int bodyLen, const char *sender, const char *from, int hasPGP, int hasDGP);
int CtlFromMatches(const char *pat, const char *from);
static void CtlLogArticle(FILE *fo, const char *art, int artLen);
const char *locateXTrace(const char *art, int headLen, int *pxtLen);

void
ExecuteControl(Connection *conn, const char *ctlMsg, const char *art, int artLen)
{
    const char *body;
    const char *newsgroup = NULL;
    char *from = NULL;
    char *sender = NULL;
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
	int nSender = 0;
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
		zfreeStr(&conn->co_MemPool, &from);
		from = zallocStrTrim2(&conn->co_MemPool, 0, line + 5, l - 5);
	    } else if (strncasecmp(line, "Sender:", 7) == 0) {
		++nSender;
		zfreeStr(&conn->co_MemPool, &sender);
		sender = zallocStrTrim2(&conn->co_MemPool, 0, line + 7, l - 7);
	    } else if (strncasecmp(line, "Newsgroups:", 11) == 0) {
		++nNewsgroups;
		zfreeStr(&conn->co_MemPool, &groups);
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
	 * article's Control: that is acted on, and the From:, Sender: and
	 * Newsgroups: matched against dcontrol.ctl, are the last ones, while a
	 * signature covers the first.
	 */
	if (nControl > 1 || nFrom > 1 || nSender > 1 || nNewsgroups > 1 || nPGPSig > 1)
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
	badCmd = "duplicate Control:, From:, Sender:, Newsgroups: or X-PGP-Sig: header\n";
	logit(LOG_NOTICE, "control %s %s refused: duplicate Control:, From:, "
	    "Sender:, Newsgroups: or X-PGP-Sig: header", cmd[0], cmd[1] ? cmd[1] : "");
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

	if (ctl->ct_LogFo)
	    CtlLogArticle(ctl->ct_LogFo, art, artLen);

	/*
	 * options
	 */

	/****************************************************************
	 *		GLUE FOR PGP/DGP VERIFICATION			*
	 ****************************************************************
	 *
	 */

	if (strcasecmp(cmd[0], "checkgroups") == 0) {
	    /*
	     * Every checkgroups line that matches the sender is used, as in
	     * INN's controlchan, each with its own verification.
	     */
	    int hasPGP = 0;
	    int hasDGP = 0;

#if DIABLO_PGP_SUPPORT
	    hasPGP = hasXPGPSig;
#endif
#if DIABLO_DGP_SUPPORT
	    hasDGP = hasXDGPSig;
#endif
	    if (badCmd == NULL)
		badCmd = CgRun(conn, ctl, art, artLen, body, artLen - (body - art), (sender && sender[0]) ? sender : from, from, hasPGP, hasDGP);
	} else if (ctl->ct_Flags & CTFF_VERIFY) {
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

	if (strcasecmp(cmd[0], "checkgroups") == 0) {
	    ;				/* done by CgRun() */
	} else if ((ctl->ct_Flags & CTF_EXECUTE)) {
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
    zfreeStr(&conn->co_MemPool, &sender);
    zfreeStr(&conn->co_MemPool, &groups);
    zfree(&conn->co_MemPool, cmdBuf, cmdBufLen);
    FreeControl(conn);
}

/*
 * The log=/doit= file that ParseControl() opened for the message, as named in
 * dcontrol.ctl ("" for none).
 */
static char CtlLogName[256];

/*
 * CtlLogArticle() - the article, as the control log starts each message.
 */
static void
CtlLogArticle(FILE *fo, const char *art, int artLen)
{
    int i;
    int phead = 1;
    time_t t = time(NULL);
    struct tm *tp = localtime(&t);
    char tbuf[64];

    strftime(tbuf, sizeof(tbuf), "%c", tp);
    fprintf(fo, "\nCONTROL %s\n", tbuf);

    for (i = 0; i < artLen; ++i) {
	if (art[i] == '\r')
	    continue;
	if (phead) {
	    fprintf(fo, "<< ");
	    phead = 0;
	}
	fputc((int)(uint8)art[i], fo);
	if (art[i] == '\n')
	    phead = 1;
    }
    fprintf(fo, "\n");
}

/*
 * CtlTempLog() - a temporary file for the log of a control message that has no
 * log file, for the mail.
 */
static void
CtlTempLog(Connection *conn, Control *ctl)
{
    char path[256];
    int fd;

    /*
     * mkstemp: a fixed name in /tmp could be replaced by a symlink
     * between remove() and open() (GHSA-7prm-44hq-cpq3).
     */
    snprintf(path, sizeof(path), "/tmp/dreaderd.ctl%d.XXXXXX",
							(int)getpid());

    if ((fd = mkstemp(path)) >= 0) {
	if (DebugOpt)
	    printf("created temporary log/copy file %s\n", path);
	if ((ctl->ct_LogFo = fdopen(fd, "r+")) != NULL) {
	    ctl->ct_TmpFileName = zallocStr(&conn->co_MemPool, path);
	    ctl->ct_LogSeekPos = 0;
	} else {
	    close(fd);
	    remove(path);
	}
    }
}

/*
 * CtlFromMatches() - the <from> pattern of a dcontrol.ctl line matches the From:
 * header: the whole header, or the address in <...> or before or after a space.
 */
int
CtlFromMatches(const char *pat, const char *from)
{
    char tmp[512];

    if (WildOrCmp(pat, from) == 0)
	return(1);
    if (strlen(pat) > 128)
	return(0);
    sprintf(tmp, "*<%s>*|* %s|%s *", pat, pat, pat);
    return(WildOrCmp(tmp, from) == 0);
}

Control *
ParseControl(Connection *conn, const char *cmd, const char *from, const char *newsgroup)
{
    FILE *fi;
    char buf[256];
    int tooLong;
    int lineNo = 0;
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
    CtlLogName[0] = 0;

    /*
     * Check From: before opening dcontrol.ctl: returning without the
     * fclose() leaked one descriptor per control article without From:
     * (GHSA-qjg5-xp53-hj7h).
     */
    if (from == NULL)
	return(NULL);
    if ((fi = fopen(PatLibExpand(DControlCtlPat), "r")) == NULL)
	return(NULL);

    while (ConfigGets(buf, sizeof(buf), fi, &tooLong) != NULL) {
	++lineNo;
	if (tooLong) {
	    /* its tail must not be read as a line of its own (D-94) */
	    /* ConfigGets() takes sizeof(buf) - 1 characters before the newline */
	    logit(LOG_ERR, "%s: line %d too long (at most %d characters), ignored",
		PatLibExpand(DControlCtlPat), lineNo, (int)sizeof(buf) - 1);
	    continue;
	}
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
	if (!CtlFromMatches(sfrom, from))
	    continue;

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
	    /* the later line decides the verifier too (D-110) */
	    zfreeStr(&conn->co_MemPool, &ctl->ct_Verify);
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
		zfreeStr(&conn->co_MemPool, &ctl->ct_Verify);
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
		    /*
		     * Forget the closed handle now: if the reopen below fails
		     * (fd < 0) ct_LogFo would otherwise stay pointing at a
		     * closed FILE*, which the temp-file fallback test, every
		     * later fprintf(), and FreeControl's fclose() would all
		     * use/close again.
		     */
		    ctl->ct_LogFo = NULL;
		}

		CtlLogName[0] = 0;
		if (logFile[0] == '/')
		    fd = xopen(O_RDWR|O_CREAT, 0640, "%s", logFile);
		else
		    fd = xopen(O_RDWR|O_CREAT, 0640, "%s/%s", PatExpand(LogHomePat), logFile);

		if (DebugOpt)
		    printf("creating log file %s fd = %d\n", logFile, fd);

		/*
		 * Other readers append to the same log: without the lock
		 * do not log this message there.
		 */
		if (fd >= 0 && hflock(fd, 0, XLOCK_EX) < 0) {
		    logit(LOG_ERR, "Unable to lock control log %s: %s",
			logFile, strerror(errno));
		    close(fd);
		    fd = -1;
		}
		if (fd >= 0 && (ctl->ct_LogFo = fdopen(fd, "r+")) == NULL) {
		    hflock(fd, 0, XLOCK_UN);
		    close(fd);
		    fd = -1;
		}
		if (fd >= 0) {
		    fseek(ctl->ct_LogFo, 0L, 2);
		    ctl->ct_LogSeekPos = ftell(ctl->ct_LogFo);
		    ctl->ct_Flags |= CTF_LOG;
		    snprintf(CtlLogName, sizeof(CtlLogName), "%s", logFile);
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
	if (DebugOpt || (ctl->ct_Flags & (CTF_MAIL|CTF_LOG)))
	    CtlTempLog(conn, ctl);
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
 * While ExecuteSupersedes() runs its dummy cancel: the Message-ID of the
 * superseding article, so that CtlCancel() logs the decision as
 * "supersede <old> by <new>" instead of "cancel <old>" (D-205).
 */
static const char *SupersedeBy;

/* "cancel <id>" or "supersede <id> by <new id>" for the logs */
static const char *
cancelWhat(char *buf, size_t size, const char *id)
{
    if (SupersedeBy != NULL)
	snprintf(buf, size, "supersede %s by %s", id, SupersedeBy);
    else
	snprintf(buf, size, "cancel %s", id);
    return(buf);
}

/*
 * The Cancel-Lock mode that applies to the request being run: a supersede
 * (ExecuteSupersedes() running its dummy cancel) uses readersupersedelock if
 * that is set and readercanlock otherwise; a cancel always uses readercanlock.
 * *opt gets the name of the option in force, for the log.
 */
static int
canLockMode(const char **opt)
{
    if (SupersedeBy != NULL && DOpts.ReaderSupersedeLock != CANLOCK_INHERIT) {
	*opt = "readersupersedelock";
	return(DOpts.ReaderSupersedeLock);
    }
    *opt = "readercanlock";
    return(DOpts.ReaderCanLock);
}

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
    const char *opt;
    int mode = canLockMode(&opt);
    static char whyBuf[96];

    switch (mode) {
    case CANLOCK_ALL:
	snprintf(whyBuf, sizeof(whyBuf), "not checked (%s all)", opt);
	*why = whyBuf;
	return(1);
    case CANLOCK_NONE:
	snprintf(whyBuf, sizeof(whyBuf), "refused (%s none)", opt);
	*why = whyBuf;
	return(-1);
    }
    if ((data = GetOverRecord(ov, artNo, &hlen, NULL, NULL, NULL, NULL)) == NULL) {
	*why = "refused, headers of the target not available";
	return(-1);
    }
    r = CanLockFindHeader(data, hlen, "Cancel-Lock", lock, sizeof(lock));
    if (r < 0) {
	*why = "refused, Cancel-Lock of the target too long";
	return(-1);
    }
    if (r == 0) {
	if (mode == CANLOCK_AUTH) {
	    snprintf(whyBuf, sizeof(whyBuf),
		"target has no Cancel-Lock, executed (%s auth)", opt);
	    *why = whyBuf;
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
    char what[2 * MAXMSGIDLEN + 32];
    const char *kind = (SupersedeBy != NULL) ? "supersede" : "cancel";
    const char *opt;
    int mode = canLockMode(&opt);
    char whyBuf[96];

    /*
     * readercanlock none (readersupersedelock none for a supersede) executes no cancel or supersede (as INN's docancels
     * none): refuse it here, before the overview of its groups is scanned
     * for the target (D-203).  Before, every cancel cost that scan, only to
     * be refused by canLockDecide() or, for a target not here, dropped.
     */
    if (mode == CANLOCK_NONE) {
	snprintf(whyBuf, sizeof(whyBuf), "refused (%s none)", opt);
	why = whyBuf;
	logit(LOG_NOTICE, "%s: %s, 0 item(s)",
	    cancelWhat(what, sizeof(what), cmd[1]), why);
	if (DebugOpt)
	    printf("CtlCancel %s: %s, not looked up: %s\n", cmd[1], why, safestr(groups, ""));
	if (ctl->ct_LogFo)
	    fprintf(ctl->ct_LogFo, ">> control %s, not looked up, %s\n", kind, why);
	return(NULL);
    }

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
	if (validGroups && mode == CANLOCK_ALL) {
	    EnterCancelCache(cmd[1]);
	    why = "target not here yet, pre-cancel cached";
	} else if (validGroups) {
	    snprintf(whyBuf, sizeof(whyBuf),
		"target not here yet, dropped (%s)", opt);
	    why = whyBuf;
	} else {
	    why = "no valid groups, nothing canceled";
	}
    }
    if (mode != CANLOCK_ALL)
	logit((isValidCancel > 0) ? LOG_INFO : LOG_NOTICE,
	    "%s: %s, %d item(s)", cancelWhat(what, sizeof(what), cmd[1]), why,
	    (isValidCancel > 0) ? count : 0);
    if (DebugOpt)
	printf("CtlCancel %s: %s, %d items found: %s\n", cmd[1], why, count, safestr(groups, ""));
    if (ctl->ct_LogFo) {
	fprintf(ctl->ct_LogFo, ">> control %s, %d items found, %s\n",
	    kind, count, why);
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

    rec = KPDBReadRecord(KDBActive, cmd[1], KP_LOCK, &recLen);
    if (rec == NULL) {
	/*
	 * Create if absent (D-254): the group may have been made, and
	 * numbered, since the read above.  Then it is the existing group
	 * below (locked): NB and NE stay.
	 */
	if (KPDBCreate(KDBActive, cmd[1], "NB", "0000000001", KP_LOCK) == 0) {
	    KPDBWrite(KDBActive, cmd[1], "NE", "0000000000", KP_LOCK_CONTINUE);
	    isNew = 1;
	} else {
	    rec = KPDBReadRecord(KDBActive, cmd[1], 0, &recLen);
	}
    }
    if (rec != NULL) {
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
 * checkgroups, as INN documents control.ctl and docheckgroups.
 *
 *	The sender is the address of Sender: (else From:), reduced to one address in
 *	lower case as INN's controlchan does.  Every checkgroups (or "all") line of
 *	dcontrol.ctl whose <from> matches it is used, not only the last one; a line
 *	with an action this does not know is ignored.  The lines are taken from the
 *	last to the first, and a line acts on the groups that match its <newsgroups>
 *	pattern and the pattern of no later line that was used, so that a later line
 *	wins:
 *
 *	    doit, verify-*		the groups are checked (below).  A verify line
 *					whose signature is missing or bad is skipped,
 *					as if it were not there.  One whose signature
 *					cannot be checked (no pgpverify, its exit
 *					status 2) leaves its groups alone, as a log
 *					line does.
 *	    dgpverify			not implemented: leaves its groups alone.
 *	    drop			the groups are left out of the list, so that
 *					a group of them that exists is removed by an
 *					earlier doit line that covers it.
 *	    log, mail			nothing is changed; the lines of the message
 *					for the groups are written to the control log
 *					(and mailed with mail).
 *
 *	A line's =xxx log gets what that line does.  INN 2.7's docheckgroups also
 *	removes a group that a later line covers; this follows INN's documentation
 *	and leaves it.
 *
 *	A check is INN's docheckgroups.  The body has 'groupname description' lines
 *	(a description with "(Moderated)" is a moderated group), and every group in
 *	it that the line acts on is checked against the active file:
 *
 *	    - the description of a group that exists is updated;
 *	    - the moderated flag of a group that exists is corrected;
 *	    - a group that is missing is created (an addition);
 *	    - a group of a top-level hierarchy of the checked groups that matches the
 *	      pattern, no later line's pattern and is not in the list is removed (a
 *	      removal).
 *
 *	An addition or a flag correction is only made where a newgroup of that group
 *	from the sender would be executed, and a removal only where an rmgroup would
 *	(D-200): the last newgroup (rmgroup) or "all" line of dcontrol.ctl that matches
 *	the From: header and the group, as ParseControl() finds it for a newgroup
 *	(CtlFromMatches(); "break" ends the scan),
 *	must be doit or verify-<key>; verify-<key> only when the checkgroups line was
 *	verified with that key; no such line, drop, log, mail and dgpverify allow
 *	nothing.  So "newgroup:*:*:drop" also stops checkgroups from creating groups.
 *	The changes that are not allowed are written to the control log as the
 *	dgrpctl commands that make them.  (INN's controlchan does not look at the
 *	newgroup and rmgroup lines for a checkgroups.)
 *
 *	The descriptions are always updated.  The additions, flag corrections (status
 *	changes) and removals are only made when no limit is exceeded; beyond that they
 *	are written to the control log as the dgrpctl commands that make them, and not
 *	made.  A limit is a line of INN's
 *		/maxdocheckgroups/:<from>:<newsgroups>:<number>
 *	each change of a group counts for the last such line that matches the sender
 *	and the group; the default is 10.  When no such line matches the sender, the
 *	older "maxcheckgroups:<from>:<newsgroups>:<number>" lines keep their meaning:
 *	the last one that matches the sender and the first group of the message
 *	limits all its changes.
 *
 *	A group that matches a line of the file "localgroups" (path_lib, one group or
 *	wildcard per line, '#' comments) is left alone: its description and flag are
 *	not changed, it is not created and never removed.  The descriptions of groups
 *	that the message does not list are never touched, removed or not.
 */

#define CG_MAXDOCHECKGROUPS	10
#define CG_MAXLINE		512

#define CGR_CHECK	1	/* doit, verify-*, dgpverify */
#define CGR_DROP	2	/* drop */
#define CGR_REPORT	3	/* log, mail */

typedef struct CgRule {
    char *cr_Groups;
    char *cr_Verify;		/* the key of verify-<key>, or NULL */
    char *cr_Log;		/* the line's =xxx log, or NULL */
    int   cr_Kind;
    int   cr_Flags;		/* CTF_PGPVERIFY, CTF_DGPVERIFY, CTF_MAIL */
} CgRule;

typedef struct CgLimit {
    char *cl_Groups;
    int   cl_Max;
    int   cl_Legacy;		/* a maxcheckgroups: line */
} CgLimit;

typedef struct CgEnt {
    char *ce_Group;
    char *ce_Desc;
    char  ce_Flags;		/* 'y' or 'm' */
    char  ce_Ok;		/* a newgroup of it would be executed */
} CgEnt;

#define CGG_ALL		0
#define CGG_NEWGROUP	1
#define CGG_RMGROUP	2

/*
 * A newgroup, rmgroup or "all" line of dcontrol.ctl whose <from> matches the
 * checkgroups sender: what a newgroup or rmgroup from that sender would do.
 */
typedef struct CgGrpRule {
    int   gr_Type;		/* CGG_* */
    char *gr_Groups;
    char *gr_Verify;		/* the key of the last verify-<key>, or NULL */
    int   gr_Flags;		/* CTF_EXECUTE, CTF_DROP, CTF_PGPVERIFY, CTF_DGPVERIFY, CTF_BREAK */
} CgGrpRule;

static int
CgCompare(const void *a, const void *b)
{
    return(strcmp(((const CgEnt *)a)->ce_Group, ((const CgEnt *)b)->ce_Group));
}

/*
 * CgSenderAddr() - the sender as the checkgroups lines are matched against it,
 * made as INN's controlchan (cleanaddr) makes it from Sender: or From:: the
 * comment, from the first '(' to the last ')' with the white space around it,
 * is removed; of a <...> the text from the last '<' before the last '>' to
 * that '>' is taken; every character but letters, digits and -+_.@% becomes
 * '_', a leading '-' too; lower case.  The result is malloc()ed, NULL when out
 * of memory.
 */
static char *
CgSenderAddr(const char *hdr)
{
    char *tmp;
    char *buf;
    const char *lp;
    const char *rp;
    const char *b;
    const char *e;
    int n = 0;

    if ((tmp = strdup(hdr ? hdr : "")) == NULL)
	return(NULL);
    if ((buf = malloc(strlen(tmp) + 1)) == NULL) {
	free(tmp);
	return(NULL);
    }
    {
	char *open = strchr(tmp, '(');
	char *close = open ? strrchr(open, ')') : NULL;

	if (open && close) {
	    while (open > tmp && (open[-1] == ' ' || open[-1] == '\t'))
		--open;
	    ++close;
	    while (*close == ' ' || *close == '\t')
		++close;
	    memmove(open, close, strlen(close) + 1);
	}
    }
    b = tmp;
    e = tmp + strlen(tmp);
    if ((rp = strrchr(tmp, '>')) != NULL) {
	for (lp = rp; lp > tmp && lp[-1] != '<'; --lp)
	    ;
	if (lp > tmp) {
	    b = lp;
	    e = rp;
	}
    }
    for (; b < e; ++b) {
	int c = (uint8)*b;

	if (!isalnum(c) && strchr("-+_.@%", c) == NULL)
	    c = '_';
	if (n == 0 && c == '-')
	    c = '_';
	buf[n++] = tolower(c);
    }
    buf[n] = 0;
    free(tmp);
    return(buf);
}

/*
 * The <from> pattern of a line matches the sender's address (lower case both).
 */
static int
CgSenderMatches(const char *pat, const char *addr)
{
    char lpat[256];
    int i;

    for (i = 0; pat[i] && i < (int)sizeof(lpat) - 1; ++i)
	lpat[i] = tolower((int)(uint8)pat[i]);
    lpat[i] = 0;
    return(pat[i] == 0 && WildOrCmp(lpat, addr) == 0);
}

/*
 * The checkgroups lines and the limits of dcontrol.ctl that match the sender's
 * address, in file order.  A line with an action it does not know is ignored,
 * with a warning, as INN does.  A matching line with "break" ends the scan, as in
 * ParseControl().  Returns -1 when memory runs out (nothing is returned then).
 */
static int
CgReadRules(const char *addr, CgRule **prules, int *pnRules, CgLimit **plimits, int *pnLimits)
{
    FILE *fi = fopen(PatLibExpand(DControlCtlPat), "r");
    char buf[256];
    CgRule *rules = NULL;
    CgLimit *limits = NULL;
    int nRules = 0;
    int nLimits = 0;
    int failed = 0;
    int tooLong;
    int i;

    while (fi != NULL && !failed && ConfigGets(buf, sizeof(buf), fi, &tooLong) != NULL) {
	char *smsg;
	char *sfrom;
	char *sgroups;
	char *saction;
	int isLimit;

	if (tooLong)
	    continue;		/* ParseControl() logs it */
	if (buf[0] == '\n' || buf[0] == '#' ||
	    (smsg = strtok(buf, ":\n")) == NULL ||
	    smsg[0] == '#'
	) {
	    continue;
	}
	if ((sfrom = strtok(NULL, ":\n")) == NULL ||
	    (sgroups = strtok(NULL, ":\n")) == NULL ||
	    (saction = strtok(NULL, ":\n")) == NULL
	) {
	    continue;
	}
	isLimit = (strcasecmp(smsg, "/maxdocheckgroups/") == 0 ||
		   strcasecmp(smsg, "maxcheckgroups") == 0);
	if (!isLimit && strcmp(smsg, "all") != 0 && strcasecmp(smsg, "checkgroups") != 0)
	    continue;
	if (!CgSenderMatches(sfrom, addr))
	    continue;

	if (isLimit) {
	    long v = strtol(saction, NULL, 10);
	    CgLimit *nl;

	    if (v < 0 || v >= 100000000)
		continue;
	    if ((nl = realloc(limits, (nLimits + 1) * sizeof(CgLimit))) == NULL) {
		failed = 1;
		break;
	    }
	    limits = nl;
	    if ((limits[nLimits].cl_Groups = strdup(sgroups)) == NULL) {
		failed = 1;
		break;
	    }
	    limits[nLimits].cl_Max = (int)v;
	    limits[nLimits].cl_Legacy = (strcasecmp(smsg, "maxcheckgroups") == 0);
	    ++nLimits;
	} else {
	    CgRule r;
	    CgRule *nr;
	    char *a;
	    char *logName = NULL;
	    int check = 0;
	    int drop = 0;
	    int brk = 0;
	    int known = 0;
	    int unknown = 0;

	    memset(&r, 0, sizeof(r));
	    for (a = strtok(saction, ",\n"); a; a = strtok(NULL, ",\n")) {
		char *logFile;

		if ((logFile = strchr(a, '=')) != NULL)
		    *logFile++ = 0;
		if (strcmp(a, "doit") == 0) {
		    check = known = 1;
		} else if (strcmp(a, "drop") == 0) {
		    drop = known = 1;
		} else if (strcmp(a, "log") == 0) {
		    known = 1;
		} else if (strcmp(a, "mail") == 0) {
		    r.cr_Flags |= CTF_MAIL;
		    known = 1;
		} else if (strcmp(a, "break") == 0) {
		    brk = 1;
		} else if (strncmp(a, "verify-", 7) == 0 && a[7]) {
		    check = known = 1;
		    r.cr_Flags |= CTF_PGPVERIFY;
		    free(r.cr_Verify);
		    if ((r.cr_Verify = strdup(a + 7)) == NULL)
			failed = 1;
		} else if (strcmp(a, "dgpverify") == 0) {
		    check = known = 1;
		    r.cr_Flags |= CTF_DGPVERIFY;
		} else {
		    logit(LOG_ERR, "dcontrol.ctl: unknown action \"%s\", checkgroups line for %s ignored",
			a, sgroups);
		    unknown = 1;
		}
		if (logFile)
		    logName = logFile;
	    }
	    if (!failed && known && !unknown) {
		r.cr_Kind = drop ? CGR_DROP : check ? CGR_CHECK : CGR_REPORT;
		r.cr_Groups = strdup(sgroups);
		if (logName)
		    r.cr_Log = strdup(logName);
		if (r.cr_Groups == NULL || (logName && r.cr_Log == NULL) ||
		    (nr = realloc(rules, (nRules + 1) * sizeof(CgRule))) == NULL
		) {
		    failed = 1;
		} else {
		    rules = nr;
		    rules[nRules++] = r;
		    memset(&r, 0, sizeof(r));
		}
	    }
	    free(r.cr_Groups);
	    free(r.cr_Verify);
	    free(r.cr_Log);
	    if (brk)
		break;
	}
    }
    if (fi != NULL)
	fclose(fi);
    if (failed) {
	for (i = 0; i < nRules; ++i) {
	    free(rules[i].cr_Groups);
	    free(rules[i].cr_Verify);
	    free(rules[i].cr_Log);
	}
	for (i = 0; i < nLimits; ++i)
	    free(limits[i].cl_Groups);
	free(rules);
	free(limits);
	rules = NULL;
	limits = NULL;
	nRules = nLimits = 0;
    }
    *prules = rules;
    *pnRules = nRules;
    *plimits = limits;
    *pnLimits = nLimits;
    return(failed ? -1 : 0);
}

static void
CgFreeGrpRules(CgGrpRule *grules, int nGrules)
{
    int i;

    for (i = 0; i < nGrules; ++i) {
	free(grules[i].gr_Groups);
	free(grules[i].gr_Verify);
    }
    free(grules);
}

/*
 * The newgroup, rmgroup and "all" lines of dcontrol.ctl that match the From:
 * header, in file order, with their actions read as ParseControl() reads them.
 * The <from> pattern is matched as for a real newgroup or rmgroup
 * (CtlFromMatches(): the From: header as it is, its <address> and the forms
 * around a space, case-sensitive), not as the checkgroups lines are (Sender:,
 * reduced to a lower-case address), so "would be executed" is what a newgroup
 * from this sender would get.  Returns -1 when memory runs out (nothing is
 * returned then).
 */
static int
CgReadGrpRules(const char *from, CgGrpRule **pgrules, int *pnGrules)
{
    FILE *fi = fopen(PatLibExpand(DControlCtlPat), "r");
    char buf[256];
    int tooLong;
    CgGrpRule *grules = NULL;
    int nGrules = 0;
    int failed = 0;

    while (fi != NULL && !failed && ConfigGets(buf, sizeof(buf), fi, &tooLong) != NULL) {
	char *smsg;
	char *sfrom;
	char *sgroups;
	char *saction;
	char *a;
	CgGrpRule g;
	CgGrpRule *ng;

	if (tooLong)
	    continue;		/* ParseControl() logs it */
	if (buf[0] == '\n' || buf[0] == '#' ||
	    (smsg = strtok(buf, ":\n")) == NULL ||
	    smsg[0] == '#'
	) {
	    continue;
	}
	if ((sfrom = strtok(NULL, ":\n")) == NULL ||
	    (sgroups = strtok(NULL, ":\n")) == NULL ||
	    (saction = strtok(NULL, ":\n")) == NULL
	) {
	    continue;
	}
	memset(&g, 0, sizeof(g));
	if (strcmp(smsg, "all") == 0)
	    g.gr_Type = CGG_ALL;
	else if (strcasecmp(smsg, "newgroup") == 0)
	    g.gr_Type = CGG_NEWGROUP;
	else if (strcasecmp(smsg, "rmgroup") == 0)
	    g.gr_Type = CGG_RMGROUP;
	else
	    continue;
	if (from == NULL || !CtlFromMatches(sfrom, from))
	    continue;
	for (a = strtok(saction, ",\n"); a; a = strtok(NULL, ",\n")) {
	    char *eq;

	    if ((eq = strchr(a, '=')) != NULL)
		*eq = 0;
	    if (strcmp(a, "doit") == 0) {
		g.gr_Flags |= CTF_EXECUTE;
	    } else if (strcmp(a, "break") == 0) {
		g.gr_Flags |= CTF_BREAK;
	    } else if (strcmp(a, "drop") == 0) {
		g.gr_Flags &= ~CTF_EXECUTE;
		g.gr_Flags |= CTF_DROP;
	    } else if (strncmp(a, "verify-", 7) == 0) {
		g.gr_Flags |= CTF_PGPVERIFY;
		if ((g.gr_Flags & CTF_DROP) == 0)
		    g.gr_Flags |= CTF_EXECUTE;
		free(g.gr_Verify);
		if ((g.gr_Verify = strdup(a + 7)) == NULL)
		    failed = 1;
	    } else if (strcmp(a, "dgpverify") == 0) {
		g.gr_Flags |= CTF_DGPVERIFY;
		if ((g.gr_Flags & CTF_DROP) == 0)
		    g.gr_Flags |= CTF_EXECUTE;
	    }
	}
	if (!failed && (g.gr_Groups = strdup(sgroups)) != NULL &&
	    (ng = realloc(grules, (nGrules + 1) * sizeof(CgGrpRule))) != NULL
	) {
	    grules = ng;
	    grules[nGrules++] = g;
	} else {
	    free(g.gr_Groups);
	    free(g.gr_Verify);
	    failed = 1;
	}
    }
    if (fi != NULL)
	fclose(fi);
    if (failed) {
	CgFreeGrpRules(grules, nGrules);
	grules = NULL;
	nGrules = 0;
    }
    *pgrules = grules;
    *pnGrules = nGrules;
    return(failed ? -1 : 0);
}

/*
 * A newgroup (type CGG_NEWGROUP) or rmgroup (CGG_RMGROUP) of the group from the
 * sender would be executed, as ParseControl() decides it: its last matching line
 * is doit, or verify-<key> with the key the checkgroups line (rule) was verified
 * with.  dgpverify is never verified for a checkgroups.
 */
static int
CgGrpAllowed(const CgGrpRule *grules, int nGrules, int type, const char *group, const CgRule *rule)
{
    const CgGrpRule *last = NULL;
    int i;

    for (i = 0; i < nGrules; ++i) {
	if (grules[i].gr_Type != CGG_ALL && grules[i].gr_Type != type)
	    continue;
	if (WildOrCmp(grules[i].gr_Groups, group) != 0)
	    continue;
	last = &grules[i];
	if (last->gr_Flags & CTF_BREAK)
	    break;
    }
    if (last == NULL || (last->gr_Flags & CTF_EXECUTE) == 0)
	return(0);
    if (last->gr_Flags & CTF_DGPVERIFY)
	return(0);
    if (last->gr_Flags & CTF_PGPVERIFY) {
	return((rule->cr_Flags & CTF_PGPVERIFY) && rule->cr_Verify &&
	    last->gr_Verify && strcmp(rule->cr_Verify, last->gr_Verify) == 0);
    }
    return(1);
}

/*
 * The limit that a change of the group counts for: the last /maxdocheckgroups/
 * line that matches it, or nLimits for the default.
 */
static int
CgLimitFor(const CgLimit *limits, int nLimits, const char *group)
{
    int i;

    for (i = nLimits - 1; i >= 0; --i) {
	if (!limits[i].cl_Legacy && WildOrCmp(limits[i].cl_Groups, group) == 0)
	    return(i);
    }
    return(nLimits);
}

/*
 * The group is one a line acts on: it matches include, and neither exclude (the
 * patterns of the later lines) nor drop (the patterns of later drop lines); NULL is
 * an empty pattern.
 */
static int
CgActsOn(const char *group, const char *include, const char *exclude, const char *drop)
{
    if (WildOrCmp(include, group) != 0)
	return(0);
    if (exclude && WildOrCmp(exclude, group) == 0)
	return(0);
    if (drop && WildOrCmp(drop, group) == 0)
	return(0);
    return(1);
}

/*
 * Add a pattern to a '|' separated list; -1 when memory runs out.
 */
static int
CgAppend(char **plist, const char *pat)
{
    size_t len = *plist ? strlen(*plist) : 0;
    char *nl = realloc(*plist, len + strlen(pat) + 2);

    if (nl == NULL)
	return(-1);
    sprintf(nl + len, "%s%s", len ? "|" : "", pat);
    *plist = nl;
    return(0);
}

/*
 * The patterns of the file "localgroups" (path_lib), as one '|' separated
 * wildcard in *ppat (NULL for none); -1 when memory runs out.
 */
static int
CgLocalGroups(char **ppat)
{
    FILE *fi = fopen(PatLibExpand("%s/localgroups"), "r");
    char buf[256];
    int r = 0;
    int tooLong;
    int lineNo = 0;

    *ppat = NULL;
    if (fi == NULL)
	return(0);
    while (r == 0 && ConfigGets(buf, sizeof(buf), fi, &tooLong) != NULL) {
	char *g = strtok(buf, " \t\r\n");

	++lineNo;
	if (g == NULL || g[0] == '#')
	    continue;
	if (tooLong) {
	    /* its tail must not be read as a pattern of its own */
	    /* ConfigGets() takes sizeof(buf) - 1 characters before the newline */
	    logit(LOG_ERR, "%s: line %d too long (at most %d characters), ignored",
		PatLibExpand("%s/localgroups"), lineNo, (int)sizeof(buf) - 1);
	    continue;
	}
	r = CgAppend(ppat, g);
    }
    fclose(fi);
    if (r < 0) {
	free(*ppat);
	*ppat = NULL;
    }
    return(r);
}

/*
 * The next 'groupname description' line of the body at *pi: the name in group
 * (MAXGNAME), the cleaned description in *pdesc (in line, CG_MAXLINE).  Returns 0 at
 * the end of the body, -1 for a line to skip.
 */
static int
CgNextLine(const char *body, int bodyLen, int *pi, char *line, char *group, char **pdesc)
{
    int i = *pi;
    int l;
    char *newsgroup;
    char *description;

    if (i >= bodyLen)
	return(0);
    for (l = i; l < bodyLen && body[l] != '\n'; ++l)
	;
    if (l < bodyLen)
	++l;
    *pi = l;
    if (l - i >= CG_MAXLINE)
	return(-1);
    memcpy(line, body + i, l - i);
    line[l-i] = 0;

    if ((newsgroup = strtok(line, " \t\r\n")) == NULL)
	return(-1);
    if (ValidGroupName(newsgroup) < 0)		/* valid group name	*/
	return(-1);
    if (strlen(newsgroup) >= MAXGNAME)
	return(-1);
    if (strchr(newsgroup, '.') == NULL)		/* sanity check 	*/
	return(-1);
    if ((description = strtok(NULL, "\r\n")) == NULL)
	return(-1);
    strcpy(group, newsgroup);

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
    *pdesc = description;
    return(1);
}

/*
 * A complete record for a group that did not exist, as CtlNewGroup() makes it.
 * Returns 1 when it made the record, and 0 when the group exists by now (made,
 * and perhaps numbered, since the caller looked): that one is left as it is.
 */
static int
CgCreate(const char *group, char flags, const char *description)
{
    char tsBuf[64];
    char fl[2];

    fl[0] = flags;
    fl[1] = 0;
    sprintf(tsBuf, "%08x", (int)time(NULL));
    if (KPDBCreate(KDBActive, group, "NB", "0000000001", KP_LOCK) != 0) {
	KPDBWrite(KDBActive, group, NULL, NULL, KP_UNLOCK);
	return(0);
    }
    KPDBWrite(KDBActive, group, "NE", "0000000000", KP_LOCK_CONTINUE);
    if (description[0])
	KPDBWriteEncode(KDBActive, group, "GD", description, KP_LOCK_CONTINUE);
    KPDBWrite(KDBActive, group, "CTS", tsBuf, KP_LOCK_CONTINUE);
    KPDBWrite(KDBActive, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
    KPDBWrite(KDBActive, group, "S", fl, KP_UNLOCK);
    return(1);
}

/*
 * The check of one doit or verify line (see above) for the groups of the message
 * that CgActsOn() takes.  Returns the number of those groups, or -1 when memory
 * runs out (the check stops there).
 *
 * A change counts for its /maxdocheckgroups/ line.  When no such line matches
 * the sender, maxcheckgroups: lines keep their earlier meaning: the last one that
 * matches the first group of the message limits all its changes.
 */
static int
CgCheck(Control *ctl, const char *body, int bodyLen, const char *include, const char *exclude, const char *drop, const CgLimit *limits, int nLimits, const CgRule *rule, const CgGrpRule *grules, int nGrules)
{
    int i;
    int r;
    int pos = 0;
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
    int noAdd = 0;		/* not allowed by the newgroup lines */
    int noFlags = 0;
    char **kept = NULL;		/* removals the rmgroup lines do not allow */
    int noGone = 0;
    int maxKept = 0;
    int *counts = NULL;		/* changes per limit, [nLimits] is the default */
    int shown;			/* the limit for the log line */
    int doit;
    int recOff;
    int recLen;
    int failed = 1;		/* until the end */
    int nNew = 0;		/* /maxdocheckgroups/ lines */
    int legacy = -1;		/* the maxcheckgroups: line for the message */
    char first[MAXGNAME];	/* the first group of the message */
    char *local;

    first[0] = 0;
    if (CgLocalGroups(&local) < 0)
	return(-1);

    for (;;) {
	char tmp[CG_MAXLINE];
	char newsgroup[MAXGNAME];
	char *description;

	if ((r = CgNextLine(body, bodyLen, &pos, tmp, newsgroup, &description)) == 0)
	    break;
	if (r < 0)
	    continue;

	/*
	 * The group must be one this line acts on, and must not be a local group.
	 */
	if (!CgActsOn(newsgroup, include, exclude, drop))
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
	if (ents[nEnts].ce_Group == NULL || ents[nEnts].ce_Desc == NULL) {
	    ++nEnts;			/* freed at out */
	    goto out;
	}
	if (nEnts == 0)
	    strcpy(first, newsgroup);
	++nEnts;
    }
    if (nEnts == 0) {
	failed = 0;
	goto out;
    }
    if ((counts = calloc(nLimits + 1, sizeof(int))) == NULL)
	goto out;
    for (i = 0; i < nLimits; ++i) {
	if (!limits[i].cl_Legacy)
	    ++nNew;
	else if (WildOrCmp(limits[i].cl_Groups, first) == 0)
	    legacy = i;
    }

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
     * Count the additions and the flags to correct, those a newgroup would make.
     */
    for (i = 0; i < nEnts; ++i) {
	const char *rec = KPDBReadRecord(KDBActive, ents[i].ce_Group, 0, &recLen);

	ents[i].ce_Ok = CgGrpAllowed(grules, nGrules, CGG_NEWGROUP, ents[i].ce_Group, rule);
	if (rec == NULL) {
	    if (!ents[i].ce_Ok) {
		++noAdd;
		continue;
	    }
	    ++nAdd;
	    ++counts[CgLimitFor(limits, nLimits, ents[i].ce_Group)];
	} else {
	    const char *p;
	    uint32 plen;

	    p = KPDBGetField(rec, recLen, "S", (int *)&plen, "n");
	    if (p == NULL || plen != 1 || p[0] != ents[i].ce_Flags) {
		if (!ents[i].ce_Ok) {
		    ++noFlags;
		    continue;
		}
		++nFlags;
		++counts[CgLimitFor(limits, nLimits, ents[i].ce_Group)];
	    }
	}
    }

    /*
     * The groups of those hierarchies that the line acts on and the list does not have
     * (a dropped group is not in the list).
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
	if (h == nHier || !CgActsOn(grpbuf, include, exclude, NULL))
	    continue;
	if (local && WildOrCmp(local, grpbuf) == 0)
	    continue;
	key.ce_Group = grpbuf;
	if (bsearch(&key, ents, nEnts, sizeof(CgEnt), CgCompare) != NULL)
	    continue;
	if (!CgGrpAllowed(grules, nGrules, CGG_RMGROUP, grpbuf, rule)) {
	    if (noGone == maxKept) {
		char **nk;

		maxKept = maxKept ? maxKept * 2 : 64;
		if ((nk = realloc(kept, maxKept * sizeof(char *))) == NULL)
		    goto out;
		kept = nk;
	    }
	    if ((kept[noGone] = strdup(grpbuf)) == NULL)
		goto out;
	    ++noGone;
	    continue;
	}
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
	++counts[CgLimitFor(limits, nLimits, grpbuf)];
    }

    /*
     * Additions, flag corrections and removals only within every limit; the descriptions always.
     */
    doit = 1;
    shown = nLimits;
    if (nNew == 0 && legacy >= 0) {
	shown = legacy;
	doit = (nAdd + nFlags + nGone <= limits[legacy].cl_Max);
    } else {
	for (i = 0; i <= nLimits; ++i) {
	    int max = (i < nLimits) ? limits[i].cl_Max : CG_MAXDOCHECKGROUPS;

	    if (i < nLimits && limits[i].cl_Legacy)
		continue;
	    if (counts[i] > max) {
		if (doit)
		    shown = i;
		doit = 0;
	    } else if (doit && counts[i] > counts[shown]) {
		shown = i;
	    }
	}
    }
    if (ctl->ct_LogFo && (nAdd + nFlags + nGone)) {
	fprintf(ctl->ct_LogFo, ">> Checkgroups: %d to add, %d to change, %d to remove (limit %d%s%s): %s (newsgroups %s)\n",
	    nAdd, nFlags, nGone,
	    (shown < nLimits) ? limits[shown].cl_Max : CG_MAXDOCHECKGROUPS,
	    (shown < nLimits) ? " for " : "",
	    (shown < nLimits) ? limits[shown].cl_Groups : "",
	    doit ? "made" : "NOT made, the commands follow",
	    include);
    }

    for (i = 0; i < nEnts; ++i) {
	const char *g = ents[i].ce_Group;
	const char *rec = KPDBReadRecord(KDBActive, g, KP_LOCK, &recLen);

	if (rec == NULL && !ents[i].ce_Ok) {
	    continue;			/* logged below */
	} else if (rec == NULL) {
	    if (doit) {
		if (CgCreate(g, ents[i].ce_Flags, ents[i].ce_Desc)) {
		    if (ctl->ct_LogFo)
			fprintf(ctl->ct_LogFo, ">> Checkgroups: Added group %s flags=%c\n", g, ents[i].ce_Flags);
		} else if (ctl->ct_LogFo) {
		    fprintf(ctl->ct_LogFo, ">> Checkgroups: Group %s was made meanwhile, left as it is\n", g);
		}
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

	    if (!doit && wrongFlags && ents[i].ce_Ok && ctl->ct_LogFo)
		fprintf(ctl->ct_LogFo, "dgrpctl modgroup %s %c \"\" \"%s\"\n", g, ents[i].ce_Flags, ents[i].ce_Desc);
	    if (!doit || !ents[i].ce_Ok)
		wrongFlags = 0;			/* only the description */
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

    /*
     * The changes a newgroup or rmgroup would not make: the commands that make them.
     */
    if ((noAdd + noFlags + noGone) && ctl->ct_LogFo) {
	fprintf(ctl->ct_LogFo, ">> Checkgroups: %d to add, %d to change, %d to remove: not allowed by the newgroup/rmgroup lines of dcontrol.ctl, NOT made, the commands follow (newsgroups %s)\n",
	    noAdd, noFlags, noGone, include);
	for (i = 0; i < nEnts; ++i) {
	    const char *g = ents[i].ce_Group;
	    const char *rec;
	    const char *p;
	    uint32 plen;

	    if (ents[i].ce_Ok)
		continue;
	    if ((rec = KPDBReadRecord(KDBActive, g, 0, &recLen)) == NULL) {
		fprintf(ctl->ct_LogFo, "dgrpctl newgroup %s %c \"\" \"%s\"\n", g, ents[i].ce_Flags, ents[i].ce_Desc);
		continue;
	    }
	    p = KPDBGetField(rec, recLen, "S", (int *)&plen, "n");
	    if (p == NULL || plen != 1 || p[0] != ents[i].ce_Flags)
		fprintf(ctl->ct_LogFo, "dgrpctl modgroup %s %c \"\" \"%s\"\n", g, ents[i].ce_Flags, ents[i].ce_Desc);
	}
	for (i = 0; i < noGone; ++i)
	    fprintf(ctl->ct_LogFo, "dgrpctl rmgroup %s\n", kept[i]);
    }
    failed = 0;
out:
    r = failed ? -1 : nEnts;
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
    for (i = 0; i < noGone; ++i)
	free(kept[i]);
    free(kept);
    free(counts);
    free(local);
    return(r);
}

/*
 * A log or mail line: the lines of the message for the groups that CgActsOn() takes
 * are written to the control log, nothing is changed.  Returns their number.
 */
static int
CgReport(Control *ctl, const char *body, int bodyLen, const char *include, const char *exclude, const char *drop)
{
    int pos = 0;
    int n = 0;
    int r;

    for (;;) {
	char tmp[CG_MAXLINE];
	char newsgroup[MAXGNAME];
	char *description;

	if ((r = CgNextLine(body, bodyLen, &pos, tmp, newsgroup, &description)) == 0)
	    break;
	if (r < 0 || !CgActsOn(newsgroup, include, exclude, drop))
	    continue;
	if (ctl->ct_LogFo) {
	    if (n == 0)
		fprintf(ctl->ct_LogFo, ">> Checkgroups: not acted on (newsgroups %s):\n", include);
	    fprintf(ctl->ct_LogFo, "%s\t%s\n", newsgroup, description);
	}
	++n;
    }
    return(n);
}

/*
 * CgOpenLog() - a line's own =xxx log (as in ParseControl()), locked, after the
 * article; NULL when it cannot be opened or locked.
 */
static FILE *
CgOpenLog(const char *name, const char *art, int artLen)
{
    FILE *fo;
    int fd;

    if (name[0] == '/')
	fd = xopen(O_RDWR|O_CREAT, 0640, "%s", name);
    else
	fd = xopen(O_RDWR|O_CREAT, 0640, "%s/%s", PatExpand(LogHomePat), name);
    if (fd < 0)
	return(NULL);
    if (hflock(fd, 0, XLOCK_EX) < 0) {
	logit(LOG_ERR, "Unable to lock control log %s: %s", name, strerror(errno));
	close(fd);
	return(NULL);
    }
    if ((fo = fdopen(fd, "a")) == NULL) {
	hflock(fd, 0, XLOCK_UN);
	close(fd);
	return(NULL);
    }
    CtlLogArticle(fo, art, artLen);
    return(fo);
}

/*
 * CgRun() - a checkgroups control message: the lines of dcontrol.ctl that match the
 * sender (Sender:, else From:, reduced to one address), from the last to the first
 * (see above).  pgpverify runs at most once.  A verify line whose signature is
 * missing or bad is skipped; one whose signature cannot be checked at all (no
 * pgpverify, its exit status 2, dgpverify, which is not implemented) blocks: its
 * groups are left alone, as by a log line.
 */
const char *
CgRun(Connection *conn, Control *ctl, const char *art, int artLen, const char *body, int bodyLen, const char *sender, const char *from, int hasPGP, int hasDGP)
{
    CgRule *rules;
    CgLimit *limits;
    int nRules;
    int nLimits;
    char *addr;
    CgGrpRule *grules = NULL;
    int nGrules = 0;
    char *exclude = NULL;	/* the patterns of the later lines used */
    char *drop = NULL;		/* the patterns of the later drop lines */
    char signer[256];
    const char *pgpBad = NULL;
    int pgpDone = 0;
    int pgpCannot = 0;
    const char *bad = NULL;
    int acted = 0;
    int mail = 0;
    int i;

    if ((addr = CgSenderAddr(sender)) == NULL)
	return("checkgroups: out of memory\n");
    i = CgReadRules(addr, &rules, &nRules, &limits, &nLimits);
    if (i == 0 && CgReadGrpRules(from, &grules, &nGrules) < 0) {
	for (i = 0; i < nRules; ++i) {
	    free(rules[i].cr_Groups);
	    free(rules[i].cr_Verify);
	    free(rules[i].cr_Log);
	}
	free(rules);
	for (i = 0; i < nLimits; ++i)
	    free(limits[i].cl_Groups);
	free(limits);
	i = -1;
    }
    free(addr);
    if (i < 0)
	return("checkgroups: out of memory\n");

    for (i = 0; i < nRules; ++i) {
	if (rules[i].cr_Flags & CTF_MAIL)
	    mail = 1;
    }
    if (mail && ctl->ct_LogFo == NULL) {
	CtlTempLog(conn, ctl);
	if (ctl->ct_LogFo)
	    CtlLogArticle(ctl->ct_LogFo, art, artLen);
    }

    for (i = nRules - 1; i >= 0 && bad == NULL; --i) {
	CgRule *rule = &rules[i];
	FILE *saved = ctl->ct_LogFo;
	FILE *own = NULL;
	int n = 0;

	/*
	 * A line's own log, when it is not the one ParseControl() chose, and
	 * only for a line that has groups of the message to act on (a catch-all
	 * line whose groups later lines took logs nothing).
	 */
	if (rule->cr_Kind != CGR_DROP && rule->cr_Log && strcmp(rule->cr_Log, CtlLogName) != 0) {
	    int some;

	    ctl->ct_LogFo = NULL;
	    some = CgReport(ctl, body, bodyLen, rule->cr_Groups, exclude, drop);
	    if (rule->cr_Log[0] && some > 0 && (own = CgOpenLog(rule->cr_Log, art, artLen)) != NULL)
		ctl->ct_LogFo = own;
	}

	if (rule->cr_Kind == CGR_DROP) {
	    if (ctl->ct_LogFo)
		fprintf(ctl->ct_LogFo, ">> Checkgroups: dropped (newsgroups %s)\n", rule->cr_Groups);
	    if (CgAppend(&drop, rule->cr_Groups) < 0)
		bad = "checkgroups: out of memory\n";
	} else {
	    const char *vbad = NULL;
	    int cannot = 0;

	    if (rule->cr_Flags & CTF_DGPVERIFY) {
		(void)hasDGP;
		vbad = "dgp verification is not implemented\n";
		cannot = 1;
	    }
	    if (vbad == NULL && (rule->cr_Flags & CTF_PGPVERIFY)) {
#if DIABLO_PGP_SUPPORT
		if (!hasPGP) {
		    vbad = "pgp verification required, signature missing\n";
		} else {
		    if (!pgpDone) {
			pgpBad = PGPSigner(art, artLen, signer, sizeof(signer), &pgpCannot);
			pgpDone = 1;
		    }
		    if (pgpBad) {
			vbad = pgpBad;
			cannot = pgpCannot;
		    } else if (strcmp(signer, rule->cr_Verify) != 0) {
			vbad = "pgp parsed ok, but label did not match\n";
		    }
		}
#else
		(void)hasPGP;
		vbad = "pgp verification required, not compiled in\n";
		cannot = 1;
#endif
	    }
	    if (vbad && cannot) {
		if (ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> Checkgroups: cannot verify, groups left alone (newsgroups %s): %s",
			rule->cr_Groups, vbad);
		if (CgAppend(&exclude, rule->cr_Groups) < 0)
		    bad = "checkgroups: out of memory\n";
	    } else if (vbad) {
		if (ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> Checkgroups: skipped (newsgroups %s): %s", rule->cr_Groups, vbad);
	    } else {
		if ((rule->cr_Flags & CTFF_VERIFY) && ctl->ct_LogFo)
		    fprintf(ctl->ct_LogFo, ">> Checkgroups: verified (newsgroups %s)\n", rule->cr_Groups);
		if (rule->cr_Kind == CGR_CHECK)
		    n = CgCheck(ctl, body, bodyLen, rule->cr_Groups, exclude, drop, limits, nLimits, rule, grules, nGrules);
		else
		    n = CgReport(ctl, body, bodyLen, rule->cr_Groups, exclude, drop);
		if (n < 0 || CgAppend(&exclude, rule->cr_Groups) < 0)
		    bad = "checkgroups: out of memory\n";
		acted = 1;
	    }
	}
	if (own) {
	    fflush(own);
	    hflock(fileno(own), 0, XLOCK_UN);
	    fclose(own);
	}
	ctl->ct_LogFo = saved;
    }
    if (bad == NULL && !acted)
	bad = "checkgroups: no line of dcontrol.ctl acted on it\n";
    ctl->ct_Flags &= ~CTF_MAIL;		/* the checkgroups lines decide */
    if (mail)
	ctl->ct_Flags |= CTF_MAIL;

    for (i = 0; i < nRules; ++i) {
	free(rules[i].cr_Groups);
	free(rules[i].cr_Verify);
	free(rules[i].cr_Log);
    }
    free(rules);
    for (i = 0; i < nLimits; ++i)
	free(limits[i].cl_Groups);
    free(limits);
    CgFreeGrpRules(grules, nGrules);
    free(exclude);
    free(drop);
    return(bad);
}

/*
 * ExecuteSupersedes(): cancel the article pointed to by the supersedes.
 * We dummy-up a cancel control.
 */

void
ExecuteSupersedes(Connection *conn, const char *supers, const char *msgid, const char *art, int artLen)
{
    char *cmd = zalloc(&conn->co_MemPool, strlen(supers) + 32);

    sprintf(cmd, "cancel %s", supers);
    SupersedeBy = (msgid != NULL && *msgid) ? msgid : "<unknown>";
    ExecuteControl(conn, cmd, art, artLen);
    SupersedeBy = NULL;
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

