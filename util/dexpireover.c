
/*
 * UTIL/DEXPIREOVER.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 * 	In this incarnation, dexpireover cleans up overview information as
 *	specified in the 'x' fields in dexpire.ctl (see the sample dexpire.ctl)
 *
 *	This is still very rough.  I still need to add in a free-space-target
 *	option and have dexpireover adjust the expiration dynamically based on
 *	free space.  IT DOESN'T DO THIS YET!! You have to make sure your
 *	expiration ('x' option) in dexpire.ctl is not set too high.
 *
 *	I also need to have a remote-server scanning option to allow 
 *	dexpireover to adjust expirations based on remote server retentions.
 *	It does not do this yet either.
 *
 *	Modifications by Nickolai Zeldovich to allow spool-based expiration
 *	(ExpireBySpool and ExpireFromFile)
 *
 *	Specifying the -pN option will fork N dexpireover processes and
 *	perform the expiration process in parallel. This is useful to speed
 *	up ExpireBySpool where dhistory lookups take a long time.
 *
 *	LOCKING INFO
 *
 *	The data file will have an advisory lock at offset 4 from dreaderd
 *	when it has the file open (can't rewrite data)
 *	The info file will have an advisory lock at offset 4 from dreaderd
 *	when it has the file open (can't resize)
 *
 *	od_HFd = data. file
 *	ov_OFd = over. file
 *
 */

#include <dreaderd/defs.h>

#define GZ_MAGIC0       0x1F
#define GZ_MAGIC1       0x8B
#define GZ_EXTRA        0x04
#define GZ_ORIGNAME     0x08
#define GZ_COMMENT      0x10
#define GZ_HEADCRC      0x02

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

/*
 * gzEmpty() - the gzip file at p (n bytes) holds a complete deflate stream
 * that inflates to nothing: -z gzipped an empty data file (a range that has
 * only readerxover trackonly records, which have no overview data).
 */
static int
gzEmpty(const unsigned char *p, size_t n)
{
    size_t hl = gzHeaderLen(p, n);
    unsigned char out[1];
    z_stream z;
    int code;

    if (hl == 0)
	return(0);
    bzero(&z, sizeof(z));
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
	return(0);
    z.next_in = (unsigned char *)p + hl;
    z.avail_in = n - hl;
    z.next_out = out;
    z.avail_out = sizeof(out);
    code = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return(code == Z_STREAM_END && z.total_out == 0);
}
#endif


typedef struct Group {
    struct Group *gr_Next;
    int		gr_State;
    artno_t	gr_StartNo;
    artno_t	gr_EndNo;
    int		gr_CTS;
    int		gr_LMTS;
    int		gr_Iter;
    char	*gr_GroupName;
    char	*gr_Flags;
    char	*gr_Hash;
    int		gr_UpdateFlag;
} Group;

#define GRF_DESCRIPTION 0x00000001
#define GRF_STARTNO     0x00000002
#define GRF_ENDNO       0x00000004
#define GRF_FLAGS       0x00000008
#define GRF_FROMLOCAL   0x00000800
#define GRF_NEW         0x00001000
#define GRF_FROMREMOTE  0x00002000
#define GRF_MODIFIED    0x00008000
#define GRF_EDITEDBEG   0x00010000
#define GRF_GONE        0x00020000	/* left dactive.kp during the run */
#define GRF_NEEDZLIB    0x00040000	/* -R left it alone: .gz data, no zlib */

#define GHSIZE		1024
#define GHMASK		(GHSIZE-1)

typedef struct FileCacheList {
    struct FileCacheList	*fc_Next;
    int				fc_Fd;
    artno_t			fc_ArtBase;
} FileCacheList;

#define DEXPOVER_READ_BUFFER_SIZE	4096
#define DEXPOVER_HASH_SIZE		32768

/* Since we only look at the first char of the first-level directory,
 * we do not support more than 16 forks.
 */
#define	MAX_PAR_COUNT			16

/*
 * These aren't really buckets, they're parts of a bucket
 */

typedef struct bucket_t {
    struct bucket_t *next;
    hash_t hash_item;
    short valid;
} bucket_t;

KPDB  *KDBActive;
Group *GHash[GHSIZE];

void ScanDirectories(void);
/*
 * DirFrame - one directory of the path from the group home down to the
 * directory being scanned, by (st_dev, st_ino) of its stat(): a symlink
 * to a directory on that path (cd -> ., up -> ..) is a loop.
 */
typedef struct DirFrame {
    dev_t		df_Dev;
    ino_t		df_Ino;
    const struct DirFrame *df_Up;
} DirFrame;

void scanDirectory(const char *dirpath, char *dirname, int *level, int noLinks,
						const DirFrame *dirs);
static void scanFiles(const char *path, int *level, int noLinks,
					const DirFrame *dirs, int top);
static int hierName(const char *name);
static int topRegular(const char *path, const char *name, int say);
static void topGoneAdd(const char *name);
static int topGone(const char *name);
static int gzPairShared(const Group *group, char *pair, size_t size);
static int activeHas(const char *name);
static int dataRegular(const Group *group, const char *dirPath, artno_t startNo, artno_t endNo, int dataEntries, int top, char *bad, size_t size);
static int dataOpen(const char *path);
static int topOverIrregular(const Group *group, char *bad, size_t size);
void DeleteJunkFile(const char *dirPath, const char *name);
void ProcessOverviewFile(const char *dirPath, const char *name, int type, int top);
int getFirstArtAge(Group *group, int fd, OverHead *oh);
char *allocTmpCopy(const char *buf, int bufLen);
Group *EnterGroup(const char *groupName, artno_t begNo, artno_t endNo, int lmts, int cts, int iter, const char *flags);
Group *FindGroupByHash(char *Hash, int iter, int hier, const char *dirPath);
int SetField(char **pptr, const char *str);
void ExpireByDays(Group *group, int fd, OverHead *oh, int expireSecs);
void ExpireBySpool(Group *group, int fd, OverHead *oh);
void ExpireFromFile(Group *group, int fd, OverHead *oh, int expireSecs);
int RewriteData(Group *group, int fd, OverHead *oh, const char *dirPath, int oldDataEntries, int newDataEntries, int *fdl);
int rewriteDataFile(Group *group, const char *cacheBase, int cacheSize, const OverArt *oa, OverArt *ob, const char *dirPath, int oldDataEntriesMask, int newDataEntriesMask, artno_t artNo);
int ResizeGroup(Group *group, int fd, OverHead *oh, int maxArts, int OldDataEntries, int *fdl);
static int rangeHasRecords(const OverArt *oa, long long n, artno_t base, int entries, artno_t startNo, artno_t endNo);
#ifdef USE_ZLIB
static int rangeHasData(const OverArt *oa, long long n, artno_t base, int entries, artno_t startNo, artno_t endNo);
#endif
#ifndef USE_ZLIB
static int gzipOnlyData(Group *group, const char *dirPath, artno_t startNo, artno_t endNo, int dataEntries);
#endif
static void groupGone(Group *group, const char *what);
static int createTempFile(const char *path);
static int writeActiveField(Group *group, const char *tok, const char *data);
static int deleteStaleGroup(Group *group, int t);
static int enterActiveRecord(int recOff, int recLen, int late);
static int rescanActive(void);
static int currentIter(char *Hash, int iter);
/* hierDirClass(): where a hierarchy file is for a group (D-336) */
#define HIERDIR_OFF	0
#define HIERDIR_IN	1
#define HIERDIR_OTHER	2
static int hierDirOf(const char *name, GroupHashType *method, char *dir, size_t size);
static int hierDirClass(const char *name, const char *dirPath);
static int hierGroupKnown(const char *name, const char *dirPath);
static int hierOtherOver(const char *name);
static int overNamesGroup(const char *path, const char *name);
static int overFileType(const char *name);
static int tempNameIs(const char *group, int gftype, artno_t artBase, int iter, int gz, const char *want);
static int tempOfGroup(const char *Hash, int gftype, artno_t artBase, int iter, int gz, const char *want);
static int tempRewriteName(const char *dirPath, const char *name);
static int dataFileStale(Group *group, artno_t artBase, const char *path, int *dataEntries);
static int gzNameTaken(Group *group, const char *path, artno_t artBase, int hier, char *why, size_t whySize);
static void expireOverArt(int fd, off_t pos, const hash_t *mh, int rcvd);
int nearestPower(int n);
void ReadDExpOverList(void);
int expOverListCheckExpired(hash_t *hv);
int hexCharToInt(char c);
int getDataEntries(const char *path);
long long x64write(int d, const void *buf, long long nbytes);
long long x64read(int d, void *buf, long long nbytes);
void dumpovmap(OverArt *oa, long long n, char *filename);
void dumpoh(OverHead *oh, char *filename);

int UpdateBegArtNoOpt = 0;
int UpdateCTSOpt = 0;
int RewriteDataOpt = 0;
int CheckDataEntries = 0;
int BadGroups = 0;
int ResizedGroups = 0;
int NoResizedGroups = 0;
int GoodGroups = 0;
int ActiveUpdated = 0;
int VerboseOpt = -1;
int ResizeOpt = -1;
int ForReal = 1;
int OldGroups = 0;
int UseExpireByDays = 0;
int UseExpireBySpool = 0;
int UseExpireFromFile = 0;
int ParallelCount = 0;
int FactorExpire = 0;
int LockWaitTime= 1;
int MustExit = 0;
int CompressDataOpt = 0;
int CompressedFiles = 0;
int CompressFailed = 0;
int CompressEligible = 0;
int NeedZlibGroups = 0;
int RewriteFailed = 0;		/* groups whose -R rewrite failed */
char *Wild;
bucket_t *dexpover_msgid_hash;
int ParallelIdx = 0;
int ActiveCTS0 = 0;		/* time of the dactive.kp scan */
int ActiveScanOff = 0;		/* last dactive.kp record scanned ... */
int ActiveScanLen = 0;		/* ... and its length */
int ParallelPid[MAX_PAR_COUNT];

void
sigInt(int sigNo)
{
    printf("Exit signal caught - exiting\n");
    ++MustExit;
    if (MustExit > 3)
	exit(1);
}

/*
 * We use alarms to cancel a lock wait, so just ignore the alarm
 */
void
sigAlarm(int sigNo)
{
 ;;
}

void Usage(char *progname)
{
    fprintf(stderr, "Expire the reader header database\n");
    fprintf(stderr, "dexpireover [-a] [-e] [-f active] [-l#] [-NB] [-n] [-O#] [-o] [-p#] [-R[R]] [-s] [-U] [-v#] [-w wildmat] [-x] [-y] [-C diablo.config] [-d[n]] [-V]\n");
    fprintf(stderr, "\t-a\t\tDo a standard header expire run (-NB -U -s -y)\n");
    fprintf(stderr, "\t-e\t\tExpire by checking a local spool\n");
    fprintf(stderr, "\t-f file\t\tSpecify the name of the active file\n");
    fprintf(stderr, "\t-l#\t\tWait N seconds for lock files\n");
    fprintf(stderr, "\t-NB\t\tUpdate the begining article number (NB) in active\n");
    fprintf(stderr, "\t-n\t\tDon't actually make any changes (dry run)\n");
    fprintf(stderr, "\t-O#\t\tRemove groups not used within # days\n");
    fprintf(stderr, "\t-o\t\tExpire from file of msgid hashes\n");
    fprintf(stderr, "\t-p#\t\tRun # parallel expire runs (speed up)\n");
    fprintf(stderr, "\t-R\t\tRewrite header data files\n\t\t\t(-RR to rewrite and remove corrupted entries)\n");
    fprintf(stderr, "\t-s\t\tResize group indexes\n");
    fprintf(stderr, "\t-U\t\tAdd CTS (group create time) in active if not present\n");
    fprintf(stderr, "\t-v[#]\t\tVerbose mode\n");
    fprintf(stderr, "\t-w wildmat\tSpecify a wildmat of groups to expire\n");
    fprintf(stderr, "\t-x\t\tUse factoring for expire (see man page)\n");
    fprintf(stderr, "\t-y\t\tActually expire headers based on dexpire.ctl\n");
    fprintf(stderr, "\t-z\t\tCompress header data files based on dexpire.ctl\n");
    fprintf(stderr, "\t-C file\tspecify diablo.config to use\n");
    fprintf(stderr, "\t-d[n]\tset debug [with optional level]\n");
    fprintf(stderr, "\t-V\tprint version and exit\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    char *dbfile = NULL;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected argument: %s\n", ptr);
	    Usage(av[0]);
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'a':
	    UseExpireByDays = 1;
	    UpdateBegArtNoOpt = 1;
	    UpdateCTSOpt = 1;
	    ResizeOpt = 1;
	    break;
	case 'e':
	    UseExpireBySpool = 1;
	    break;
	case 'f':
	    dbfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'l':
	    LockWaitTime = strtol((*ptr) ? ptr : av[++i], NULL, 0);
	    break;
	case 'N':
	    while (*ptr) {
		switch(*ptr) {
		case 'B':
		    UpdateBegArtNoOpt = 1;
		    break;
		default:
		    break;
		}
		++ptr;
	    }
	    break;
	case 'n':
	    ForReal = 0;
	    if (VerboseOpt < 0)
		VerboseOpt = 1;
	    break;
	case 'O':
	    if (*ptr)
		OldGroups = strtol(ptr, NULL, 0);
	    else
		OldGroups = 30 * 3;	/* 3 months by default */
	    break;
	case 'o':
	    UseExpireFromFile = 1;
	    break;
	case 'p':
	    if (*ptr)
		ParallelCount = strtol(ptr, NULL, 0);
	    else
		ParallelCount = 1;
	    if (ParallelCount > MAX_PAR_COUNT)
		ParallelCount = MAX_PAR_COUNT;
	    if (ParallelCount < 0) {
		fprintf(stderr, "Invalid number of processes for -p: %s\n", ptr);
		exit(1);
	    }
	    /* Note that a parcount of 1 doesn't do anything useful. */
	    break;
	case 'R':
	    RewriteDataOpt = 1;
	    if (*ptr == 'R') {
		CheckDataEntries = 1;
		ptr++;
	    }
	    break;
	case 's':
	    ResizeOpt = 1;
	    break;
	case 'U':
	    UpdateCTSOpt = 1;
	    break;
	case 'v':
	    VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'w':
	    Wild = (*ptr) ? ptr : av[++i];
	    break;
	case 'x':
	    FactorExpire = 1;
	    break;
	case 'y':
	    UseExpireByDays = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'z':
#ifdef USE_ZLIB
	    CompressDataOpt = 1;
#else
	    fprintf(stderr, "-z: dexpireover was built without zlib\n");
	    exit(1);
#endif
	    break;
	/* Common options */
	case 'C':           /* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    Usage(av[0]);
	}
    }

    if (!UseExpireByDays && ResizeOpt <= 0 && !RewriteDataOpt &&
			!UseExpireFromFile && !UseExpireBySpool && !OldGroups)
	Usage(av[0]);
    /*
     * Read in the list of expired msgid hashes, if we are using it
     */

    if (UseExpireFromFile)
	ReadDExpOverList();

    /*
     * fork off parallel copies of dexpireover at this point
     */

    if (ParallelCount) {
	char *stdout_buffer;

	int started = 0;
	int failed = 0;

	for (ParallelIdx=0; ParallelIdx < ParallelCount; ParallelIdx++) {
	    int pid;

	    pid = fork();
	    if(pid == 0)
		break;
	    if (pid < 0) {
		fprintf(stderr, "Parallel worker %d not started: fork failed (%s)\n",
					ParallelIdx, strerror(errno));
		ParallelPid[ParallelIdx] = 0;
		++failed;
		continue;
	    }
	    ParallelPid[ParallelIdx] = pid;
	    ++started;
	}

	stdout_buffer = (char *)malloc(BUFSIZ);
	setvbuf(stdout, stdout_buffer, _IOLBF, BUFSIZ);

	if (ParallelIdx == ParallelCount) {
	    pid_t pid;
	    int status;
	    int remaining = started;

	    /* only workers that really started are waited for */
	    while (remaining) {
		pid = wait3(&status, 0, NULL);
		if (pid < 0) {
		    if (errno == EINTR)
			continue;
		    fprintf(stderr, "wait3 failed with %d workers left (%s)\n",
						remaining, strerror(errno));
		    failed += remaining;
		    break;
		}
		for (i=0; i<ParallelCount; i++)
		    if(ParallelPid[i] == pid) {
			ParallelPid[i] = 0;
			--remaining;
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			    fprintf(stderr, "Parallel worker %d did not finish cleanly\n", i);
			    ++failed;
			}
		    }
	    }

	    printf("Parallelizing dexpireover (%d forks) finished.\n",
	       started);
	    if (failed)
		printf("%d of %d parallel workers did not run to completion\n",
						failed, ParallelCount);
	    exit(failed ? 1 : 0);
	}
    }

    /*
     * Open active file database
     */

    if (VerboseOpt)
	printf("Loading active file\n");
    if (dbfile) {
	KDBActive = KPDBOpen(dbfile, O_RDWR);
    } else {
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR);
    }
    if (KDBActive == NULL) {
	fprintf(stderr, "Unable to open dactive.kp\n");
	exit(1);
    }
    if (OldGroups && Wild == NULL) {
	fprintf(stderr, "group wildcard must be specified if -O option used\n");
	Usage(av[0]);
    }

    LoadExpireCtl(1);

    /*
     * Open the history file if we are going to expire based on local spool
     */

    /*
     * Read-only: the history is only looked up here.  Opened read-write, a
     * missing history was created empty, and every overview entry was then
     * expired as "not in the history".
     */
    if (UseExpireBySpool)
	HistoryOpen(NULL, HGF_READONLY);

    /*
     * scan dactive.kp
     */

    if (VerboseOpt)
	printf("Hashing newsgroups\n");
    {
	int recLen;
	int recOff;

	ActiveCTS0 = (int)time(NULL);

	for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	     recOff;
	     recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	) {
	    ActiveScanOff = recOff;
	    ActiveScanLen = recLen;
	    (void)enterActiveRecord(recOff, recLen, 0);
	}
    }

    rsignal(SIGINT, sigInt);
    rsignal(SIGHUP, sigInt);
    rsignal(SIGTERM, sigInt);
    rsignal(SIGALRM, SIG_IGN);

    /*
     * Scan /news/spool/group/ and do the actual expire, over resize
     * and data rewrite
     *
     */

    ScanDirectories();

    /*
     * Writeback active file
     */

    if (UpdateBegArtNoOpt || OldGroups || UpdateCTSOpt) {
	int t0 = (int)time(NULL);	/* int-sized for LMTS compare */
	int t;
	int i;
	int count = 0;

	t = t0 - OldGroups * (60 * 60 * 24);

	for (i = 0; i < GHSIZE; ++i) {
	    Group *group;

	    for (group = GHash[i]; group; group = group->gr_Next) {
		/*
		 * If we have a new group not previously in the database,
		 * we only add it if SyncGroupsOpt is set.
		 */
		int add = 0;

		/* removed from the active file during the run: leave it so */
		if (group->gr_State & GRF_GONE)
		    continue;

		if (OldGroups) {
		    if (group->gr_LMTS) {
			/*
			 * Existing LMTS
			 */
			if ((int)(t - group->gr_LMTS) > 0 &&
					deleteStaleGroup(group, t) > 0) {
			    if (VerboseOpt)
				printf("%s: stale group deleted\n", group->gr_GroupName);
			    add = 1;
			    group->gr_State &= ~GRF_MODIFIED;	/* prevent NB update */
			} 
		    } else {
			/*
			 * no LMTS in record, add one
			 */
			if (ForReal) {
			    char tsBuf[16];
			    sprintf(tsBuf, "%08x", (int)t0);
			    if (writeActiveField(group, "LMTS", tsBuf) == 0)
				add = 1;
			}
			if (VerboseOpt)
			    printf("%s: added missing LMTS\n", group->gr_GroupName);
		    }
		}

		if (UpdateCTSOpt) {
		    if (group->gr_CTS) {
			if (ForReal) {
			    char tsBuf[16];
			    sprintf(tsBuf, "%08x", group->gr_CTS);
			    (void)writeActiveField(group, "CTS", tsBuf);
			}
			if (VerboseOpt)
			    printf("%s: added missing CTS\n", group->gr_GroupName);
			add = 1;
		    }
		}

		/*
		 * A group with an over file in its directory of the other
		 * hierarchy layout (D-336) may have its articles there, for a
		 * dreaderd started before readergrouphash was changed: its
		 * first article number is not moved by what the files of the
		 * configured layout lack (D-337).  Asked of the file, not of
		 * the scan: a worker of -p meets only some directories, and
		 * the over file of the other layout may be in one it does not
		 * scan (x.y: the top in plain hierarchy, x/ in hierarchy-full).
		 */
		if ((group->gr_State & (GRF_MODIFIED | GRF_EDITEDBEG)) ==
				(GRF_MODIFIED | GRF_EDITEDBEG) &&
		    hierOtherOver(group->gr_GroupName)) {
		    if (VerboseOpt)
			printf("%s: first article number not moved to %lld: the group has an over file in the directory of the other hierarchy layout\n",
				group->gr_GroupName, group->gr_StartNo);
		    group->gr_State &= ~GRF_EDITEDBEG;
		}
		if (ForReal && (group->gr_State & GRF_MODIFIED)) {
		    if (group->gr_State & GRF_EDITEDBEG) {
			char startBuf[20];
			snprintf(startBuf, sizeof(startBuf), "%010lld", group->gr_StartNo);
			if (writeActiveField(group, "NB", startBuf) == 0)
			    add = 1;
		    }
		}
		count += add;
	    }
	}
	printf("Updated article range in %d groups\n", count);
    }
    if (KDBActive)
	KPDBClose(KDBActive);

    /*
     * Close history if we had it open
     */
    if (UseExpireBySpool)
	HistoryClose();

    if (RewriteFailed)
	printf("Rewrite failed for %d group(s)\n", RewriteFailed);
    return((CompressFailed || NeedZlibGroups || RewriteFailed) ? 1 : 0);
}

/*
 * enterActiveRecord() - enter the group of the dactive.kp record at recOff
 * into the group table.  late: the record was appended after the scan at
 * the start; a group we know already (its record was only replaced) is
 * left as it is, with what the run has done to it.  Returns 1 when a group
 * was entered.
 */
static int
enterActiveRecord(int recOff, int recLen, int late)
{
    int groupLen;
    int flagsLen;
    const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
    const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
    const char *flags = KPDBGetField(rec, recLen, "S", &flagsLen, "y");
    artno_t begNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
    artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
    int lmts = (int)strtoul(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
    int cts = (int)strtoul(KPDBGetField(rec, recLen, "CTS", NULL, "0"), NULL, 16);
    int iter = (int)strtoul(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);	/* written as %06d */
    Group *grp;

    if (cts == 0)	/* enter non-zero cts only if group has no CTS field */
	cts = ActiveCTS0;
    else
	cts = 0;

    if (group)
	group = allocTmpCopy(group, groupLen);
    if (flags)
	flags = allocTmpCopy(flags, flagsLen);

    /*
     * ignore bad group or group that does not match the wildcard
     */

    if (group == NULL)
	return(0);
    if (Wild && WildCmp(Wild, group) != 0)
	return(0);

    if (late) {
	hash_t hv = hhash(GFHash(group, &DOpts.ReaderGroupHashMethod));

	for (grp = GHash[hv.h1 & GHMASK]; grp; grp = grp->gr_Next) {
	    if (strcmp(group, grp->gr_GroupName) == 0)
		return(0);
	}
	if (VerboseOpt)
	    printf("group %s was added to the active file during the run\n", group);
    }

    grp = EnterGroup(
	group,
	begNo,
	endNo,
	lmts,
	cts,
	iter,
	flags
    );
    grp->gr_State &= ~(GRF_NEW|GRF_MODIFIED);
    return(1);
}

/*
 * rescanActive() - enter the groups that were added to dactive.kp since
 * the last scan.  New records are always appended, so the scan goes on
 * behind the last record it saw; with nothing appended this is one look
 * at the append sequence in the header.  Returns the number of groups
 * entered.
 */
static int
rescanActive(void)
{
    int recLen = ActiveScanLen;
    int recOff;
    int count = 0;

    if (ActiveScanOff == 0)	/* no record so far */
	recOff = KPDBScanFirst(KDBActive, 0, &recLen);
    else
	recOff = KPDBScanNext(KDBActive, ActiveScanOff, 0, &recLen);
    while (recOff) {
	ActiveScanOff = recOff;
	ActiveScanLen = recLen;
	count += enterActiveRecord(recOff, recLen, 1);
	recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen);
    }
    return(count);
}

/*
 * expireOverArt() - mark the index slot at pos expired, if it still holds
 * the article we looked at (message-id hash mh, received at rcvd).  The
 * decision was made from the shared map without a lock; dreaderd writes
 * and cancels a slot under the exclusive lock at offset 0, so we take it,
 * read the slot again and leave it alone when a new article has taken it
 * meanwhile.  Writing back a copy made before would wipe that article.
 * The caller holds the structural lock at offset 4 (ProcessOverviewFile()).
 */
static void
expireOverArt(int fd, off_t pos, const hash_t *mh, int rcvd)
{
    OverArt cur;

    if (hflock(fd, 0, XLOCK_EX) < 0) {
	printf("overview lock failed: %s\n", strerror(errno));
	return;
    }
    if (pread(fd, &cur, sizeof(cur), pos) != sizeof(cur)) {
	printf("overview read failed: %s\n", strerror(errno));
    } else if (OA_ARTVALID(&cur) && cur.oa_TimeRcvd == rcvd &&
	cur.oa_MsgHash.h1 == mh->h1 && cur.oa_MsgHash.h2 == mh->h2
    ) {
	cur.oa_ArtNo = -2;		/* EXPIRED */
	lseek(fd, pos, 0);
	if (WriteAll(fd, &cur, sizeof(OverArt)) < 0)
	    printf("overview write failed: %s\n", strerror(errno));
    }
    hflock(fd, 0, XLOCK_UN);
}

/*
 * createTempFile() - create a temporary file of RewriteData() (the new
 * over. file or a new data. file under its ".<name>"), opened read-write.
 * Whatever is at path is unlinked first, a regular file left by an
 * interrupted -R or a symlink, and the file is created with O_EXCL, so a
 * symlink there is not followed and its target, wherever it points, is
 * not truncated or written (#515 r5).  For a leftover regular file that
 * is the same as O_TRUNC.  Returns the descriptor, or -1 with errno set
 * (unlink() other than ENOENT, or open()).
 */
static int
createTempFile(const char *path)
{
    if (unlink(path) < 0 && errno != ENOENT)
	return(-1);
    return(open(path, O_RDWR|O_CREAT|O_EXCL, 0644));
}

/*
 * groupGone() - the group's record left dactive.kp after the scan began
 * (a KP_LOCK read found none): stop working on the group and drop its
 * pending write-back.
 */
static void
groupGone(Group *group, const char *what)
{
    if (VerboseOpt)
	printf("group %s no longer in the active file, %s\n",
					group->gr_GroupName, what);
    group->gr_State |= GRF_GONE;
    group->gr_State &= ~GRF_MODIFIED;
}

/*
 * writeActiveField() - update a field of the group's record, which must
 * still exist.  The record stays locked from the check through the write,
 * so a removal either comes first (and we skip) or waits for us.  A plain
 * KPDBWrite() would create the record again with only this field.
 */
static int
writeActiveField(Group *group, const char *tok, const char *data)
{
    int recLen;

    if (KPDBReadRecord(KDBActive, group->gr_GroupName, KP_LOCK, &recLen) == NULL) {
	groupGone(group, "not updated");
	return(-1);
    }
    KPDBWriteEncode(KDBActive, group->gr_GroupName, tok, data, KP_UNLOCK);
    return(0);
}

/*
 * deleteStaleGroup() - -O: delete the group from the active file if its
 * last-article time (LMTS) is still older than t.  The LMTS read at the
 * start of the run can be hours old; dreaderd sets it under the record
 * lock when it numbers an article, so look at it again under that lock.
 * Returns 1 when the group is deleted (or would be, with -n), 0 when it
 * got an article meanwhile and stays, -1 when it has left the active file.
 */
static int
deleteStaleGroup(Group *group, int t)
{
    const char *rec;
    int recLen;
    int lmts;

    rec = KPDBReadRecord(KDBActive, group->gr_GroupName, KP_LOCK, &recLen);
    if (rec == NULL) {
	groupGone(group, "not deleted");
	return(-1);
    }
    lmts = (int)strtoul(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
    if (lmts == 0 || (int)(t - lmts) <= 0) {
	KPDBUnlock(KDBActive, rec);
	if (VerboseOpt && lmts == 0)
	    printf("%s: LMTS now missing or 0 in the record, not deleted\n",
						group->gr_GroupName);
	else if (VerboseOpt)
	    printf("%s: new article during the run, not deleted\n",
						group->gr_GroupName);
	group->gr_LMTS = lmts;
	return(0);
    }
    if (ForReal)
	KPDBDelete(KDBActive, group->gr_GroupName);	/* unlocks the record */
    else
	KPDBUnlock(KDBActive, rec);
    return(1);
}

/*
 * Scan /news/spool/group/ and do the actual expire, over resize
 * and data rewrite
 *
 * This works by scanning all files in the directories, mapping the
 * filename (hash) to a newsgroup name and performing the following,
 * depending on file type:
 *
 * over.* :
 * *.o.* :
 *		1) resize over (if requested)
 *		2) expire articles (by days, spool or file)
 *		3) rewrite data (if requested)
 *
 * data.*:
 * *.d.*:
 *		1) if the range of articles for the file falls below NB,
 *		   then delete the file
 *
 * If a newsgroup for a file cannot be found, the file is removed
 */

/*
 * DirOnPath() - the directory st (of stat(), so a link's target) is one of
 * dirs, the path from the group home down to the directory being scanned.
 */
static int
DirOnPath(const DirFrame *dirs, const struct stat *st)
{
    for (; dirs != NULL; dirs = dirs->df_Up) {
	if (dirs->df_Dev == st->st_dev && dirs->df_Ino == st->st_ino)
	    return(1);
    }
    return(0);
}

/*
 * EntryIsLink() - the entry name of the current directory is a symlink.
 */
static int
EntryIsLink(const char *name)
{
    struct stat lst;

    return(lstat(name, &lst) == 0 && S_ISLNK(lst.st_mode));
}

void
ScanDirectories(void)
{
    DIR *dir;
    int level = 0;
    DirFrame home;
    struct stat hst;

    if (VerboseOpt)
	printf("Scanning group directories\n");

    if (chdir(PatExpand(GroupHomePat)) < 0) {
	fprintf(stderr, "Unable to chdir(\"%s\"): %s\n",
				PatExpand(GroupHomePat), strerror(errno));
	exit(1);
    }
    /* the root of every path: a link back to the group home is a loop */
    if (stat(".", &hst) != 0) {
	fprintf(stderr, "Unable to stat(\"%s\"): %s\n",
				PatExpand(GroupHomePat), strerror(errno));
	exit(1);
    }
    home.df_Dev = hst.st_dev;
    home.df_Ino = hst.st_ino;
    home.df_Up = NULL;
    if ((dir = opendir(".")) != NULL) {
	den_t *den;
	struct stat st;
	DirFrame top;

	while ((den = readdir(dir)) != NULL) {
	    /*
	     * The first character of a layout directory is alphanumeric,
	     * but for md5-64 "\N" it can also be '+' or '-' (the base64
	     * alphabet of the hash): those directories were skipped (D-326).
	     * Such a name must be a real directory (lstat()): a symlink of
	     * it is not followed, so that a link out of the group home is
	     * not scanned and its files removed as of no group.  An
	     * alphanumeric name is followed with stat(), as before.
	     */
	    int signDir = DOpts.ReaderGroupHashMethod.gh_type == HASHGRP_64MD5 &&
		(den->d_name[0] == '+' || den->d_name[0] == '-');

	    if (signDir && lstat(den->d_name, &st) == 0 && S_ISLNK(st.st_mode)) {
		if (VerboseOpt > 0)
		    printf("Skipping symlink %s/%s: followed only when the name starts with an alphanumeric\n", PatExpand(GroupHomePat), den->d_name);
		continue;
	    }
	    if ((isalnum((unsigned char)den->d_name[0]) || signDir) &&
	    	(signDir ? lstat(den->d_name, &st) : stat(den->d_name, &st)) == 0 &&
		S_ISDIR(st.st_mode) &&
		/*
		 * We explicitly use the first char, because overview
		 * sizes appear to be not evenly distributed wrt second
		 * char.
		 */
		(ParallelCount ? ((GFIndex(den->d_name[0]) % ParallelCount) ==
				      ParallelIdx) : 1)
	    ) {
		if (DirOnPath(&home, &st)) {
		    if (VerboseOpt > 0)
			printf("Skipping directory loop %s/%s: it is the group home\n", PatExpand(GroupHomePat), den->d_name);
		    continue;
		}
		top.df_Dev = st.st_dev;
		top.df_Ino = st.st_ino;
		top.df_Up = &home;
		/*
		 * Below a sign directory no symlink is followed or opened
		 * (#515 r3, r4): main never entered those trees.  The
		 * alphanumeric trees follow links as before.
		 */
		scanDirectory(PatExpand(GroupHomePat), den->d_name, &level, signDir, &top);
	    }
	}
	closedir(dir);
    }

    /*
     * The files at the top of the group home (D-338): the hierarchy layouts
     * put there the files of a group whose directory (FindGroupDir()) is
     * empty: with plain hierarchy a group of two components whose first
     * has one character or none (x.y, a.b, x.yz: a one-character component
     * is joined to the next, and the last is no directory), with both a
     * group of one component (junk).  With plain hierarchy a group of one
     * character and the same name plus .gz share the top (x and x.gz:
     * FindGroupDir() skips the component "x" of x.gz), so "d.<n>.x.gz" is
     * x's compressed name and x.gz's plain data (D-325): -R rewrites
     * neither of them, as in a subdirectory (gzPairShared()); the D-318
     * rule judges the file by both.  Other groups at the top have X.gz in
     * a directory of its own (x.y.gz in x.y/, junk.gz in junk/).  crc and
     * md5 put no file there, so their top is not looked at.  The two passes of
     * scanDirectory() run on the group home itself (scanFiles(), top 1):
     * no directory is entered again, and only the hierarchy names of groups
     * whose directory is the top are acted on (ProcessOverviewFile()).
     * With -p only worker 0 does it (ParallelIdx is 0 without -p too): a
     * group's files of the configured layout are all in its one directory,
     * so no other worker meets a file of these groups, and every worker but
     * one would open, resize and expire them again.
     */
    if (DOpts.ReaderGroupHashMethod.gh_type == HASHGRP_HIER && ParallelIdx == 0)
	scanFiles(PatExpand(GroupHomePat), &level, 0, &home, 1);

    printf("Scanned %d files, %d were bad, %d/%d indexes resized successfully\n",
	GoodGroups + BadGroups, 
	BadGroups, 
	ResizedGroups, 
	NoResizedGroups + ResizedGroups
    );
    if (CompressedFiles) {
	printf("Compressed %d files\n", CompressedFiles);
    }
    if (CompressFailed) {
	printf("Compression failed for %d files\n", CompressFailed);
    }
    if (CompressEligible) {
	printf("%d files would be compressed\n", CompressEligible);
    }
    if (NeedZlibGroups) {
	printf("%d group(s) not rewritten: compressed overview data needs a zlib build\n", NeedZlibGroups);
    }

}

/*
 * scanDirectory() - expire the group files of dirpath/dirname and remove
 * the files of no group.  With noLinks every child that is a symlink, of
 * any type and also a dangling one, is skipped: not followed, not opened
 * (an over file would be expired in the link's target), not removed
 * (#515 r4).  Without noLinks child links are followed, as on main.  dirs
 * is the path from the group home down to dirname itself: a child
 * directory on it (a link such as cd -> . or up -> ..) is skipped, as
 * the recursion would not end there.
 */
void
scanDirectory(const char *dirpath, char *dirname, int *level, int noLinks,
						const DirFrame *dirs)
{
    char path[PATH_MAX];
    char origpath[PATH_MAX];
    int n;

    n = snprintf(path, sizeof(path), "%s/%s", dirpath, dirname);
    if (n < 0 || (size_t)n >= sizeof(path)) {
	fprintf(stderr, "Skipping overlong directory pathname: %s/%s\n", dirpath, dirname);
	return;
    }

    if (getcwd(origpath, sizeof(origpath)) == NULL) {
	printf("Unable to getcwd(%s): %s\n", dirname, strerror(errno));
	return;
    }
    if (chdir(dirname) != 0) {
	printf("Unable to chdir(%s)\n", dirname);
	return;
    }

    scanFiles(path, level, noLinks, dirs, 0);

    if (chdir(origpath) != 0) {
	printf("FATAL: Unable to return with chdir(%s)\n", dirname);
	exit(1);
    }
}

/*
 * scanFiles() - the two passes over the entries of the current directory,
 * whose path is path: first the over files (and, unless top, the
 * subdirectories, by scanDirectory()), then the temporary files of an
 * interrupted -R or -z and the data files, so that a group's over file is
 * expired before its data files are judged.  top: the current directory is
 * the group home itself (D-338); its subdirectories were scanned already
 * and are not entered, and of the other entries only the hierarchy names
 * (hierName()) and the temporary names tempRewriteName() accepts are
 * looked at; ProcessOverviewFile() acts on them only for groups whose
 * directory is the top.  Everything else there (an admin's files, a
 * directory named like a data file) is left as it is, unreported.
 * *level is never incremented, so the depth below the group home has no
 * limit (as on main).
 */
static void
scanFiles(const char *path, int *level, int noLinks, const DirFrame *dirs,
								int top)
{
    DIR *dir2;

    if ((dir2 = opendir(".")) != NULL) {
	den_t *den2;
	struct stat st;
	DirFrame sub;

	while ((den2 = readdir(dir2)) != NULL) {
	    if (strcmp(den2->d_name, ".") == 0 || strcmp(den2->d_name, "..") == 0)
		continue;
	    if (top && !hierName(den2->d_name))
		continue;
	    if (top && !topRegular(path, den2->d_name, 1))
		continue;
	    if (noLinks && EntryIsLink(den2->d_name)) {
		if (VerboseOpt > 0)
		    printf("Skipping symlink %s/%s: not followed below a top-level directory whose name does not start with an alphanumeric\n", path, den2->d_name);
		continue;
	    }
	    if (stat(den2->d_name, &st) == 0 && S_ISDIR(st.st_mode)) {
		if (top) {
		    /* scanned by ScanDirectories() or not ours */
		} else if (DirOnPath(dirs, &st)) {
		    if (VerboseOpt > 0)
			printf("Skipping directory loop %s/%s: already on the path from the group home\n", path, den2->d_name);
		} else if (*level <= 2) {
		    sub.df_Dev = st.st_dev;
		    sub.df_Ino = st.st_ino;
		    sub.df_Up = dirs;
		    scanDirectory(path, den2->d_name, level, noLinks, &sub);
		}
	    } else if (overFileType(den2->d_name) == 1)
		ProcessOverviewFile(path, den2->d_name, 1, top);
	    if (MustExit)
		exit(1);
	}
	rewinddir(dir2);
	while ((den2 = readdir(dir2)) != NULL) {
	    /*
	     * delete junk files from previously interrupted
	     * dexpireover -R
	     */
	    if (tempRewriteName(path, den2->d_name)) {
		/*
		 * a symlink was reported by the first pass; a directory
		 * is not a temporary file, whatever its name; at the top
		 * only a regular file, no link (D-338)
		 */
		if (top && !topRegular(path, den2->d_name, 1))
		    continue;
		if (!(noLinks && EntryIsLink(den2->d_name)) &&
		    !(stat(den2->d_name, &st) == 0 && S_ISDIR(st.st_mode)))
		    DeleteJunkFile(path, den2->d_name);
		continue;
	    }

	    /*
	     * process data. files
	     */
	    /*
	     * A directory is not a data file, though a name an administrator
	     * gave it can read as one (a real hierarchy directory is a
	     * group name component and never parses as "d.<n>.<group>").
	     */
	    if ((!top || (hierName(den2->d_name) &&
			  topRegular(path, den2->d_name, 0))) &&
		overFileType(den2->d_name) == 2 &&
		!(noLinks && EntryIsLink(den2->d_name)) &&
		!(stat(den2->d_name, &st) == 0 && S_ISDIR(st.st_mode)))
		ProcessOverviewFile(path, den2->d_name, 2, top);
	    if (MustExit)
		exit(1);
	}
	closedir(dir2);
    }
}

/*
 * hierName() - name parses as a hierarchy over or data file name (in the
 * configured layout's order, D-335), not as a crc or md5 one.
 */
static int
hierName(const char *name)
{
    char Hash[PATH_MAX];
    artno_t artBase = -1;
    int iter = 0;
    int ghtype = ExtractGroupHashInfo(name, DOpts.ReaderGroupHashMethod.gh_type,
						Hash, &artBase, &iter);

    return(ghtype != HASHGRP_NONE &&
	(ghtype & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) == HASHGRP_HIER);
}

/*
 * topRegular() - the entry name at the top of the group home (the current
 * directory, path) is a regular file and no symlink (lstat(), D-338).  Main
 * never looked at the files there, so as for the trees #515 added nothing
 * there is followed, opened or removed through a link (a dangling one
 * too), and nothing but a regular file is opened: a FIFO named like an
 * over file would block the open().  say: report a link with -v.
 */
static int
topRegular(const char *path, const char *name, int say)
{
    struct stat lst;

    if (lstat(name, &lst) != 0)
	return(0);
    if (S_ISLNK(lst.st_mode)) {
	if (say && VerboseOpt > 0)
	    printf("Skipping symlink %s/%s: not followed at the top of the group home\n", path, name);
	return(0);
    }
    return(S_ISREG(lst.st_mode));
}

/*
 * gzPairShared() - group X's compressed data name in its directory
 * ("<dir>/d.<n>.X.gz") is the plain data file of the group X.gz in either
 * hierarchy layout, and X.gz is in the active file; or group X.gz's plain
 * data name in its directory is X's compressed data in either layout, and X
 * is in the active file (D-325).  The active file itself is asked
 * (activeHas()), not the groups loaded: -w loads only the selected ones,
 * and hierGroupKnown() says yes to any name then, so every X of a
 * one-character last component (comp.lang.c) would be taken for a pair.
 * With plain hierarchy such an X and X.gz share a directory:
 * FindGroupDir() skips X's last component in the name X.gz, so both have
 * their files in one directory (e2e.x and e2e.x.gz in e2e/, o.1.2 and
 * o.1.2.gz in o.1/, x and x.gz at the top, D-338).  With hierarchy-full X
 * after a switch from plain hierarchy, whose X.gz files of the old layout
 * are still in X's directory (go-while's R2-F1 on #524, at the top and in
 * a subdirectory).  For the other layout the other group must have an over
 * file in its directory of that layout (hierOtherOver(), D-336: a leftover
 * of the switch or a dreaderd still on the old setting); in a spool never
 * switched (hierarchy-full: x at the top and x.gz in x/, e2e.x in e2e/ and
 * e2e.x.gz in e2e/x/) both are rewritten.  RewriteData() of X removes both
 * names "d.<n>.X" and "d.<n>.X.gz" in its directory, which emptied OVER of
 * X.gz; RewriteData() of X.gz reads "d.<n>.X.gz" as its plain data although
 * it may be X's compressed data.  So with -R neither group is rewritten.
 * The paths compared are those of GFName() below the group home, so a
 * group anywhere is judged alike.  The other group's name goes in pair.
 */
static int
gzPairShared(const Group *group, char *pair, size_t size)
{
    const char *g = group->gr_GroupName;
    size_t len = strlen(g);
    GroupHashType m[2];
    char mine[PATH_MAX];
    char other[PATH_MAX];
    int i;
    int n;

    if (DOpts.ReaderGroupHashMethod.gh_type != HASHGRP_HIER)
	return(0);			/* crc and md5 names hold a hash */
    m[0] = DOpts.ReaderGroupHashMethod;
    m[1] = DOpts.ReaderGroupHashMethod;
    m[1].gh_hierfull = !m[1].gh_hierfull;

    /* X: its compressed name in its directory, X.gz's plain data in m[i] */
    n = snprintf(mine, sizeof(mine), "%s.gz",
		GFName(g, GRPFTYPE_DATA, 0, 1, 0, &DOpts.ReaderGroupHashMethod));
    if (n > 0 && (size_t)n < sizeof(mine) &&
	(n = snprintf(other, sizeof(other), "%s.gz", g)) > 0 &&
	(size_t)n < sizeof(other)) {
	for (i = 0; i < 2; ++i) {
	    if (strcmp(mine, GFName(other, GRPFTYPE_DATA, 0, 1, 0, &m[i])) == 0 &&
		(i == 0 || hierOtherOver(other)) && activeHas(other)) {
		snprintf(pair, size, "%s", other);
		return(1);
	    }
	}
    }
    /* X.gz: its plain data in its directory, X's compressed name in m[i] */
    if (len > 3 && strcmp(g + len - 3, ".gz") == 0) {
	snprintf(mine, sizeof(mine), "%s",
		GFName(g, GRPFTYPE_DATA, 0, 1, 0, &DOpts.ReaderGroupHashMethod));
	snprintf(other, sizeof(other), "%.*s", (int)(len - 3), g);
	for (i = 0; i < 2; ++i) {
	    char z[PATH_MAX];

	    n = snprintf(z, sizeof(z), "%s.gz",
			GFName(other, GRPFTYPE_DATA, 0, 1, 0, &m[i]));
	    if (n > 0 && (size_t)n < sizeof(z) && strcmp(mine, z) == 0 &&
		(i == 0 || hierOtherOver(other)) && activeHas(other)) {
		snprintf(pair, size, "%s", other);
		return(1);
	    }
	}
    }
    return(0);
}

/*
 * dataRegular() - every data file name of group in dirPath for the articles
 * startNo..endNo, plain and ".gz", as RewriteData() opens and removes them,
 * is a regular file or not there (D-338, go-while's R2-F2): a FIFO there
 * blocked RewriteData()'s open() (on main too).  At the top (top) by
 * lstat(): no link, as for the scan there; below it by stat(), so a link to
 * a regular file is read and replaced by the rewrite, as on main.  Else 0,
 * with the first other one in bad.
 */
static int
dataRegular(const Group *group, const char *dirPath, artno_t startNo,
		artno_t endNo, int dataEntries, int top, char *bad, size_t size)
{
    artno_t artNo;
    struct stat lst;
    char path[PATH_MAX];
    int gz;

    if (dataEntries <= 0)
	dataEntries = OD_HARTS;
    for (artNo = startNo & ~((artno_t)dataEntries - 1); artNo <= endNo;
						artNo += dataEntries) {
	for (gz = 0; gz < 2; ++gz) {
	    snprintf(path, sizeof(path), "%s/%s%s", dirPath,
		    GFName(group->gr_GroupName, GRPFTYPE_DATA, artNo, 0,
				group->gr_Iter, &DOpts.ReaderGroupHashMethod),
		    gz ? ".gz" : "");
	    if ((top ? lstat(path, &lst) : stat(path, &lst)) == 0 &&
		!S_ISREG(lst.st_mode)) {
		snprintf(bad, size, "%s", path);
		return(0);
	    }
	}
    }
    return(1);
}

/*
 * dataOpen() - open the data file path for RewriteData(): O_NONBLOCK, and
 * only a regular file (fstat(); D-338, R2-F2).  A FIFO or another file
 * that dataRegular() did not see (it came after the check) gives -1 with
 * errno EINVAL, which stops the group's rewrite before anything is changed
 * ("Unable to open"), as for a file that cannot be read.
 */
static int
dataOpen(const char *path)
{
    struct stat st;
    int fd = open(path, O_RDONLY | O_NONBLOCK);

    if (fd >= 0 && (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))) {
	printf("%s is not a regular file\n", path);
	close(fd);
	errno = EINVAL;
	return(-1);
    }
    return(fd);
}

/*
 * topOverIrregular() - group's over file, when its directory is the top of
 * the group home, is there but no regular file (a link, a FIFO; lstat(),
 * D-338), with its path in bad.  0 for a group in a subdirectory.
 */
static int
topOverIrregular(const Group *group, char *bad, size_t size)
{
    const char *gf = GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 1,
				group->gr_Iter, &DOpts.ReaderGroupHashMethod);
    struct stat lst;

    if (strchr(gf, '/') != NULL)
	return(0);
    snprintf(bad, size, "%s/%s", PatExpand(GroupHomePat), gf);
    return(lstat(bad, &lst) == 0 && !S_ISREG(lst.st_mode));
}

/*
 * TopGone - the groups whose over file at the top of the group home this
 * run removed as the file of no group (its header named the group, D-338):
 * the evidence that such a group existed, by which its data files at the
 * top are removed in the second pass.  A data file name alone is no
 * evidence (an admin's d.0.junk~ is the data file name of a group junk~).
 */
static char **TopGone;
static int TopGoneCount;

static void
topGoneAdd(const char *name)
{
    char **n;

    if (topGone(name))
	return;
    if ((n = realloc(TopGone, (TopGoneCount + 1) * sizeof(*TopGone))) == NULL)
	return;			/* the data files then stay: no harm */
    TopGone = n;
    if ((TopGone[TopGoneCount] = strdup(name)) != NULL)
	++TopGoneCount;
}

static int
topGone(const char *name)
{
    int i;

    for (i = 0; i < TopGoneCount; ++i) {
	if (strcmp(TopGone[i], name) == 0)
	    return(1);
    }
    return(0);
}

/*
 * overFileType() - what a file name is, by parsing it as ProcessOverviewFile()
 * does: 1 = over file, 2 = data file, 0 = neither.  A substring test cannot
 * tell: the hierarchy names "o.<n>.<group>" and "d.<n>.<group>" hold the
 * group name, which may contain ".d." (the over file of e2e.d.q would be
 * taken for a data file as well) or ".o." (the data file of e2e.o.x would be
 * taken for an over file and opened read-write).  The crc and md5 names have
 * a fixed form, for which the parse and the old tests agree.  The parse
 * tries the names of the configured layout first (D-335: in a hierarchy
 * spool the data file "d.0.o.1.2" of the group o.1.2 also has the form of
 * an md5 over file).
 */
static int
overFileType(const char *name)
{
    char Hash[PATH_MAX];
    artno_t artBase = -1;
    int iter = 0;
    int ghtype;

    if ((ghtype = ExtractGroupHashInfo(name, DOpts.ReaderGroupHashMethod.gh_type, Hash, &artBase, &iter)) == HASHGRP_NONE)
	return(0);
    if (ghtype & HASHGRPTYPE_OVER)
	return(1);
    return((ghtype & HASHGRPTYPE_DATA) ? 2 : 0);
}

/*
 * tempNameIs() - want (a path below the group home) is the over or data file
 * name GFName() gives group (gftype, artBase, iter), in its directory, with
 * ".gz" added when gz, and nothing more.
 */
static int
tempNameIs(const char *group, int gftype, artno_t artBase, int iter, int gz,
							const char *want)
{
    const char *gf = GFName(group, gftype, artBase, 1, iter,
					&DOpts.ReaderGroupHashMethod);
    size_t len = strlen(gf);

    if (strncmp(gf, want, len) != 0)
	return(0);
    return(strcmp(want + len, gz ? ".gz" : "") == 0);
}

/*
 * tempRewriteName() - name, in the directory dirPath below the group home,
 * is a temporary file of dexpireover: a '.' followed by exactly the name of
 * an over or data file of the configured layout in that directory, as
 * RewriteData() and rewriteDataFile() create them (createTempFile(),
 * "." + GFName()), or by a data file name and ".gz", as diablo_minigzip()
 * writes the compressed copy of a data file:
 *
 *	crc		.over.<n>.<hash>[.<iter>]
 *			.data.<n>.<hash>[.<iter>][.gz]
 *	md5-32, md5-64	.<hash>.<iter>.o.<32|64>.<n>
 *			.<hash>.<iter>.d.<32|64>.<n>[.gz]
 *	hierarchy(-full) .o.<n>.<group>
 *			.d.<n>.<group>[.gz]
 *
 * An interrupted -R or -z leaves them behind (D-333: the md5 data and the
 * over temporaries of every layout were not matched before).  The name after
 * the '.' is parsed (ExtractGroupHashInfoGz()) and GFName() is asked for the
 * name of the file it reads as; only when that is the whole name, and the
 * directory is the one GFName() puts the file in, is it a temporary file.
 * The parse alone is not enough: it ignores what follows the fields it
 * reads (an admin's ".over.0.<hash>.backup" reads as an over file), and a
 * hierarchy name takes any rest as part of the group name
 * (".o.0.<group>~" or ".o.0.<group>.orig" reads as the over file of the
 * group "<group>~" or "<group>.orig", which can have the same directory).
 * So the group must be in the group table, in every layout (#519 r2): the
 * name GFName() gives is built from the name of a loaded group whose hash
 * (for hierarchy: the group name) is the one read, and both readings of a
 * hierarchy ".gz" name are tried (D-318).  A temporary file of a group that
 * is not in the table (removed from the active file, or not selected with
 * -w) is left.  No group file name starts with '.', so the group's own
 * files are never taken.  dirPath may be the group home itself (D-338):
 * then only a hierarchy name of a group whose directory is the top (GFName()
 * gives no directory) matches.
 */
static int
tempRewriteName(const char *dirPath, const char *name)
{
    char home[PATH_MAX];
    char want[PATH_MAX];
    char Hash[PATH_MAX];
    char HashZ[PATH_MAX];
    artno_t artBase = -1;
    int iter = 0;
    int ghtype;
    int gftype;
    int n;
    size_t homeLen;

    if (name[0] != '.')
	return(0);
    Hash[0] = 0;
    if ((ghtype = ExtractGroupHashInfoGz(name + 1, DOpts.ReaderGroupHashMethod.gh_type, Hash, HashZ, &artBase, &iter)) == HASHGRP_NONE)
	return(0);
    gftype = (ghtype & HASHGRPTYPE_OVER) ? GRPFTYPE_OVER : GRPFTYPE_DATA;

    /* the path GFName() gives with wantdir 1: "<dir below home>/<name>" */
    n = snprintf(home, sizeof(home), "%s", PatExpand(GroupHomePat));
    if (n < 0 || (size_t)n >= sizeof(home))
	return(0);
    homeLen = (size_t)n;
    if (strncmp(dirPath, home, homeLen) != 0)
	return(0);
    if (dirPath[homeLen] == 0) {
	/* the top of the group home (D-338): only hierarchy groups have files there */
	if ((ghtype & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) != HASHGRP_HIER)
	    return(0);
	n = snprintf(want, sizeof(want), "%s", name + 1);
    } else if (dirPath[homeLen] == '/')
	n = snprintf(want, sizeof(want), "%s/%s", dirPath + homeLen + 1, name + 1);
    else
	return(0);
    if (n < 0 || (size_t)n >= sizeof(want))
	return(0);

    if ((ghtype & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) == HASHGRP_HIER) {
	/* no iteration in the name; "d.<n>.X.gz" is X.gz's plain or X's .gz */
	if (tempOfGroup(Hash, gftype, artBase, 0, 0, want))
	    return(1);
	return(HashZ[0] != 0 && tempOfGroup(HashZ, gftype, artBase, 0, 1, want));
    }
    if (tempOfGroup(Hash, gftype, artBase, iter, 0, want))
	return(1);
    return(gftype == GRPFTYPE_DATA && tempOfGroup(Hash, gftype, artBase, iter, 1, want));
}

/*
 * tempOfGroup() - a group in the table has the hash Hash (gr_Hash, GFHash():
 * for hierarchy the group name) and tempNameIs() its file want.
 */
static int
tempOfGroup(const char *Hash, int gftype, artno_t artBase, int iter, int gz,
							const char *want)
{
    hash_t hv = hhash(Hash);
    Group *group;

    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0 &&
	    tempNameIs(group->gr_GroupName, gftype, artBase, iter, gz, want))
	    return(1);
    }
    return(0);
}

void
DeleteJunkFile(const char *dirPath, const char *name)
{
    char path[PATH_MAX];
    int n;

    n = snprintf(path, sizeof(path), "%s/%s", dirPath, name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
	fprintf(stderr, "Skipping overlong temporary-file pathname: %s/%s\n",
			dirPath, name);
	return;
    }
    if (VerboseOpt)
	printf("Removing old temp file: %s\n", path);
    if (ForReal)
	remove(path);
}

/*
 * ProcessOverviewFile() - process over. and data. files.  All over. files
 *			   are processed first. 
 *
 *	When processing over. files, we may resize the index array (-s)
 *	and/or cleanup the file (-R).
 *
 *	When processing data. files, we typically remove whole files. 
 *	If the -R option was used, however, we rewrite the files.  We can
 *	safely copy/rename-over data. files as long as we are able to
 *	lock the associated over. file.  If we are compressing (-z) then
 *	we need to be careful to avoid data files that further data may
 *	potentially be written to.
 */

void
ProcessOverviewFile(const char *dirPath, const char *name, int type, int top)
{
    artno_t artBase = -1;
    Group *group = NULL;
    Group *groupZ = NULL;
    char path[PATH_MAX];
    char Hash[PATH_MAX];
    char HashZ[PATH_MAX];
    int iter = 0;
    int hier;
    int otherDir = 0;
    int ghtype;
    OverHead oh;

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);

    if (DebugOpt > 0)
	printf("ProcessOverviewFile(%s)\n", path);

    bzero(Hash, sizeof(Hash));
    bzero(HashZ, sizeof(HashZ));

    if ((ghtype = ExtractGroupHashInfoGz(name, DOpts.ReaderGroupHashMethod.gh_type, Hash, HashZ, &artBase, &iter)) == HASHGRP_NONE)
	return;
    hier = ((ghtype & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) == HASHGRP_HIER);

    if (DebugOpt > 2)
	printf("File: %s  type=%d  artBase=%lld  iter=%d  Hash=%s\n",
						name, type, artBase, iter, Hash);

    /*
     * Not in the group table: before the file is taken as stale, look for
     * groups created since the table was read.  dreaderd may be writing
     * the files of such a group right now.
     *
     * A hierarchy data file "d.<n>.X.gz" is the plain data file of group
     * "X.gz" or the compressed one of group "X" (D-318): HashZ is "X".  It
     * is kept for as long as it is not stale for every one of the two groups
     * that exist, as in doverctl.  Either may have been created since the
     * scan, so a reading that is missing causes the rescan, also when the
     * other one was found.
     */
    /*
     * A hierarchy name is the file of a group only in the group's directory
     * (D-336), and the directory depends on the layout (hierDirClass()):
     *  - a reading whose group (in the table, or not known with -w) has the
     *    file's directory in the other hierarchy layout only leaves the file
     *    alone: it may be the live file of a dreaderd started before
     *    readergrouphash was changed, or a copy left by the switch (D-337).
     *    It is neither expired nor removed.  Except when the other reading
     *    is of the configured layout and the group of the first has no over
     *    file in its directory of the other layout (hierOtherOver()): then no
     *    dreaderd writes the file for it (one makes the over file first),
     *    and both readings stay (D-318).  With hierarchy-full that is X's
     *    compressed data "e2e/d.<n>.e2e.x.gz" beside a group e2e.x.gz, whose
     *    plain data has this path in plain hierarchy;
     *  - a reading whose group has its files elsewhere in both layouts, or
     *    in the other layout only but is no group, is dropped: "d.<n>.X.gz"
     *    in the directory of X alone is X's compressed data, in that of X.gz
     *    alone X.gz's plain data.  When both readings are of the directory
     *    (plain hierarchy: e2e.x and e2e.x.gz in e2e/), both stay (D-318).
     *    A file of no reading is the file of no group.
     */
    if (hier) {
	int c = hierDirClass(Hash, dirPath);
	int cZ = HashZ[0] ? hierDirClass(HashZ, dirPath) : HIERDIR_OFF;
	const char *other = NULL;

	if (c == HIERDIR_OTHER && hierGroupKnown(Hash, dirPath))
	    other = Hash;
	else if (cZ == HIERDIR_OTHER && hierGroupKnown(HashZ, dirPath))
	    other = HashZ;
	if (other != NULL && HashZ[0] &&
	    (other == Hash ? cZ : c) == HIERDIR_IN && !hierOtherOver(other)) {
	    /* no writer of the other layout: both readings, D-318 */
	    c = cZ = HIERDIR_IN;
	    other = NULL;
	}
	if (other != NULL) {
	    if (VerboseOpt)
		printf("file of %s in the directory of the other hierarchy layout, left alone: %s\n",
								other, path);
	    return;
	}
	/*
	 * At the top of the group home (D-338) a name of no group whose
	 * directory is the top is no file of ours: an admin's file, or the
	 * file of a group whose directory is a subdirectory (o.0.e2e.test),
	 * or that of the other layout for a group that is gone.  Left as it
	 * is, not removed as the file of no group.
	 */
	if (top && c != HIERDIR_IN && cZ != HIERDIR_IN) {
	    if (VerboseOpt > 1)
		printf("not a file of a group whose directory is the top of the group home, left as it is: %s\n", path);
	    return;
	}
	if (c != HIERDIR_IN && cZ == HIERDIR_IN)
	    snprintf(Hash, sizeof(Hash), "%s", HashZ);
	if (c != HIERDIR_IN || cZ != HIERDIR_IN)
	    HashZ[0] = 0;
	otherDir = (c != HIERDIR_IN && cZ != HIERDIR_IN);
    }
    if (!otherDir) {
	group = FindGroupByHash(Hash, iter, hier, dirPath);
	if (HashZ[0])
	    groupZ = FindGroupByHash(HashZ, iter, hier, dirPath);
    }
    if (!otherDir && (group == NULL || (HashZ[0] && groupZ == NULL)) &&
	Wild == NULL && rescanActive() > 0) {
	if (group == NULL)
	    group = FindGroupByHash(Hash, iter, hier, dirPath);
	if (HashZ[0] && groupZ == NULL)
	    groupZ = FindGroupByHash(HashZ, iter, hier, dirPath);
    }

    if (group == NULL && groupZ == NULL) {
	/*
	 * If we gave a wildcard, we can't remove stale groups because
	 * we do not have a full group list.
	 */

	/*
	 * Not for a hierarchy name: it has no iteration, and FindGroupByHash()
	 * finds any group of the name, so currentIter() could only find the
	 * group of a file in another directory (D-336), which no ITER changes.
	 */
	if (Wild == NULL && !hier && currentIter(Hash, iter)) {
	    if (VerboseOpt)
		printf("file of a group recreated or changing its iteration, left alone: %s\n", path);
	} else if (Wild == NULL && top && type == 1 && !overNamesGroup(path, Hash)) {
	    /*
	     * The top of the group home is where an admin keeps files too: an
	     * over file there is removed as the file of a group that is gone
	     * only when its header names that group (D-338), so a copy such
	     * as o.0.junk~ (the over file name of a group junk~, whose
	     * directory is the top too) stays.
	     */
	    if (VerboseOpt)
		printf("over file of no group at the top of the group home whose header does not name %s, left alone: %s\n", Hash, path);
	} else if (Wild == NULL && top && type == 2 && !topGone(Hash) &&
					!(HashZ[0] && topGone(HashZ))) {
	    /*
	     * A data file of no group at the top is removed only when this
	     * run removed that group's over file at the top (first pass): its
	     * header named the group, so the group existed.  Else the name is
	     * all there is, and an admin's d.0.junk~ stays (D-338).
	     */
	    if (VerboseOpt)
		printf("data file of no group at the top of the group home with no over file of that group, left alone: %s\n", path);
	} else if (Wild == NULL) {
	    ++BadGroups;
	    printf("Group for over file not found, removing %s\n", path);
	    /*
	     * At the top the group counts as removed (its data files go in the
	     * second pass) only when its over file is gone: when the remove()
	     * fails, the data files stay with it (D-338).  With -n as if it
	     * had worked, so that -n reports what a run would do.
	     */
	    if (ForReal && remove(path) != 0) {
		printf("Unable to remove %s: %s\n", path, strerror(errno));
	    } else if (top && type == 1)
		topGoneAdd(Hash);
	}
	return;
    }
    /*
     * With -w only the selected groups are loaded: of the two readings
     * the one that is not loaded may exist, and then this file may be
     * its, so it is not judged by the other one alone.
     */
    if (Wild != NULL && HashZ[0] && (group == NULL || groupZ == NULL)) {
	if (VerboseOpt)
	    printf("file of a group that -w did not select, left alone: %s\n", path);
	return;
    }
    if (group == NULL) {
	group = groupZ;
	groupZ = NULL;
    }
    ++GoodGroups;

    if (DebugOpt > 2)
	printf("Maps to group: %s\n", group->gr_GroupName);

    /* found missing while working on another of its files */
    if ((group->gr_State & (GRF_GONE | GRF_NEEDZLIB)) ||
	(groupZ != NULL && (groupZ->gr_State & (GRF_GONE | GRF_NEEDZLIB))))
	return;

    if (type == 1) {
	/*
	 * over. file	(fixed length file)
	 */
	int fd;
	int retry = 0;

again:
	if ((fd = open(path, O_RDWR)) >= 0) {
	    int r;
	    int OldDataEntries=0;
	    int fdLocked=0; /* offset 4 held exclusive (else shared) */
	    struct stat st;

	    /*
	     * The structural lock (offset 4) first, shared, as dreaderd
	     * takes it.  A resize or rewrite, here or in another dexpireover,
	     * holds it exclusive while it changes the file: the header, the
	     * map and the slot positions used below are good only while no
	     * one does.  The slot lock (offset 0, expireOverArt()) comes
	     * second, in dreaderd's order.  ResizeGroup() and RewriteData()
	     * turn the shared lock into an exclusive one; fcntl() does that
	     * in place, and when it fails we still hold the shared one.
	     * Each wait ends after -l seconds (-l0: no limit, as before).
	     * A file renamed over meanwhile (-R) is opened again.
	     */
	    nrsignal(SIGALRM, sigAlarm);
	    alarm(LockWaitTime);
	    if (hflock(fd, 4, XLOCK_SH|XLOCK_INTR) < 0) {
		nrsignal(SIGALRM, SIG_IGN);
		if (VerboseOpt)
		    printf("over file in use, skipped grp=%s\n",
						group->gr_GroupName);
		close(fd);
		return;
	    }
	    nrsignal(SIGALRM, SIG_IGN);
	    if (fstat(fd, &st) != 0 || st.st_nlink == 0) {
		close(fd);	/* drops the lock */
		if (++retry < 10)
		    goto again;
		printf("over file of %s keeps being replaced, skipped\n",
						group->gr_GroupName);
		return;
	    }

	    r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	    if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
		r = 0;
	    if (r && oh.oh_Version > 1 &&
				strcmp(oh.oh_Gname, group->gr_GroupName) != 0)
		r = 0;
	    if (r) {
		if (oh.oh_Version < 3)
		    OldDataEntries = OD_HARTS;
		else {
		    OldDataEntries = oh.oh_DataEntries;
		    if (OldDataEntries <= 0 ||
			((OldDataEntries ^ (OldDataEntries - 1)) != (OldDataEntries << 1) - 1))
			OldDataEntries = OD_HARTS;
		}
	    }

	    if (r) {
		int expireSecs;
		OverExpire save;
		int recLen;
		int gone = 0;
		int rewrite;		/* -R for this group */
		const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
                                                        KP_LOCK, &recLen);

		/*
		 * The group left the active file after the scan began
		 * (removed before or while we waited for its record):
		 * leave its files alone (close() drops the shared lock).
		 */
		if (rec == NULL) {
		    groupGone(group, "skipped");
		    close(fd);
		    return;
		}

		/*
		 * Refresh the EndNo
		 */
		group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE",
							NULL, "-1"), NULL, 10);
		KPDBUnlock(KDBActive, rec);

		if (group->gr_EndNo - group->gr_StartNo >= oh.oh_MaxArts) {
		    if (VerboseOpt)
			printf("adjust %s startno %lld to %lld (endno=%lld  maxarts=%d)\n",
					group->gr_GroupName,
					group->gr_StartNo,
					group->gr_EndNo - oh.oh_MaxArts + 1,
					group->gr_EndNo,
					oh.oh_MaxArts);
		    group->gr_StartNo = group->gr_EndNo - oh.oh_MaxArts + 1;
		    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
		}

#ifndef USE_ZLIB
		/*
		 * -R cannot read a .gz data file without zlib and would
		 * delete it: leave all files of the group as they are.
		 */
		if (RewriteDataOpt > 0 &&
		    gzipOnlyData(group, dirPath, group->gr_StartNo,
					group->gr_EndNo, OldDataEntries)) {
		    group->gr_State |= GRF_NEEDZLIB;
		    group->gr_State &= ~GRF_MODIFIED;	/* no NB write-back */
		    close(fd);
		    return;
		}
#endif

		/*
		 * -R: a group is not rewritten when it shares its data file
		 * names with X or X.gz (gzPairShared(), D-325: the rewrite of one
		 * would remove or misread the other's data; D-338 did this at
		 * the top only, now in every directory), and no group is when one
		 * of the data file names of its range is there but no regular
		 * file (dataRegular(): a FIFO would block RewriteData()'s open(),
		 * on main too; at the top also a link, which would be read and
		 * replaced).  Nothing of -R is done for it then, also not the
		 * forced resize; expiry is as without -R.
		 */
		rewrite = (RewriteDataOpt > 0);
		if (rewrite) {
		    char why[PATH_MAX];

		    if (gzPairShared(group, why, sizeof(why))) {
			if (VerboseOpt)
			    printf("%s: not rewritten: the group %s has its files in the same directory%s (D-325)\n",
						group->gr_GroupName, why,
						top ? ", the top of the group home" : "");
			rewrite = 0;
		    } else if (!dataRegular(group, dirPath, group->gr_StartNo,
				group->gr_EndNo, OldDataEntries, top, why, sizeof(why))) {
			if (VerboseOpt)
			    printf("%s: not rewritten: %s is not a regular file\n",
						group->gr_GroupName, why);
			rewrite = 0;
		    }
		}

		/*
		 * Figure out expire values
		 */
		expireSecs = GetOverExpire(group->gr_GroupName, &save);

		/*
		 * Make sure we never have a dataEntries value of zero
		 */
		if (save.oe_DataSize <= 0 ||
			((save.oe_DataSize ^ (save.oe_DataSize - 1)) != (save.oe_DataSize << 1) - 1))
		    save.oe_DataSize = OldDataEntries;
		
		/*
		 * Force regeneration of over. file if RewriteDataOpt, else
		 * only regenerate if ResizeOpt and a size differential.  Valid
		 * overview index sizes run in steps if the nearest higher
		 * power of 2 divided by 3.
		 *
		 */
		if (ResizeOpt > 0 || rewrite) {
		    int numArts = group->gr_EndNo - group->gr_StartNo + 1;
		    int maxArts = oh.oh_MaxArts;
		    int artAge;

		    /*
		     * Find out the age of the first article in the
		     * group and calculate an age factor based on this
		     * and the expire value in dexpire.ctl.
		     *
		     * We don't let the age factor get too large
		     * if the age of the first articles is less than an
		     * hour because this can lead to some bloated files.
		     * We also adjust the age factor if there are a
		     * large number of articles in the group and the
		     * age factor is low as we don't need a huge
		     * number of spare slots.
		     */
		    artAge = getFirstArtAge(group, fd, &oh);
		    if (FactorExpire && artAge > 0 && expireSecs > 0) {
			float ageFactor;

			if (artAge <= 0)
			    artAge = 1;
			ageFactor = (expireSecs * 1.0) / (artAge * 1.0);
			if (ageFactor < 1.5)
			    ageFactor = 1.5;
			if (numArts > 20000 && ageFactor == 1.5)
			    ageFactor = 1.3;
			if (artAge < 3600 && ageFactor > 50.0)
			    ageFactor = 50.0;
			if (numArts > 500 && ageFactor > 20.0)
			    ageFactor = 20.0;
			if (numArts > 5000 && ageFactor > 5.0)
			    ageFactor = 5.0;
			if (ageFactor > 400.0)
			    ageFactor = 400.0;
			numArts = ageFactor * numArts;
		    } else if (numArts < maxArts / 2)
			numArts = maxArts - nearestPower(maxArts) / 3;
		    else if (numArts > maxArts * 2 / 3)
			numArts = maxArts + nearestPower(maxArts) / 3;
		    else
			numArts = maxArts;

		    /*
		     * The minimum is somewhat contrived, but if we can
		     * fit the index file into a fragment for unused groups
		     * we save a considerable amount of space.
		     */

		    if (save.oe_MaxArts > 0 && numArts > save.oe_MaxArts)
			numArts = save.oe_MaxArts;
		    if (save.oe_MinArts >= 0 && numArts < save.oe_MinArts)
			numArts = save.oe_MinArts;

		    {
			int needResize = rewrite;
			int d = abs(maxArts - numArts);
			/*
			 * We only rewrite if there has been a radical
			 * change in the number of articles needed.
			 */
			if (numArts < 100 && d >= 10)
				needResize = 1;
			if (numArts >= 100 && numArts < 1000 && d > 20)
				needResize = 1;
			if (numArts >= 1000 && d > 200)
				needResize = 1;
			if (needResize &&
			    ResizeGroup(group, fd, &oh, numArts, OldDataEntries, &fdLocked) < 0)
			    gone = 1;
		    }
		}

		/*
		 * If the group left the active file while the resize waited
		 * for the over.* lock: no expire and no rewrite for it.
		 */
		if (gone == 0) {
		    if (UseExpireBySpool)
			ExpireBySpool(group, fd, &oh);
		    else if (UseExpireFromFile)
			ExpireFromFile(group, fd, &oh, expireSecs);
		    else if (UseExpireByDays)
			ExpireByDays(group, fd, &oh, expireSecs);

		    /*
		     * Rewrite data files associated with over. file if -R.
		     */
		    if (rewrite)
			(void)RewriteData(group, fd, &oh, dirPath, OldDataEntries, save.oe_DataSize, &fdLocked);
		}
	    } else {
		printf("group %s, file \"%s\" bad file header\n",
		    group->gr_GroupName,
		    path
		);
		if (oh.oh_Version > OH_VERSION)
		    printf("   expected version %d, got version %d\n",
						OH_VERSION, oh.oh_Version);
	    }
	    hflock(fd, 4, XLOCK_UN);
	    close(fd);
	}
    } else {
	/*
	 * data. file, modulo OD_HARTS.  Stale when it is for every group that
	 * has the file's name (two for a hierarchy ".gz" name, see above).
	 */
	int dataEntries;
	int stale;
	char why[PATH_MAX];

	/*
	 * At the top (D-338) the data file is judged by the header of its
	 * group's over file, which dataFileStale() opens by its name: that
	 * must be a regular file (a FIFO there would block the open(), a
	 * link would be read); else the data file is left as it is.
	 */
	if (top && (topOverIrregular(group, why, sizeof(why)) ||
		    (groupZ != NULL && topOverIrregular(groupZ, why, sizeof(why))))) {
	    if (VerboseOpt)
		printf("data file left alone: the over file %s of its group is not a regular file: %s\n",
								why, path);
	    return;
	}
	if ((stale = dataFileStale(group, artBase, path, &dataEntries)) < 0)
	    return;
	if (stale && groupZ != NULL &&
		(stale = dataFileStale(groupZ, artBase, path, NULL)) < 0)
	    return;
	if (stale) {
	    if (VerboseOpt)
		printf("Deleting stale overview data for %s: %s (artBase=%lld  StartNo=%lld  EndNo=%lld\n",
				group->gr_GroupName, path,
				artBase, group->gr_StartNo, group->gr_EndNo
		);
	    if (ForReal)
		remove(path);
	}

	/*
	 * check to see if we might want to compress this file; the per-file
	 * lines only with -v or -n (VerboseOpt is -1 without them)
	 */
	if (CompressDataOpt && strcmp(path + strlen(path) - 3, ".gz")) {
	    OverExpire save;
	    (void)GetOverExpire(group->gr_GroupName, &save);
	    if (DebugOpt)
		printf("No .gz on %s artBase %lld\n", path, artBase);
	    if (artBase >= group->gr_EndNo - (artno_t)save.oe_StoreGZMinFiles * dataEntries) {
		if (VerboseOpt > 0) {
		    printf("Overview %s not eligible for compression: proximity to end of data (%lld/%lld), %d files protected\n", path, artBase, group->gr_EndNo, save.oe_StoreGZMinFiles);
		}
	    } else {
		/* Eligible based on window, but is it eligible based on time? */
		struct stat st;
		char why[PATH_MAX + 256];
		if (stat(path, &st) == 0) {
		    if (st.st_mtime > time(NULL) - save.oe_StoreGZHours * 3600) {
		        if (VerboseOpt > 0) {
			    printf("Overview %s not eligible for compression: modified too recently\n", path);
		        }
		    } else if (st.st_size == 0) {
			/* no overview data (readerxover trackonly): nothing to compress */
		        if (VerboseOpt > 0) {
			    printf("Overview %s not eligible for compression: empty\n", path);
		        }
		    } else if (gzNameTaken(group, path, artBase, hier, why, sizeof(why))) {
			if (VerboseOpt > 0) {
			    printf("Overview %s not compressed: %s\n", path, why);
			}
		    } else {
		        if (VerboseOpt > 0) {
			    printf("Overview %s compressing\n", path);
		        }
			if (!ForReal) {
			    CompressEligible++;
			} else if (diablo_minigzip(path) == 0) {
			    CompressedFiles++;
			} else {
			    printf("Unable to compress %s\n", path);
			    CompressFailed++;
			}
		    }
		}
	    }
	}
    }
}

/*
 * gzNameTaken() - 1 when the plain hierarchy data file "path" of group "<G>"
 * must not be compressed, because its compressed name (path + ".gz") is also
 * the plain data file name of another group, "<G>.gz" (D-325).  The directory
 * of a group leaves out a last component of one character, so for a group
 * like e2e.x the files of e2e.x and e2e.x.gz lie in one directory, and
 * "d.<n>.e2e.x.gz" is the compressed data of the one and the plain data of
 * the other.  A gzip file on that name is taken by the group "<G>.gz"
 * (dreaderd) for its own plain data, from the day it exists: not only a group
 * that exists now, but one made later.  So in this layout the file is never
 * compressed, whether the group "<G>.gz" exists, is loaded (-w) or the
 * compressed file exists.  Only when the two names are the same path
 * (GFName() of "<G>.gz" equals the compressed name; with e2e.foo and
 * e2e.foo.gz, in e2e/ and e2e/foo/, they are not) is the file kept plain;
 * otherwise 0.  A name too long to check counts as taken.  The reason goes
 * in why.
 */
static int
gzNameTaken(Group *group, const char *path, artno_t artBase, int hier,
		char *why, size_t whySize)
{
    char zname[PATH_MAX];
    char plain[PATH_MAX];
    char zplain[PATH_MAX];
    int n;

    if (!hier)
	return(0);
    n = snprintf(zname, sizeof(zname), "%s.gz", group->gr_GroupName);
    if (n < 0 || (size_t)n >= sizeof(zname) - 64) {
	snprintf(why, whySize, "group name too long to check for a group %s.gz", group->gr_GroupName);
	return(1);
    }
    n = snprintf(plain, sizeof(plain), "%s.gz", GFName(group->gr_GroupName,
			GRPFTYPE_DATA, artBase, 1, group->gr_Iter,
			&DOpts.ReaderGroupHashMethod));
    if (n < 0 || (size_t)n >= sizeof(plain)) {
	snprintf(why, whySize, "name too long");
	return(1);
    }
    n = snprintf(zplain, sizeof(zplain), "%s", GFName(zname,
			GRPFTYPE_DATA, artBase, 1, group->gr_Iter,
			&DOpts.ReaderGroupHashMethod));
    if (n < 0 || (size_t)n >= sizeof(zplain)) {
	snprintf(why, whySize, "name too long");
	return(1);
    }
    if (strcmp(plain, zplain) != 0)
	return(0);			/* a directory of its own: no collision */

    snprintf(why, whySize, "%s.gz is also the data file name of the group %s (D-325)", path, zname);
    return(1);
}

/*
 * dataFileStale() - the data file path (artBase) is outside the range of
 * group, or is not aligned to its over file's slots; 1 = stale, 0 = not,
 * -1 = the group left the active file after the scan began (skip the file).
 * The data entries per file come from the header of the group's over file,
 * which is opened by its path from the group home: the working directory is
 * the directory of the file being checked (scanDirectory() changes into it),
 * and the over file of the group may be in another one (the compressed
 * reading of a hierarchy ".gz" data file, whose group "X" has its files in
 * the directory of X, not in that of the file).  OD_HARTS is used when that
 * file cannot be read or gives no valid value: it is only a guess, and the
 * wrong one for a group with another number of data entries (dexpire.ctl
 * "e"), which then can judge a live data file stale, so the over file must
 * be found.  The data entries are returned in *dataEntries.
 */
static int
dataFileStale(Group *group, artno_t artBase, const char *path, int *dataEntries)
{
    int recLen;
    int de;
    int n;
    char ovname[PATH_MAX];
    char ovpath[PATH_MAX];
    const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
                                                        KP_LOCK, &recLen);

    /* the group left the active file after the scan began: skip it */
    if (rec == NULL) {
	groupGone(group, "skipped");
	return(-1);
    }
    /*
     * Refresh the EndNo
     */
    group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE",
							NULL, "-1"), NULL, 10);
    KPDBUnlock(KDBActive, rec);

    /*
     * GFName() with wantdir 1 is the path below the group home for every
     * layout (crc "ab/over.0.<hash>", md5 and hierarchy as well); wantdir 2
     * puts the group home in front for crc and md5, but not for the
     * hierarchy layout, whose path stays relative to the group home and so
     * does not open from the directory scanDirectory() is in.
     */
    snprintf(ovname, sizeof(ovname), "%s",
			GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 1,
				group->gr_Iter,
				&DOpts.ReaderGroupHashMethod));
    n = snprintf(ovpath, sizeof(ovpath), "%s/%s", PatExpand(GroupHomePat),
								ovname);
    if (n < 0 || (size_t)n >= sizeof(ovpath))
	de = OD_HARTS;
    else
	de = getDataEntries(ovpath);
    if (de <= 0 || ((de ^ (de - 1)) != (de << 1) - 1))
	de = OD_HARTS;
    if (dataEntries)
	*dataEntries = de;

    /* data files have been rewritten if DataEntries has been changed */
    if (artBase + de <= group->gr_StartNo ||
	artBase >= group->gr_EndNo + de ||
	group->gr_StartNo > group->gr_EndNo ||
	(artBase & (de-1))
    ) {
	if (DebugOpt)
	    printf("%s artBase=%lld oh_DataEntries=%d\n",
			    group->gr_GroupName, artBase, de);
	return(1);
    }
    return(0);
}

int
getFirstArtAge(Group *group, int fd, OverHead *oh)
{
    const OverArt *oaBase;
    struct stat st;
    long long n;
    time_t t = time(NULL);
    int dt = 0;

    if (fstat(fd, &st) != 0)
	return(-1);

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
    if (oh->oh_MaxArts <= 0 || n < oh->oh_MaxArts) {
	fprintf(stderr, "Over file of group %s has no article slots (damaged header?) - skipped\n", group->gr_GroupName);
	return(-1);
    }

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd,
			oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in getFirstArtAge (%s)\n",
			group->gr_GroupName, n, strerror(errno));
	return(-1);
    }

    {
        long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % oh->oh_MaxArts;
	const OverArt *oa = &oaBase[i];

	/*
	 * Get the age of the first valid article
	 */
	if (oa && i <= n)
	    dt = (int)(t - oa->oa_TimeRcvd);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
    return(dt);
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
EnterGroup(const char *groupName, artno_t begNo, artno_t endNo, int lmts, int cts, int iter, const char *flags)
{
    hash_t hv = hhash(GFHash(groupName, &DOpts.ReaderGroupHashMethod));
    Group **pgroup = &GHash[hv.h1 & GHMASK];
    Group *group;

    while ((group = *pgroup) != NULL) {
	if (strcmp(groupName, group->gr_GroupName) == 0)
	    break;
	pgroup = &group->gr_Next;
    }
    if (group == NULL) {
	*pgroup = group = calloc(sizeof(Group) + strlen(groupName) + 1, 1);
	group->gr_State = GRF_NEW;
	group->gr_GroupName = (char *)(group + 1);
	group->gr_Hash = strdup(GFHash(groupName, &DOpts.ReaderGroupHashMethod));
	group->gr_Iter = iter;
	strcpy(group->gr_GroupName, groupName);
    }

    /*
     * update fields
     */
    if (begNo >= 0) {
	group->gr_State |= GRF_STARTNO;
	if (group->gr_StartNo != begNo) {
	    group->gr_State |= GRF_MODIFIED;
	    group->gr_StartNo = begNo;
	}
    }
    if (endNo >= 0) {
	group->gr_State |= GRF_ENDNO;
	if (endNo < group->gr_EndNo) {
	    printf("*** Would be adjusting NE down - not doing it ***\n");
	    printf("adjust %s NE from %lld to %lld\n", group->gr_GroupName,
				endNo, group->gr_EndNo);
	} else if (group->gr_EndNo != endNo) {
	    group->gr_EndNo = endNo;
	    group->gr_State |= GRF_MODIFIED;
	}
    }
    group->gr_LMTS = lmts;

    if (cts) {
	group->gr_CTS = cts;
    }

    if (flags) {
	group->gr_State |= GRF_FLAGS;
	if (SetField(&group->gr_Flags, flags))
	    group->gr_State |= GRF_MODIFIED;
    }
    return(group);
}

/*
 * currentIter() - the file of iteration iter for Hash belongs to no group in
 * the table, but a group we know by Hash may be taking it right now: it was
 * removed and created again during the run (its new record may also have
 * replaced the old one in place, which rescanActive() does not see), or it
 * changes its iteration: a group whose hash is already used by another gets
 * the next iteration, and dreaderd makes the files of the new iteration first
 * and writes ITER into dactive.kp only afterwards (D-319).  So a file of the
 * group's current iteration or of a higher one can be that group's, and the
 * table, read before, has the old iteration.  Files of a lower iteration than
 * the group has now stay stale.  The same rule as IterInProgress() in
 * doverctl.  ITER is read from the live record, not from the table.
 */
static int
currentIter(char *Hash, int iter)
{
    Group *group;
    hash_t hv = hhash(Hash);

    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0) {
	    int recLen;
	    int cur;
	    const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
							KP_LOCK, &recLen);

	    if (rec == NULL)
		continue;
	    cur = (int)strtoul(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);
	    KPDBUnlock(KDBActive, rec);
	    if (cur <= iter)
		return(1);
	}
    }
    return(0);
}

/*
 * OverFileOwner() - who the over file of iteration iter for group in dirPath
 * belongs to by its header: 1 = names group, 0 = no such file, an unreadable
 * header or another group's name, -1 = a header of version 1, which has no
 * name.
 */
static int
OverFileOwner(const Group *group, int iter, const char *dirPath)
{
    char ovpath[PATH_MAX];
    OverHead oh;
    int fd;
    int r = 0;
    int n;

    n = snprintf(ovpath, sizeof(ovpath), "%s/%s", dirPath,
			GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 0, iter,
						&DOpts.ReaderGroupHashMethod));
    if (n < 0 || (size_t)n >= sizeof(ovpath))
	return(0);
    if ((fd = open(ovpath, O_RDONLY)) >= 0) {
	if (read(fd, &oh, sizeof(oh)) == sizeof(oh) &&
	    oh.oh_Version >= 1 && oh.oh_Version <= OH_VERSION &&
	    oh.oh_ByteOrder == OH_BYTEORDER) {
	    if (oh.oh_Version == 1)
		r = -1;
	    else
		r = (strncmp(oh.oh_Gname, group->gr_GroupName,
						sizeof(oh.oh_Gname)) == 0);
	}
	close(fd);
    }
    return(r);
}

/*
 * hierDirOf() - the directory below the group home of the files of the group
 * name in the hierarchy layout method, as GFName() (wantdir 1) puts them,
 * without the trailing '/' ("" for a group of one component).  0 when the
 * path does not fit.
 */
static int
hierDirOf(const char *name, GroupHashType *method, char *dir, size_t size)
{
    const char *gf = GFName(name, GRPFTYPE_OVER, 0, 1, 0, method);
    const char *slash = strrchr(gf, '/');
    size_t len = slash ? (size_t)(slash - gf) : 0;

    if (len >= size)
	return(0);
    memcpy(dir, gf, len);
    dir[len] = 0;
    return(1);
}

/*
 * hierDirClass() - where a hierarchy file in dirPath is for the group name
 * (D-336).  A hierarchy name holds the whole group name, but hierarchy and
 * hierarchy-full put the files of many groups in different directories
 * (e2e.x.gz: e2e/ or e2e/x/, a.b.c: a.b/ or a/b/), and the two readings of a
 * data name "d.<n>.X.gz" (the plain data of X.gz, the compressed data of X,
 * D-318) can be in different directories.  dirPath is compared below the
 * group home as scanDirectory() built it from the names it read, links not
 * resolved (a linked top-level directory "e2e" -> elsewhere is "e2e"), with
 * the directory of the group in the configured layout and in the other
 * hierarchy layout (gh_hierfull flipped); the directory follows from the
 * name and the layout only, so the group need not be loaded.  Also the same
 * directory by stat() counts (a link below the group home, a file system
 * that ignores case).
 *	HIERDIR_IN	the group's directory in the configured layout, or
 *			not known (dirPath not below the group home, a path
 *			too long): the file can be the group's, as before
 *	HIERDIR_OTHER	the group's directory in the other hierarchy layout
 *			only: a copy left by a switch made by editing
 *			readergrouphash, or the live file of a dreaderd
 *			started before the change (it reads readergrouphash
 *			only when it starts)
 *	HIERDIR_OFF	neither layout puts the group's files there
 * The same as that of doverctl.
 */
static int
hierDirClass(const char *name, const char *dirPath)
{
    GroupHashType other = DOpts.ReaderGroupHashMethod;
    char home[PATH_MAX];
    char dir[PATH_MAX];
    char path[PATH_MAX];
    const char *rel;
    struct stat st;
    struct stat gst;
    size_t homeLen;
    int haveSt;
    int pass;
    int n;

    if (DOpts.ReaderGroupHashMethod.gh_type != HASHGRP_HIER)
	return(HIERDIR_IN);	/* a hierarchy name in a crc or md5 spool */
    n = snprintf(home, sizeof(home), "%s", PatExpand(GroupHomePat));
    if (n < 0 || (size_t)n >= sizeof(home))
	return(HIERDIR_IN);
    homeLen = (size_t)n;
    if (strncmp(dirPath, home, homeLen) != 0)
	return(HIERDIR_IN);
    if (dirPath[homeLen] == 0)
	rel = "";
    else if (dirPath[homeLen] == '/')
	rel = dirPath + homeLen + 1;
    else
	return(HIERDIR_IN);
    haveSt = (stat(dirPath, &st) == 0);
    other.gh_hierfull = !other.gh_hierfull;
    for (pass = 0; pass < 2; ++pass) {
	if (!hierDirOf(name, pass ? &other : &DOpts.ReaderGroupHashMethod,
							dir, sizeof(dir)))
	    return(HIERDIR_IN);
	n = snprintf(path, sizeof(path), "%s/%s", home, dir);
	if (n < 0 || (size_t)n >= sizeof(path))
	    return(HIERDIR_IN);
	if (strcmp(rel, dir) == 0 ||
	    (haveSt && stat(path, &gst) == 0 &&
	     st.st_dev == gst.st_dev && st.st_ino == gst.st_ino))
	    return(pass ? HIERDIR_OTHER : HIERDIR_IN);
    }
    if (VerboseOpt > 1)
	printf("files named for %s in %s are not that group's: not its directory in either hierarchy layout\n",
							name, dirPath);
    return(HIERDIR_OFF);
}

/*
 * activeHas() - the group name is in the active file now (read from the
 * file, so also a group that -w did not load or that was created since the
 * scan).
 */
static int
activeHas(const char *name)
{
    int recLen;
    const char *rec = KPDBReadRecord(KDBActive, name, KP_LOCK, &recLen);

    if (rec == NULL)
	return(0);
    KPDBUnlock(KDBActive, rec);
    return(1);
}

/*
 * hierGroupKnown() - the group name may exist: it is in the table, or was
 * created since it was read (rescanActive()), or -w did not load every
 * group.
 */
static int
hierGroupKnown(const char *name, const char *dirPath)
{
    if (Wild != NULL || FindGroupByHash((char *)name, 0, 1, dirPath) != NULL)
	return(1);
    return(rescanActive() > 0 && FindGroupByHash((char *)name, 0, 1, dirPath) != NULL);
}

/*
 * hierOtherOver() - the group name has an over file in its directory of the other
 * hierarchy layout (gh_hierfull flipped), a regular file that is not its
 * over file of the configured layout (another directory, by name and by
 * stat()).  Over file names are those of one group, and dreaderd makes a
 * group's over file before any of its data files, so in a spool that was
 * never switched this is never so: only a leftover of a switch made by
 * editing readergrouphash, a copy, or a dreaderd started before the change,
 * which still writes there, makes it (D-336, D-337).  When it cannot be
 * told (a path too long, stat() fails other than with ENOENT), 1.
 */
static int
hierOtherOver(const char *name)
{
    GroupHashType other = DOpts.ReaderGroupHashMethod;
    char opath[PATH_MAX];
    char path[PATH_MAX];
    struct stat st;
    struct stat ost;
    int n;

    if (DOpts.ReaderGroupHashMethod.gh_type != HASHGRP_HIER)
	return(0);
    other.gh_hierfull = !other.gh_hierfull;
    n = snprintf(opath, sizeof(opath), "%s/%s", PatExpand(GroupHomePat),
			GFName(name, GRPFTYPE_OVER, 0, 1, 0, &other));
    if (n < 0 || (size_t)n >= sizeof(opath))
	return(1);
    n = snprintf(path, sizeof(path), "%s/%s", PatExpand(GroupHomePat),
			GFName(name, GRPFTYPE_OVER, 0, 1, 0,
					&DOpts.ReaderGroupHashMethod));
    if (n < 0 || (size_t)n >= sizeof(path))
	return(1);
    if (strcmp(opath, path) == 0)
	return(0);			/* the same directory in both layouts */
    if (stat(opath, &ost) != 0)
	return(errno != ENOENT);
    if (!S_ISREG(ost.st_mode))
	return(0);
    if (stat(path, &st) == 0 && st.st_dev == ost.st_dev && st.st_ino == ost.st_ino)
	return(0);			/* the configured one, by a link */
    return(1);
}

/*
 * overNamesGroup() - the over file path has a header of version 2 or later
 * that names the group name (D-338).  A header of version 1 names no group,
 * so it does not tell: 0, as for a file that cannot be read.
 */
static int
overNamesGroup(const char *path, const char *name)
{
    OverHead oh;
    int fd;
    int r = 0;

    /* O_NONBLOCK: a FIFO must not block (the caller checks for a regular file) */
    if ((fd = open(path, O_RDONLY | O_NONBLOCK)) >= 0) {
	if (read(fd, &oh, sizeof(oh)) == sizeof(oh) &&
	    oh.oh_Version >= 2 && oh.oh_Version <= OH_VERSION &&
	    oh.oh_ByteOrder == OH_BYTEORDER &&
	    strncmp(oh.oh_Gname, name, sizeof(oh.oh_Gname)) == 0 &&
	    strlen(name) < sizeof(oh.oh_Gname))
	    r = 1;
	close(fd);
    }
    return(r);
}

/*
 * FindGroupByHash() - the group a file in dirPath belongs to.  The hash in a
 * file name does not tell groups apart whose names have the same hash: the
 * iteration does, but a group that has no over file yet has iteration 0 like
 * the group that holds the hash.  Of several groups with this hash and
 * iteration the one whose over file names it is the owner of the files
 * (D-313); else the first, as before.  hier: the file name is a hierarchy
 * name, which has no iteration, so the group's ITER (it keeps the iteration
 * of its files for a conversion back to crc or md5) is not compared (D-314).
 * With -w only the groups that match are loaded, so a file of a group that
 * does not match can have one candidate (the empty group that shares its
 * hash) or several (empty groups that share it): one of them must own the over
 * file, else no group is returned and the file is left alone
 * (ProcessOverviewFile() removes nothing under -w, D-313, D-322).  A header of
 * version 1 has no name, so it does not tell that the owner is not loaded:
 * the first candidate is taken as before.  Hierarchy names are made of the
 * group name, so they have no such twin and no header check (the directory
 * is checked before, hierDirClass(), D-336).
 * The rest is the same as in doverctl, which loads every group.
 */
Group *
FindGroupByHash(char *Hash, int iter, int hier, const char *dirPath)
{
    Group *group;
    Group *first = NULL;
    hash_t hv = hhash(Hash);
    int n = 0;
    int v1 = 0;

    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0 && (hier || group->gr_Iter == iter)) {
	    if (first == NULL)
		first = group;
	    ++n;
	}
    }
    /*
     * Without -w every group is loaded: one candidate is the owner, and with
     * several the over file tells.  With -w the owner may not be loaded, so
     * one or several candidates must be named by the over file, else the
     * file is left alone.
     */
    if (n == 0 || (n == 1 && (Wild == NULL || hier)))
	return(first);
    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0 && (hier || group->gr_Iter == iter)) {
	    int r = OverFileOwner(group, iter, dirPath);

	    if (r == 1)
		return(group);
	    if (r < 0)
		v1 = 1;
	}
    }
    if (Wild != NULL && !hier && !v1) {
	if (VerboseOpt)
	    printf("files of hash %s iteration %d in %s left alone: with -w the over file does not name a selected group (missing, damaged or another group's)\n",
							Hash, iter, dirPath);
	return(NULL);
    }
    return(first);
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

/*
 * Scan overview records from beginning article to ending article
 */

void
ExpireByDays(Group *group, int fd, OverHead *oh, int expireSecs)
{
    const OverArt *oaBase;
    struct stat st;
    int count = 0;
    int jumped = 0;
    int expired = 0;
    int canceled = 0;
    int stale = 0;
    long long n;
    time_t t = time(NULL);

    if (fstat(fd, &st) != 0)
	return;

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
    if (oh->oh_MaxArts <= 0 || n < oh->oh_MaxArts) {
	fprintf(stderr, "Over file of group %s has no article slots (damaged header?) - skipped\n", group->gr_GroupName);
	return;
    }

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in ExpireByDays (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    /*
     * Delete expired overview
     */

    {
	long long i;

	for (i = 0; i < n; ++i) {
	    const OverArt *oa = &oaBase[i];
	    int expiredart = 0;
	    int artno = oa->oa_ArtNo;

	    if (VerboseOpt > 3) {
		printf("entry %lld artNo %d\n", i, artno);
	    }
	    if (OA_ARTVALID(oa)) {
		hash_t mh = oa->oa_MsgHash;
		int rcvd = oa->oa_TimeRcvd;
		int dt = (int)(t - rcvd);

		expiredart = 0;
		if (expireSecs > 0 && 
		    (dt > expireSecs || dt < -(60 * 60 * 24))
		) {
		    expiredart = 1;
		    if (ForReal)
			expireOverArt(fd, (off_t)oh->oh_HeadSize + i * sizeof(OverArt), &mh, rcvd);
		    ++count;
		}

		if (VerboseOpt > 2)
		    printf("DT %d/%d %08lx %08lx %s\n", dt, expireSecs, (long)t, (long)oa->oa_TimeRcvd, expiredart ? "(expired)" : "(kept)");

	    }
	}
    }

    {
	/*
	 * Update active file begin sequence number
	 */
	while (group->gr_StartNo <= group->gr_EndNo) {
	    long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % n;
	    const OverArt *oa = &oaBase[i];

	    if (VerboseOpt > 2)
		printf("test %d vs %lld (i = %lld)\n", oa->oa_ArtNo, group->gr_StartNo, i);
	    if (OA_ARTNOEQ(group->gr_StartNo, oa->oa_ArtNo))
		break;
	    ++group->gr_StartNo;
	    switch(oa->oa_ArtNo) {
	    case -2:
		++expired;
		break;
	    case -1:
		++canceled;
		break;
	    default:
		++stale;
		break;
	    }
	    ++jumped;
	}
	if (jumped)
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
    }
    if (VerboseOpt && (jumped || count)) {
	printf("expired %-4d NB += %-4d (%3d can, %3d stale, %3d exprd) left %-4d expires in %6.2f days, grp=%s\n", 
	    count,
	    jumped,
	    canceled, stale, expired,
	    (int)(group->gr_EndNo - group->gr_StartNo + 1),
	    ((expireSecs>0) ? (double)expireSecs / (60.0 * 60.0 * 24.0) :-1.0),
	    group->gr_GroupName
	);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
}

/*
 * Scan overview records from beginning article to ending article
 *
 * Expire by checking the history file for the expired bit
 */

void
ExpireBySpool(Group *group, int fd, OverHead *oh)
{
    const OverArt *oaBase;
    struct stat st;
    int count = 0;
    int jumped = 0;
    int expired = 0;
    int canceled = 0;
    int stale = 0;
    long long n;

    if (fstat(fd, &st) != 0)
	return;

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
    if (oh->oh_MaxArts <= 0 || n < oh->oh_MaxArts) {
	fprintf(stderr, "Over file of group %s has no article slots (damaged header?) - skipped\n", group->gr_GroupName);
	return;
    }

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in ExpireBySpool (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    /*
     * Delete expired overview
     */

    {
	long long i;

	for (i = 0; i < n; ++i) {
	    const OverArt *oa = &oaBase[i];

	    if (oa && OA_ARTVALID(oa)) {
		hash_t dh = oa->oa_MsgHash;
		int rcvd = oa->oa_TimeRcvd;
		History dh_lookup;

		/*
		 * Make sure that the history entry exists. It's possible
		 * that dexpire already removed the article, and dhistory
		 * was cleaned, so the msgID doesn't exist.
		 *
		 * If the article does not exist or is expired, then expire
		 * its overview entry as well.
		 */
		if ((HistoryLookupByHash(dh, &dh_lookup) == -1) ||
						H_EXPIRED(dh_lookup.exp)) {
		    if (ForReal)
			expireOverArt(fd, (off_t)oh->oh_HeadSize + i * sizeof(OverArt), &dh, rcvd);
		    ++count;
		}
	    }
	}
    }

    {
	/*
	 * Update history file begin sequence number
	 */
	while (group->gr_StartNo <= group->gr_EndNo) {
	    long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % n;
	    const OverArt *oa = &oaBase[i];

	    if (VerboseOpt > 2)
		printf("test %d vs %lld (i = %lld)\n", oa->oa_ArtNo, group->gr_StartNo, i);
	    if (OA_ARTNOEQ(group->gr_StartNo, oa->oa_ArtNo))
		break;
	    ++group->gr_StartNo;
	    switch(oa->oa_ArtNo) {
	    case -2:
		++expired;
		break;
	    case -1:
		++canceled;
		break;
	    default:
		++stale;
		break;
	    }
	    ++jumped;
	}
	if (jumped)
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
    }
    if (VerboseOpt && (jumped || count)) {
	printf("expired %-4d NB += %-4d (%3d can, %3d stale, %3d exprd) left %-4d expires by spool, grp=%s\n", 
	    count,
	    jumped,
	    canceled, stale, expired,
	    (int)(group->gr_EndNo - group->gr_StartNo + 1),
	    group->gr_GroupName
	);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
}

/*
 * Similar to ExpireBySpool but uses a file generated by dexpire as a list
 * of msgid hashes of messages which are expired.
 * 
 * The 'x' parameter in dexpire.ctl is also checked, to punt articles which
 * have been laying in overview for a long time and somehow escaped being
 * written to the dexpover.dat file.
 */

void
ExpireFromFile(Group *group, int fd, OverHead *oh, int expireSecs)
{
    const OverArt *oaBase;
    struct stat st;
    int count = 0;
    int jumped = 0;
    int expired = 0;
    int canceled = 0;
    int stale = 0;
    long long n;
    time_t t = time(NULL);

    if (fstat(fd, &st) != 0)
	return;

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
    if (oh->oh_MaxArts <= 0 || n < oh->oh_MaxArts) {
	fprintf(stderr, "Over file of group %s has no article slots (damaged header?) - skipped\n", group->gr_GroupName);
	return;
    }

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in ExpireFromFile (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    /*
     * Delete expired overview
     */

    {
	long long i;

	for (i = 0; i < n; ++i) {
	    const OverArt *oa = &oaBase[i];

	    if (oa && OA_ARTVALID(oa)) {
		hash_t mh = oa->oa_MsgHash;
		int rcvd = oa->oa_TimeRcvd;
		int dt = (int)(t - rcvd);
		hash_t *hv = &mh;

		if (VerboseOpt > 2)
		    printf("DT %d/%d %08lx %08lx\n", dt, expireSecs, (long)t, (long)rcvd);

		if ((expOverListCheckExpired(hv) == 0) ||
		    ((expireSecs > 0) && 
		     (dt > expireSecs || dt < -(60 * 60 * 24)))
		) {
		    if (ForReal)
			expireOverArt(fd, (off_t)oh->oh_HeadSize + i * sizeof(OverArt), &mh, rcvd);
		    ++count;
		}
	    }
	}
    }

    {
	/*
	 * Update history file begin sequence number
	 */
	while (group->gr_StartNo <= group->gr_EndNo) {
	    long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % n;
	    const OverArt *oa = &oaBase[i];

	    if (VerboseOpt > 2)
		printf("test %d vs %lld (i = %lld)\n", oa->oa_ArtNo, group->gr_StartNo, i);
	    if (OA_ARTNOEQ(group->gr_StartNo, oa->oa_ArtNo))
		break;
	    ++group->gr_StartNo;
	    switch(oa->oa_ArtNo) {
	    case -2:
		++expired;
		break;
	    case -1:
		++canceled;
		break;
	    default:
		++stale;
		break;
	    }
	    ++jumped;
	}
	if (jumped)
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
    }
    if (VerboseOpt && (jumped || count)) {
	printf("expired %-4d NB += %-4d (%3d can, %3d stale, %3d exprd) left %-4d expires in %6.2f days, grp=%s\n", 
	    count,
	    jumped,
	    canceled, stale, expired,
	    (int)(group->gr_EndNo - group->gr_StartNo + 1),
	    ((expireSecs>0) ? (double)expireSecs / (60.0 * 60.0 * 24.0) :-1.0),
	    group->gr_GroupName
	);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
}

/*
 * Rewrite the data.* files associated with an over. file
 */
int
RewriteData(Group *group, int fd, OverHead *oh, const char *dirPath, int oldDataEntries, int newDataEntries, int *fdl)
{
    const OverArt *oaBase;
    OverArt *oaBaseNew;
    struct stat st;
    artno_t startNo;
    artno_t endNo;
    artno_t artNo;
    int rblt;
    long long n;
    int ok = 1;
    char ovPath1[PATH_MAX];
    char ovPath2[PATH_MAX];
#ifdef USE_ZLIB
    int tmpfd;
    unsigned char *zmap, *tmpmap, *zpos;
    size_t hl;
    unsigned long zlen;
    z_stream z;
    int code;
#endif

    /*
     * If not for real, do not do anything
     */

    if (ForReal == 0)
	return(0);

    /*
     * We need an exclusive lock for this to work, and if the
     * number of links has fallen to zero we are in a race with
     * another expireover.
     */

    if (*fdl==0) {
	nrsignal(SIGALRM, sigAlarm);
	alarm(LockWaitTime);
	if (hflock(fd, 4, XLOCK_EX|XLOCK_INTR) < 0) {
	    nrsignal(SIGALRM, SIG_IGN);
	    if (VerboseOpt > 2)
		printf("unable to rewrite data for grp=%s - file in use\n",
							group->gr_GroupName);
	    return(0);
	}
	nrsignal(SIGALRM, SIG_IGN);
        *fdl=1;
    }

    if (fstat(fd, &st) != 0 || st.st_nlink == 0) {
        hflock(fd, 4, XLOCK_SH);	/* back to shared */
	*fdl = 0;
        return(0);
    }

    /*
     * Calculate number of overview records
     */
    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
    if (oh->oh_MaxArts <= 0 || n < oh->oh_MaxArts) {
	fprintf(stderr, "Over file of group %s has no article slots (damaged header?) - skipped\n", group->gr_GroupName);
	return(0);
    }

    if ((sizeof(size_t) < sizeof(long long)) && (n > 0x7FFFFFFF)) {
	fprintf(stderr, "Sanity check: dangerous number of records on non-64-bit system\n");
	return(0);
    }

    /*
     * Map the over map into mem
     */

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in RewriteData (%s)\n", group->gr_GroupName, n, strerror(errno));
	++RewriteFailed;
	return(0);
    }

    oaBaseNew = calloc((size_t)n, sizeof(OverArt));
    if (oaBaseNew == NULL) {
	fprintf(stderr, "Unable to calloc new over.* file space for group %s (%lld) in RewriteData (%s)\n", group->gr_GroupName, n, strerror(errno));
        xunmap((void *)oaBase, n * sizeof(OverArt));
	++RewriteFailed;
	return(0);
    }


    /*
     * Get new copies of group start and end numbers in case they moved
     */
    {
	const char *rec;
	int recLen;

	rec = KPDBReadRecord(KDBActive, group->gr_GroupName, KP_LOCK, &recLen);
	if (rec == NULL) {
	    /* removed from the active file meanwhile: rebuild nothing */
	    groupGone(group, "data not rewritten");
	    xunmap((void *)oaBase, n * sizeof(OverArt));
	    free(oaBaseNew);
	    return(-1);
	}
	startNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	KPDBUnlock(KDBActive, rec);
    }
    if (group->gr_State & GRF_MODIFIED)
	startNo = group->gr_StartNo;

#ifndef USE_ZLIB
    /* the range may have moved since the check before the resize */
    if (gzipOnlyData(group, dirPath, startNo, endNo, oldDataEntries)) {
	group->gr_State |= GRF_NEEDZLIB;
	xunmap((void *)oaBase, n * sizeof(OverArt));
	free(oaBaseNew);
	return(0);
    }
#endif

    /*
     * Scan entire overview index, building replacement data.* files on the
     * fly.  We have already cleaned it up, so there should not be any
     * garbarge in the index.
     */

    {
	const char *cacheBase = NULL;
	int cacheSize = 0;
	artno_t cacheArtBase = -1;

	for (artNo = startNo; artNo <= endNo; ++artNo) {
	    long long i = ((artNo & 0x7FFFFFFFFFFFFFFFLL) % n);
	    const OverArt *oa = &oaBase[i];

	    if (! OA_ARTVALID(oa))
		continue;

	    if (! OA_ARTNOEQ(artNo, oa->oa_ArtNo)) {
		printf("artNo != oa_ArtNo, panic XXXXXXXXXXXX\n");
		exit(1);
	    }

	    if (cacheArtBase == -1 ||
		artNo < cacheArtBase ||
		artNo >= (cacheArtBase + oldDataEntries)
	    ) {
		char path[PATH_MAX];
		struct stat st;
		int cfd;

		if (cacheBase != NULL) {
		    xunmap((void *)cacheBase, cacheSize);
		    cacheSize = 0;
		    cacheBase = NULL;
		}
		cacheArtBase = artNo & ~(oldDataEntries - 1);
		snprintf(path, sizeof(path), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, cacheArtBase, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		if (DebugOpt > 1)
		    printf("Opening: %s\n", path);
		/*
		 * A source that is there but cannot be read (open, stat,
		 * map or unpack fails, /tmp full included) stops the
		 * group, like a failure to write: its records would be
		 * left out and the original removed.  Only a data. file
		 * that does not exist (ENOENT, plain and .gz) is a range
		 * without data.
		 */
		cfd = dataOpen(path);
		if (cfd < 0 && errno != ENOENT) {
		    printf("Unable to open %s: %s\n", path, strerror(errno));
		    ok = 0;
		} else if (cfd >= 0) {
		    if (fstat(cfd, &st) != 0) {
			printf("Unable to stat %s: %s\n", path, strerror(errno));
			ok = 0;
		    } else if (st.st_size > 0) {
			cacheBase = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, cfd, 0);
			if (cacheBase == NULL) {
			    printf("Unable to xmap %s: %s\n", path, strerror(errno));
			    ok = 0;
			} else {
			    cacheSize = st.st_size;
			    xadvise(cacheBase, st.st_size, XADV_WILLNEED);
			}
		    }
		} else {
#ifdef USE_ZLIB
		    snprintf(path, sizeof(path), "%s/%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, cacheArtBase, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		    if ((cfd = dataOpen(path)) >= 0) {
			/* Atomic creation prevents the tmpnam/open race. */
			char tmpfile[] = "/tmp/diablo-over-XXXXXX";
			tmpfd = mkstemp(tmpfile);

			if (! (tmpfd < 0)) {
			    st.st_size = 0;
			    fstat(cfd, &st);

			    zmap = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, cfd, 0);
			    if (! zmap) {
		    		printf("Unable to xmap for gzcat %s: %s\n",
					path, strerror(errno));
				close(tmpfd);
				ok = 0;
			    } else {
				zpos = zmap + (st.st_size >= 18 ? st.st_size - 4 : 0);
				zlen = st.st_size < 18 ? 0 :
				       (((unsigned)zpos[0] & 0xff) |
				       ((unsigned)zpos[1] & 0xff) << 8 |
				       ((unsigned)zpos[2] & 0xff) << 16 |
				       ((unsigned)zpos[3] & 0xff) << 24);

				if (zlen == 0 && gzEmpty(zmap, st.st_size) &&
				    !rangeHasData(oaBase, n, cacheArtBase,
					oldDataEntries, startNo, endNo)) {
				    /*
				     * an empty data file, gzipped, for a range
				     * whose records have no data (trackonly)
				     */
				    xunmap((void *)zmap, st.st_size);
				    close(tmpfd);
				} else if (zlen < 256 || zlen > (64 * 1048576)) {
				    printf("Bad zlen %d for gzcat %s\n",
						(int)zlen, path);
				    xunmap((void *)zmap, st.st_size);
				    close(tmpfd);
				    ok = 0;
				} else {
				    /* a mapping past the end of the file would fault */
				    if (ftruncate(tmpfd, zlen) < 0)
					tmpmap = NULL;
				    else
					tmpmap = xmap(NULL, zlen, PROT_READ|PROT_WRITE, MAP_SHARED, tmpfd, 0);
				    if (! tmpmap) {
					printf("Unable to xmap for gztmp %s: %s\n", tmpfile, strerror(errno));
					xunmap((void *)zmap, st.st_size);
					close(tmpfd);
					ok = 0;
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

					inflateInit2(&z, -MAX_WBITS);
					code = inflate(&z, Z_FINISH);
					inflateEnd(&z);
					}

					if (code != Z_STREAM_END) {
					    printf("inflate error (%i) for gzcat %s: %s\n",
					     	code, path, z.msg);
					    xunmap((void *)zmap, st.st_size);
					    xunmap((void *)tmpmap, zlen);
					    close(tmpfd);
					    ok = 0;
					} else {
					    xunmap((void *)zmap, st.st_size);

					    cacheBase = (char *)tmpmap;
					    cacheSize = zlen;
					    /* The mapping survives closing the descriptor. */
					    close(tmpfd);
					}
				    }
				}
			    }
			    if (unlink(tmpfile) < 0) {
				printf("Unable to remove gztmp %s: %s\n",
					tmpfile, strerror(errno));
			    }
			} else {
			    printf("Unable to open/create gztmp %s: %s\n",
				tmpfile, strerror(errno));
			    ok = 0;
			}
		    } else if (errno != ENOENT) {
			printf("Unable to open %s: %s\n", path, strerror(errno));
			ok = 0;
		    }
#endif
		}
		if (cfd >= 0)
		    close(cfd);
	    }
	    if (ok == 0)
		break;
	    if (rewriteDataFile(group, cacheBase, cacheSize, oa,
					&oaBaseNew[i], dirPath,
					oldDataEntries - 1, newDataEntries - 1, artNo) < 0)
		ok = 0;
	}
	if (rewriteDataFile(group, (char*)NULL, 0, (OverArt*)NULL, (OverArt*)NULL, (char*)NULL, 0, 0, (artno_t)0) < 0)
	    ok = 0;
	if (cacheBase != NULL) {
	    xunmap((void *)cacheBase, cacheSize);
	    cacheSize = 0;
	    cacheBase = NULL;
	}
    }

    /*
     * Write the new over. file under its temporary name first, while the
     * original files are all still there.  If that fails, or a new data.
     * file could not be written in full (ok is 0 already), the group stays
     * as it was: the index we would be left with is the old one, and its
     * offsets do not fit the rewritten data. files.  The temporary name
     * is created anew (createTempFile()): a symlink there is removed, not
     * followed.
     */

    snprintf(ovPath1, sizeof(ovPath1), "%s/.%s", dirPath,
				    GFName(group->gr_GroupName,
				    GRPFTYPE_OVER, 0, 0,
				    group->gr_Iter,
				    &DOpts.ReaderGroupHashMethod));
    snprintf(ovPath2, sizeof(ovPath2), "%s/%s", dirPath,
				    GFName(group->gr_GroupName,
				    GRPFTYPE_OVER, 0, 0,
				    group->gr_Iter,
				    &DOpts.ReaderGroupHashMethod));
    {
	int ovFd;

	/* Make sure we are using the latest versions */
	if (oh->oh_Version < OH_VERSION) {
	    printf("Upgrading overinfo header version %d->%d in %s\n",
						oh->oh_Version, OH_VERSION,
						group->gr_GroupName);
	    oh->oh_Version = OH_VERSION;
	    strncpy(oh->oh_Gname, group->gr_GroupName,
						sizeof(oh->oh_Gname) - 1);
	    oh->oh_Gname[sizeof(oh->oh_Gname) - 1] = '\0';
	    oh->oh_HeadSize = sizeof(OverHead);
	}
	oh->oh_DataEntries = newDataEntries;
	if (DebugOpt)
	    printf("Setting data size to %d\n", oh->oh_DataEntries);
	if (ok == 0) {
	    printf("Rewrite of %s data files failed, group left as it was\n",
							group->gr_GroupName);
	} else if ((ovFd = createTempFile(ovPath1)) < 0) {
	    printf("Rewrite of %s over. file failed, group left as it was\n",
							group->gr_GroupName);
	    ok = 0;
	} else {
	    if (write(ovFd, oh, oh->oh_HeadSize) != oh->oh_HeadSize)
		ok = 0;
	    if (x64write(ovFd, oaBaseNew, n*sizeof(OverArt)) != n*sizeof(OverArt))
		ok = 0;
	    if (close(ovFd) < 0)
		ok = 0;
	    if (ok == 0)
		printf("Rewrite of %s over. file failed, group left as it was\n",
							group->gr_GroupName);
	}
	if (ok == 0) {
	    ++RewriteFailed;
	    remove(ovPath1);
	    if (startNo <= endNo) {
		for (artNo = (startNo & ~(newDataEntries-1)); artNo <= endNo;
						artNo += newDataEntries) {
		    char path1[PATH_MAX];

		    snprintf(path1, sizeof(path1), "%s/.%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		    remove(path1);
		}
	    }
	    xunmap((void *)oaBase, n * sizeof(OverArt));
	    free(oaBaseNew);
	    return(0);
	}
    }

    /*
     * Remove original data.* files so we can put the new ones and the
     * new over. file in place.  If we are broken after this point,
     * portions or all of the overview information relating to this group
     * will be lost.
     */
    {
	artno_t artNo;
	for (artNo = (startNo & ~(oldDataEntries-1));artNo <= endNo;
						artNo += oldDataEntries) {
	    char path1[PATH_MAX];
	    snprintf(path1, sizeof(path1), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    if (DebugOpt > 1)
		printf("Remove: %s\n", path1);
	    if (ForReal)
		remove(path1);

	    snprintf(path1, sizeof(path1), "%s/%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    if (DebugOpt > 1)
		printf("Remove: %s\n", path1);
	    if (ForReal)
		remove(path1);
	}
    }

    /*
     * Rename the temporary .data files to the real ones.  There is none
     * for a range without a record (both renames find no file), but one
     * the new index has records in must be there.  Any other failure
     * loses the records of that file: say so, and go on, as the other
     * files and the new over. file still fit together while the old
     * over. file fits nothing that is left.
     */
    rblt = 0;
    if (startNo<=endNo) {
	for (artNo = (startNo & ~(newDataEntries-1)); artNo <= endNo;
						artNo += newDataEntries) {
	    char path1[PATH_MAX];
	    char path2[PATH_MAX];
	    int lost = 0;

	    snprintf(path1, sizeof(path1), "%s/.%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    snprintf(path2, sizeof(path2), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    if (DebugOpt > 1)
		printf("Renaming data: %s -> %s\n", path1, path2);
	    if (ForReal && rename(path1, path2) < 0) {
		if (errno != ENOENT) {
		    printf("Error renaming %s -> %s (%s), its records are lost\n",
					path1, path2, strerror(errno));
		    ok = 0;
		    lost = 1;
		}
		remove(path1);

		/*
		 * For gzip support.  Doing it this way is more I/O
		 * intensive, but doesn't require us to remember what
		 * we chose to compress.
		 */

	    	snprintf(path1, sizeof(path1), "%s/.%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    	snprintf(path2, sizeof(path2), "%s/%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		if (rename(path1, path2) < 0) {
		    if (errno != ENOENT) {
			printf("Error renaming %s -> %s (%s), its records are lost\n",
					path1, path2, strerror(errno));
			ok = 0;
		    } else if (!lost && rangeHasRecords(oaBaseNew, n,
				    artNo, newDataEntries, startNo, endNo)) {
			printf("New data file of %s from article %lld is gone, its records are lost\n",
					group->gr_GroupName, (long long)artNo);
			ok = 0;
		    }
		    remove(path1);
		    continue;
		}
	    }
	    rblt++;
	}
    }

    /*
     * Replace the over. file, last: we hold the exclusive lock on the old
     * one, so a dreaderd that opens the group waits on it until now and
     * then finds the new index with all of its data. files in place.
     *
     * If the rename fails, the old over. file must not stay as it is: its
     * data. files are gone and its offsets do not fit the new ones.  Write
     * the new index into it in place instead, as ResizeGroup() does under
     * the same lock.  The run still fails: something is wrong with the
     * file system.
     */

    if (DebugOpt > 1)
	printf("Renaming over: %s -> %s\n", ovPath1, ovPath2);
    if (rename(ovPath1, ovPath2) < 0) {
	printf("Error renaming %s -> %s (%s)\n", ovPath1, ovPath2,
							strerror(errno));
	ok = 0;
	if (lseek(fd, 0L, 0) != 0 ||
	    ftruncate(fd, oh->oh_HeadSize + n * sizeof(OverArt)) < 0 ||
	    x64write(fd, oh, oh->oh_HeadSize) != oh->oh_HeadSize ||
	    x64write(fd, oaBaseNew, n * sizeof(OverArt)) != n * sizeof(OverArt)) {
	    printf("Rewrite of %s over. file failed: its overview no longer fits its data files\n",
					group->gr_GroupName);
	} else {
	    printf("Rewrite of %s: new over. file written in place\n",
					group->gr_GroupName);
	}
	remove(ovPath1);
    }
    if (ok == 0)
	++RewriteFailed;

    if (VerboseOpt)
	printf("Rebuilt %d data file(s) for %s\n", rblt, group->gr_GroupName);

    /*
     * cleanup
     */
    xunmap((void *)oaBase, n * sizeof(OverArt));
    free(oaBaseNew);
    return(0);
}

#ifdef USE_ZLIB
/*
 * rangeHasData() - the index oa (n slots) has a record with overview data
 * (a data offset, not a trackonly record) for articles base .. base +
 * entries - 1 (within startNo .. endNo).
 */
static int
rangeHasData(const OverArt *oa, long long n, artno_t base, int entries, artno_t startNo, artno_t endNo)
{
    artno_t artNo;

    for (artNo = (base > startNo) ? base : startNo;
	 artNo < base + entries && artNo <= endNo; ++artNo) {
	const OverArt *ob = &oa[(artNo & 0x7FFFFFFFFFFFFFFFLL) % n];

	if (OA_ARTVALID(ob) && OA_ARTNOEQ(artNo, ob->oa_ArtNo) &&
							ob->oa_SeekPos >= 0)
	    return(1);
    }
    return(0);
}
#endif

/*
 * rangeHasRecords() - the new index oa (n slots) has a record in the data.
 * file for articles base .. base + entries - 1 (within startNo .. endNo):
 * rewriteDataFile() wrote that file, and it must be moved in.
 */
static int
rangeHasRecords(const OverArt *oa, long long n, artno_t base, int entries, artno_t startNo, artno_t endNo)
{
    artno_t artNo;

    for (artNo = (base > startNo) ? base : startNo;
	 artNo < base + entries && artNo <= endNo; ++artNo) {
	const OverArt *ob = &oa[(artNo & 0x7FFFFFFFFFFFFFFFLL) % n];

	if (OA_ARTVALID(ob) && OA_ARTNOEQ(artNo, ob->oa_ArtNo))
	    return(1);
    }
    return(0);
}

/*
 * rewriteDataFile() - rewrite the file from cacheFd/oa-params to
 *		       ob, maintaining the FileCacheList list.
 *
 *	Returns -1 when a new data. file could not be created, written or
 *	closed (the failure is printed): its records would be missing from
 *	the rewritten group, so the caller must not put the new files in
 *	place.  The closing call (oa NULL) reports a failure of the group
 *	once more and starts the next group afresh.
 */

int
rewriteDataFile(Group *group, const char *cacheBase, int cacheSize, const OverArt *oa, OverArt *ob, const char *dirPath, int oldDataEntriesMask, int newDataEntriesMask, artno_t artNo)
{
    artno_t oldArtBase, newArtBase;
    static FileCacheList *fc = NULL;
    static int failed = 0;

    if (oa==NULL || ob==NULL) { /* end of for, closing fd */
	int r;

	if (VerboseOpt > 2)
	    printf("End of rewrite: %s\n", group->gr_GroupName);
	if (fc != NULL && fc->fc_Fd != -1) {
	    if (close(fc->fc_Fd) < 0) {
		printf("Error writing data file of %s: %s\n",
				group->gr_GroupName, strerror(errno));
		failed = 1;
	    }
	    fc->fc_Fd = -1;
	    fc->fc_ArtBase = -1;
	}
	r = failed ? -1 : 0;
	failed = 0;
	return(r);
    }

    oldArtBase = artNo & ~oldDataEntriesMask;
    newArtBase = artNo & ~newDataEntriesMask;

    if (VerboseOpt > 2)
	printf("Rewriting: %s:%lld:%lld\n", group->gr_GroupName, oldArtBase, newArtBase);

    if (fc == NULL) {
	fc = calloc(1, sizeof(FileCacheList));
	if (fc == NULL) {
	    printf("Unable to allocate rewrite state for %s\n", group->gr_GroupName);
	    failed = 1;
	    return(-1);
	}
	fc->fc_Fd = -1;
	fc->fc_ArtBase = -1;
    }

    if (fc->fc_ArtBase != newArtBase || fc->fc_Fd == -1) {
	char path[PATH_MAX];

	if (fc->fc_Fd != -1 && close(fc->fc_Fd) < 0) {
	    printf("Error writing data file of %s: %s\n",
				group->gr_GroupName, strerror(errno));
	    failed = 1;
	}

	snprintf(path, sizeof(path), "%s/.%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, newArtBase, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	if (DebugOpt > 1)
            printf("Creating: %s\n", path);
	/* created anew: a symlink at the name is removed, not followed */
	fc->fc_Fd = createTempFile(path);
	if (fc->fc_Fd == -1) {
	    printf("Error opening %s: %s\n", path, strerror(errno));
	    failed = 1;
	}
	fc->fc_ArtBase = newArtBase;
    }

    /*
     * copy data from cacheFd/oa-params to rf->fc_Fd and fill in ob.  It is
     * possible for cacheBase to be NULL if there is no valid data source
     * for the file.
     */
    if (
	cacheBase != NULL && 
	fc->fc_Fd >= 0 && 
	oa->oa_SeekPos >= 0 &&
	oa->oa_Bytes > 0 &&
	oa->oa_SeekPos + oa->oa_Bytes < cacheSize &&
	cacheBase[oa->oa_SeekPos + oa->oa_Bytes] == 0 && 	  /* guard */
	(oa->oa_SeekPos == 0 || cacheBase[oa->oa_SeekPos-1] == 0) /* guard */

    ) {
	*ob = *oa;
	ob->oa_SeekPos = lseek(fc->fc_Fd, 0L, 1);
	if (write(fc->fc_Fd, cacheBase + oa->oa_SeekPos, oa->oa_Bytes + 1) != oa->oa_Bytes + 1) {
	    lseek(fc->fc_Fd, ob->oa_SeekPos, 0);
	    if (ftruncate(fc->fc_Fd, ob->oa_SeekPos) < 0)
		printf("truncate failed %s: %s\n", group->gr_GroupName, strerror(errno));
	    ob->oa_ArtNo = -2;
	    ob->oa_SeekPos = 0;
	    ob->oa_Bytes = 0;
	    printf("copy failed %s:%d, write error\n", group->gr_GroupName, oa->oa_ArtNo);
	    failed = 1;
	}
    } else if (oa->oa_SeekPos == -1) {
	/*
	 * readerxover trackonly: the record has no overview data, only
	 * the article number.  Keep it as it is; leaving the slot empty
	 * would drop the article from the group's index.
	 */
	*ob = *oa;
    } else if (CheckDataEntries) {
	*ob = *oa;
	printf("entry removed %s:%d, %s\n",
	    group->gr_GroupName,
	    oa->oa_ArtNo,
	    ((cacheBase == NULL) ? "source-missing" :
	    (fc->fc_Fd < 0) ? "dest-failure" :
	    (oa->oa_Bytes <= 0) ? "source-bounds1" :
	    (oa->oa_SeekPos + oa->oa_Bytes >= cacheSize) ? "source-bounds2" :
	    "source-corrupt")
	);
	ob->oa_ArtNo = -2;
	ob->oa_SeekPos = 0;
	ob->oa_Bytes = 0;
    } else {
	printf("copy failed %s:%d, %s\n",
	    group->gr_GroupName,
	    oa->oa_ArtNo,
	    ((cacheBase == NULL) ? "source-missing" :
	    (fc->fc_Fd < 0) ? "dest-failure" :
	    (oa->oa_Bytes <= 0) ? "source-bounds1" :
	    (oa->oa_SeekPos + oa->oa_Bytes >= cacheSize) ? "source-bounds2" :
	    "source-corrupt")
	);
    }
    return(failed ? -1 : 0);
}

/*
 * Resize a newsgroup's over.* index file, if possible.  If called via the -s
 * option, only groups that need resizing are rebuilt.  If called via the -R
 * option, the group is always rebuild AND the associated data files are 
 * rebuilt.
 */

int
ResizeGroup(Group *group, int fd, OverHead *oh, int newSize, int OldDataEntries, int *fdl)
{
    int oldSize = oh->oh_MaxArts;
    struct stat st;

    if (*fdl==0) {
	nrsignal(SIGALRM, sigAlarm);
	alarm(LockWaitTime);
	if (hflock(fd, 4, XLOCK_EX|XLOCK_INTR) < 0) {
	    nrsignal(SIGALRM, SIG_IGN);
	    ++NoResizedGroups;
	    if (VerboseOpt)
		printf("resize maxArts from %d to %d failed, file in use grp=%s\n",
					oldSize, newSize, group->gr_GroupName);
	    return(0);
	}
	nrsignal(SIGALRM, SIG_IGN);
	*fdl=1;
    }

    if (fstat(fd, &st) < 0 || st.st_nlink == 0) {
        hflock(fd, 4, XLOCK_SH);	/* back to shared */
	*fdl = 0;
        ++NoResizedGroups;
        if (VerboseOpt) {
    	    printf("resize maxArts from %d to %d failed, file in use grp=%s\n",
				oldSize, newSize, group->gr_GroupName);
        }
        return(0);
    }

    /* a header that promises more records than the file holds, or none */
    if (oldSize <= 0 ||
		(st.st_size - oh->oh_HeadSize) / (long long)sizeof(OverArt) < oldSize) {
	hflock(fd, 4, XLOCK_SH);	/* back to shared, for the expiry */
	*fdl = 0;
	++NoResizedGroups;
	fprintf(stderr, "Over file of group %s has no article slots (damaged header?) - skipped\n", group->gr_GroupName);
	return(0);
    }

    /*
     * Resize a group.  We 'own' the overview index file (because other 
     * processes must get a shared lock on offset 4 and we got the exclusive
     * lock).  We can do anything we want with it, but we must rewrite the
     * file in-place to maintain lock consistancy.
     *
     * Resize the group by copying the existing data into an array, validating
     * it based on known information, then putting it back.
     */

    {
#ifdef	READOA
	OverArt *oa = calloc(oldSize, sizeof(OverArt));
#else
	OverArt *oa = NULL;
#endif
	OverArt *ob = calloc(newSize, sizeof(OverArt));
	long long n;
	long long i;
	int recLen;
	const char *rec;

#ifndef	READOA
        if (fstat(fd, &st) != 0) {
	    free(ob);
	    return(0);
	}

        /*
         * Calculate number of overview records
         */

        n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
	oa = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
#endif

	if (! oa || ! ob) {
	    fprintf(stderr, "Unable to calloc space for over.* file for group %s\n", group->gr_GroupName);
	    exit(1);
	}

#ifdef	READOA
	lseek(fd, oh->oh_HeadSize, 0);
	if (((n = x64read(fd, oa, oldSize * sizeof(OverArt)))) < 0) {
	    fprintf(stderr, "Unable to read over.* file for group %s: %s\n",
	    			group->gr_GroupName,
				strerror(errno));
	    exit(1);
	}
	if (n != oldSize * sizeof(OverArt)) {
	    fprintf(stderr, "Read of group over.* file smaller than expected (%s %d %d)\n", 
				group->gr_GroupName,
				n,
				oldSize * sizeof(OverArt));
	}
	n = n / sizeof(OverArt);
#endif

	/*
	 * We need to get the NE field again now that we have locked the
	 * over.* file because it could have been adjusted by dreaderd
	 * since we originally got it.
	 *
	 * If the group has left the active file meanwhile, do not resize:
	 * with no range, nothing would be copied and the index would be
	 * rewritten empty.  The caller still holds and releases the over.*
	 * lock, and stops working on the group.
	 */
	rec = KPDBReadRecord(KDBActive, group->gr_GroupName, KP_LOCK, &recLen);
	if (rec == NULL) {
	    ++NoResizedGroups;
	    groupGone(group, "not resized");
	    free(ob);
#ifdef	READOA
	    free(oa);
#else
	    xunmap((void *)oa, n * sizeof(OverArt));
#endif
	    return(-1);
	}
	group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"),
								NULL, 10);
	KPDBUnlock(KDBActive, rec);

	if (group->gr_EndNo - group->gr_StartNo >= newSize) {
	    if (VerboseOpt)
		printf("adjust %s startno %lld to %lld (endno=%lld  newsize=%d)\n",
				group->gr_GroupName,
				group->gr_StartNo,
				group->gr_EndNo - newSize + 1,
				group->gr_EndNo,
				newSize);
	    group->gr_StartNo = group->gr_EndNo - newSize + 1;
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
	}

	/*
	 * Run through and copy validated entries
	 */

	for (i = group->gr_StartNo; i <= group->gr_EndNo; ++i) {
	    OverArt *op = &oa[(i & 0x7FFFFFFFFFFFFFFFLL) % oh->oh_MaxArts];

	    if (OA_ARTNOEQ(i, op->oa_ArtNo)) {
		ob[(i & 0x7FFFFFFFFFFFFFFFLL) % newSize] = *op;
		if (VerboseOpt > 2)
		    printf("resize %s copying ArtNo at %lld (got %d wanted %d)\n", 
				group->gr_GroupName,
				i,
				op->oa_ArtNo,
				OA_ARTNOSET(i));
	    } else {
		if (VerboseOpt > 2 || (VerboseOpt && op->oa_ArtNo))
		    printf("resize %s mismatched ArtNo at %lld (got %d wanted %d)\n", 
				group->gr_GroupName,
				i,
				op->oa_ArtNo,
				OA_ARTNOSET(i));
	    }
	}

	/*
	 * Rewrite.  Only if ForReal.  Note that oh_MaxArts isn't updated 
	 * (for use in other parts of dexpireover) if not for real.
	 */

	if (ForReal) {
	    long long obsize;

	    /* Take this opportunity to set the correct version */
	    if (oh->oh_Version < OH_VERSION) {
		printf("Upgrading overinfo header version %d->%d\n",
						oh->oh_Version, OH_VERSION);
		oh->oh_Version = OH_VERSION;
		strncpy(oh->oh_Gname, group->gr_GroupName,
						sizeof(oh->oh_Gname) - 1);
		oh->oh_Gname[sizeof(oh->oh_Gname) - 1] = '\0';
		oh->oh_HeadSize = sizeof(OverHead);
		oh->oh_DataEntries = OldDataEntries;
	    }
	    lseek(fd, 0L, 0);
	    if (ftruncate(fd, oh->oh_HeadSize + newSize * sizeof(OverArt)) < 0) {
	        fprintf(stderr, "ftruncate of new group over.* file failed (%s): %s\n", 
						group->gr_GroupName,
						strerror(errno));
	    }
	    oh->oh_MaxArts = newSize;
	    if (x64write(fd, oh, sizeof(OverHead)) != sizeof(OverHead)) {
		fprintf(stderr, "write of new group over.* OverHead failed (%s): %s\n",
						group->gr_GroupName,
						strerror(errno));
	    }

	    /*
	     * This is no longer an atomic write.  We do hold
	     * a write lock, in theory this should be OK.  Have
	     * to write chunks because write can't handle > 32bit
	     */

	    obsize = newSize * sizeof(OverArt);
	    if (x64write(fd, ob, obsize) != obsize) {
		fprintf(stderr, "write of new group over.* data failed (%s, %lld): %s\n",
						group->gr_GroupName,
						obsize,
						strerror(errno));
	    }
	}

	free(ob);
#ifdef	READOA
	free(oa);
#else
        xunmap((void *)oa, n * sizeof(OverArt));
#endif
    }
    ++ResizedGroups;

    if (VerboseOpt && oldSize != newSize) {
	printf("resized maxArts from %d to %d grp=%s\n", oldSize, newSize, group->gr_GroupName);
    }
    return(0);
}

#ifndef USE_ZLIB
/*
 * gzipOnlyData() - without zlib a .gz data file (made by dexpireover -z)
 * cannot be read.  Return 1, with a message, if a data file in the range
 * exists only as .gz.
 */
static int
gzipOnlyData(Group *group, const char *dirPath, artno_t startNo, artno_t endNo, int dataEntries)
{
    char path[PATH_MAX];
    struct stat st;
    artno_t artNo;

    if (dataEntries <= 0 || startNo > endNo)
	return(0);
    for (artNo = startNo & ~((artno_t)dataEntries - 1); artNo <= endNo;
						artNo += dataEntries) {
	const char *name = GFName(group->gr_GroupName, GRPFTYPE_DATA, artNo,
					0, group->gr_Iter,
					&DOpts.ReaderGroupHashMethod);

	snprintf(path, sizeof(path), "%s/%s", dirPath, name);
	if (stat(path, &st) == 0)
	    continue;
	snprintf(path, sizeof(path), "%s/%s.gz", dirPath, name);
	if (stat(path, &st) != 0)
	    continue;
	fprintf(stderr, "group %s not rewritten: compressed overview data %s needs a zlib build\n",
					group->gr_GroupName, path);
	++NeedZlibGroups;
	return(1);
    }
    return(0);
}
#endif

int
nearestPower(int n)
{
    int i;

    for (i = 1; i < n; i <<= 1)
	;
    return(i);
}

void
ReadDExpOverList(void)
{
    FILE *DExpOverList;
    hash_t read_buffer[DEXPOVER_READ_BUFFER_SIZE];
    int i, n;
    char path[PATH_MAX];

    dexpover_msgid_hash =
	(struct bucket_t *)malloc(DEXPOVER_HASH_SIZE * sizeof(struct bucket_t));
    for(i=0; i<DEXPOVER_HASH_SIZE; i++) {
	dexpover_msgid_hash[i].valid=0;
	dexpover_msgid_hash[i].next=NULL;
    }

    if (snprintf(path, sizeof(path), "%s.bak",
	PatDbExpand(DExpireOverListPat)) >= (int)sizeof(path)) {
	logit(LOG_ERR, "dexpover list path too long");
	return;
    }
    rename(PatDbExpand(DExpireOverListPat), path);
    DExpOverList = fopen(path, "r");

    if(DExpOverList == NULL) return;

    while((n = fread(read_buffer, sizeof(hash_t),
		     DEXPOVER_READ_BUFFER_SIZE, DExpOverList))) {
	for(i=0; i<n; i++) {
	    int hashval;
	    struct bucket_t *chain;

	    hashval = (read_buffer[i].h1)&(DEXPOVER_HASH_SIZE-1);
	    chain = &dexpover_msgid_hash[hashval];

	    while((chain->valid == 1) && (chain->next != NULL))
		chain = chain->next;

	    if(chain->valid == 1) {
		chain->next = (struct bucket_t *)malloc(sizeof(struct bucket_t));
		chain = chain->next;
	    }

	    chain->valid = 1;
	    chain->hash_item = read_buffer[i];
	    chain->next = NULL;
	}
    }

    fclose(DExpOverList);
}

int
hexCharToInt(char c)
{
    return
	(c == '0') ? 0 :
	(c == '1') ? 1 :
	(c == '2') ? 2 :
	(c == '3') ? 3 :
	(c == '4') ? 4 :
	(c == '5') ? 5 :
	(c == '6') ? 6 :
	(c == '7') ? 7 :
	(c == '8') ? 8 :
	(c == '9') ? 9 :

	(c == 'a') ? 10 :
	(c == 'b') ? 11 :
	(c == 'c') ? 12 :
	(c == 'd') ? 13 :
	(c == 'e') ? 14 :
	(c == 'f') ? 15 :

	(c == 'A') ? 10 :
	(c == 'B') ? 11 :
	(c == 'C') ? 12 :
	(c == 'D') ? 13 :
	(c == 'E') ? 14 :
	(c == 'F') ? 15 :

	-1;
}

int
expOverListCheckExpired(hash_t *hv)
{
    int hashval;
    bucket_t *chain;

    hashval = (hv->h1)&(DEXPOVER_HASH_SIZE-1);
    chain = &dexpover_msgid_hash[hashval];

    while(chain && chain->valid) {
	if((chain->hash_item.h1 == hv->h1) &&
	   (chain->hash_item.h2 == hv->h2)) {
	    return 0;
	}
	chain = chain->next;
    }

    return -1;
}

int
getDataEntries(const char *path)
{
    /*
     * over. file	(fixed length file)
     */
    int fd;
    OverHead oh;
    int de = OD_HARTS;

    if ((fd = open(path, O_RDONLY)) >= 0) {
	int r;
	r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
	    r = 0;
	if (r == 1 && oh.oh_Version >= 3)
	    de = oh.oh_DataEntries;
	close(fd);
    }
    return(de);
}

long long
x64write(int d, const void *buf, long long nbytes)
{
    long long nwrite, owrite = 0LL;
    int rval;

    while ((nwrite = nbytes)) {
	if (nwrite > 1024 * 1024 * 1024LL) {
	    nwrite = 1024 * 1024 * 1024LL;
	}
	if ((rval = write(d, buf + owrite, (int)nwrite)) < 0) {
		return(-1LL);
	}
	nbytes -= rval;
	owrite += rval;
    }
    return(owrite);
}

long long
x64read(int d, void *buf, long long nbytes)
{
    long long nread, oread = 0LL;
    int rval;

    while ((nread = nbytes)) {
	if (nread > 1024 * 1024 * 1024LL) {
	    nread = 1024 * 1024 * 1024LL;
	}
	if ((rval = read(d, (char *)buf + oread, (int)nread)) <= 0) {
		return(-1LL);
	}
	nbytes -= rval;
	oread += rval;
    }
    return(oread);
}

void dumpoh(OverHead *oh, char *filename)
{
    FILE *fp;
    if (! ((fp = PrivateFOpen(filename, "w", 0640)))) {
	fprintf(stderr, "failed to open %s\n", filename);
	return;
    }
    fprintf(fp, "OverHead version %d, byteorder %08x\n", oh->oh_Version, oh->oh_ByteOrder);
    fprintf(fp, "	HeadSize	%d\n	MaxArts		%d\nDataEntries	%d\nGroup		%s\n\n", oh->oh_HeadSize, oh->oh_MaxArts, oh->oh_DataEntries, oh->oh_Gname);
    fclose(fp);
}



void dumpovmap(OverArt *oa, long long n, char *filename)
{
    long long i;
    int nulrun = 0;
    FILE *fp;

    if (! ((fp = PrivateFOpen(filename, "w", 0640)))) {
	fprintf(stderr, "failed to open %s\n", filename);
	return;
    }
    for (i = 0; i < n; i++) {
	if (oa[i].oa_ArtNo == 0 && nulrun != 0) {
	    continue;
	}
	fprintf(fp, "%012lld a=%010d o=%07d b=%07d h=%08x s=%08d t=%08d %s\n", i, oa[i].oa_ArtNo, oa[i].oa_SeekPos, oa[i].oa_Bytes, oa[i].oa_MsgHash.h1, oa[i].oa_ArtSize, oa[i].oa_TimeRcvd, OA_ARTVALID(&oa[i]) ? "(valid)" : "(BAD)");
	if (oa[i].oa_ArtNo == 0 && oa[i].oa_TimeRcvd == 0) {
	    nulrun = 1;
	    fprintf(fp, "	null entries\n");
	} 
    }
    fclose(fp);
}


