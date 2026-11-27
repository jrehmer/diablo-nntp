
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
void ProcessFile(const char *dirPath, const char *name);
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
    printf("Usage: %s [-f activefile][-m][-n][-w wild][-v] action params ...\n", progname);
    printf("    where:\n");
    printf("            -f file    set the active file to use\n");
    printf("            -w wild    limit the set of groups to work on\n");
    printf("            -n         don't actually make any changes (dummy run)\n");
    printf("            -m         move files with a file copy, instead of renaming them\n");
    printf("                          (needed when converting across filesystems)\n");
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
		i++;
		if (i < ac)
		    CleanDir = av[i];
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
		ProcessFile(path, den2->d_name);
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
ProcessFile(const char *dirPath, const char *name)
{
    artno_t artBase = -1;
    Group *group;
    Group *groupZ = NULL;
    char path[PATH_MAX];
    char Hash[PATH_MAX];
    char HashZ[PATH_MAX];
    int iter = 0;
    int type = 0;
    int hier;

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);

    if (DebugOpt > 0)
	printf("ProcessOverviewFile(%s)\n", path);

    ++TotalFiles;

    bzero(Hash, sizeof(Hash));
    bzero(HashZ, sizeof(HashZ));

    if ((type = ExtractGroupHashInfoGz(name, Hash, HashZ, &artBase, &iter)) == HASHGRP_NONE) {
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
    group = FindGroupByHash(Hash, iter, hier);
    if (HashZ[0])
	groupZ = FindGroupByHash(HashZ, iter, hier);
    if ((group == NULL || (HashZ[0] && groupZ == NULL)) && RescanActive()) {
	/* created since the scan: look up again what was missing */
	if (group == NULL)
	    group = FindGroupByHash(Hash, iter, hier);
	if (HashZ[0] && groupZ == NULL)
	    groupZ = FindGroupByHash(HashZ, iter, hier);
    }
    if (group == NULL && groupZ == NULL && IterInProgress(Hash, iter)) {
	/* a known group is taking this iteration (D-285) */
	if (VerboseOpt)
	    printf("file of a group changing its iteration, left alone: %s\n", path);
	return;
    }
    if (group == NULL && groupZ == NULL) {
	/*
	 * If we gave a wildcard, we can't remove stale groups because
	 * we do not have a full group list.
	 */

	++BadGroups;
	printf("Group for over file not found, removing %s\n", path);
	DeleteFile(dirPath, name);
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
