
/*
 * OpenCache()
 *
 *	return +1	if a valid cache file was found, set *pcfd, *psize
 *	return 0	if a valid cache file was not found or if an
 *			empty file is found (uncacheable article)
 *
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"
#include <stdint.h>

Prototype int OpenCache(const char *msgid, int *pcfd, int *psize);
Prototype void CreateCache(Connection *conn);
Prototype void AbortCache(int fd, const char *msgid, int closefd);
Prototype void CommitCache(Connection *conn, int closefd);
Prototype void DumpArticleFromCache(Connection *conn, const char *map, int size, int grpIter, artno_t endNo);

Prototype void OpenCacheHits(void);

struct CacheHitEntry* FindCacheHitEntry(struct CacheHash_t *ch);
struct CacheHitEntry* CreateCacheHitEntry(struct CacheHash_t *ch, artno_t endNo);
struct CacheHitEntry* UpdateCacheHits(char *groupName, int grpIter, artno_t endNo, int cachehit);

int XPageMask=0;
int CacheHitsFD=-1;
char *CacheHits=NULL;
uint32 CacheHitsEnd=0;

/* Write the complete scoreboard range, including after EINTR/short writes.
 * Explicit offsets avoid disturbing other users of CacheHitsFD's file offset.
 */
static int
CacheHitsWriteAt(int fd, const void *data, size_t len, off_t offset)
{
    const char *ptr = data;

    while (len != 0) {
        ssize_t n = pwrite(fd, ptr, len, offset);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (n == 0)
                errno = EIO;
            return -1;
        }
        ptr += n;
        offset += n;
        len -= n;
    }
    return 0;
}

static int
CacheHitsZeroRange(int fd, uint32 first, uint32 end)
{
    char zeros[512];

    bzero(zeros, sizeof(zeros));
    while (first < end) {
        size_t len = end - first;

        if (len > sizeof(zeros))
            len = sizeof(zeros);
        if (CacheHitsWriteAt(fd, zeros, len, first) < 0)
            return -1;
        first += len;
    }
    return 0;
}

unsigned int
cacheHashNum(char *st)
{
    hash_t h = hhash(st);
    return(abs(h.h1 + h.h2));
}

void
cacheFile(hash_t hv, char *path, int makedir)
{
    int blen = 0;
    char fstr[32];
    int i;

    sprintf(fstr, "%08x%08x", (int)hv.h1, (int)hv.h2);

    strcpy(path, PatExpand(CacheHomePat));
    for (i = 0; i < DOpts.ReaderCacheDirs.dt_dirlvl; i++) {
	int c = DOpts.ReaderCacheDirs.dt_dirinfo[i];
	int formsize = 0;
	char format[32];

	while (c > 0) {
	    formsize++;
	    c = (c - 1) / 16;
	}
	sprintf(format, "/%%0%dx", formsize);
	blen = strlen(path);
	sprintf(path + blen, format,
		cacheHashNum(&fstr[i]) % DOpts.ReaderCacheDirs.dt_dirinfo[i]);
	if (makedir) {
	    struct stat st;
	    if (stat(path, &st) < 0) {
		mkdir(path, 0755);
	    }
	}
    }
    blen = strlen(path);
    sprintf(path + blen, "/%08x.%08x", (int)hv.h1, (int)hv.h2);
}

int
OpenCache(const char *msgid, int *pcfd, int *psize)
{
    int fd;
    hash_t hv = hhash(msgid);
    char path[PATH_MAX];

    *pcfd = -1;
    *psize = 0;

    /*
     * open cache file
     */

    cacheFile(hv, path, 1);

    if ((fd = open(path, O_RDWR)) >= 0) {
	struct stat st;

	if (fstat(fd, &st) == 0) {
	    if (st.st_size == 0) {	/* uncachable */
		close(fd);
		return(0);
	    }
	    *pcfd = fd;			/* positively cached */
	    *psize = st.st_size;
#if MMAP_DOES_NOT_UPDATE_ATIME
	    {
		char t[1];
	    	read(fd, &t, 1);
	    }
#endif
	    return(1);
	}
	close(fd);			/* error	     */
	return(0);
    }
    return(0);
}

void
CreateCache(Connection *conn)
{
    int fd;
    hash_t hv;
    char path[PATH_MAX], tmp[PATH_MAX];

    /* do not create new cache files for old articles */
    if (conn->co_Desc->d_CacheableTime > 0 &&
	(conn->co_Desc->d_CacheableTime < (time(NULL) - conn->co_SReq->sr_TimeRcvd))) {
	return;
    }

    hv = hhash(conn->co_SReq->sr_MsgId);

    /*
     * open cache file
     */

    cacheFile(hv, path, 1);
    {
	struct stat st;
	if (!stat(path, &st)) { /* uncacheable article */
	    return;
	}
    }

    strcpy(tmp, path);
    strcat(tmp, ".tmp");

    if (conn->co_Desc->d_Cache == CACHE_LAZY) {
	/* 
	 * Lazy cache, if tmp file does not exit, we just create it and
	 * return
	 */
	if ((fd = open(tmp, O_RDWR, 0644)) < 0) {
	    close(creat(tmp, 0644));
	    return;
	}
    } else if (conn->co_Desc->d_Cache == CACHE_SCOREBOARD) {
	struct CacheHitEntry *che;
	int new;
	double read;
	/* Check cache hits ratio */
	che = UpdateCacheHits(conn->co_SReq->sr_Group, conn->co_SReq->sr_GrpIter, conn->co_SReq->sr_endNo, 0);
	if (che==NULL) return;
	read = che->che_ReadArt+che->che_Hits;
	new = che->che_NewArt+conn->co_SReq->sr_endNo-che->che_LastHi;
	if (new < 1) new=1;
	if ( (read / new) > conn->co_Desc->d_ReadNewRatio) {
	    if ((che->che_Hits/che->che_ReadArt) > conn->co_Desc->d_CacheReadRatio) {
		/* cache on */
		if ((fd = open(tmp, O_RDWR|O_CREAT, 0644)) < 0) {
		    return;	/* error	     */
		}
	    } else {
		/* partly cached */
		if ((fd = open(tmp, O_RDWR, 0644)) < 0) {
	    	    close(creat(tmp, 0644));
		    return;
		} else {
		    /* correcting stat */
		    che->che_ReadArt--;
		    che->che_Hits++;
		    /* lazy cache in scoring mode, no return */
		}
	    }
	} else {
	    return;
	}
    } else {
	if ((fd = open(tmp, O_RDWR|O_CREAT, 0644)) < 0) {
	    return;			/* error	     */
	}
    }
    if (hflock(fd, 0, XLOCK_EX|XLOCK_NB) < 0) {
	close(fd);			/* someone else owns it */
	return;
    }
    {
	struct stat st;
	struct stat st2;
	if (fstat(fd, &st) < 0 || st.st_nlink == 0) {
	    close(fd);			/* delete race		*/
	    return;
	}
	if (stat(tmp, &st2) < 0 || st.st_ino != st2.st_ino) {
	    close(fd);			/* rename race		*/
	    return;
	}
	if (st.st_size != 0 && ftruncate(fd, 0) < 0) {
	    logit(LOG_ERR, "CreateCache: cannot truncate %s (%s)",
		  tmp, strerror(errno));
	    close(fd);
	    return;
	}
    }

    conn->co_SReq->sr_Cache = fdopen(fd, "w");
    if (!conn->co_SReq->sr_Cache) {
	close(fd);
    }
}

/*
 * AbortCache() - cache not successfully written, destroy
 */

void
AbortCache(int fd, const char *msgid, int closefd)
{
    char path[PATH_MAX];
    hash_t hv = hhash(msgid);

    cacheFile(hv, path, 0);
    strcat(path, ".tmp");
    remove(path);
    if (ftruncate(fd, 0) < 0)
	logit(LOG_ERR, "AbortCache: cannot truncate %s (%s)",
	      path, strerror(errno));
    hflock(fd, 0, XLOCK_UN);
    if (closefd)
	close(fd);
}

/*
 * CommitCache() - cache successfully written, commit to disk
 */

void
CommitCache(Connection *conn, int closefd)
{
    char path1[PATH_MAX];
    char path2[PATH_MAX];
    char *msgid = conn->co_SReq->sr_MsgId;
    hash_t hv = hhash(msgid);
    int fd = fileno(conn->co_SReq->sr_Cache);

    cacheFile(hv, path2, 0);
    strcpy(path1, path2);
    strcat(path1, ".tmp");
    if (rename(path1, path2) < 0) {
	logit(LOG_ERR, "CommitCache: cannot rename %s to %s (%s)",
	      path1, path2, strerror(errno));
	remove(path1);
    } else {
	struct stat st;

	/* Check the file we own, not a pathname another process may replace. */
	if (fstat(fd, &st) == 0 &&
	    ((conn->co_Desc->d_CacheMax > 0 &&
	      st.st_size > conn->co_Desc->d_CacheMax) ||
	     (conn->co_Desc->d_CacheMin > 0 &&
	      st.st_size < conn->co_Desc->d_CacheMin))) {
	    if (ftruncate(fd, 0) < 0) {
		logit(LOG_ERR, "CommitCache: cannot truncate %s (%s)",
		      path2, strerror(errno));
		/* Do not offer an out-of-policy file as a completed cache. */
		if (stat(path2, &st) == 0) {
		    struct stat owned;
		    if (fstat(fd, &owned) == 0 &&
			st.st_dev == owned.st_dev && st.st_ino == owned.st_ino)
			remove(path2);
		}
	    }
	}
    }
    hflock(fd, 0, XLOCK_UN);
    if (closefd)
	close(fd);
}

/*
 * DUMPARTICLEFROMCACHE() - article buffer is passed as an argument.   The
 *			    buffer is already '.' escaped (but has no 
 *			    terminating '.\r\n'), and \r\n terminated.
 *
 *			    if (conn->co_ArtMode == COM_BODYNOSTAT), just
 *			    do the body.  Otherwise do the whole thing.
 */

void
DumpArticleFromCache(Connection *conn, const char *map, int size, int grpIter, artno_t endNo)
{
    int b = 0;
    int inHeader = 1;
    int nonl = 0;
    char line[8192];
    char *vserver;
    char *buf;
    char ch;

    if (conn->co_Auth.dr_VServerDef)
	vserver = conn->co_Auth.dr_VServerDef->vs_ClusterName;
    else
	vserver = "";

    while (b < size) {
	int i;
	int yes = 0;

	for (i = b; i < size && map[i] != '\n'; ++i)
	    ;
	if (i < size) 
	    ++i;
	else
	    nonl = 1;
	switch(conn->co_ArtMode) {
	case COM_STAT:
	    break;
	case COM_HEAD:
	    if (inHeader == 1) {
		yes = 1;
		if (i == b + 2 && map[b] == '\r' && map[b+1] == '\n')
		    yes = 0;
	    }
	    break;
	case COM_ARTICLEWVF:
	    {
		const char *ovdata;
		int ovlen;

		if ((ovdata = NNRetrieveHead(conn, &ovlen, NULL, NULL, NULL, NULL)) != NULL) {
		    DumpOVHeaders(conn, ovdata, ovlen);
		    MBPrintf(&conn->co_TMBuf, "\r\n");
		    conn->co_ArtMode = COM_BODYNOSTAT;
		} else {
		    yes = 1;
		}
	    }
	    break;
	case COM_ARTICLE:
	    yes = 1;
	    break;
	case COM_BODY:
	case COM_BODYWVF:
	case COM_BODYNOSTAT:
	    if (inHeader == 0)
		yes = 1;
	    break;
	}
	/*
	 * Do some header rewriting for virtual servers
	 */

	*line = '\0';
	buf = (char *)&map[b];
	ch = tolower(*buf);

	if (inHeader && *vserver &&
		!conn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate &&
		ch == 'x' && strncasecmp(buf, "Xref:", 5) == 0) {
	    char *ptr;

	    ptr = (char *)buf + 5;
	    while (isspace((int)*ptr))
		ptr++;
	    while (!isspace((int)*ptr))
		ptr++;
	    while (isspace((int)*ptr))
		ptr++;
	    /* ptr should point to first group name */
	    if (*ptr) {
		int len;
		int e = i - 1;
		while (map[e] == '\r' || map[e] == '\n')
		    e--;
		len = (e - b + 1) - (ptr - buf);
		if (len > sizeof(line) - 100)
		    len = sizeof(line) - 100;
		sprintf(line, "Xref: %s ", vserver); 
		e = strlen(line);
		memcpy(&line[e], ptr, len);
		line[e + len] = '\0';
		strcat(line, "\r\n");
            }

	}
	if (inHeader && *vserver && ch == 'p' &&
		!conn->co_Auth.dr_VServerDef->vs_NoReadPath &&
				strncasecmp(buf, "Path:", 5) == 0) {
	    char *ptr;
	    int vsl = strlen(vserver);

	    ptr = (char *)buf + 5;
	    while (isspace((int)*ptr))
		ptr++;
	    if (ptr && *ptr && (strncmp(vserver, ptr, vsl) ||
				((ptr[vsl] != '\0') &&
				 (ptr[vsl] != '!')))) {
		int len;
		int e = i - 1;
		while (map[e] == '\r' || map[e] == '\n')
		    e--;
		len = (e - b + 1) - (ptr - buf);
		if (len > sizeof(line) - 100)
		    len = sizeof(line) - 100;
		sprintf(line, "Path: %s!", vserver); 
		e = strlen(line);
		memcpy(&line[e], ptr, len);
		line[e + len] = '\0';
		strcat(line, "\r\n");
            }
	}

	if (yes) {
	    if (*line)
		MBWrite(&conn->co_TMBuf, line, strlen(line));
	    else
		MBWrite(&conn->co_TMBuf, map + b, i - b);
	}

	if (inHeader && i == b + 2 && map[b] == '\r' && map[b+1] == '\n')
	    inHeader = 0;

	b = i;
    }
    if (nonl && conn->co_ArtMode != COM_STAT)
	MBPrintf(&conn->co_TMBuf, "\r\n", 2);

    /* update the cache hits */
    if (CacheHits) {
	UpdateCacheHits(conn->co_GroupName, grpIter, endNo, 1); 
    }
}

struct CacheHitEntry*
FindCacheHitEntry(struct CacheHash_t *ch) {
    uint32 *i=NULL;
    uint32 max=CacheHitsEnd-sizeof(struct CacheHitEntry);
    struct CacheHitEntry *che=NULL;
    struct CacheHitHead *chh=(struct CacheHitHead *)CacheHits;

    i = (uint32*) (CacheHits+sizeof(struct CacheHitHead)+((ch->h1^ch->h2)%chh->chh_hashSize)*sizeof(uint32));

    while ((*i != 0) && (*i < max)) {
	che = (struct CacheHitEntry*) (CacheHits + (*i));
	if (memcmp(ch, &(che->che_hash), sizeof(struct CacheHash_t))==0) {
	    return che;
	}
	i = &(che->che_Next);
    }
    return NULL;
}

/*** Any call to CreateCacheHitEntry may change CacheHits value ***
 *
 * There is no hurry to re-mmap CacheHitsFD, we may only check just before
 * creating a new entry
 */
struct CacheHitEntry*
CreateCacheHitEntry(struct CacheHash_t *ch, artno_t endNo) {
    uint32 *i = NULL;
    uint32 ne, next, oldEnd, newEnd;
    uint64_t next64, end64;
    size_t linkOffset;
    struct CacheHitEntry *che = NULL;
    struct CacheHitHead *chh;
    char *newMap;

    if (CacheHits == NULL || CacheHitsFD < 0)
	return NULL;
    if (hflock(CacheHitsFD, 0, XLOCK_EX) < 0) {
	logit(LOG_ERR, "CreateCacheHitEntry: cannot lock cache.hits (%s)",
	      strerror(errno));
	return NULL;
    }

    chh = (struct CacheHitHead *)CacheHits;
    /* Another process may have enlarged the scoreboard.  Keep the old
     * mapping alive until the replacement has been mapped successfully.
     */
    if (chh->chh_end != CacheHitsEnd) {
	struct stat st;
	uint32 end = chh->chh_end;

	if (end < sizeof(struct CacheHitHead) ||
	    fstat(CacheHitsFD, &st) < 0 || st.st_size < end) {
	    logit(LOG_ERR, "CreateCacheHitEntry: invalid cache.hits growth");
	    goto failed;
	}
	newMap = mmap(NULL, end, PROT_READ|PROT_WRITE, MAP_SHARED,
		      CacheHitsFD, 0);
	if (newMap == MAP_FAILED) {
	    logit(LOG_ERR, "CreateCacheHitEntry: cache.hits remap failed (%s)",
		  strerror(errno));
	    goto failed;
	}
	munmap(CacheHits, CacheHitsEnd);
	CacheHits = newMap;
	CacheHitsEnd = end;
	chh = (struct CacheHitHead *)CacheHits;
    }

    i = (uint32 *)(CacheHits + sizeof(struct CacheHitHead) +
	((ch->h1 ^ ch->h2) % chh->chh_hashSize) * sizeof(uint32));
    while (*i != 0) {
	che = (struct CacheHitEntry *)(CacheHits + *i);
	if (memcmp(ch, &che->che_hash, sizeof(struct CacheHash_t)) == 0) {
	    hflock(CacheHitsFD, 0, XLOCK_UN);
	    return che;
	}
	i = &che->che_Next;
    }

    /* Do not publish the hash-chain link or the new-entry pointer until
     * the backing storage exists and can be mapped.  In particular, a
     * short/failed zero write must leave the old scoreboard usable.
     */
    ne = chh->chh_newEntry;
    oldEnd = chh->chh_end;
    next64 = (uint64_t)ne + sizeof(struct CacheHitEntry);
    if (next64 > UINT32_MAX || ne > oldEnd ||
	ne < sizeof(struct CacheHitHead) +
	     (uint64_t)chh->chh_hashSize * sizeof(uint32)) {
	logit(LOG_ERR, "CreateCacheHitEntry: cache.hits offset overflow");
	goto failed;
    }
    next = (uint32)next64;
    newEnd = oldEnd;
    if (next > oldEnd) {
	end64 = (next64 + XPageMask) & ~(uint64_t)XPageMask;
	if (end64 > UINT32_MAX) {
	    logit(LOG_ERR, "CreateCacheHitEntry: cache.hits size overflow");
	    goto failed;
	}
	newEnd = (uint32)end64;
	if (CacheHitsZeroRange(CacheHitsFD, oldEnd, newEnd) < 0 ||
	    fsync(CacheHitsFD) < 0) {
	    logit(LOG_ERR, "CreateCacheHitEntry: cache.hits extension failed (%s)",
		  strerror(errno));
	    if (ftruncate(CacheHitsFD, oldEnd) < 0)
		logit(LOG_ERR, "CreateCacheHitEntry: rollback truncate failed (%s)",
		      strerror(errno));
	    goto failed;
	}
	newMap = mmap(NULL, newEnd, PROT_READ|PROT_WRITE, MAP_SHARED,
		      CacheHitsFD, 0);
	if (newMap == MAP_FAILED) {
	    logit(LOG_ERR, "CreateCacheHitEntry: cache.hits extension remap failed (%s)",
		  strerror(errno));
	    if (ftruncate(CacheHitsFD, oldEnd) < 0)
		logit(LOG_ERR, "CreateCacheHitEntry: rollback truncate failed (%s)",
		      strerror(errno));
	    goto failed;
	}
	linkOffset = (char *)i - CacheHits;
	munmap(CacheHits, CacheHitsEnd);
	CacheHits = newMap;
	CacheHitsEnd = newEnd;
	chh = (struct CacheHitHead *)CacheHits;
	i = (uint32 *)(CacheHits + linkOffset);
    }

    che = (struct CacheHitEntry *)(CacheHits + ne);
    memset(che, 0, sizeof(*che));
    memcpy(&che->che_hash, ch, sizeof(struct CacheHash_t));
    che->che_LastHi = endNo;
    chh->chh_newEntry = next;
    chh->chh_end = newEnd;
    *i = ne;

    hflock(CacheHitsFD, 0, XLOCK_UN);
    return che;

failed:
    hflock(CacheHitsFD, 0, XLOCK_UN);
    return NULL;
}

struct CacheHitEntry*
UpdateCacheHits(char *groupName, int grpIter, artno_t endNo, int cachehit) {
    struct CacheHash_t ch;
    struct CacheHitEntry *che=NULL;

    if ((groupName==NULL) || (grpIter<0)) {
	return NULL;
    }
    if (CacheHits == NULL || CacheHitsFD < 0)
	return NULL;
    SetCacheHash(&ch, groupName, grpIter, &DOpts.ReaderGroupHashMethod);

    che = FindCacheHitEntry(&ch);
    if (che==NULL) {
	che = CreateCacheHitEntry(&ch, endNo);
	if (che==NULL) {
	    return NULL;
	}
    }

    if (cachehit) {
	che->che_Hits++;
    } else {
	che->che_ReadArt++;
    }
    return che;
}

void
OpenCacheHits(void) {
    struct CacheHitHead chh;
    struct stat st;
    int r;
    int locked = 0;

    if (CacheHitsFD >= 0)
	return;
    if (XPageMask == 0)
	XPageMask = getpagesize() - 1;
    CacheHitsFD = open(PatDbExpand(CacheHitsPat), O_RDWR|O_CREAT, 0644);
    if (CacheHitsFD < 0) {
	logit(LOG_ERR, "Cannot open cache hits file (%s)",
	      PatDbExpand(CacheHitsPat));
	return;
    }

    do {
	r = pread(CacheHitsFD, &chh, sizeof(chh), 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
	logit(LOG_ERR, "OpenCacheHits: cannot read cache.hits (%s)",
	      strerror(errno));
	goto failed;
    }
    if (r != sizeof(chh) || chh.chh_magic != CHMAGIC ||
	chh.chh_version != CHVERSION) {
	/* A second reader may already be repairing the scoreboard. */
	if (hflock(CacheHitsFD, 0, XLOCK_EX) < 0) {
	    logit(LOG_ERR, "OpenCacheHits: cannot lock cache.hits (%s)",
		  strerror(errno));
	    goto failed;
	}
	locked = 1;
	do {
	    r = pread(CacheHitsFD, &chh, sizeof(chh), 0);
	} while (r < 0 && errno == EINTR);
	if (r < 0) {
	    logit(LOG_ERR, "OpenCacheHits: cannot reread cache.hits (%s)",
		  strerror(errno));
	    goto failed;
	}
	if (r != sizeof(chh) || chh.chh_magic != CHMAGIC ||
	    chh.chh_version != CHVERSION) {
	    uint64_t needed, aligned;

	    needed = sizeof(chh) +
		(uint64_t)DOpts.ReaderCacheHashSize * sizeof(uint32);
	    aligned = (needed + XPageMask) & ~(uint64_t)XPageMask;
	    if (DOpts.ReaderCacheHashSize == 0 || aligned > UINT32_MAX) {
		logit(LOG_ERR, "OpenCacheHits: invalid cache hash size");
		goto failed;
	    }
	    if (ftruncate(CacheHitsFD, 0) < 0) {
		logit(LOG_ERR, "OpenCacheHits: cannot truncate cache.hits (%s)",
		      strerror(errno));
		goto failed;
	    }
	    bzero(&chh, sizeof(chh));
	    chh.chh_magic = CHMAGIC;
	    chh.chh_version = CHVERSION;
	    chh.chh_hashSize = DOpts.ReaderCacheHashSize;
	    chh.chh_newEntry = (uint32)needed;
	    chh.chh_end = (uint32)aligned;
	    time(&chh.chh_lastExpired);

	    /* Initialize the body before publishing the magic/version header. */
	    if (CacheHitsZeroRange(CacheHitsFD, 0, chh.chh_end) < 0 ||
		CacheHitsWriteAt(CacheHitsFD, &chh, sizeof(chh), 0) < 0 ||
		fsync(CacheHitsFD) < 0) {
		logit(LOG_ERR, "OpenCacheHits: initializing cache.hits failed (%s)",
		      strerror(errno));
		/* Ensure an interrupted initialization is not mistaken for
		 * a valid cache on the next open. */
		if (ftruncate(CacheHitsFD, 0) < 0)
		    logit(LOG_ERR, "OpenCacheHits: cleanup truncate failed (%s)",
			  strerror(errno));
		goto failed;
	    }
	}
    }
    if (fstat(CacheHitsFD, &st) < 0 ||
	chh.chh_hashSize == 0 ||
	chh.chh_end < sizeof(chh) ||
	chh.chh_newEntry < sizeof(chh) +
	    (uint64_t)chh.chh_hashSize * sizeof(uint32) ||
	chh.chh_end < chh.chh_newEntry ||
	st.st_size < chh.chh_end) {
	logit(LOG_ERR, "OpenCacheHits: invalid or truncated cache.hits");
	goto failed;
    }
    CacheHits = mmap(NULL, chh.chh_end, PROT_READ|PROT_WRITE,
		     MAP_SHARED, CacheHitsFD, 0);
    if (CacheHits == MAP_FAILED) {
	logit(LOG_ERR, "OpenCacheHits: cache.hits mmap failed (%s)",
	      strerror(errno));
	CacheHits = NULL;
	goto failed;
    }
    CacheHitsEnd = chh.chh_end;
    if (locked)
	hflock(CacheHitsFD, 0, XLOCK_UN);
    return;

failed:
    if (locked)
	hflock(CacheHitsFD, 0, XLOCK_UN);
    close(CacheHitsFD);
    CacheHitsFD = -1;
    CacheHitsEnd = 0;
    CacheHits = NULL;
}
