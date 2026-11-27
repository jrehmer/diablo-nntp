
/*
 * GROUPCTL.C
 *
 * Create/Modify/Delete newsgroups in the active file
 *
 */

#include "dreaderd/defs.h"

#define MAXCMDARGS	8

void GrpCtlMassCancel(char *group);
const char *GrpCtlGroupInfo(char *group);
const char *GrpCtlListActive(char *match);
const char *GrpCtlListGroupHash(char *match, char *hash, int fname);
const char *GrpCtlListNewsgroups(char *match);
const char *GrpCtlNewGroup(int ac, char **av, int pos);
const char *GrpCtlRmGroup(char *group);
const char *GrpCtlAdjust(int ac, char **av, int pos);
const char *GrpCtlCheckGroups(int ac, char **av, int pos);
OverInfo * GetOverInfo(const char *group);
const OverArt * GetOverArt(OverInfo *ov, artno_t artno, off_t *ppos);

KPDB *KDBActive;

typedef struct GrpList {
    char *group;
    struct GrpList *next;
} GrpList;

#define	MASSCANCELHASHSIZ	262144

typedef struct CancelArt {
    hash_t hv;
    int found;
    struct CancelArt *next;
} CancelArt;

void
Usage(void)
{
    printf("Usage:\n");
    printf("  dgrpctl [-s] newgroup NEWSGROUP [FLAGS] [\"[MODERATOR]\" [\"DESCRIPTION\"]]\n");
    printf("       - add/modify the specified newsgroup\n");
    printf("	   flags       = 'y' or 'm' (default is 'y')\n");
    printf("	   moderator   = moderator email address or \"\"\n");
    printf("	   description = group description\n");
    printf("  dgrpctl [-s] rmgroup NEWSGROUP\n");
    printf("       - remove the specified newsgroup\n");
    printf("  dgrpctl [-s] groupinfo NEWSGROUP\n");
    printf("       - display detailed information about a newsgroup\n");
    printf("  dgrpctl [-s] adjust NEWSGROUP NB|NE|NX [+|-|=]value\n");
    printf("       - adjust one of the specified numbers for group\n");
    printf("         no sign increments item by value, = sets absolute item value\n");
    printf("  dgrpctl [-s] checkgroups [exec] filename|-\n");
    printf("       - parse the checkgroups list and print changes required\n");
    printf("	   exec = actually make all the necessary changes\n");
    printf("	   -    = read list on stdin\n");
    printf("  dgrpctl [-s] listactive [wildmat]\n");
    printf("       - list the contents of active file\n");
    printf("  dgrpctl [-s] listnewsgroups [wildmat]\n");
    printf("       - list the newsgroups and their descriptions\n");
    printf("  dgrpctl [-s] listgrouphash [wildmat [HASH]]\n");
    printf("       - list the hashes of newsgroup names\n");
    printf("  dgrpctl [-s] listgrouphashfile [wildmat [HASH]]\n");
    printf("       - list the index path/filename of a newsgroup\n");
    printf("  dgrpctl masscancel NEWSGROUP\n");
    printf("       - perform one-pass cancellation of a list of message-id's\n");
    printf("         provided on stdin in a single newsgroup's overview data\n");
    printf("  -s  option makes command act on *server* instead of *reader* active file\n");
    printf("\n");
}

int
main(int ac, char **av)
{
    int i = 1;
    const char *activefile;

    LoadDiabloConfig(ac, av);

    activefile = PatDbExpand(ReaderDActivePat);

    while (av[i] && av[i][0] == '-') {
	switch (av[i][1]) {
	    case 'V':
		PrintVersion();
		break;
	    case 's':
		activefile = PatDbExpand(ServerDActivePat);
		break;
	    default:
		Usage();
	}
	i++;
    }

    KDBActive = KPDBOpen(activefile, O_RDWR);
    if (!KDBActive) {
	printf("Unable to open active file: %s\n", activefile);
	exit(1);
    }

    if (av[i] && av[i+1] && strcmp(av[i], "newgroup") == 0)
	GrpCtlNewGroup(ac, av, i+1);
    else if (av[i] && av[i+1] && strcmp(av[i], "modgroup") == 0)
	GrpCtlNewGroup(ac, av, i+1);
    else if (av[i] && av[i+1] && strcmp(av[i], "rmgroup") == 0)
	GrpCtlRmGroup(av[i+1]);
    else if (av[i] && strcmp(av[i], "listactive") == 0)
	GrpCtlListActive(av[i+1]);
    else if (av[i] && strcmp(av[i], "listgrouphash") == 0)
	GrpCtlListGroupHash(av[i+1], av[i+1] ? av[i+2] : NULL, 0);
    else if (av[i] && strcmp(av[i], "listgrouphashfile") == 0)
	GrpCtlListGroupHash(av[i+1], av[i+1] ? av[i+2] : NULL, 1);
    else if (av[i] && strcmp(av[i], "listnewsgroups") == 0)
	GrpCtlListNewsgroups(av[i+1]);
    else if (av[i] && strcmp(av[i], "masscancel") == 0)
	GrpCtlMassCancel(av[i+1]);
    else if (av[i] && strcmp(av[i], "groupinfo") == 0)
	GrpCtlGroupInfo(av[i+1]);
    else if (av[i] && strcmp(av[i], "adjust") == 0)
	GrpCtlAdjust(ac, av, i+1);
    else if (av[i] && strcmp(av[i], "checkgroups") == 0)
	GrpCtlCheckGroups(ac, av, i+1);
    else  {
	Usage();
    }

    KPDBClose(KDBActive);
    return(0);
}

/*
 * OutAppend() - add formatted text to *pbuf (D-321).  A line meant for stdout
 * while a record lock is held is collected here and printed after the unlock:
 * a stdout that does not drain (a pipe to a pager left unscrolled) would keep
 * the record locked, and every writer of the group (the feeder's
 * GenerateXRef(), dreaderd's AssignArticleNo()) waits for it.
 */

static void
OutAppend(char **pbuf, size_t *plen, const char *fmt, ...)
{
    va_list ap;
    va_list cp;
    int n;
    char *nb;

    va_start(ap, fmt);
    va_copy(cp, ap);
    n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n < 0 || (nb = realloc(*pbuf, *plen + n + 1)) == NULL) {
	va_end(ap);
	/* not stdout: the lock is held; the exit releases it */
	fprintf(stderr, ">> masscancel: malloc fail\n");
	exit(1);
    }
    vsnprintf(nb + *plen, n + 1, fmt, ap);
    va_end(ap);
    *pbuf = nb;
    *plen += n;
}

/*
 * CANCEL -
 */

void
GrpCtlMassCancel(char *group)
{
    OverInfo *ov = NULL;
    CancelArt *msgids[MASSCANCELHASHSIZ], *elem;
    char workbuf[2048], *ptr;
    hash_t hv;
    unsigned int loc;
    const char *rec;
    int recLen;
    char *out = NULL;		/* the text printed after the unlock (D-321) */
    size_t outLen = 0;

    bzero(msgids, sizeof(msgids));

    /*
     * Build up a hash table of all the entries.  The expectation here is
     * that we can do one-pass through the data files, and work pretty
     * quickly through the corresponding linked list in the hash table to
     * see if any of the linked list entries match when we walk the data
     * file.
     */
    while (fgets(workbuf, sizeof(workbuf), stdin) != NULL) {
	if (((ptr = strrchr(workbuf, '\n')))) {
	    *ptr = '\0';
	}
	if (*workbuf != '<' || *(workbuf + strlen(workbuf) - 1) != '>') {
	    printf(">> masscancel: invalid message-id: %s\n", workbuf);
	} else {
	    hv = msgidhash(workbuf);
	    loc = hv.h2 % MASSCANCELHASHSIZ;
	    if (! ((elem = (CancelArt *)malloc(sizeof(CancelArt))))) {
		printf(">> masscancel: malloc fail\n");
		exit(1);
	    }
	    bzero(elem, sizeof(CancelArt));
	    elem->hv = hv;
	    elem->found = 0;
	    elem->next = msgids[loc];
	    msgids[loc] = elem;
	}
    }

    if ((rec = KPDBReadRecord(KDBActive, group, 0, &recLen)) != NULL) {
	if ((ov = GetOverInfo(group)) != NULL) {
	    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) != NULL) {
		artno_t artBeg;
		artno_t artEnd;

		artBeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
		artEnd = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);

		if (artEnd - artBeg > ov->ov_MaxArts)
		    artBeg = artEnd - ov->ov_MaxArts;

		while (artEnd >= artBeg) {
		    off_t ovpos = 0;
		    const OverArt *oa;

		    oa = GetOverArt(ov, artEnd, &ovpos);

		    if (oa && oa->oa_ArtNo == artEnd) {
			int found = 0;

		        /* Copy the hv into a more friendly form */
		        bcopy(&oa->oa_MsgHash, &hv, sizeof(hv));

			/* Find the appropriate msgid linked list in the hash */
			loc = hv.h2 % MASSCANCELHASHSIZ;

			elem = msgids[loc];

			/* Walkem, Texas Ranger */
			while (elem && ! found) {
			    if (bcmp(&hv, &elem->hv, sizeof(hv)) == 0) {
				OverArt copy = *oa;

				copy.oa_ArtNo = -1;	/* Flag as cancelled */

				if (hflock(ov->ov_OFd, 0, XLOCK_EX) < 0) {
				    OutAppend(&out, &outLen, ">> masscancel: overview lock failed: %s\n",
							strerror(errno));
				    break;
				}
				lseek(ov->ov_OFd, ovpos, 0);
				if (WriteAll(ov->ov_OFd, &copy, sizeof(copy)) < 0)
				    OutAppend(&out, &outLen, ">> masscancel: overview write failed: %s\n",
							strerror(errno));
				hflock(ov->ov_OFd, 0, XLOCK_UN);

				elem->found++;
				found++;
				OutAppend(&out, &outLen, ">> cancelled %s:%lld\n", group, artEnd);
			    }
			    elem = elem->next;
			}
		    }

		    artEnd--;
		}
		KPDBUnlock(KDBActive, rec);
		if (out != NULL) {
		    printf("%s", out);
		    free(out);
		}
	    }
	}
    } else {
	printf(">> masscancel: invalid group: %s\n", group);
    }

    /* XXX todo should print out the number of MsgID's it did NOT find */
}

/*
 * GROUPINFO -
 */

const char *
GrpCtlGroupInfo(char *group)
{
    int recLen;
    const char *rec;
    artno_t nb;
    int iter = 0;

    if (ValidGroupName(group) < 0) {
	printf(">> groupinfo: Illegal newsgroup name\n");
	return(NULL);
    }

    if ((rec = KPDBReadRecord(KDBActive, group, 0, &recLen)) == NULL) {
	printf(">> groupinfo: Unable to access group active data\n");
	return(NULL);
    } else {
	const char *flags;
	artno_t a;
	int n;
	uint32 flen;
	char buf[MAXGNAME];
	time_t t;
	char tbuf[32];	/* ctime_r: at least 26 */

	printf("%s\n", group);
	a = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "0"), NULL, 10);
	printf("  NE=%010lld ", a);
	nb = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "0"), NULL, 10);
	printf("NB=%010lld ", nb);
	a = strtoll(KPDBGetField(rec, recLen, "NX", NULL, "0"), NULL, 10);
	printf("NX=%010lld ", a);
	flags = KPDBGetField(rec, recLen, "S", (int *)&flen, "n");
	if (flen >= sizeof(buf))
	    flen = sizeof(buf) - 1;
	bcopy(flags, buf, flen);
	buf[flen] = 0;
	printf("flag=%s ", buf);
	iter = strtol(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);
	printf("ITER=%d\n", iter);
	n = strtol(KPDBGetField(rec, recLen, "CTS", NULL, "0"), NULL, 16);
	t = (time_t)(uint32)n;
	printf("  CTS=%u=%s", n, ctime_r(&t, tbuf) ? tbuf : "?\n");
	n = strtol(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
	t = (time_t)(uint32)n;
	printf("  LMTS=%u=%s", n, ctime_r(&t, tbuf) ? tbuf : "?\n");
	flags = KPDBGetField(rec, recLen, "GD", (int *)&flen, "?");
	if (flen >= sizeof(buf))
	    flen = sizeof(buf) - 1;
	bcopy(flags, buf, flen);
	buf[flen] = 0;
	printf("  GD=%s\n", buf);
    }
    {
	int fd;
	int maxarts;
	off_t pos;
	char path[PATH_MAX];
	OverHead oh;
	OverArt oa;
	struct stat st;
	time_t t;
	char tbuf[32];	/* ctime_r: at least 26 */
	const char *gname = GFName(group, GRPFTYPE_OVER, 0, 1, iter,
						&DOpts.ReaderGroupHashMethod);

	printf("  grouphash=%s\n", GFHash(group, &DOpts.ReaderGroupHashMethod));
	snprintf(path, sizeof(path), "%s/%s", PatExpand(GroupHomePat), gname);
	printf("  overfile=%s\n", path);

	if ((fd = open(path, O_RDONLY)) < 0) {
	    printf("  Unable to open %s\n", path);
	    return(NULL);
	}
	if (read(fd, &oh, sizeof(oh)) != sizeof(oh)) {
	    printf("  Error reading over header data\n");
	    close(fd);
	    return(NULL);
	}
	if (oh.oh_Version != OH_VERSION ||
					oh.oh_ByteOrder != OH_BYTEORDER) {
	    printf("  Wrong version or byte order\n");
	    close(fd);
	    return(NULL);
	}
	fstat(fd, &st);
	maxarts = (st.st_size - oh.oh_HeadSize) / sizeof(OverArt);
	printf("  overfilesize=%u  maxarts=%u\n", (unsigned)st.st_size,
							maxarts);

	if (maxarts <= 0) {
	    printf("  overfile has no article slots\n");
	    close(fd);
	    return(NULL);
	}

	pos = oh.oh_HeadSize + ((nb & 0x7FFFFFFFFFFFFFFFLL) % maxarts) *
							sizeof(OverArt);
	lseek(fd, pos, 0);
	if (read(fd, &oa, sizeof(oa)) != sizeof(oa)) {
	    printf("  Unable to read OverArt\n");
	    close(fd);
	    return(NULL);
	}
	if (oa.oa_ArtNo > 0) {
	    printf("  StartNo=%d\n", oa.oa_ArtNo);
	} else if (oa.oa_ArtNo == -1) {
	    printf("  Article cancelled\n");
	} else if (oa.oa_ArtNo == -2) {
	    printf("  Article expired\n");
	} else {
	    printf("  Article not found\n");
	}
	if (!OA_ARTNOEQ(nb, oa.oa_ArtNo)) {
	    printf("  artNoMismatch (got=%d  wanted=%d)\n", oa.oa_ArtNo,
			OA_ARTNOSET(nb));
	}
	t = (time_t)oa.oa_TimeRcvd;
	printf("  Time received: %d = %s", oa.oa_TimeRcvd,
		ctime_r(&t, tbuf) ? tbuf : "?\n");
    }
    return(NULL);
}

/*
 * LISTACTIVE -
 */

const char *
GrpCtlListActive(char *match)
{
    int recLen;
    int recOff;

    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	recOff;
	recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
    ) {
	uint32 groupLen;
	char grpbuf[MAXGNAME];
	const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	const char *group = KPDBGetField(rec, recLen, NULL, (int *)&groupLen, NULL);
	if (group) {
	    if (groupLen >= MAXGNAME)
		groupLen = MAXGNAME - 1;
	    bcopy(group, grpbuf, groupLen);
	    grpbuf[groupLen] = 0;
	    if (match == NULL || WildCmp(match, grpbuf) == 0) {
		const char *flags;
		artno_t ne;
		artno_t nb;
		uint32 flen;

		flags = KPDBGetField(rec, recLen, "S", (int *)&flen, "n");
		ne = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "0"), NULL, 10);
		nb = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "0"), NULL, 10);
		printf("%s %010lld %010lld ", grpbuf, ne, nb);
		if (flen >= sizeof(grpbuf))
		    flen = sizeof(grpbuf) - 1;
		bcopy(flags, grpbuf, flen);
		grpbuf[flen] = 0;
		printf("%s\n", grpbuf);
	    }
	}
    }
    return(NULL);
}

/*
 * LISTGROUPHASH -
 */

const char *
GrpCtlListGroupHash(char *match, char *hash, int dir)
{
    int recLen;
    int recOff;
    int wild = 1;

    if (hash != NULL)
	SetGroupHashMethod(hash,  &DOpts.ReaderGroupHashMethod);

    if (match != NULL && index(match, '?') == NULL && index(match, '*') == NULL)
	wild = 0;
    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	wild && recOff;
	recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
    ) {
	uint32 groupLen;
	char grpbuf[MAXGNAME];
	const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	const char *group = KPDBGetField(rec, recLen, NULL, (int *)&groupLen, NULL);
	if (group) {
	    if (groupLen >= MAXGNAME)
		groupLen = MAXGNAME - 1;
	    bcopy(group, grpbuf, groupLen);
	    grpbuf[groupLen] = 0;
	    if (match == NULL || WildCmp(match, grpbuf) == 0) {
		if (dir)
		    printf("%s %s\n",
			GFName(grpbuf, GRPFTYPE_OVER, 0, 1, 0,
				&DOpts.ReaderGroupHashMethod),
			grpbuf
		    );
		else
		    printf("%s %s\n",
			GFHash(grpbuf, &DOpts.ReaderGroupHashMethod), grpbuf);
	    }
	}
    }
    if (!wild) {
	if (dir)
	    printf("%s %s\n",
			GFName(match, GRPFTYPE_OVER, 0, 1, 0,
						&DOpts.ReaderGroupHashMethod),
			match
	    );
	else
	    printf("%s %s\n",
			GFHash(match, &DOpts.ReaderGroupHashMethod),
			match);
    }
    return(NULL);
}

/*
 * LISTNEWSGROUPS -
 */

const char *
GrpCtlListNewsgroups(char *match)
{
    int recLen;
    int recOff;

    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	recOff;
	recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
    ) {
	uint32 groupLen;
	char grpbuf[MAXGNAME];
	const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	const char *group = KPDBGetField(rec, recLen, NULL, (int *)&groupLen, NULL);
	if (group) {
	    if (groupLen >= MAXGNAME)
		groupLen = MAXGNAME - 1;
	    bcopy(group, grpbuf, groupLen);
	    grpbuf[groupLen] = 0;
	    if (match == NULL || WildCmp(match, grpbuf) == 0) {
		const char *desc;

		desc  = KPDBGetFieldDecode(rec, recLen, "GD", NULL, NULL);
		printf("%s\t%s\n", grpbuf, (desc != NULL) ? desc : "?");
	    }
	}
    }
    return(NULL);
}

/*
 * NEWGROUP -
 */

const char *
GrpCtlNewGroup(int ac, char **av, int pos)
{
    char *flags;
    char *moderator = NULL;
    char description[256];
    char *group = NULL;
    int isNew = 0;
    int hasCts = 0;
    const char *rec;
    int recLen;

    description[0] = 0;
    group = av[pos++];
    if ((flags = av[pos++]) == NULL)
	flags = "y";
    else {
	if (av[pos]) {
	    moderator = av[pos++];
	    if (strcmp(moderator, "\"\"") == 0)
		moderator = NULL;
	}
	if (av[pos])
	    snprintf(description, sizeof(description), "%s", av[pos]);
    }

    if (ValidGroupName(group) < 0) {
	printf(">> Newgroup: Illegal newsgroup name\n");
	return(NULL);
    }

    if (flags && strlen(flags) != 1) {
	printf(">> Newgroup: Illegal flag: %s \n", flags);
	return(NULL);
    }
    /*
     * Read and lock the record.  If the record does not exist, create a new
     * record (and lock that).
     */

    rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen);
    if (rec == NULL) {
	/*
	 * Create if absent (D-254): the group may have been made, and
	 * numbered, since the read above.  Then it is the existing group
	 * below (locked): NB and NE stay.
	 */
	if (KPDBCreate(KDBActive, group, "NB", "0000000001", KP_LOCK) == 0) {
	    KPDBWrite(KDBActive, group, "NE", "0000000000", KP_LOCK_CONTINUE);
	    isNew = 1;
	} else {
	    rec = KPDBReadRecord(KDBActive, group, 0, &recLen);
	}
    }
    if (rec != NULL) {
	if (KPDBGetField(rec, recLen, "CTS", NULL, NULL) != NULL)
	    hasCts = 1;
    }

    if (moderator) {
	SanitizeString(moderator);
	KPDBWriteEncode(KDBActive, group, "M", moderator, KP_LOCK_CONTINUE);
    }
    if (description[0]) {
	SanitizeDescString(description);
	KPDBWriteEncode(KDBActive, group, "GD", description, KP_LOCK_CONTINUE);
    }

    {
	/*
	 * add creation-time-stamp and last-modified-time-stamp
	 * for group.
	 */
	char tsBuf[64];

	sprintf(tsBuf, "%08x", (int)time(NULL));
	if (hasCts == 0)
	    KPDBWrite(KDBActive, group, "CTS", tsBuf, KP_LOCK_CONTINUE);
	KPDBWrite(KDBActive, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
    }

    KPDBWrite(KDBActive, group, "S", flags, KP_UNLOCK);

    if (isNew) {
	printf(">> Newgroup: Created new group %s flags=%s\n", group, flags );
    } else
	printf(">> Newgroup: updated group %s flags=%s moderator=%s description=%s\n",
		group,
		flags,
		(moderator ? moderator : "no chg"),
		(description[0] ? description : "no chg")
	    );
    return(NULL);
}

const char *
GrpCtlRmGroup(char *options)
{
    const char *rec;
    int recLen;
    char *group;

    group = options;

    if (ValidGroupName(group) < 0) {
	printf(">> Rmgroup: Illegal newsgroup name\n");
	return(NULL);
    }

    /*
     * Note that the record isn't locked in this case.
     */

    if ((rec = KPDBReadRecord(KDBActive, group, 0, &recLen)) != NULL) {
	KPDBDelete(KDBActive, group);
	printf(">> RmGroup: Deleted group %s\n", group);
    } else {
	printf(">> RmGroup: No group %s\n", group);
    }

    return(NULL);
}

/*
 * ADJUST -
 */

const char *
GrpCtlAdjust(int ac, char **av, int pos)
{
    char *field;
    char *group = NULL;
    artno_t adjust = 0;
    artno_t oldval = 0;
    const char *rec;
    int recLen;
    int setabs = 0;

    if ((group = av[pos++]) == NULL) {
	printf(">> adjust: missing newsgroup\n");
	return(NULL);
    }
    if ((field = av[pos++]) == NULL) {
	printf(">> adjust: missing field to adjust\n");
	return(NULL);
    }
    if ((av[pos]) == NULL) {
	printf(">> adjust: missing adjust value\n");
	return(NULL);
    }
    if (av[pos][0]=='=') {
	setabs=1;
	adjust=strtoll(&av[pos++][1], NULL, 10);
    } else {
	setabs=0;
        adjust = strtoll(av[pos++], NULL, 10);
    }

    if (ValidGroupName(group) < 0) {
	printf(">> adjust: Illegal newsgroup name\n");
	return(NULL);
    }

    if (strcmp(field, "NB") != 0 && strcmp(field, "NE") != 0 &&
			strcmp(field, "NX") != 0) {
	printf(">> adjust: Illegal field: %s \n", field);
	return(NULL);
    }
    /*
     * Read and lock the record.  If the record does not exist, create a new
     * record (and lock that).
     */

    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) != NULL) {
	char buf[32];
	oldval = strtoll(KPDBGetField(rec, recLen, field, NULL, "0"), NULL, 10);
	if (setabs) {
	    sprintf(buf, "%010lld", adjust);
	} else {
	    sprintf(buf, "%010lld", oldval + adjust);
        }
	KPDBWrite(KDBActive, group, field, buf, KP_LOCK);
    } else {
	printf(">> adjust: group '%s' not found\n", group);
	return(NULL);
    }

    printf(">> adjust: %s for %s adjusted from %lld to %lld\n", field, group,
						oldval, setabs?adjust:oldval + adjust);
    return(NULL);
}

/*
 * Read one whole line of any length from f into a malloc'd buffer (with its
 * newline, if any).  Returns NULL at end of file or on a read error.
 */

#define MAXDESCLEN	255

static char *
ReadWholeLine(FILE *f)
{
    char *line = NULL;
    size_t len = 0;
    size_t cap = 0;
    char chunk[256];

    while (fgets(chunk, sizeof(chunk), f) != NULL) {
	size_t n = strlen(chunk);

	if (len + n + 1 > cap) {
	    char *nl;

	    cap = (len + n + 1) * 2;
	    if ((nl = (char *)realloc(line, cap)) == NULL) {
		free(line);
		return(NULL);
	    }
	    line = nl;
	}
	memcpy(line + len, chunk, n + 1);
	len += n;
	if (n > 0 && chunk[n - 1] == '\n')
	    break;
    }
    return(line);
}

/*
 * CHECKGROUPS -
 */

const char *
GrpCtlCheckGroups(int ac, char **av, int pos)
{
    char *filename;
    int exec = 0;
    FILE *f;
    char *buf = NULL;		/* the current line of any length */
    const char *rec;
    int recLen;
    GrpList *grpList = NULL;
    char *hierarchy = NULL;

    if (av[pos] != NULL && strcmp(av[pos], "exec") == 0) {
	pos++;
	exec = 1;
    }

    if ((filename = av[pos++]) == NULL) {
	printf(">> checkgroups: missing filename\n");
	return(NULL);
    }
    if (strcmp(filename, "-") == 0) {
	f = stdin;
    } else {
	f = fopen(filename, "r");
	if (f == NULL) {
	    printf(">> checkgroups: cannot open %s\n", filename);
	    return(NULL);
	}
    }
    while (free(buf), (buf = ReadWholeLine(f)) != NULL) {
	char *newsgroup;
	char *description = NULL;
	char *flags = "y";

	if ((newsgroup = strtok(buf, " \t\r\n")) == NULL)
	    continue;
	if (ValidGroupName(newsgroup) < 0)              /* valid group name */
	    continue;
	if (strlen(newsgroup) >= MAXGNAME)              /* fits the buffers below */
	    continue;
	if (strchr(newsgroup, '.') == NULL)             /* sanity check */
	    continue;
	if ((description = strtok(NULL, "\r\n")) == NULL)
	    continue;
     
	if (hierarchy == NULL) {
	    char *p;
	    hierarchy = (char *)malloc(strlen(newsgroup) + 2);
	    strcpy(hierarchy, newsgroup);
	    p = strchr(hierarchy, '.');
	    if (p != NULL)
		*p = 0;
	    strcat(hierarchy, ".*");
	}

	/*
	 * clean up the description
	 */
	SanitizeDescString(description);
	/*
	 * A line is no longer cut at 127 bytes, but a description stays
	 * within the 255 bytes GrpCtlNewGroup() takes, which is what the
	 * buffers that read it back (groupinfo) are sized for.
	 */
	if (strlen(description) > MAXDESCLEN)
	    description[MAXDESCLEN] = 0;
	{
	    int l = strlen(description);
	    while (*description == ' ' || *description == '\t') {
		++description;
		--l;
	    }
	    while (l > 0 && (description[l-1] == ' ' || description[l-1] == '\t')) {
		description[--l] = 0;
	    }
	}

	if (strstr(description, "(Moderated)") != 0)
	    flags = "m";
	else
	    flags = "y";

	/*
	 * Keep a list of all the newsgroups to check for deleted groups
	 */
	{
	    GrpList *gl = (GrpList *)malloc(sizeof(GrpList));
	    gl->next = grpList;
	    gl->group = strdup(newsgroup);
	    grpList = gl;
	}

	rec = KPDBReadRecord(KDBActive, newsgroup, KP_LOCK, &recLen);
	if (rec == NULL && exec) {
	    char tsBuf[64];

	    /* as GrpCtlNewGroup(): S last, which releases the lock */
	    sprintf(tsBuf, "%08x", (int)time(NULL));
	    /* create if absent (D-254): not over a group made since the read */
	    if (KPDBCreate(KDBActive, newsgroup, "NB", "0000000001",
							KP_LOCK) != 0) {
		/*
		 * Made since the read (D-312): check it as a group that was
		 * there, status and description, as newgroup does.  Its
		 * numbers stay.
		 */
		KPDBWrite(KDBActive, newsgroup, NULL, NULL, KP_UNLOCK);
		printf(">> Checkgroups: Group %s was made meanwhile, checked as an existing group\n", newsgroup);
		rec = KPDBReadRecord(KDBActive, newsgroup, KP_LOCK, &recLen);
		if (rec == NULL)
		    continue;			/* removed again */
	    } else {
		KPDBWrite(KDBActive, newsgroup, "NE", "0000000000",
							KP_LOCK_CONTINUE);
		if (description[0])
		    KPDBWriteEncode(KDBActive, newsgroup, "GD", description,
							KP_LOCK_CONTINUE);
		KPDBWrite(KDBActive, newsgroup, "CTS", tsBuf, KP_LOCK_CONTINUE);
		KPDBWrite(KDBActive, newsgroup, "LMTS", tsBuf, KP_LOCK_CONTINUE);
		KPDBWrite(KDBActive, newsgroup, "S", flags, KP_UNLOCK);
		printf(">> Checkgroups: Added group %s\n", newsgroup);
		continue;
	    }
	}
	if (rec == NULL) {
	    printf("dgrpctl newgroup %s %s \"\" \"%s\"\n",
						newsgroup,
						flags,
						description);
	} else {
	    const char *p;
	    char buf[MAXGNAME];
	    uint32 plen;
	    int needflags = 0;
	    int needdesc = 0;
	    int modline = 0;

	    p = KPDBGetField(rec, recLen, "S", (int *)&plen, "n");
	    if (plen >= sizeof(buf))
		plen = sizeof(buf) - 1;
	    bcopy(p, buf, plen);
	    buf[plen] = 0;
	    if (strcmp(buf, flags) != 0)
		needflags = 1;

	    p = KPDBGetFieldDecode(rec, recLen, "GD", NULL, NULL);
	    /* compare the whole decoded description: no copy, so no truncation */
	    p = KPDBGetFieldDecode(rec, recLen, "GD", NULL, NULL);
	    if (p == NULL || strcmp(p, description) != 0)
		needdesc = 1;

	    if (needflags || needdesc) {
		if (exec) {
		    char tsBuf[64];

		    sprintf(tsBuf, "%08x", (int)time(NULL));
		    KPDBWriteEncode(KDBActive, newsgroup, "GD", description,
							KP_LOCK_CONTINUE);
		    KPDBWrite(KDBActive, newsgroup, "LMTS", tsBuf,
							KP_LOCK_CONTINUE);
		    KPDBWrite(KDBActive, newsgroup, "S", flags, KP_UNLOCK);
		    printf(">> Checkgroups: Modified group %s\n", newsgroup);
		    continue;		/* the S write released the lock */
		} else {
		    modline = 1;	/* printed after the unlock (D-321) */
		}
	    }
	    KPDBUnlock(KDBActive, rec);
	    if (modline)
		printf("dgrpctl modgroup %s %s \"\" \"%s\"\n",
						newsgroup,
						flags,
						description);
	}
    }
    if (hierarchy == NULL) {
	/* no usable group in the list: nothing to check for deleted groups */
	printf(">> checkgroups: no valid group in the list, nothing to check\n");
	return(NULL);
    }
	{
	    int recLen;
	    int recOff;

	    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
		recOff;
		recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	    ) {
		uint32 groupLen;
		char grpbuf[MAXGNAME];
		const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
		const char *group = KPDBGetField(rec, recLen, NULL, (int *)&groupLen, NULL);
		if (group) {
		    if (groupLen >= MAXGNAME)
			groupLen = MAXGNAME - 1;
		    bcopy(group, grpbuf, groupLen);
		    grpbuf[groupLen] = 0;
		    if (WildCmp(hierarchy, grpbuf) == 0) {
			GrpList *g;
			int found = 0;
			for (g = grpList; g != NULL; g = g->next) {
			    if (strcmp(grpbuf, g->group) == 0) {
				found = 1;
				break;
			    }
			}
			if (!found) {
			    if (exec) {
				const char *rec;
				if ((rec = KPDBReadRecord(KDBActive, grpbuf, 0, &recLen)) != NULL) {
				    KPDBDelete(KDBActive, grpbuf);
				    printf(">> Checkgroups: Deleted group %s\n", grpbuf);
				}
			    } else {
				printf("dgrpctl rmgroup %s\n", grpbuf);
			    }
			}
		    }
		}
	    }
	}
    return(NULL);
}



OverInfo *
GetOverInfo(const char *group)
{
    OverInfo *ov = NULL;

    if (ov == NULL) {
	struct stat st;
	char path[PATH_MAX];
	int iter = 0;
	artno_t endNo = 0;

	bzero(&st, sizeof(st));

	ov = zalloc(&SysMemPool, sizeof(OverInfo));
	ov->ov_Group = zallocStr(&SysMemPool, group);

	{
	    const char *rec;
	    int recLen;
	    if ((KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR)) == NULL) {
		fprintf(stderr, "Unable to open active file (%s)\n", strerror(errno));
		exit(1);
	    }
	    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) == NULL) {
		return(NULL);
	    }
	    iter = strtol(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);
	    endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    KPDBUnlock(KDBActive, rec);
	}

again:
	{
	    /*
	     * GFName() may return up to PATH_MAX bytes ('readergrouphash
	     * hierarchy' is about twice the group name); this was the group
	     * home plus 48 bytes.  Same as dcancel and dreaderd now.
	     */
	    const char *gfname = GFName(group, GRPFTYPE_OVER, 0, 1, iter,
						&DOpts.ReaderGroupHashMethod);

	    ov->ov_OFd = -1;
	    if (snprintf(path, sizeof(path), "%s/%s", PatExpand(GroupHomePat),
				gfname) >= (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		logit(LOG_ERR, "Overview path too long for group %s", group);
	    } else if (MakeGroupDirectory(path) == -1)
		logit(LOG_ERR, "Error on overview dir open/create: %s (%s)",
						path, strerror(errno));
	    else
		ov->ov_OFd = xopen(O_RDWR|O_CREAT, 0644, "%s", path);
	}
	if (ov->ov_OFd < 0) {
	    logit(LOG_ERR, "Error on overview open/create for group %s: %s (%s)",
						group, path, strerror(errno));
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
		int error = errno;

		logit(LOG_ERR, "Error on overview lock for group %s: %s (%s)",
						group, path, strerror(error));
		fprintf(stderr, "%s: overview lock failed: %s\n",
						group, strerror(error));
		close(ov->ov_OFd);
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
	    if (r == 0 && oh.oh_ByteOrder != OH_BYTEORDER) {
		logit(LOG_CRIT, "Incorrect overview byte order for %s (%s)",
								group, path);
		r = -1;
	    }
	    if (r == 0 && oh.oh_Version > OH_VERSION) {
		logit(LOG_CRIT, "Incorrect overview version for %s (%s)",
								group, path);
		r = -1;
	    }
	    if (r == 0 &&
		(off_t)st.st_size - (off_t)oh.oh_HeadSize < (off_t)sizeof(OverArt)) {
		/* header present but no room for even one OverArt: treat as a
		 * new/empty file so it is re-initialised, instead of computing
		 * ov_MaxArts 0 and taking a modulo by zero. */
		r = -1;
	    }
	    if (r != 0) {
		if (hflock(ov->ov_OFd, 0, XLOCK_EX) < 0) {
		    int error = errno;

		    logit(LOG_ERR, "Error on overview lock for group %s: %s (%s)",
						group, path, strerror(error));
		    fprintf(stderr, "%s: overview lock failed: %s\n",
						group, strerror(error));
		    hflock(ov->ov_OFd, 4, XLOCK_UN);
		    close(ov->ov_OFd);
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
		if (read(ov->ov_OFd, &oh, sizeof(oh)) != sizeof(oh) ||
		    oh.oh_ByteOrder != OH_BYTEORDER ||
		    (off_t)st.st_size - (off_t)oh.oh_HeadSize < (off_t)sizeof(OverArt)
		) {
		    OverExpire save;

		    GetOverExpire(group, &save);

		    /*
		     * If 'aInitArts' option not given in expireCtl
		     */

		    if (save.oe_InitArts <= 0)
			save.oe_InitArts = DEFARTSINGROUP;

		    st.st_size = sizeof(oh) + sizeof(OverArt) * save.oe_InitArts;
		    if (ftruncate(ov->ov_OFd, 0) < 0 ||
			ftruncate(ov->ov_OFd, st.st_size) < 0)
			fprintf(stderr, "%s: overview truncate failed: %s\n",
						group, strerror(errno));
		    fsync(ov->ov_OFd);
		    lseek(ov->ov_OFd, 0L, 0);
		    bzero(&oh, sizeof(oh));
		    oh.oh_Version = OH_VERSION;
		    oh.oh_ByteOrder = OH_BYTEORDER;
		    oh.oh_HeadSize = sizeof(oh);
		    oh.oh_MaxArts = save.oe_InitArts;
		    strncpy(oh.oh_Gname, group, sizeof(oh.oh_Gname) - 1);
		    oh.oh_DataEntries = save.oe_DataSize;
		    if (WriteAll(ov->ov_OFd, &oh, sizeof(oh)) < 0)
			fprintf(stderr, "%s: overview header write failed: %s\n",
						group, strerror(errno));
		    fsync(ov->ov_OFd);
		}
		hflock(ov->ov_OFd, 0, XLOCK_UN);
	    }
	    if (oh.oh_Version < 3)
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
		ov = NULL;
	    }
	}
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
	printf("OA %08lx %d,%lld\n", (long)oa, oa->oa_ArtNo, artno);
    return(oa);
}
