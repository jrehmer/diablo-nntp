
/*
 * UTIL/DSYNCGROUPS.C
 *
 * dsyncgroups -h remotehost [-a] [-o] [-D] [-N[B,E,R]] [-g] [-i] [-m] [-w wildcard] [-p port] [-f/F dactive.kp]
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

typedef struct Group {
    struct Group *gr_Next;
    int		gr_State;
    artno_t	gr_StartNo;
    artno_t	gr_StartDiff;
    artno_t	gr_EndNo;
    artno_t	gr_EndDiff;	/* article numbers are 64 bit: so is a difference */
    artno_t	gr_SynNo;
    artno_t	gr_SynDiff;
    int		gr_NoCTS;
    int		gr_NoLMTS;
    char	*gr_GroupName;
    char	*gr_Flags;
    char	*gr_Description;
} Group;

#define GRF_DESCRIPTION	0x0001
#define GRF_STARTNO	0x0002
#define GRF_ENDNO	0x0004
#define GRF_FLAGS	0x0008
#define GRF_SYNNO	0x0010
#define GRF_LMTS	0x0020	/* last modified time stamp */
#define GRF_CTS		0x0040	/* creation time stamp */
#define GRF_BADNUM	0x0100	/* local record with a number out of range */
#define GRF_FROMLOCAL	0x0800
#define GRF_NEW		0x1000
#define GRF_FROMREMOTE	0x2000
#define GRF_MODIFIED	0x8000

#define GHSIZE		1024
#define GHMASK		(GHSIZE-1)

FILE *Fi;
FILE *Fo;
Group *GHash[GHSIZE];
KPDB  *KDBActive;

int SyncOverwriteOpt = 0; /* allow remote info to replace existing info	*/
int SyncDeleteOpt = 0;	/* delete local info not present on remote	*/
int ListLinesDropped = 0; /* list lines too long for ReadListLine()	*/
int GFileNoDot = 0;	/* the -G file ended without a "." line (D-328)	*/
int SyncGroupsOpt = 0;	/* synchronize newsgroup names 			*/
int SyncDescrOpt = 0;	/* synchronize newsgroup descriptions		*/
int SyncModStatusOpt = 0; /* synchronize newsgroup moderation status	*/
int SyncArtNumbersBegOpt = 0; /* synchronize article numbers	*/
int SyncArtNumbersEndOpt = 0; /* synchronize article numbers	*/
int SyncArtNumbersSynOpt = 0; /* synchronize article numbers	*/
int SyncArtNumbersRange = 0;  /* relative sync beginning based on range	*/
int SyncArtNumbersAddition = 0;  /* add this when syncing ending article no. */
int SyncArtNumbersMaxPerc = 0;	/* max % value to increase upwards	*/

/*
 * Article numbers above this are refused, from the remote and from the
 * local database alike.  It is far above any real group, keeps every
 * number in 15 digits, and keeps the percentage arithmetic of -P (a
 * difference times 100, -P times a number) inside 64 bits.
 */
#define DSYNC_ARTNO_MAX	100000000000000LL	/* 10^14 */
#define DSYNC_PERC_MAX	10000
#define DSYNC_ADD_MAX	1000000000	/* -n: 10^9 */
int VerboseOpt = 0;
int ForReal = 1;
char *SyncModStatusFlag = NULL;
char *BindHost = NULL;
char *BindPort = NULL;

void Usage(void);
void processLine(char *cmd, char *wild, int *count, int *ok);
char *allocTmpCopy(const char *buf, int bufLen);
Group *EnterGroup(int pass, const char *, artno_t, artno_t, artno_t, int, int, const char *, const char *);
int ConnectToHost(const char *host, int port);
int CommandResponse(const char *cmd, char **pres);
int hash(const char *str);
int SetField(char **pptr, const char *str);
static void CreateNewGroup(Group *group, int nowt);
static int ReadListLine(char *buf, int size);

/*
 * Read one line of a list reply or of the -G file into buf (size bytes) (D-320).
 * Returns 1 for a line (the line end is kept when it fits), 0 at the end of the
 * input, and -1 for a line that does not fit: it is read to its end and
 * dropped whole, so its tail is not taken for a line of its own (its first
 * word as a group name).  The limit is size-1 bytes before the line end: a line
 * of size-1 bytes and its LF or CR LF fits.  A NUL byte does not end a line (as
 * with fgets(), the count goes on after it); the parsers, which read C strings,
 * see only what is before it.
 * The dropped lines are counted: a list with a missing line is incomplete, so
 * what it does not name proves nothing and -D deletes nothing (see Writeback).
 */
static int
ReadListLine(char *buf, int size)
{
    int c = 0;
    int n = 0;

    while (n < size - 1 && (c = getc(Fi)) != EOF) {
	buf[n++] = c;
	if (c == '\n')
	    break;
    }
    buf[n] = 0;
    if (n == 0)
	return(0);
    if (c == '\n' || c == EOF)
	return(1);

    /*
     * The buffer is full without a line end: a line of exactly size-1 bytes
     * (or the last, unterminated one) still fits, a longer one does not.  The
     * line end may be CR LF: with size-1 bytes read, a CR followed by LF or
     * the end of the input is eaten like the LF (fgets() left that "\r\n"
     * as a line the parsers skip), a CR followed by more is part of a longer line.
     */
    c = getc(Fi);
    if (c == '\r')
	c = getc(Fi);
    if (c == EOF || c == '\n')
	return(1);
    while (c != EOF && c != '\n')
	c = getc(Fi);
    {
	/* the first word shows which line it was; it is the remote's text */
	char name[41];
	int k;

	for (k = 0; k < 40 && buf[k] != ' ' && buf[k] != '\t' &&
			buf[k] != '\r' && buf[k] != '\n' && buf[k] != 0; ++k)
	    name[k] = buf[k];
	name[k] = 0;
	SanitizeString(name);
	printf("Line over %d bytes - skipping : %s\n", size - 1, name);
    }
    ++ListLinesDropped;
    return(-1);
}

/*
 * Make the record of a new group: all its fields in one append (D-316), so a
 * dsyncgroups that is killed leaves no record or the whole one, never one
 * with NB and without NE and NX (but see KPDBCreateRecord(): a record longer
 * than a page can be cut by a kill inside the write).  A complete record needs no lock of its own,
 * and the -v text is only collected here and printed once the record is made
 * (D-315): a stdout that does not drain held the record locked, and every
 * writer of the group with it.
 *
 * It is made only if there is none (D-254): a group that was not there at the
 * scan may have been made, and numbered, since.  Then it is left as it is.
 * The caller has printed "Creating <group>"; this prints the rest of the line.
 */
static void
CreateNewGroup(Group *group, int nowt)
{
    const char *desc = NULL;
    const char *flags = NULL;
    size_t dl, fl, cap, vcap;
    char *rec, *vb, *enc;
    size_t rn = 0;
    size_t vn = 0;
    char num[32];

    if ((group->gr_State & GRF_DESCRIPTION) && group->gr_Description)
	desc = group->gr_Description;
    if (group->gr_Flags && (group->gr_State & GRF_FLAGS))
	flags = group->gr_Flags;
    dl = desc ? strlen(desc) : 0;
    fl = flags ? strlen(flags) : 0;
    cap = 3 * (dl + fl) + 512;		/* encoded values and the fixed fields */
    vcap = dl + fl + 512;
    rec = malloc(cap);
    vb = malloc(vcap);
    enc = malloc(3 * (dl > fl ? dl : fl) + 1);
    if (rec == NULL || vb == NULL || enc == NULL) {
	printf("\nout of memory\n");
	exit(1);
    }
    rec[0] = vb[0] = 0;

    /* the order of the fields as they were written one by one: NB first */
    snprintf(num, sizeof(num), "%010lld", (long long)group->gr_StartNo);
    rn += snprintf(rec + rn, cap - rn, "NB=%s", num);
    if (desc) {
	KPDBEncode(desc, enc, 3 * dl + 1);
	rn += snprintf(rec + rn, cap - rn, " GD=%s", enc);
	vn += snprintf(vb + vn, vcap - vn, " desc='%s'", desc);
    }
    vn += snprintf(vb + vn, vcap - vn, " NB=%s(%+lld)", num, (long long)group->gr_StartDiff);
    snprintf(num, sizeof(num), "%010lld", (long long)group->gr_EndNo);
    rn += snprintf(rec + rn, cap - rn, " NE=%s", num);
    vn += snprintf(vb + vn, vcap - vn, " NE=%s(%+lld)", num, (long long)group->gr_EndDiff);
    snprintf(num, sizeof(num), "%010lld", (long long)group->gr_SynNo);
    rn += snprintf(rec + rn, cap - rn, " NX=%s", num);
    vn += snprintf(vb + vn, vcap - vn, " NX=%s(%+lld)", num, (long long)group->gr_SynDiff);
    if (group->gr_State & GRF_CTS) {
	rn += snprintf(rec + rn, cap - rn, " CTS=%08x", nowt);
	vn += snprintf(vb + vn, vcap - vn, " CTS=%08x", nowt);
    }
    if (group->gr_State & GRF_LMTS) {
	rn += snprintf(rec + rn, cap - rn, " LMTS=%08x", nowt);
	vn += snprintf(vb + vn, vcap - vn, " LMTS=%08x", nowt);
    }
    if (flags) {
	KPDBEncode(flags, enc, 3 * fl + 1);
	rn += snprintf(rec + rn, cap - rn, " S=%s", enc);
	vn += snprintf(vb + vn, vcap - vn, " S=%s", flags);
    }

    if (ForReal && KPDBCreateRecord(KDBActive, group->gr_GroupName, rec, 0) != 0) {
	printf(": made meanwhile, left as it is\n");
    } else {
	if (VerboseOpt)
	    printf("%s", vb);
	printf("\n");
    }
    free(rec);
    free(vb);
    free(enc);
}

void
Usage(void)
{
    fprintf(stderr, "dsyncgroups -h remotehost | -G filename [-p port] [-B hostname] [-w wild] [-f/F file.kp] [-a] [-o] [-g] [-i] [-m] [-M c] [-D] [-N[B,E,X,R]] [-X[EX]] [-n#] [-v] [-z]\n");
    fprintf(stderr, "  -h remotehost = the host that we connect to\n");
    fprintf(stderr, "  -G filename   = specify an active file to use as input\n");
    fprintf(stderr, "  -p port       = port on remote host (default: 119)\n");
    fprintf(stderr, "  -B bindhost   = interface to bind to\n");
    fprintf(stderr, "  -w wild       = only update groups matching wildmat (default: *)\n");
    fprintf(stderr, "  -f file.kp    = specify the active to update (Default: dactive.kp)\n");
    fprintf(stderr, "  -F file.kp    = create active file if it doesn't exist\n");
    fprintf(stderr, "  -a            = shortcut for -o, -g, -i and -m\n");
    fprintf(stderr, "  -o            = overwrite description\n");
    fprintf(stderr, "  -g            = add missing groups\n");
    fprintf(stderr, "  -i            = sync newsgroup description\n");
    fprintf(stderr, "  -m            = update moderation status\n");
    fprintf(stderr, "  -M c          = update moderation status to the specified flag\n");
    fprintf(stderr, "  -D            = delete local groups in not on remote\n");
    fprintf(stderr, "                  (not if a list line was skipped; with -G only if the file ends with a . line)\n");
    fprintf(stderr, "  -N            = sync begin and end numbers\n");
    fprintf(stderr, "  -NB           = sync begin numbers\n");
    fprintf(stderr, "  -NE           = sync end numbers\n");
    fprintf(stderr, "  -NX           = sync end numbers but only upwards\n");
    fprintf(stderr, "  -u            = Log into remotehost using this password\n");
    fprintf(stderr, "  -U            = Log into remotehost using this username\n");
    fprintf(stderr, "  -XE           = sync NX field - feeder sync\n");
    fprintf(stderr, "  -XX           = sync NX but only upwards\n");
    fprintf(stderr, "  -n#           = if we sync end number then increase local value by remote+#\n");
    fprintf(stderr, "  -N#           = don't sync end number  more than #\n");
    fprintf(stderr, "  -v            = be more verbose about what is being done\n");
    fprintf(stderr, "  -z            = don't actually make changes\n");
    exit(1);
}
int
main(int ac, char **av)
{
    int i;
    int ocreate = 0;
    char *host = NULL;
    char *wild = NULL;
    char *dbfile = NULL;
    char *grpfile = NULL;
    char *username = NULL;
    char *password = NULL;
    int loggedIn = 0;
    int port = 119;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *p;
	char *ptr = av[i];

	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected argument: %s\n", ptr);
	    exit(1);
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'a':
	    SyncOverwriteOpt = 1;
	    SyncGroupsOpt = 1;
	    SyncDescrOpt = 1;
	    SyncModStatusOpt = 1;
	    break;
	case 'B':
	    if (*ptr == 0)
		ptr = av[++i];
	    if ((p = strrchr(ptr, ':')) != NULL) {
		*p++ = 0;
		BindPort = p;
		BindHost = strdup(SanitiseAddr(ptr));
	    } else {  
		BindHost = strdup(SanitiseAddr(ptr));
	    }
	    break;
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'D':
	    SyncDeleteOpt = 1;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'F':
	    ocreate = O_CREAT;
	    /* fall through */
	case 'f':
	    dbfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'G':
	    grpfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'g':
	    SyncGroupsOpt = 1;
	    break;
	case 'i':
	    SyncDescrOpt = 1;
	    break;
	case 'h':
	    host = (*ptr) ? ptr : av[++i];
	    break;
	case 'm':
	    SyncModStatusOpt = 1;
	    break;
	case 'M':
	    SyncModStatusOpt = 1;
	    SyncModStatusFlag = (*ptr) ? ptr : av[++i];
	    break;
	case 'N':
	    if (*ptr == 0) {
		SyncArtNumbersBegOpt = 1;
		SyncArtNumbersEndOpt = 1;
	    }
	    while (*ptr) {
		if (*ptr == 'B')
		    SyncArtNumbersBegOpt = 1;
		if (*ptr == 'E')
		    SyncArtNumbersEndOpt = 1;
		if (*ptr == 'X')
		    SyncArtNumbersEndOpt = 2;
		if (*ptr == 'R')
		    SyncArtNumbersRange = 1;
		++ptr;
	    }
	    break;
	case 'n':
	    {
		/* as -P: an int would take 4294967296 for 0 */
		char *e;
		const char *arg = (*ptr) ? ptr : av[++i];
		long v;

		errno = 0;
		v = (arg != NULL) ? strtol(arg, &e, 0) : 0;
		if (arg == NULL || e == arg || *e != 0 || errno != 0 ||
				v < -DSYNC_ADD_MAX || v > DSYNC_ADD_MAX) {
		    fprintf(stderr, "-n must be -%d..%d\n", DSYNC_ADD_MAX, DSYNC_ADD_MAX);
		    exit(1);
		}
		SyncArtNumbersAddition = (int)v;
	    }
	    break;
	case 'o':
	    SyncOverwriteOpt = 1;
	    break;
	case 'P':
	    {
		/* checked as a long: an int would take 4294967296 for 0 */
		char *e;
		const char *arg = (*ptr) ? ptr : av[++i];
		long v;

		errno = 0;
		v = (arg != NULL) ? strtol(arg, &e, 0) : -1;
		if (arg == NULL || e == arg || *e != 0 || errno != 0 ||
					v < 0 || v > DSYNC_PERC_MAX) {
		    fprintf(stderr, "-P must be 0..%d\n", DSYNC_PERC_MAX);
		    exit(1);
		}
		SyncArtNumbersMaxPerc = (int)v;
	    }
	    break;
	case 'p':
	    ptr = (*ptr) ? ptr : av[++i];
	    port = strtol(ptr, NULL, 0);
	    break;
	case 'U':
	    username = (*ptr) ? ptr : av[++i];
	    break;
	case 'u':
	    password = (*ptr) ? ptr : av[++i];
	    break;
	case 'w':
	    wild = (*ptr) ? ptr : av[++i];
	    break;
	case 'X':
	    if (*ptr == 0) {
		SyncArtNumbersSynOpt = 1;
	    }
	    while (*ptr) {
		if (*ptr == 'E')
		    SyncArtNumbersSynOpt = 1;
		if (*ptr == 'X')
		    SyncArtNumbersSynOpt = 2;
		++ptr;
	    }
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'v':
	    VerboseOpt = 1;
	    break;
	case 'z':
	    ForReal = 0;
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    break;
	}
    }
    if (host == NULL && grpfile == NULL) {
	if (ac == 1)
	    Usage();
	fprintf(stderr, "Missing host/filename option\n");
	exit(1);
    }
    if (port <= 0) {
	fprintf(stderr, "Illegal port option\n");
	exit(1);
    }
    if (wild && strlen(wild) > 128) {
	fprintf(stderr, "Wildcard too large\n");
	exit(1);
    }

    /*
     * open dactive.kp file
     */

    if (VerboseOpt)
	printf("%s local active file\n",
				ocreate ? "Loading/creating" : "Loading");

    if (dbfile) {
	KDBActive = KPDBOpen(dbfile, O_RDWR|ocreate);
    } else {
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR|ocreate);
    }
    if (KDBActive == NULL) {
	printf("Unable to open/create dactive.kp\n");
	exit(1);
    }

    /*
     * scan dactive.kp
     */

    {
	int recLen;
	int recOff;
	int saveSyncGroupsOpt = SyncGroupsOpt;

	SyncGroupsOpt = 1;	/* force new Group structures to be created */

	for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	     recOff;
	     recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	) {
	    int groupLen;
	    int flagsLen;
	    const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	    const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	    const char *flags = KPDBGetField(rec, recLen, "S", &flagsLen, "y");
	    const char *desc  = KPDBGetFieldDecode(rec, recLen, "GD", NULL, NULL);
	    artno_t begNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	    artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    artno_t synNo = strtoll(KPDBGetField(rec, recLen, "NX", NULL, "-1"), NULL, 10);
	    int noCTS = !(int)strtoul(KPDBGetField(rec, recLen, "CTS", NULL, "0"), NULL, 16);
	    int noLMTS = !(int)strtoul(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
	    Group *grp;

	    if (group)
		group = allocTmpCopy(group, groupLen);
	    if (flags)
		flags = allocTmpCopy(flags, flagsLen);
	    if (desc)
		desc = allocTmpCopy(desc, strlen(desc));

	    /*
	     * ignore bad group or group that does not match the wildcard
	     */

	    if (group == NULL)
		continue;
	    if (wild && WildCmp(wild, group) != 0)
		continue;

	    grp = EnterGroup(
		1,
		group,
		begNo,
		endNo,
		synNo,
		noCTS,
		noLMTS,
		flags,
		desc
	    );
	    if (grp == NULL)
		continue;
	    grp->gr_State &= ~(GRF_NEW|GRF_MODIFIED);
	    grp->gr_State &= ~(GRF_CTS|GRF_LMTS);
	    grp->gr_State |= GRF_FROMLOCAL;
	}

	SyncGroupsOpt = saveSyncGroupsOpt;
    }

    /*
     * Connect to remote host
     */

    if (host != NULL)
    {
	int fd;

	if (VerboseOpt)
	    printf("Connecting to %s:%d\n", host, port);

	fd = ConnectToHost(host, port);

	if (fd < 0) {
	    fprintf(
		stderr, 
		"Unable to connect to %s:%d (%s)\n", 
		host, 
		port, 
		strerror(errno)
	    );
	    exit(1);
	}
	Fi = fdopen(fd, "r");
	Fo = fdopen(dup(fd), "w");
    } else {
	if (VerboseOpt)
	    printf("Syncing with %s\n", grpfile);
	Fi = fopen(grpfile, "r");
	if (Fi == NULL) {
	    printf("Unable to open %s\n", grpfile);
	    exit(1);
	}
	Fo = NULL;
    }

    /*
     * startup
     */

    if (host != NULL)
    {
	char *line;

	switch(CommandResponse(NULL, &line)) {
	case 200: case 201:
	    printf("Successful connection: %s\n", line);
	    break;
	default:
	    printf("Initial Response: %s\n", line);
	    exit(1);
	}
    }

    /*
     * login
     */

    if (host != NULL && username != NULL) {
	char *line;
	char cmd[4096];

	if (snprintf(cmd, sizeof(cmd), "authinfo user %s", username) >= (int)sizeof(cmd)) {
	    printf("username is too long\n");
	    exit(1);
	}
	switch(CommandResponse(cmd, &line)) {
	    case 381:
		if (password == NULL) {
		    printf("authinfo wants a password (-u): %s\n", line);
		    exit(1);
		}
		printf("authinfo accepted: %s\n", line);
		break;
	    case 281:
		/* logged in without a password (RFC 4643 2.3): no PASS */
		printf("authinfo accepted: %s\n", line);
		loggedIn = 1;
		break;
	    default:
		printf("authinfo denied: %s\n", line);
		exit(1);
	}
    }

    if (host != NULL && password != NULL && !loggedIn) {
	char *line;
	char cmd[4096];

	if (snprintf(cmd, sizeof(cmd), "authinfo pass %s", password) >= (int)sizeof(cmd)) {
	    printf("password is too long\n");
	    exit(1);
	}
	switch(CommandResponse(cmd, &line)) {
	    case 281:
		printf("authinfo accepted: %s\n", line);
		break;
	    default:
		printf("authinfo denied: %s\n", line);
		exit(1);
	}
    }

    /*
     * mode reader
     */

    if (host)
    {
	char *line;

	switch(CommandResponse("mode reader", &line)) {
	case 200: case 201:
	    printf("Entering reader mode: %s\n", line);
	    break;
	case 400: case 401: case 403: case 480: case 483:
	case 500: case 501: case 502: case 503:
	    /*
	     * a one-line refusal (the replies RFC 3977 3.2.1 allows to any
	     * command): the server may still list its groups
	     */
	    printf("Unable to switch to reader mode: %s\n", line);
	    printf("Going on anyway\n");
	    break;
	default:
	    /*
	     * No reply MODE READER has (or none at all): lines left over
	     * from it would be read as the reply to LIST.
	     */
	    printf("Unexpected reply to mode reader: %s\n", line);
	    exit(1);
	}
    }

    /*
     * list active [wild]
     */

    if (host)
    {
	char *line;
	char cmd[4096];
	int count = 0;
	int ok = 0;
	int r;

	snprintf(cmd, sizeof(cmd), "list active%s%s", ((wild) ? " " : ""), ((wild) ? wild : ""));
	switch(CommandResponse(cmd, &line)) {
	case 215:
	    while ((r = ReadListLine(cmd, sizeof(cmd))) != 0) {
		if (r < 0)
		    continue;
		processLine(cmd, wild, &count, &ok);
		if (ok)
		    break;
	    }
	    break;
	default:
	    printf("list active command failed: %s\n", line);
	    break;
	}
	printf("%d groups returned by list active\n", count);
	if (ok == 0) {
	    printf("Did not receive . terminator to list active command\n");
	    exit(1);
	}
    } else {
	char cmd[4096];
	int count = 0;
	int r;
	int dot = 0;

	/*
	 * A -G file ends at its "." line (LF or CR LF; a "." at the end of the
	 * input too), as a list from a remote does: the lines after it are not
	 * read.  A file without one may be cut short (a full disk, a bad copy),
	 * so -D must not take what it does not name for gone (D-328, see
	 * Writeback).  Without -D a missing "." is nothing to note.
	 */
	while ((r = ReadListLine(cmd, sizeof(cmd))) != 0) {
	    if (r < 0)
		continue;
	    if (strcmp(cmd, ".\n") == 0 || strcmp(cmd, ".\r\n") == 0 ||
		strcmp(cmd, ".") == 0) {
		dot = 1;
		break;
	    }
	    processLine(cmd, wild, &count, NULL);
	}
	if (!dot)
	    GFileNoDot = 1;
	printf("%d groups found\n", count);
    }

    /*
     * list newsgroups [wild]
     */

   if (host && SyncDescrOpt)
    {
	char *line;
	char cmd[4096];
	int count = 0;
	int ok = 0;
	int r;

	sprintf(cmd, "list newsgroups%s%s", ((wild) ? " " : ""), ((wild) ? wild : ""));
	switch(CommandResponse(cmd, &line)) {
	case 215:
	    while ((r = ReadListLine(cmd, sizeof(cmd))) != 0) {
		char *group;
		char *desc;
		Group *grp;

		if (r < 0)
		    continue;

		if (strcmp(cmd, ".\r\n") == 0) {
		    ok = 1;
		    break;
		}
		group = strtok(cmd, " \t\r\n");
		desc  = strtok(NULL, "\r\n");
		if (group == NULL)
		    continue;

		/*
		 * the name comes from the remote: it becomes a key in
		 * dactive.kp and part of the overview file names
		 */
		if (ValidGroupName(group) < 0)
		    continue;

		/*
		 * ignore group that does not match the wildcard, even though
		 * the remote end is supposed to do this for us.
		 */

		if (wild && WildCmp(wild, group) != 0)
		    continue;

		/*
		 * pass 3: only the description of a group that is there, from
		 * the scan of dactive.kp or from LIST ACTIVE; a group named
		 * here alone is not made (D-320)
		 */
		grp = EnterGroup(
		    3,
		    group, 
		    -1,
		    -1,
		    -1,
		    -1,
		    -1,
		    NULL,
		    desc
		);
		if (grp)
		    grp->gr_State |= GRF_FROMREMOTE;
		++count;
	    }
	    break;
	default:
	    printf("list newsgroups command failed: %s\n", line);
	    break;
	}
	printf("%d groups returned by list newsgroups\n", count);
	if (ok == 0) {
	    printf("Did not receive . terminator to list newsgroups command\n");
	    exit(1);
	}
    }

    /*
     * close remote connections
     */

    if (host)
    {
	char *line;

	printf("All remote commands completed, sending quit\n");
	switch(CommandResponse("quit", &line)) {
	case 205:
	    printf("quit response ok\n");
	    break;
	default:
	    printf("error sending quit command: %s\n", line);
	    exit(1);
	}
    }

    fclose(Fi);
    if (Fo)
	fclose(Fo);

    printf("Writing to .kp file\n");
    fflush(stdout);

    /*
     * Writeback
     */

    {
	int i;
	int nowt = (int)time(NULL);

	/*
	 * A line that was dropped may have named a local group (LIST ACTIVE,
	 * -G file, or LIST NEWSGROUPS, which also marks a group as listed):
	 * the lists are incomplete, so a group missing from them is no proof
	 * that the remote lacks it.  The same as a list without its "."
	 * terminator, which ends the run above.
	 */
	if (SyncDeleteOpt && ListLinesDropped) {
	    printf("%d list lines over 4095 bytes were skipped: not deleting groups (-D)\n",
		ListLinesDropped);
	    SyncDeleteOpt = 0;
	}

	/*
	 * The same for a -G file that ends without its "." line: it may be
	 * cut short, and a group it does not name is no proof that the
	 * remote lacks it (D-328).  Creates and updates go on.
	 */
	if (SyncDeleteOpt && GFileNoDot) {
	    printf("-G file %s has no \".\" line at its end: not deleting groups (-D)\n",
		grpfile);
	    SyncDeleteOpt = 0;
	}

	for (i = 0; i < GHSIZE; ++i) {
	    Group *group;

	    for (group = GHash[i]; group; group = group->gr_Next) {
		/* its record is left as it is, and stays (see EnterGroup()) */
		if (group->gr_State & GRF_BADNUM)
		    continue;

		/*
		 * If we have a new group not previously in the database,
		 * we only add it if SyncGroupsOpt is set.
		 */
		if (group->gr_State & GRF_NEW) {
		    if (SyncGroupsOpt == 0)
			continue;
		}

		/*
		 * If group did not come from remote and SyncDeleteOpt is set,
		 * delete the group.
		 */
		if (!(group->gr_State & GRF_FROMREMOTE) &&
		    (group->gr_State & GRF_FROMLOCAL) &&
		    SyncDeleteOpt
		) {
		    printf("Deleting %s\n", group->gr_GroupName);
		    if (ForReal)
			KPDBDelete(KDBActive, group->gr_GroupName);
		} else if (group->gr_State & (GRF_MODIFIED | GRF_NEW)) {
		    if (group->gr_State & GRF_NEW) {
			printf("Creating %s", group->gr_GroupName);
			CreateNewGroup(group, nowt);
			continue;
		    }
		    printf("Updating %s", group->gr_GroupName);
		    if (group->gr_State & GRF_DESCRIPTION) {
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "GD", group->gr_Description, 0);
			if (VerboseOpt)
			    printf(" desc='%s'", group->gr_Description);
		    }
		    if (group->gr_State & GRF_STARTNO) {
			char startBuf[32];
			snprintf(startBuf, sizeof(startBuf), "%010lld", (long long)group->gr_StartNo);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "NB", startBuf, 0);
			if (VerboseOpt)
			    printf(" NB=%s(%+lld)", startBuf, group->gr_StartDiff);
		    }
		    if (group->gr_State & GRF_ENDNO) {
			char endBuf[32];
			snprintf(endBuf, sizeof(endBuf), "%010lld", (long long)group->gr_EndNo);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "NE", endBuf, 0);
			if (VerboseOpt)
			    printf(" NE=%s(%+lld)", endBuf, group->gr_EndDiff);
		    }
		    if (group->gr_State & GRF_SYNNO) {
			char synBuf[32];
			snprintf(synBuf, sizeof(synBuf), "%010lld", (long long)group->gr_SynNo);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "NX", synBuf, 0);
			if (VerboseOpt)
			    printf(" NX=%s(%+lld)", synBuf, group->gr_SynDiff);
		    }
		    if (group->gr_State & GRF_CTS) {
			char ctsBuf[16];
			sprintf(ctsBuf, "%08x", nowt);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "CTS", ctsBuf, 0);
			if (VerboseOpt)
			    printf(" CTS=%s", ctsBuf);
		    }
		    if (group->gr_State & GRF_LMTS) {
			char lmtsBuf[16];
			sprintf(lmtsBuf, "%08x", nowt);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "LMTS", lmtsBuf, 0);
			if (VerboseOpt)
			    printf(" LMTS=%s", lmtsBuf);
		    }
		    if (group->gr_Flags && group->gr_State & GRF_FLAGS) {
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "S", group->gr_Flags, 0);
			if (VerboseOpt)
			    printf(" S=%s",
				(group->gr_Flags ? group->gr_Flags : "y"));
		    }
		    printf("\n");
		}
	    }
	}
    }

    return(0);
}

void
processLine(char *cmd, char *wild, int *count, int *ok)
{
    char *group;
	char *gbeg;
	char *gend;
	char *flags;
	Group *grp;

	if (strcmp(cmd, ".\r\n") == 0) {
	    if (ok)
		*ok = 1;
	    return;
	}
	group = strtok(cmd, " \r\n");
	gend  = strtok(NULL, " \r\n");
	gbeg  = strtok(NULL, " \r\n");
	flags = strtok(NULL, " \r\n");

	/*
	 * a blank or short line has no group name or numbers: skip it
	 */

	if (group == NULL || gend == NULL || gbeg == NULL)
	    return;

	/*
	 * the name comes from the remote (or the -G file): it becomes a key
	 * in dactive.kp and part of the overview file names
	 */
	if (ValidGroupName(group) < 0) {
	    SanitizeString(group);
	    printf("Invalid group name - skipping : %s\n", group);
	    return;
	}

	/*
	 * ignore group that does not match the wildcard, even though
	 * the remote end is supposed to do this for us.
	 */

	if (wild && WildCmp(wild, group) != 0)
	    return;

	if (*gend == '-' || *gbeg == '-') {
	    printf("Invalid group begin or end number - skipping : %s\n", group);
	    return;
	}

	grp = EnterGroup(
	    2,
	    group, 
	    strtoll(gbeg, NULL, 10),
	    strtoll(gend, NULL, 10),
	    strtoll(gend, NULL, 10), /* end repeated */
	    -1,
	    -1,
	    flags, 
	    NULL
	);

	/*
	 * mark group as coming from remote
	 */
	if (grp)
	    grp->gr_State |= GRF_FROMREMOTE;
	++*count;
}

char *
allocTmpCopy(const char *buf, int bufLen)
{
    static char *SaveAry[8];
    static int SaveCnt;
    char **pptr;

    SaveCnt = (SaveCnt + 1) % arysize(SaveAry);
    pptr = &SaveAry[SaveCnt];
    if (*pptr)
	free(*pptr);
    *pptr = malloc(bufLen + 1);
    memcpy(*pptr, buf, bufLen);
    (*pptr)[bufLen] = 0;
    return(*pptr);
}

Group *
EnterGroup(int pass, const char *groupName, artno_t begNo, artno_t endNo, artno_t synNo, int noCTS, int noLMTS, const char *flags, const char *desc)
{
    Group **pgroup = &GHash[hash(groupName)];
    Group *group;
    int bad;

    /*
     * A number of 16 digits or more overran the 16-byte buffers the NE
     * and NX fields are formatted into; see DSYNC_ARTNO_MAX.
     */
    bad = (begNo > DSYNC_ARTNO_MAX || endNo > DSYNC_ARTNO_MAX ||
						synNo > DSYNC_ARTNO_MAX);

    while ((group = *pgroup) != NULL) {
	if (strcmp(groupName, group->gr_GroupName) == 0)
	    break;
	pgroup = &group->gr_Next;
    }
    /* a local record out of range: nothing of it is changed (below) */
    if (group != NULL && (group->gr_State & GRF_BADNUM))
	return(group);
    if (bad) {
	/*
	 * A known group keeps its numbers but is still there (the caller
	 * marks it as listed by the remote, so -D does not delete it); an
	 * unknown one is not created.  A local record (pass 1) is still
	 * entered, marked: else a remote line for it would take it as a
	 * new group (with -g) and write over the record.  It is left as it
	 * is, -D included.
	 */
	printf("Article number out of range - skipping : %s\n", groupName);
	if (pass == 1 && group == NULL) {
	    *pgroup = group = calloc(sizeof(Group) + strlen(groupName) + 1, 1);
	    group->gr_State = GRF_BADNUM;
	    group->gr_GroupName = (char *)(group + 1);
	    strcpy(group->gr_GroupName, groupName);
	}
	return(group);
    }
    if (group == NULL) {
	/*
	 * ignore groups not already in the dactive.kp file if SyncGroupsOpt is not
	 * set.  Pass 3 (LIST NEWSGROUPS) never makes one (D-320).
	 */
	if (SyncGroupsOpt == 0 || pass == 3)
	    return(NULL);
	*pgroup = group = calloc(sizeof(Group) + strlen(groupName) + 1, 1);
	group->gr_State = GRF_NEW | GRF_LMTS | GRF_CTS;
	group->gr_GroupName = (char *)(group + 1);
	group->gr_NoCTS = 1;
	group->gr_NoLMTS = 1;
	group->gr_StartNo = 1;
	strcpy(group->gr_GroupName, groupName);
    }

    /*
     * update fields
     */

    if (synNo >= 0) {
	if (pass == 1) {
	    group->gr_SynNo = synNo;
	} else if (SyncArtNumbersSynOpt) {
	    group->gr_State |= GRF_SYNNO;
	    if (group->gr_SynNo != synNo) {
		if (SyncArtNumbersSynOpt != 2 || synNo > group->gr_SynNo) {
		    artno_t perc;
		    /*
		     * 0 counts as 1 for the percentage (as before), but the
		     * number is only changed once the result is known to
		     * be in range: a refused update leaves it as it was.
		     */
		    artno_t base = group->gr_SynNo ? group->gr_SynNo : 1;
		    artno_t nv;

		    group->gr_SynDiff = (synNo + SyncArtNumbersAddition) - base;
		    perc = group->gr_SynDiff * 100 / base;
		    if (SyncArtNumbersMaxPerc &&
				(group->gr_SynDiff > 0) &&
				(perc > SyncArtNumbersMaxPerc))
			group->gr_SynDiff = SyncArtNumbersMaxPerc * base / 100;
		    if (group->gr_SynDiff == 0)
			group->gr_SynDiff = 1;
		    nv = base + group->gr_SynDiff;
		    if (nv < 0 || nv > DSYNC_ARTNO_MAX) {
			/* -n took it out of range: refused as such a remote number */
			printf("Article number out of range - skipping NX : %s\n", groupName);
			group->gr_SynDiff = 0;
		    } else {
			group->gr_SynNo = nv;
			group->gr_State |= GRF_MODIFIED;
		    }
		}
	    }
	}
    }

    if (endNo >= 0) {
	if (pass == 1) {
	    group->gr_EndNo = endNo;
	} else if (SyncArtNumbersEndOpt) {
	    group->gr_State |= GRF_ENDNO;
	    if (group->gr_EndNo != endNo) {
		if (SyncArtNumbersEndOpt != 2 || endNo > group->gr_EndNo) {
		    artno_t perc;
		    /*
		     * 0 counts as 1 for the percentage (as before), but the
		     * number is only changed once the result is known to
		     * be in range: a refused update leaves it as it was.
		     */
		    artno_t base = group->gr_EndNo ? group->gr_EndNo : 1;
		    artno_t nv;

		    group->gr_EndDiff = (synNo + SyncArtNumbersAddition) - base;
		    perc = group->gr_EndDiff * 100 / base;
		    if (SyncArtNumbersMaxPerc &&
				(group->gr_EndDiff > 0) &&
				(perc > SyncArtNumbersMaxPerc))
			group->gr_EndDiff = SyncArtNumbersMaxPerc * base / 100;
		    if (group->gr_EndDiff == 0)
			group->gr_EndDiff = 1;
		    nv = base + group->gr_EndDiff;
		    if (nv < 0 || nv > DSYNC_ARTNO_MAX) {
			/* -n took it out of range: refused as such a remote number */
			printf("Article number out of range - skipping NE : %s\n", groupName);
			group->gr_EndDiff = 0;
		    } else {
			group->gr_EndNo = nv;
			group->gr_NoLMTS = 1;
			group->gr_State |= GRF_MODIFIED;
		    }
		}
	    }
	}
    }

    if (begNo >= 0) {
	if (pass == 1) {
	    group->gr_StartNo = begNo;
	} else if (SyncArtNumbersBegOpt ||
	    (SyncArtNumbersRange && (group->gr_State & GRF_ENDNO) && endNo > 0)
	) {
	    if (SyncArtNumbersRange) {
		/*
		 * Set beginning based on ending minus the difference
		 * between the remote ending and beginning.
		 */
		begNo = group->gr_EndNo - (endNo - begNo);
		if (begNo < group->gr_StartNo)
		    begNo = group->gr_StartNo;
		if (begNo > group->gr_EndNo)
		    begNo = group->gr_EndNo;
	    }
	    if (group->gr_StartNo != begNo) {
		group->gr_State |= GRF_STARTNO;
		group->gr_State |= GRF_MODIFIED;
	    }
	    group->gr_StartDiff = begNo - group->gr_StartNo;
	    group->gr_StartNo = begNo;
	}
    }

    if (flags) {
	if (pass == 1) {
	    SetField(&group->gr_Flags, flags);
	} else if ((SyncOverwriteOpt && SyncModStatusOpt) ||
					(group->gr_State & GRF_NEW)) {
	    group->gr_State |= GRF_FLAGS;
	    if (SetField(&group->gr_Flags,
				SyncModStatusFlag ? SyncModStatusFlag : flags))
		group->gr_State |= GRF_MODIFIED;
	}
    }
    if (desc) {
	if (pass == 1) {
	    SetField(&group->gr_Description, desc);
	} else if ((SyncOverwriteOpt && SyncDescrOpt) ||
			(SyncDescrOpt && (group->gr_State & GRF_NEW))) {
	    group->gr_State |= GRF_DESCRIPTION;
	    if (SetField(&group->gr_Description, desc))
		group->gr_State |= GRF_MODIFIED;
	}
    }

    if (noCTS >= 0 && group->gr_NoCTS != noCTS) {
	group->gr_NoCTS = noCTS;
	if (pass > 1)
	    group->gr_State |= GRF_MODIFIED | GRF_CTS;
    }

    if (noLMTS >= 0 && group->gr_NoLMTS != noLMTS) {
	group->gr_NoLMTS = noLMTS;
	if (pass > 1)
	    group->gr_State |= GRF_MODIFIED | GRF_LMTS;
    }

    return(group);
}

int
ConnectToHost(const char *host, int port)
{
    int fd, e;
    struct addrinfo hints, *src = NULL, *dst;
    char portbuf[16];
    char addrbuf[64];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AddrConfigFlag(host);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf(portbuf, sizeof(portbuf), "%d", port);
    e = getaddrinfo(host, portbuf, &hints, &dst);
    if (e != 0) {
	fprintf(stderr, "Cannot resolve %s:%s: %s\n",
			host, portbuf, gai_strerror(e));
	exit(1);
    }

    fd = socket(dst->ai_family, dst->ai_socktype, 0);
    if (fd < 0) {
	freeaddrinfo(dst);
	return(-1);
    }

    if (BindHost != NULL || BindPort != NULL) {
	hints.ai_flags = AddrConfigFlag(BindHost);
	e = getaddrinfo(BindHost, BindPort, &hints, &src);
	if (e != 0) {
	    fprintf(stderr, "Cannot resolve bindhost/bindport %s%s%s: %s\n",
			BindHost ? BindHost : "", BindPort ? ":" : "",
			BindPort ? BindPort : "", gai_strerror(e));
	    close(fd);
	    exit(1);
	}

	if (bind(fd, src->ai_addr, src->ai_addrlen) < 0) {
	    freeaddrinfo(src);
	    close(fd);
	    freeaddrinfo(dst);
	    return(-1);
	}
	freeaddrinfo(src);
    }

    if (DebugOpt) {
	getnameinfo(dst->ai_addr, dst->ai_addrlen,
			addrbuf, sizeof(addrbuf), NULL, 0, NI_NUMERICHOST);
	fprintf(stderr, "Connecting to %s (%s)\n", host, addrbuf);
	fflush(stderr);
    }

    if (connect(fd, dst->ai_addr, dst->ai_addrlen) < 0) {
	if (DebugOpt) {
	    fprintf(stderr, "connection failed: %s\n", strerror(errno));
	    fflush(stderr);
	}
	close(fd);
	freeaddrinfo(dst);
	return(-1);
    }
    freeaddrinfo(dst);
    if (DebugOpt) {
	fprintf(stderr, "connection success\n");
	fflush(stderr);
    }
    return(fd);
}

int
CommandResponse(const char *cmd, char **pres)
{
    static char buf[4096];

    if (DebugOpt && cmd) {
	fprintf(stderr, ">> %s\n", cmd);
	fflush(stderr);
    }

    if (cmd)
	fprintf(Fo, "%s\r\n", cmd);
    fflush(Fo);
    buf[0] = 0;
    if (pres)
	*pres = buf;
    if (fgets(buf, sizeof(buf), Fi) != NULL) {
	if (DebugOpt) {
	    fprintf(stderr, "<< %s", buf);
	    fflush(stderr);
	}
	/*
	 * A line longer than buf would leave its rest to be read as the
	 * next reply: no code for it, so the session ends.
	 */
	if (strchr(buf, '\n') == NULL)
	    return(-1);
	return(NntpCode(buf));
    } else if (DebugOpt) {
	fprintf(stderr, "<< (EOF)\n");
	fflush(stderr);
    }
    return(0);
}

int
hash(const char *str)
{
    unsigned int hv = 0xA432BCDD;

    while (*str) {
	hv = (hv << 5) ^ *str ^ (hv >> 23);
	++str;
    }
    hv ^= (hv >> 16);
    return(hv & GHMASK);
}

int
SetField(char **pptr, const char *str)
{
    if (*pptr && strcmp(*pptr, str) == 0)
	return(0);
    if (*pptr)
	free(*pptr);
    *pptr = strcpy(malloc(strlen(str) + 1), str);
    return(1);
}
