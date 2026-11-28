
/*
 * UTIL/CONVOVER.C
 *
 * (c)Copyright 2001, Russell Vincent , All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 *	This program performs maintenance tasks on the dreaderd
 *	overview/header database.
 *
 *	WARNING: The server must be down when this is run
 */

#include <dreaderd/defs.h>

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

char *allocTmpCopy(const char *buf, int bufLen);
void ConvertHash(const char *group, GroupHashType *srcHash, GroupHashType *dstHash, int iter, artno_t begNo, artno_t endNo);
int SetField(char **pptr, const char *str);
Group *EnterGroup(const char *groupName, artno_t begNo, artno_t endNo, int lmts, int cts, int iter, const char *flags);
Group *FindGroupByHash(char *Hash, int iter, int hier);
/* HierDirClass(): where a hierarchy file is for a group (D-336) */
#define HIERDIR_OFF	0
#define HIERDIR_IN	1
#define HIERDIR_OTHER	2
static int HierDirOf(const char *name, GroupHashType *method, char *dir, size_t size);
static int HierDirClass(const char *name, const char *dirPath);
static int HierGroupKnown(const char *name, const char *dirPath);
static int HierOtherOver(const char *name);
int OverFileIsGroups(const Group *group, int iter);
Group *FindGroupByName(const char *groupName);
int IterInProgress(char *Hash, int iter);
int ScanActive(int convert, int newOnly);
int RescanActive(void);
void CleanSpool(char *cleanDir);
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
int DeleteFile(const char *dirPath, const char *name);
int MoveGroupFile(char *path1, char *path2);
void ProcessFile(const char *dirPath, const char *name, int top);
static void CleanTop(void);
static int HierName(const char *name);
static int TopTempName(const char *name);
static int OverNamesGroup(const char *path, const char *name);
static void TopGoneAdd(const char *name);
static int TopGone(const char *name);
static int TopOverIrregular(const Group *group, int iter, char *bad, size_t size);
static int ReaderRunning(char *why, size_t size);
int getDataEntries(const char *path);

#define	ACT_CONVERT	0x01
#define	ACT_CLEAN	0x02

#define GRF_DESCRIPTION 0x00000001
#define GRF_STARTNO     0x00000002
#define GRF_ENDNO       0x00000004
#define GRF_FLAGS       0x00000008
#define GRF_FROMLOCAL   0x00000800
#define GRF_NEW         0x00001000
#define GRF_FROMREMOTE  0x00002000
#define GRF_MODIFIED    0x00008000
#define GRF_EDITEDBEG   0x00010000

#define GHSIZE		1024
#define GHMASK		(GHSIZE-1)

int Action = 0;
GroupHashType SrcHash;
GroupHashType DstHash;
int VerboseOpt = 0;
int ForReal = 1;
int MustExit = 0;
char *GroupMatch = NULL;
char *CleanDir = NULL;
KPDB  *KDBActive;
Group *GHash[GHSIZE];
int TotalFiles = 0;
int BadGroups = 0;
int RemovedFiles = 0;
int Failures = 0;		/* moves and removals that failed: exit status 1 */
int FileCopy = 0;
int ActiveSeq = -1;		/* append count of dactive.kp when it was last scanned */
int CleanOther = 0;		/* clean -o: remove the other hierarchy layout's files (D-337) */

void
sigInt(int sigNo)
{
    ++MustExit;
    if (MustExit > 3)
	exit(1);
}

void
Usage(char *progname)
{
    printf("Usage: %s [-f activefile][-m][-n][-o][-w wild][-v] action params ...\n", progname);
    printf("    where:\n");
    printf("            -f file    set the active file to use\n");
    printf("            -w wild    limit the set of groups to work on\n");
    printf("            -n         don't actually make any changes (dummy run)\n");
    printf("            -m         move files with a file copy, instead of renaming them\n");
    printf("                          (needed when converting across filesystems)\n");
    printf("            -o         clean: also remove the files of the groups in the\n");
    printf("                          directory of the other hierarchy layout, left\n");
    printf("                          by a switch between hierarchy and hierarchy-full\n");
    printf("                          (refused while dreaderd runs)\n");
    printf("\n");
    printf("           action is one of\n");
    printf("\tclean				delete unused files\n");
    printf("\tconvert srchash dsthash		convert hash method\n");
    printf("\t    srchash and dsthash are one of\n");
    printf("\t\tcrc\n");
    printf("\t\tmd5-32/N[/N]\n");
    printf("\t\tmd5-64/N[/N]\n");
    printf("\t\thierarchy\n");
    printf("\t\thierarchy-full\n");
    printf("\t    WARNING: No other diablo processes must be running during\n");
    printf("\t\tthe conversion\n");
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
	    if (strcmp(ptr, "convert") == 0) {
		if (++i >= ac || SetGroupHashMethod(av[i], &SrcHash) != 0) {
		    printf("Missing or bad src hash method\n");
		    Usage(av[0]);
		}
		if (++i >= ac || SetGroupHashMethod(av[i], &DstHash) != 0) {
		    printf("Missing or bad dst hash method\n");
		    Usage(av[0]);
		}
		if (bcmp(&SrcHash, &DstHash, sizeof(SrcHash)) == 0) {
		    printf("Cannot convert to same method\n");
		    Usage(av[0]);
		}
		/*
		 * hierarchy and hierarchy-full are both HASHGRP_HIER with the
		 * same file names; only the directories differ.  In hierarchy
		 * the plain data of "X.gz" and the compressed data of "X" can be
		 * one file (D-325), and which group it belongs to cannot be told,
		 * so there is no safe conversion between the two yet.
		 */
		if (SrcHash.gh_type == HASHGRP_HIER &&
		    DstHash.gh_type == HASHGRP_HIER &&
		    SrcHash.gh_hierfull != DstHash.gh_hierfull) {
		    printf("Cannot convert between hierarchy and hierarchy-full (not supported yet, D-325)\n");
		    Usage(av[0]);
		}
		Action |= ACT_CONVERT;
	    } else if (strcmp(ptr, "clean") == 0) {
		/* an option after clean is no directory: "clean -o" (D-337) */
		if (i + 1 < ac && av[i + 1][0] != '-')
		    CleanDir = av[++i];
		Action |= ACT_CLEAN;
	    }
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'C':           /* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'f':
	    dbfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'm':
	    FileCopy = 1;
	    break;
	case 'n':
	    ForReal = 0;
	    break;
	case 'o':
	    CleanOther = 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'v':
	    VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'w':
	    GroupMatch = (*ptr) ? ptr : av[++i];
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    Usage(av[0]);
	}
    }

    if (Action == 0)
	Usage(av[0]);

    /*
     * clean -o (D-337): the files of a group in its directory of the other
     * hierarchy layout are left alone by clean and dexpireover, as a
     * dreaderd started before readergrouphash was changed may still write
     * them (D-336).  They are removed only with dreaderd stopped, and
     * before anything is touched.
     */
    if (CleanOther) {
	char why[PATH_MAX + 128];

	if ((Action & ACT_CLEAN) == 0 || (Action & ACT_CONVERT)) {
	    fprintf(stderr, "-o is an option of clean only\n");
	    exit(1);
	}
	if (DOpts.ReaderGroupHashMethod.gh_type != HASHGRP_HIER) {
	    char hashStr[256];

	    fprintf(stderr, "clean -o: readergrouphash is %s, not hierarchy or hierarchy-full: there is no other hierarchy layout, nothing done\n",
				GetHash(&DOpts.ReaderGroupHashMethod, hashStr));
	    exit(1);
	}
	if (ReaderRunning(why, sizeof(why))) {
	    fprintf(stderr, "clean -o: dreaderd may be running (%s): stop it first, nothing done\n", why);
	    exit(1);
	}
    }

    /*
     * If we are converting, make sure that we set the default hash method
     * to the new one, so that we can do the correct type of clean otherwise
     * we are in serious trouble
     */
    if (Action & ACT_CONVERT) {
	char srcHashStr[256];	/* GetHash() needs about 150 */
	char dstHashStr[256];
	bcopy(&DstHash, &DOpts.ReaderGroupHashMethod, sizeof(DstHash));
	printf("Converting from %s to %s\n", GetHash(&SrcHash, srcHashStr),
					GetHash(&DstHash, dstHashStr));
    }

    /*
     * Open active file database
     */

    if (dbfile) {
	KDBActive = KPDBOpen(dbfile, O_RDWR);
    } else {
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR);
    }
    if (KDBActive == NULL) {
	fprintf(stderr, "Unable to open dactive.kp\n");
	exit(1);
    }

    rsignal(SIGINT, sigInt);
    rsignal(SIGHUP, sigInt);
    rsignal(SIGTERM, sigInt);
    rsignal(SIGALRM, SIG_IGN);

    /*
     * scan dactive.kp
     */

    ScanActive(Action & ACT_CONVERT, 0);

    if (Action & ACT_CLEAN)
	CleanSpool(CleanDir);

    if (KDBActive)
	KPDBClose(KDBActive);

    if (Action & ACT_CONVERT)
	printf("Don't forget to set the new hash method in diablo.config\n");
    if (Failures)
	printf("%d file operations failed\n", Failures);

    return(Failures ? 1 : 0);
}

/*
 * Move one file to its new name, by copy (-m) or rename.  A failure is
 * reported and the group is not marked converted.
 */
int
MoveGroupFile(char *path1, char *path2)
{
    if (FileCopy) {
	if (MoveFile(path1, path2) != 0) {
	    printf("Cannot move %s -> %s\n", path1, path2);
	    ++Failures;
	    return(-1);
	}
    } else if (rename(path1, path2) != 0) {
	printf("Cannot rename %s -> %s (%s)\n", path1, path2, strerror(errno));
	++Failures;
	return(-1);
    }
    return(0);
}

void
ConvertHash(const char *group, GroupHashType *srcHash, GroupHashType *dstHash, int iter, artno_t begNo, artno_t endNo)
{
    char path1[PATH_MAX];
    char path2[PATH_MAX];
    char ovSrc[PATH_MAX];
    char ovDst[PATH_MAX];
    char dir[PATH_MAX];
    struct stat st;
    artno_t artNo;
    artno_t artBase;
    artno_t prevArtBase;
    int dataEntriesMask;

    if (chdir(PatExpand(GroupHomePat)) != 0) {
	printf("Unable to chdir %s (%s)\n", PatExpand(GroupHomePat), strerror(errno));
	return;
    }

    snprintf(path1, sizeof(path1), "%s", GFName(group, GRPFTYPE_OVER, 0, 1, iter, srcHash));
    snprintf(path2, sizeof(path2), "%s", GFName(group, GRPFTYPE_OVER, 0, 1, iter, dstHash));
    snprintf(dir, sizeof(dir), "%s", path2);

    if (DebugOpt > 1)
	printf("Converting %s -> %s\n", path1, path2);
    if (stat(path1, &st) != 0) {
	if (VerboseOpt > 2)
	    printf("over file for %s not found - skipping conversion\n", group);
	return;
    }
    /* the entry count is read from the old file, before it moves */
    dataEntriesMask = getDataEntries(path1) - 1;
    snprintf(ovSrc, sizeof(ovSrc), "%s", path1);
    snprintf(ovDst, sizeof(ovDst), "%s", path2);
    MakeGroupDirectory(dir);
    prevArtBase = -1;
    for (artNo = begNo; artNo <= endNo; artNo++) {
	int variant;

	artBase = artNo & ~dataEntriesMask;
	if (artBase == prevArtBase)
	    continue;
	prevArtBase = artBase;
	snprintf(path1, sizeof(path1), "%s", GFName(group, GRPFTYPE_DATA, artBase, 1, iter, srcHash));
	snprintf(path2, sizeof(path2), "%s", GFName(group, GRPFTYPE_DATA, artBase, 1, iter, dstHash));
	/* a data file is either plain or compressed (.gz) */
	for (variant = 0; variant < 2; variant++) {
	    char src[PATH_MAX];
	    char dst[PATH_MAX];
	    const char *ext = variant ? ".gz" : "";

	    if (snprintf(src, sizeof(src), "%s%s", path1, ext) >= (int)sizeof(src) ||
		snprintf(dst, sizeof(dst), "%s%s", path2, ext) >= (int)sizeof(dst)) {
		printf("Path too long for data file of %s\n", group);
		return;
	    }
	    if (stat(src, &st) != 0) {
		if (variant == 0 && VerboseOpt > 1)
		    printf("data file for %s not found - ignoring\n", group);
		continue;
	    }
	    if (DebugOpt > 1)
		printf("Converting %s -> %s\n", src, dst);
	    if (ForReal && MoveGroupFile(src, dst) != 0)
		return;
	}
    }
    /*
     * The over file goes last: if a data file could not be moved, the group
     * still has its over file in the old place, and a second run converts
     * what is left (files that already moved are not found there again).
     */
    if (ForReal && MoveGroupFile(ovSrc, ovDst) != 0)
	return;
    if (VerboseOpt)
	printf("Converted %s\n", group);
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
	if (group->gr_EndNo != endNo) {
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
 * ScanActive() - read the groups of dactive.kp into GHash.  With newOnly
 * only groups that are not in the table yet are entered (the rescan of
 * RescanActive(): the groups already known keep their range).  Returns the
 * number of groups entered.
 */
int
ScanActive(int convert, int newOnly)
{
    int recLen;
    int recOff;
    int count = 0;
    int entered = 0;
    /*
     * The append count is read before the scan: a group appended while it
     * runs may be missed by it, and the count then still differs afterwards.
     */
    int seq = KPDBAppendCount(KDBActive);

    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	 recOff;
	 recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
    ) {
	int groupLen;
	const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	artno_t begNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	int iter = (int)strtoul(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);	/* written as %06d */

	if (group)
	    group = allocTmpCopy(group, groupLen);

	if (group == NULL)
	    continue;

	if (newOnly && FindGroupByName(group) != NULL)
	    continue;

	EnterGroup(group, begNo, endNo, 0, 0, iter, NULL);
	++entered;

	if (GroupMatch && WildCmp(GroupMatch, group) != 0)
	    continue;

	if (convert) {
	    ConvertHash(group, &SrcHash, &DstHash, iter, begNo, endNo);
	    if (++count % 1000 == 0)
		printf("Scanned %d groups\n", count);
	}

	if (MustExit)
	    break;
    }
    ActiveSeq = seq;
    return(entered);
}

/*
 * RescanActive() - a file whose group is not in the table may belong to a
 * group created since the scan at the start (a newgroup control, the
 * admin): its overview must not be deleted.  A file name holds only a hash
 * of the group name, so the group is looked up by entering the groups that
 * were added since.  The active file only grows by appends, each of which
 * bumps its append count, so the scan is repeated only when the count has
 * changed since the last one (D-285).  Returns 1 when groups were entered.
 */
int
RescanActive(void)
{
    if (KPDBAppendCount(KDBActive) == ActiveSeq)
	return(0);
    return(ScanActive(0, 1) > 0);
}

Group *
FindGroupByName(const char *groupName)
{
    hash_t hv = hhash(GFHash(groupName, &DOpts.ReaderGroupHashMethod));
    Group *group;

    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_GroupName, groupName) == 0)
	    break;
    }
    return(group);
}

/*
 * IterInProgress() - the file of iteration iter for Hash belongs to no group
 * in the table, but a group we know by Hash may be taking it right now.  A
 * group whose hash is already used by another gets the next iteration: the
 * writer (dreaderd, dcancel, dgrpctl) creates the files of iteration n and
 * writes ITER=n into dactive.kp only afterwards, so a file of the group's
 * current iteration or a higher one can be that group's, and an append count
 * that has not changed yet does not show it.  Files of a lower iteration
 * than the group has now stay stale (a group removed and created again).
 * ITER is read from the live record, not from the table (D-285).
 */
int
IterInProgress(char *Hash, int iter)
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
 * OverFileIsGroups() - the over file of iteration iter for group exists and
 * its header names the group.  Called in the directory of the file.  A header
 * of version 1 has no name: no group owns it by this.
 */
int
OverFileIsGroups(const Group *group, int iter)
{
    const char *ovpath = GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 0, iter,
						&DOpts.ReaderGroupHashMethod);
    OverHead oh;
    int fd;
    int r = 0;

    if ((fd = open(ovpath, O_RDONLY)) >= 0) {
	r = (read(fd, &oh, sizeof(oh)) == sizeof(oh) &&
	    oh.oh_Version > 1 && oh.oh_Version <= OH_VERSION &&
	    oh.oh_ByteOrder == OH_BYTEORDER &&
	    strncmp(oh.oh_Gname, group->gr_GroupName, sizeof(oh.oh_Gname)) == 0);
	close(fd);
    }
    return(r);
}

/*
 * HierDirOf() - the directory below the group home of the files of the group
 * name in the hierarchy layout method, as GFName() (wantdir 1) puts them,
 * without the trailing '/' ("" for a group of one component).  0 when the
 * path does not fit.
 */
static int
HierDirOf(const char *name, GroupHashType *method, char *dir, size_t size)
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
 * HierDirClass() - where a hierarchy file in dirPath is for the group name
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
 * The same as that of dexpireover.
 */
static int
HierDirClass(const char *name, const char *dirPath)
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
	if (!HierDirOf(name, pass ? &other : &DOpts.ReaderGroupHashMethod,
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
 * HierGroupKnown() - the group name exists: it is in the table, or was
 * created since it was read (RescanActive()).
 */
static int
HierGroupKnown(const char *name, const char *dirPath)
{
    (void)dirPath;
    if (FindGroupByHash((char *)name, 0, 1) != NULL)
	return(1);
    return(RescanActive() && FindGroupByHash((char *)name, 0, 1) != NULL);
}

/*
 * HierOtherOver() - the group name has an over file in its directory of the other
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
HierOtherOver(const char *name)
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
 * FindGroupByHash() - the group a file belongs to.  The hash in a file name
 * does not tell groups apart whose names have the same hash: the iteration
 * does, but a group that has no over file yet has iteration 0 like the group
 * that holds the hash.  Of several groups with this hash and iteration the
 * one whose over file names it is the owner of the files (D-313); else the
 * first, as before.  hier: the file name is a hierarchy name, which has no
 * iteration, so the group's ITER (it keeps the iteration of its files for a
 * conversion back to crc or md5) is not compared (D-314).
 */
Group *
FindGroupByHash(char *Hash, int iter, int hier)
{
    Group *group;
    Group *first = NULL;
    hash_t hv = hhash(Hash);
    int n = 0;

    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0 && (hier || group->gr_Iter == iter)) {
	    if (first == NULL)
		first = group;
	    ++n;
	}
    }
    if (n < 2)
	return(first);
    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0 &&
	    (hier || group->gr_Iter == iter) &&
					OverFileIsGroups(group, iter))
	    return(group);
    }
    return(first);
}

/*
 * LayoutDirName() - name is the name of a top-level directory of the group
 * home that the layout (readergrouphash) can have made, as GFName() builds
 * it (D-326):
 *	crc		"%02x" of a byte: two lowercase hex digits
 *	md5 "/N"	"%0Wx" of a bucket below N: W lowercase hex digits,
 *			W the decimal digits of N-1 (at least 1), with a value
 *			below N ("md5-32/1" makes only "0")
 *	md5-32 "\N"	the first N characters of the base32 hash: [a-z0-5]
 *	md5-64 "\N"	the first N characters of the base64 hash: [a-zA-Z0-9+-]
 *			(N at most :B minus 1 and the 22 characters of the hash)
 *	hierarchy	a group name component or the components joined by "."
 *			(a one-character component is joined to the next:
 *			"a.bcd"): entered only when it starts with a letter
 *			or digit, so ".hidden" and the junk files of
 *			dexpireover -R are not; "lost+found" is left alone (a
 *			group "lost+found.x.y" is not expected).  A group name
 *			component may also start with '+', '-' or '_' (lib
 *			ValidGroupName() accepts them): such groups are not
 *			cleaned, as on main (D-330)
 *	hierarchy-full	the first component of the group name alone ("a" for
 *			"a.b.c", "e2e" for "e2e.x.gz"), by the same first-
 *			character rule as hierarchy
 * The length is the exact length of the layout's first level: a directory
 * that is not ours, such as lost+found, can have the right characters.
 */
static int
LayoutDirName(const char *name)
{
    const GroupHashType *m = &DOpts.ReaderGroupHashMethod;
    static const char hexdigits[] = "0123456789abcdef";
    static const char b32[] = "abcdefghijklmnopqrstuvwxyz012345";
    static const char b64[] = "abcdefghijklmnopqrstuvwxyz0123456789"
			      "ABCDEFGHIJKLMNOPQRSTUVWXYZ+-";
    const char *alpha = "";
    size_t len = strlen(name);
    size_t want = 0;
    unsigned long bound = 0;	/* numbered md5: buckets 0 .. bound-1 */

    switch (m->gh_type) {
	case HASHGRP_HIER:
	    return(isalnum((unsigned char)name[0]) &&
					strcmp(name, "lost+found") != 0);
	case HASHGRP_CRC:
	    want = 2;
	    alpha = hexdigits;
	    break;
	case HASHGRP_32MD5:
	case HASHGRP_64MD5:
	    if (m->gh_dirlvl < 1 || m->gh_dirinfo[0] < 1)
		return(0);
	    if (m->gh_dirtype == HASHGRP_DIR_NUM) {
		int c = m->gh_dirinfo[0];

		alpha = hexdigits;
		bound = (unsigned long)m->gh_dirinfo[0];
		while (c > 0) {
		    ++want;
		    c = (c - 1) / 10;
		}
	    } else {
		alpha = (m->gh_type == HASHGRP_64MD5) ? b64 : b32;
		want = (size_t)m->gh_dirinfo[0];
		if (m->gh_sigbytes > 0 && want > (size_t)m->gh_sigbytes - 1)
		    want = (size_t)m->gh_sigbytes - 1;
		if (want > 22)		/* the length of the md5 hash */
		    want = 22;
	    }
	    break;
	default:
	    return(0);
    }
    if (len == 0 || len != want || strspn(name, alpha) != len)
	return(0);
    if (bound > 0) {
	/* W hex digits can be more than the bucket count (W >= 2 for N > 10) */
	char *end;
	unsigned long v;

	errno = 0;
	v = strtoul(name, &end, 16);
	if (errno != 0 || *end != 0 || v >= bound)
	    return(0);
    }
    return(1);
}

/*
 * OldDirName() - main's rule for the top-level directories of the group
 * home: the first two characters are alphanumeric.  A symlink of such a
 * name is followed, as on main (an admin can spread the overview over
 * disks that way); a name that only LayoutDirName() accepts must be a
 * real directory (D-326).
 */
static int
OldDirName(const char *name)
{
    return(isalnum((unsigned char)name[0]) && isalnum((unsigned char)name[1]));
}

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

void
CleanSpool(char *cleanDir)
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
	int isLink;
	DirFrame top;

	while ((den = readdir(dir)) != NULL) {
	    /*
	     * Only a directory the layout can have made (D-326).  Not by
	     * the first two characters: md5 "\1" and the hierarchy layout
	     * ("a.bcd.efg" lives in "a.bcd/") have names that do not start
	     * with two alphanumerics.  scanDirectory() removes every file
	     * and entry of a directory that is not a group file, so a
	     * directory that is not ours (lost+found) must not be entered.
	     */
	    if (!LayoutDirName(den->d_name) || lstat(den->d_name, &st) != 0)
		continue;
	    /*
	     * A symlink is followed only for a name main followed: a name
	     * that only the layout rule accepts ("a.bcd", md5 "\1") must
	     * be a real directory, so that a link out of the group home
	     * is not scanned (scanDirectory() removes what it does not
	     * parse).
	     */
	    isLink = S_ISLNK(st.st_mode);
	    if (isLink && !OldDirName(den->d_name)) {
		if (VerboseOpt)
		    printf("Skipping symlink %s/%s: followed only when the name starts with two alphanumerics\n", PatExpand(GroupHomePat), den->d_name);
		continue;
	    }
	    if (isLink && stat(den->d_name, &st) != 0)
		continue;
	    if (S_ISDIR(st.st_mode) && DirOnPath(&home, &st)) {
		if (VerboseOpt)
		    printf("Skipping directory loop %s/%s: it is the group home\n", PatExpand(GroupHomePat), den->d_name);
		continue;
	    }
	    if (S_ISDIR(st.st_mode)) {
		top.df_Dev = st.st_dev;
		top.df_Ino = st.st_ino;
		top.df_Up = &home;
		/*
		 * Below a directory that main did not enter (a name only the
		 * layout rule accepts), no symlink is followed, opened or
		 * removed either (#515 r3, r4): main never scanned such a
		 * tree, so a link out of it would be a new way to remove or
		 * change files outside the group home.  The trees main
		 * entered follow links as before.
		 */
		scanDirectory(PatExpand(GroupHomePat), den->d_name, &level,
					!OldDirName(den->d_name), &top);
		/*
		 * This is a bit of a hack, but we try to remove the
		 * directory in case it is empty. If it is not empty, the
		 * remove should fail silently - at least we hope so!
		 * Not a symlink: remove() would unlink the link itself.
		 */
		if (ForReal && !isLink) {
		    if (remove(den->d_name) == 0)
			printf("Removed empty directory: %s/%s\n", PatExpand(GroupHomePat), den->d_name);
		    else if (errno != ENOTEMPTY && errno != EEXIST) {
			printf("Unable to remove directory %s/%s: %s\n", PatExpand(GroupHomePat), den->d_name, strerror(errno));
			++Failures;
		    }
		}
	    }
	}
	closedir(dir);
    }
    /*
     * The files at the top of the group home (D-338): hierarchy and
     * hierarchy-full put there the files of the groups whose directory is
     * the top (plain hierarchy: two components, the first of one
     * character, x.y, a.b, x.yz; both: one component, junk).  With plain
     * hierarchy a group x of one character shares the top with x.gz, so
     * "d.<n>.x.gz" is judged by both groups (D-318), as in a directory.  crc and md5 put
     * no file there.
     */
    if (DOpts.ReaderGroupHashMethod.gh_type == HASHGRP_HIER)
	CleanTop();
}

/*
 * CleanTop() - the files at the top of the group home, the current
 * directory (D-338).  The top also holds the directories of the groups and
 * an admin's own files, so unlike scanDirectory() only the hierarchy names
 * (HierName()) of regular files are looked at, by ProcessFile() with top
 * set, which acts only on the files of groups whose directory is the top;
 * and the temporary names of dexpireover -R and -z that TopTempName()
 * accepts are removed.  Two passes: the over files first, so that a data
 * file of a removed group is judged after its over file (TopGone()).  Main
 * never looked at the files there, so as for the trees #515 added no
 * symlink is followed, opened or removed (lstat(), a dangling one too),
 * and nothing but a regular file is opened (a FIFO would block the
 * open()).  No directory is entered or removed, and nothing else is
 * reported or removed.
 */
static void
CleanTop(void)
{
    const char *home = PatExpand(GroupHomePat);
    DIR *dir;
    den_t *den;
    struct stat st;
    int pass;

    if ((dir = opendir(".")) == NULL) {
	printf("Unable to open %s: %s\n", home, strerror(errno));
	++Failures;
	return;
    }
    for (pass = 1; pass <= 2; ++pass) {
	rewinddir(dir);
	while ((den = readdir(dir)) != NULL) {
	    int temp = (pass == 2 && TopTempName(den->d_name));
	    int ht = temp ? 0 : HierName(den->d_name);

	    /* pass 1: over files; pass 2: temporary and data files */
	    if (!temp && (ht == 0 || (ht == 1) != (pass == 1)))
		continue;
	    if (lstat(den->d_name, &st) != 0)
		continue;
	    if (S_ISLNK(st.st_mode)) {
		if (VerboseOpt)
		    printf("Skipping symlink %s/%s: not followed at the top of the group home\n", home, den->d_name);
		continue;
	    }
	    if (!S_ISREG(st.st_mode))
		continue;
	    if (temp) {
		printf("Deleting old temp file: %s/%s\n", home, den->d_name);
		DeleteFile(home, den->d_name);
	    } else
		ProcessFile(home, den->d_name, 1);
	    if (MustExit)
		exit(1);
	}
    }
    closedir(dir);
}

/*
 * HierName() - name parses as a hierarchy over (1) or data (2) file name
 * (in the configured layout's order, D-335), not as a crc or md5 one (0).
 */
static int
HierName(const char *name)
{
    char Hash[PATH_MAX];
    artno_t artBase = -1;
    int iter = 0;
    int type = ExtractGroupHashInfo(name, DOpts.ReaderGroupHashMethod.gh_type,
						Hash, &artBase, &iter);

    if (type == HASHGRP_NONE ||
	(type & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) != HASHGRP_HIER)
	return(0);
    return((type & HASHGRPTYPE_OVER) ? 1 : 2);
}

/*
 * TopOverIrregular() - group's over file (iteration iter, as
 * DataFileStale() opens it), when its directory is the top of the group
 * home, is there but no regular file (a link, a FIFO; lstat(), D-338),
 * with its path in bad.  0 for a group in a subdirectory.
 */
static int
TopOverIrregular(const Group *group, int iter, char *bad, size_t size)
{
    const char *gf = GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 1, iter,
						&DOpts.ReaderGroupHashMethod);
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
 * top are removed in the second pass of CleanTop().  A data file name alone
 * is no evidence (an admin's d.0.junk~ is the data file name of a group
 * junk~).  As in dexpireover.
 */
static char **TopGoneList;
static int TopGoneCount;

static void
TopGoneAdd(const char *name)
{
    char **n;

    if (TopGone(name))
	return;
    if ((n = realloc(TopGoneList, (TopGoneCount + 1) * sizeof(*TopGoneList))) == NULL)
	return;			/* the data files then stay: no harm */
    TopGoneList = n;
    if ((TopGoneList[TopGoneCount] = strdup(name)) != NULL)
	++TopGoneCount;
}

static int
TopGone(const char *name)
{
    int i;

    for (i = 0; i < TopGoneCount; ++i) {
	if (strcmp(TopGoneList[i], name) == 0)
	    return(1);
    }
    return(0);
}

/*
 * TopTempName() - name, at the top of the group home, is a temporary file
 * of dexpireover -R or -z (D-338): a '.' and exactly the over or data file
 * name GFName() gives a group in the table whose directory is the top, or
 * such a data file name and ".gz" (both readings of a hierarchy ".gz" name,
 * D-318), as dexpireover's tempRewriteName() takes them.  Not by the name
 * alone: ".o.0.junk~" reads as a name of the group junk~.  Below the top
 * scanDirectory() removes every name that does not parse, these among them.
 */
static int
TopTempName(const char *name)
{
    char Hash[PATH_MAX];
    char HashZ[PATH_MAX];
    artno_t artBase = -1;
    int iter = 0;
    int type;
    int gftype;
    int z;

    if (name[0] != '.')
	return(0);
    Hash[0] = HashZ[0] = 0;
    type = ExtractGroupHashInfoGz(name + 1, DOpts.ReaderGroupHashMethod.gh_type,
					Hash, HashZ, &artBase, &iter);
    if (type == HASHGRP_NONE ||
	(type & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) != HASHGRP_HIER)
	return(0);
    gftype = (type & HASHGRPTYPE_OVER) ? GRPFTYPE_OVER : GRPFTYPE_DATA;
    for (z = 0; z < 2; ++z) {
	const char *g = z ? HashZ : Hash;
	const char *gf;
	size_t len;

	if (g[0] == 0 || FindGroupByName(g) == NULL)
	    continue;
	/* "d.<n>.X.gz": the plain data of X.gz (Hash) or X's ".gz" (HashZ) */
	gf = GFName(g, gftype, artBase, 1, 0, &DOpts.ReaderGroupHashMethod);
	len = strlen(gf);
	if (strncmp(gf, name + 1, len) == 0 &&
	    strcmp(name + 1 + len, z ? ".gz" : "") == 0)
	    return(1);
    }
    return(0);
}

/*
 * OverNamesGroup() - the over file path has a header of version 2 or later
 * that names the group name (D-338).  A header of version 1 names no group,
 * so it does not tell: 0, as for a file that cannot be read.
 */
static int
OverNamesGroup(const char *path, const char *name)
{
    OverHead oh;
    int fd;
    int r = 0;

    /* O_NONBLOCK: a FIFO must not block (CleanTop() checks for a regular file) */
    if ((fd = open(path, O_RDONLY | O_NONBLOCK)) >= 0) {
	if (read(fd, &oh, sizeof(oh)) == sizeof(oh) &&
	    oh.oh_Version >= 2 && oh.oh_Version <= OH_VERSION &&
	    oh.oh_ByteOrder == OH_BYTEORDER &&
	    strlen(name) < sizeof(oh.oh_Gname) &&
	    strncmp(oh.oh_Gname, name, sizeof(oh.oh_Gname)) == 0)
	    r = 1;
	close(fd);
    }
    return(r);
}

/*
 * scanDirectory() - scan dirpath/dirname for group files and remove the
 * files of no group.  With noLinks every child that is a symlink, of any
 * type and also a dangling one, is skipped: not followed, not opened, not
 * removed (#515 r4).  Without noLinks child links are followed, as on
 * main.  dirs is the path from the group home down to dirname itself: a
 * child directory on it (a link such as cd -> . or up -> ..) is skipped,
 * not scanned and not removed, as the recursion would not end there.
 */
void
scanDirectory(const char *dirpath, char *dirname, int *level, int noLinks,
						const DirFrame *dirs)
{
    DIR *dir2;
    char path[PATH_MAX];
    char origpath[PATH_MAX];
    int n;

    n = snprintf(path, sizeof(path), "%s/%s", dirpath, dirname);
    if (n < 0 || (size_t)n >= sizeof(path)) {
	fprintf(stderr, "Skipping overlong directory pathname: %s/%s\n", dirpath, dirname);
	return;
    }

    /*
     * Return to where we came from, not with chdir(".."): when dirname is
     * a symlink, ".." of its target is not this directory, and the scan
     * of the group home would go on there (as dexpireover does).
     */
    if (getcwd(origpath, sizeof(origpath)) == NULL) {
	printf("Unable to getcwd(%s): %s\n", dirname, strerror(errno));
	return;
    }
    if (chdir(dirname) != 0) {
	printf("Unable to chdir(%s)\n", dirname);
	return;
    }

    if ((dir2 = opendir(".")) != NULL) {
	den_t *den2;
	struct stat st;
	int isLink;
	DirFrame sub;

	while ((den2 = readdir(dir2)) != NULL) {
	    if (strcmp(den2->d_name, ".") == 0 || strcmp(den2->d_name, "..") == 0)
		continue;
	    if (lstat(den2->d_name, &st) != 0) {
		printf("Unable to stat(%s/%s)\n", dirname, den2->d_name);
		continue;
	    }
	    isLink = S_ISLNK(st.st_mode);
	    if (isLink && noLinks) {
		if (VerboseOpt)
		    printf("Skipping symlink %s/%s: not followed below a top-level directory whose name does not start with two alphanumerics\n", path, den2->d_name);
		continue;
	    }
	    if (isLink && stat(den2->d_name, &st) != 0) {
		printf("Unable to stat(%s/%s)\n", dirname, den2->d_name);
		continue;
	    }
	    if (S_ISDIR(st.st_mode) && DirOnPath(dirs, &st)) {
		if (VerboseOpt)
		    printf("Skipping directory loop %s/%s: already on the path from the group home\n", path, den2->d_name);
		continue;
	    }
	    if (S_ISDIR(st.st_mode)) {
		sub.df_Dev = st.st_dev;
		sub.df_Ino = st.st_ino;
		sub.df_Up = dirs;
		if (*level <= 2)
		    scanDirectory(path, den2->d_name, level, noLinks, &sub);
		/*
		 * This is a bit of a hack, but we try to remove the
		 * directory in case it is empty. If it is not empty, the
		 * remove should fail silently - at leat we hope so!
		 * Not a symlink: remove() would unlink the link itself.
		 */
		if (ForReal && !isLink) {
		    if (remove(den2->d_name) == 0)
			printf("Removed empty directory: %s/%s\n", dirpath, den2->d_name);
		    else if (errno != ENOTEMPTY && errno != EEXIST) {
			printf("Unable to remove directory %s/%s: %s\n", dirpath, den2->d_name, strerror(errno));
			++Failures;
		    }
		}
	    } else if (S_ISREG(st.st_mode)) {
		ProcessFile(path, den2->d_name, 0);
	    } else {
		if (VerboseOpt)
		    printf("Removing unknown file: %s/%s\n", dirname, den2->d_name);
		DeleteFile(path, den2->d_name);
	    }
	    if (MustExit)
		exit(1);
	}
	closedir(dir2);
    }
    if (chdir(origpath) < 0) {
	fprintf(stderr, "Unable to return with chdir(%s): %s\n", origpath, strerror(errno));
	exit(1);
    }
}

int
DeleteFile(const char *dirPath, const char *name)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);
    if (ForReal && remove(path) != 0) {
	printf("Unable to remove %s: %s\n", path, strerror(errno));
	++Failures;
	return(-1);
    }
    ++RemovedFiles;
    return(0);
}


/*
 * DataFileStale() - the data file path (artBase, iteration iter) is outside
 * the range of group, or is not aligned to its over file's slots.  OD_HARTS
 * (the data entries of the over file) is a fudge to make 100% sure we do
 * not delete a brand new data file.  A group that left the active file
 * after the scan began is not judged: the file is not stale for it.
 */
static int
DataFileStale(Group *group, artno_t artBase, int iter, const char *path)
{
    int recLen;
    int dataEntries;
    const char *ovpath;
    const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
                                                        KP_LOCK, &recLen);

    if (rec == NULL) {
	if (VerboseOpt)
	    printf("group %s no longer in the active file, skipping %s\n",
		group->gr_GroupName, path);
	return(0);
    }
    /*
     * Refresh the EndNo
     */
    group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE",
							NULL, "-1"), NULL, 10);
    KPDBUnlock(KDBActive, rec);

    ovpath = GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 0, iter,
						&DOpts.ReaderGroupHashMethod);
    dataEntries = getDataEntries(ovpath);
    /* same criteria as dexpireover: also misaligned or empty ranges */
    if (artBase + dataEntries <= group->gr_StartNo ||
	artBase >= group->gr_EndNo + dataEntries ||
	group->gr_StartNo > group->gr_EndNo ||
	(artBase & (dataEntries - 1))
    ) {
	if (DebugOpt)
	    printf("artBase=%lld, dataEntries=%d group->gr_StartNo=%lld group->gr_EndNo=%lld\n",
			artBase, dataEntries, group->gr_StartNo, group->gr_EndNo);
	return(1);
    }
    return(0);
}

/*
 * ProcessFile() - process over. and data. files.  All over. files
 *			   are processed first. 
 *
 */

void
ProcessFile(const char *dirPath, const char *name, int top)
{
    artno_t artBase = -1;
    Group *group = NULL;
    Group *groupZ = NULL;
    char path[PATH_MAX];
    char Hash[PATH_MAX];
    char HashZ[PATH_MAX];
    int iter = 0;
    int type = 0;
    int hier;
    int otherDir = 0;

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);

    if (DebugOpt > 0)
	printf("ProcessOverviewFile(%s)\n", path);

    ++TotalFiles;

    bzero(Hash, sizeof(Hash));
    bzero(HashZ, sizeof(HashZ));

    /*
     * The names of the configured layout first (D-335); with convert that
     * is the destination method (main() copies it to DOpts), the layout of
     * the files after the conversion.
     */
    if ((type = ExtractGroupHashInfoGz(name, DOpts.ReaderGroupHashMethod.gh_type, Hash, HashZ, &artBase, &iter)) == HASHGRP_NONE) {
	printf("Deleting unknown/stale file: %s/%s\n", dirPath, name);
	DeleteFile(dirPath, name);
	return;
    }

    if (DebugOpt > 2)
	printf("File: %s  type=%d  artBase=%lld  iter=%d  Hash=%s\n",
					name, type, artBase, iter, Hash);

    hier = ((type & ~(HASHGRPTYPE_OVER | HASHGRPTYPE_DATA)) == HASHGRP_HIER);
    /*
     * A hierarchy data file "d.<n>.X.gz" is the plain data file of group
     * "X.gz" or the compressed one of group "X" (D-318): HashZ is "X".  The
     * file is kept for as long as it is not stale for every one of the
     * two groups that exist.  Either group may have been created since the
     * scan, so a reading that is missing causes the rescan, also when the
     * other one was found: the file must not be judged by one group alone
     * while the other, created in between, is not known yet.
     */
    /*
     * A hierarchy name is the file of a group only in the group's directory
     * (D-336), and the directory depends on the layout (HierDirClass()):
     *  - a reading whose group (in the table, or not known with -w) has the
     *    file's directory in the other hierarchy layout only leaves the file
     *    alone: it may be the live file of a dreaderd started before
     *    readergrouphash was changed, or a copy left by the switch (D-337).
     *    It is neither expired nor removed.  Except when the other reading
     *    is of the configured layout and the group of the first has no over
     *    file in its directory of the other layout (HierOtherOver()): then no
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
	int c = HierDirClass(Hash, dirPath);
	int cZ = HashZ[0] ? HierDirClass(HashZ, dirPath) : HIERDIR_OFF;
	const char *other = NULL;

	if (c == HIERDIR_OTHER && HierGroupKnown(Hash, dirPath))
	    other = Hash;
	else if (cZ == HIERDIR_OTHER && HierGroupKnown(HashZ, dirPath))
	    other = HashZ;
	/*
	 * With clean -o dreaderd is stopped (main()), so no writer of the
	 * other layout is left: the over file there is a leftover that this
	 * run removes (-n: would remove), and the file is judged as without
	 * it, by both readings (D-337).
	 */
	if (other != NULL && HashZ[0] &&
	    (other == Hash ? cZ : c) == HIERDIR_IN &&
	    (CleanOther || !HierOtherOver(other))) {
	    /* no writer of the other layout: both readings, D-318 */
	    c = cZ = HIERDIR_IN;
	    other = NULL;
	}
	if (other != NULL) {
	    Group *og = CleanOther ? FindGroupByHash((char *)other, 0, 1) : NULL;

	    /*
	     * clean -o (D-337): the file of a group in the active file in its
	     * directory of the other hierarchy layout, whose other reading
	     * (D-318) is not of the configured layout (that case is above),
	     * is a leftover of a switch made by editing readergrouphash.
	     * -w: only the files of the groups it matches.
	     */
	    if (og != NULL &&
		(GroupMatch == NULL || WildCmp(GroupMatch, og->gr_GroupName) == 0)) {
		printf("Removing file of %s in the directory of the other hierarchy layout: %s\n",
							og->gr_GroupName, path);
		DeleteFile(dirPath, name);
		return;
	    }
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
	group = FindGroupByHash(Hash, iter, hier);
	if (HashZ[0])
	    groupZ = FindGroupByHash(HashZ, iter, hier);
    }
    if (!otherDir && (group == NULL || (HashZ[0] && groupZ == NULL)) && RescanActive()) {
	/* created since the scan: look up again what was missing */
	if (group == NULL)
	    group = FindGroupByHash(Hash, iter, hier);
	if (HashZ[0] && groupZ == NULL)
	    groupZ = FindGroupByHash(HashZ, iter, hier);
    }
    /*
     * Not for a hierarchy name: it has no iteration, and FindGroupByHash()
     * finds any group of the name, so IterInProgress() could only find the
     * group of a file in another directory (D-336), which no ITER changes.
     */
    if (group == NULL && groupZ == NULL && !hier && IterInProgress(Hash, iter)) {
	/* a known group is taking this iteration (D-285) */
	if (VerboseOpt)
	    printf("file of a group changing its iteration, left alone: %s\n", path);
	return;
    }
    if (group == NULL && groupZ == NULL && top &&
	(type & HASHGRPTYPE_OVER) && !OverNamesGroup(path, Hash)) {
	/*
	 * The top of the group home is where an admin keeps files too: an
	 * over file there is removed as the file of a group that is gone
	 * only when its header names that group (D-338), so a copy such as
	 * o.0.junk~ (the over file name of a group junk~, whose directory is
	 * the top too) stays.
	 */
	if (VerboseOpt)
	    printf("over file of no group at the top of the group home whose header does not name %s, left alone: %s\n", Hash, path);
	return;
    }
    if (group == NULL && groupZ == NULL && top &&
	(type & HASHGRPTYPE_DATA) && !TopGone(Hash) &&
	!(HashZ[0] && TopGone(HashZ))) {
	/*
	 * A data file of no group at the top is removed only when this run
	 * removed that group's over file at the top (the first pass of
	 * CleanTop()): its header named the group, so the group existed.
	 * Else the name is all there is, and an admin's d.0.junk~ stays
	 * (D-338).
	 */
	if (VerboseOpt)
	    printf("data file of no group at the top of the group home with no over file of that group, left alone: %s\n", path);
	return;
    }
    if (group == NULL && groupZ == NULL) {
	/*
	 * If we gave a wildcard, we can't remove stale groups because
	 * we do not have a full group list.
	 */

	++BadGroups;
	printf("Group for over file not found, removing %s\n", path);
	/*
	 * At the top the group counts as removed (its data files go in the
	 * second pass of CleanTop()) only when its over file is gone: when the
	 * removal fails, the data files stay with it (D-338).  DeleteFile()
	 * gives 0 with -n, so -n reports what a run would do.
	 */
	if (DeleteFile(dirPath, name) == 0 && top && (type & HASHGRPTYPE_OVER))
	    TopGoneAdd(Hash);
	return;
    }
    if (group == NULL) {
	group = groupZ;
	groupZ = NULL;
    }

    if (DebugOpt > 2)
	printf("Maps to group: %s\n", group->gr_GroupName);

    if (type & HASHGRPTYPE_OVER) {
	/*
	 * over. file	(fixed length file)
	 */
	int fd;
	OverHead oh;

	if ((fd = open(path, O_RDWR)) >= 0) {
	    int r;
	    r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	    if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
		r = 0;
	    if (r && oh.oh_Version > 1 &&
				strcmp(oh.oh_Gname, group->gr_GroupName) != 0)
		r = 0;
	    if (r == 0) {
		printf("group %s, file \"%s\" bad file header - removing\n",
		    group->gr_GroupName,
		    path
		);
		if (oh.oh_Version > OH_VERSION)
		    printf("   expected version %d, got version %d\n",
						OH_VERSION, oh.oh_Version);
		DeleteFile(dirPath, name);
	    }
	    close(fd);
	}
    } else if (type & HASHGRPTYPE_DATA) {
	char why[PATH_MAX];

	/*
	 * At the top (D-338) the data file is judged by the header of its
	 * group's over file, which DataFileStale() opens by its name: that
	 * must be a regular file (a FIFO there would block the open(), a
	 * link would be read); else the data file is left as it is.
	 */
	if (top && (TopOverIrregular(group, iter, why, sizeof(why)) ||
		    (groupZ != NULL && TopOverIrregular(groupZ, iter, why, sizeof(why))))) {
	    if (VerboseOpt)
		printf("data file left alone: the over file %s of its group is not a regular file: %s\n",
								why, path);
	    return;
	}
	/*
	 * data. file, modulo OD_HARTS.
	 */
	if (DataFileStale(group, artBase, iter, path) &&
			(groupZ == NULL || DataFileStale(groupZ, artBase, iter, path))) {
	    printf("Deleting stale overview data %s: %s\n",
		group->gr_GroupName,
		path
	    );
	    DeleteFile(dirPath, name);
	} 
    } else {
	printf("Bad Error: Unknown HASHGRPTYPE\n");
    }
}

/*
 * ReaderRunning() - a dreaderd of this site may be running (clean -o,
 * D-337), with the reason in why.  Two signs, both of dreaderd's own:
 *  - its pid file <path_log>/dreaderd.pid, which it writes when it starts
 *    and leaves behind when it exits, names a live process (kill(pid, 0);
 *    EPERM: a process of another user).  A pid that a new process got
 *    after dreaderd died counts too: the admin removes the stale file;
 *  - its control socket (dreader_socket, path_run; drcmd uses it) accepts
 *    a connection.  dreaderd binds it after writing the pid file and
 *    before it forks the processes that write the overview; a socket left
 *    behind refuses the connection (ECONNREFUSED).
 * When it cannot be told (the pid file cannot be read or holds no pid,
 * connect() fails otherwise), 1: a pid file of a crashed dreaderd that
 * holds no pid must be removed by hand.  A socket path too long is no
 * sign: dreaderd refuses to start with it.
 */
static int
ReaderRunning(char *why, size_t size)
{
    char pidPath[PATH_MAX];
    struct sockaddr_un soun;
    const char *sockpath;
    FILE *fi;
    int fd;
    int n;

    n = snprintf(pidPath, sizeof(pidPath), "%s/dreaderd.pid", PatExpand(LogHomePat));
    if (n < 0 || (size_t)n >= sizeof(pidPath)) {
	snprintf(why, size, "the path of dreaderd.pid is too long");
	return(1);
    }
    if ((fi = fopen(pidPath, "r")) != NULL) {
	long pid = 0;
	int ok = (fscanf(fi, "%ld", &pid) == 1 && pid > 0 && pid <= 0x7fffffffL);

	fclose(fi);
	if (!ok) {
	    snprintf(why, size, "%s holds no pid; remove it if no dreaderd runs", pidPath);
	    return(1);
	}
	if (kill((pid_t)pid, 0) == 0 || errno == EPERM) {
	    snprintf(why, size, "%s names the live process %ld; remove the file if that is no dreaderd", pidPath, pid);
	    return(1);
	}
    } else if (errno != ENOENT) {
	snprintf(why, size, "cannot read %s: %s", pidPath, strerror(errno));
	return(1);
    }

    sockpath = PatRunExpand(DReaderSocketPat);
    memset(&soun, 0, sizeof(soun));
    if (strlen(sockpath) >= sizeof(soun.sun_path))
	return(0);
    soun.sun_family = AF_UNIX;
    snprintf(soun.sun_path, sizeof(soun.sun_path), "%s", sockpath);
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	snprintf(why, size, "socket: %s", strerror(errno));
	return(1);
    }
    if (connect(fd, (struct sockaddr *)&soun,
		offsetof(struct sockaddr_un, sun_path[strlen(soun.sun_path) + 1])) == 0) {
	close(fd);
	snprintf(why, size, "its control socket %s accepts connections", sockpath);
	return(1);
    }
    n = errno;
    close(fd);
    if (n == ECONNREFUSED || n == ENOENT)
	return(0);
    snprintf(why, size, "cannot tell by its control socket %s: %s", sockpath, strerror(n));
    return(1);
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

    if ((fd = open(path, O_RDWR)) >= 0) {
	int r;
	r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
	    r = 0;
	if (r == 1 && oh.oh_Version >= 3 && oh.oh_DataEntries > 0 &&
	    (oh.oh_DataEntries & (oh.oh_DataEntries - 1)) == 0)
	    de = oh.oh_DataEntries;
	close(fd);
    }
    return(de);
}
