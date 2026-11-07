
/*
 * LIB/HISTORY.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * Generally speaking, the history mechanism can be pictured from the
 * point of view of a reader or from the point of view of a writer.  From
 * the point of view of a reader, A base table lookup begins a hash chain
 * of history records that runs backwards through the history file.  More
 * recent entries are encountered first, resulting in a certain degree of
 * locality of reference based on age.
 *
 * A writer must scan the chain to ensure that the message-id does not
 * already exist.  The new history record is appended to the history file
 * and inserted at the base of the hash chain.  The file append requires
 * an exclusive lock to ensure atomic operation (O_APPEND is often not atomic
 * on heavily loaded systems, the exclusive lock is required).
 *
 * In a heavily loaded system, the exclusive lock and append may become a
 * bottleneck.
 *
 * Size limits.  In a version 1 file the hash table and the next links hold
 * 32 bit byte offsets, so such a file is limited to 4GB.  A version 2 file
 * (HVERSION, the format written now) holds record numbers (HistIndex,
 * uint32) that recordOffset() turns into 64 bit file offsets: it is limited
 * to 2^32 - 1 records (record 0 is the dummy written at creation), about
 * 120GB of records plus the hash table.  HistoryAdd() does not check that
 * limit; past it the record number would wrap.
 */

#include "defs.h"

Prototype int HistoryOpen(const char *fileName, int fastMode);
Prototype int HistoryClose(void);
Prototype int HistoryShareLock(int fd, const char *fileName);
Prototype int HistoryLookup(const char *msgid, History *h);
Prototype int HistoryLookupByHash(hash_t hv, History *h);
Prototype HistIndex HistoryPosLookupByHash(hash_t hv, History *h);
Prototype int HistoryAdd(const char *msgid, History *h);
Prototype int HistoryStore(History *h);
Prototype int HistoryStoreExp(History *h, HistIndex index);
Prototype int HistoryExpire(const char *msgid, History *h, int unexp);
Prototype void PrintHistory(History *h);

Prototype uint32 NewHSize;

#define HBLKINCR	16
#define HBLKSIZE	256

HistHead	HHead;
HistHead	*HHeadMap = NULL;
HistIndex 	*HAry;
int		HFd = -1;
int		LoggedDHistCorrupt;
int		HFlags;
uint32		HSize;
uint32		HMask;
off_t		HEntryOff;		/* Start of actual history records */
uint32		NewHSize = 0;
int		HBlkIncr = HBLKINCR;
int		HBlkGood;
char		HistoryFileName[PATH_MAX];
int		DoingReOpen = 0;
static int	ReOpenLockError = 0;	/* errno of a failed lock in a reopen */
static void	reopenWait(void);

/*
 * Write an entire buffer at a fixed offset.  A successful short write is
 * not an error, and EINTR can occur before any bytes have been written.
 * pwrite() also leaves the descriptor's shared seek position unchanged.
 */
static int
HistoryWriteAt(int fd, const void *buf, size_t len, off_t offset)
{
    const char *ptr = buf;

    while (len) {
        size_t chunk = (len > 1024 * 1024) ? 1024 * 1024 : len;
        ssize_t n = pwrite(fd, ptr, chunk, offset);

        if (n > 0) {
            ptr += n;
            offset += n;
            len -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            if (n == 0)
                errno = EIO;
            return(-1);
        }
    }
    return(0);
}

/* An incomplete history write cannot safely be treated as success. */
static void
HistoryFatalIO(const char *operation)
{
    int error = errno;

    logit(LOG_ERR, "dhistory %s failed for %s: %s",
        operation, HistoryFileName, strerror(error));
    fprintf(stderr, "dhistory %s failed for %s: %s\n",
        operation, HistoryFileName, strerror(error));
    exit(1);
}

/*
 * Take the shared lock on bytes 0-3 that every history opener holds
 * while it has the file open, waiting for a fast-mode writer to finish.
 */
int
HistoryShareLock(int fd, const char *fileName)
{
    if (hflock(fd, 0, XLOCK_SH|XLOCK_NB) == 0)
	return(0);
    if (errno != EAGAIN && errno != EACCES)
	return(-1);
    logit(LOG_NOTICE, "%s is in use by a fast-mode writer, waiting", fileName);
    fprintf(stderr, "%s is in use by a fast-mode writer, waiting\n", fileName);
    return(hflock(fd, 0, XLOCK_SH));
}

/* Does a dhisexpire hold the lock it keeps while the history is dead? */
static int
deadMarkerHeld(int fd)
{
    struct flock fl = { 0 };

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = HDEADLOCK;
    fl.l_len = 4;
    if (fcntl(fd, F_GETLK, &fl) < 0)
	return(1);	/* cannot tell: wait as before */
    return(fl.l_type != F_UNLCK);
}

/* Is the history open as fd still marked dead? */
static int
stillDead(int fd)
{
    uint32 magic;

    return(pread(fd, &magic, sizeof(magic), 0) != sizeof(magic) ||
	magic == HDEADMAGIC);
}

/* Is another file at fileName now (or none)?  Then a new one came. */
static int
fileReplaced(int fd, const char *fileName)
{
    struct stat a;
    struct stat b;

    if (fstat(fd, &a) < 0 || stat(fileName, &b) < 0)
	return(1);
    return(a.st_dev != b.st_dev || a.st_ino != b.st_ino);
}

/* A failed lock in HistoryOpen: -2 in a reopen (it is retried), else exit. */
static int
openLockFailed(int fd, const char *fileName)
{
    int error = errno;

    close(fd);
    if (DoingReOpen) {
	ReOpenLockError = error;
	return(-2);
    }
    logit(LOG_ERR, "lock %s failed (%s)", fileName, strerror(error));
    fprintf(stderr, "lock %s failed (%s)\n", fileName, strerror(error));
    exit(1);
}

int
HistoryOpen(const char *fileName, int hflags)
{
    int fd;
    struct stat st;
    int openflags;
    int openLock = 0;	/* our lock on bytes 0-3: XLOCK_SH or XLOCK_EX */

    ReOpenLockError = 0;

    if (fileName == NULL)
	fileName = strdup(PatDbExpand(DHistoryPat));

    /* a reopen passes HistoryFileName itself: copying onto it empties it */
    if (fileName != HistoryFileName)
	snprintf(HistoryFileName, sizeof(HistoryFileName), "%s", fileName);

    HFlags = hflags;

    if (NewHSize == 0)
	NewHSize = DOpts.HashSize;	/* which may also be 0 */

    /*
     * open the history file
     */

    if (HFlags & HGF_READONLY)
	openflags = O_RDONLY;
    else
	openflags = O_RDWR|O_CREAT;

    if (HFlags & HGF_EXCHECK && stat(fileName, &st) == 0) {
	fprintf(stderr, "%s in fast mode already exists - exiting\n",
						fileName);
	exit(1);
    }
    fd = open(fileName, openflags, 0644);

    if (fd < 0) {
	if (DoingReOpen)
	    return(-2);
	logit(LOG_ERR, "open %s failed (%s)", fileName, 
							strerror(errno));
	fprintf(stderr, "open %s failed (%s)", fileName,
							strerror(errno));
	exit(1);
    }

    if (fstat(fd, &st) < 0) {
	logit(LOG_ERR, "fstat %s failed", fileName);
	exit(1);
    }

    /*
     * Every opener holds a lock on bytes 0-3 while it has the history
     * open: fast mode an exclusive one (it caches the hash table, so it
     * needs the file to itself), the others a shared one.
     */
    if ((HFlags & (HGF_FAST|HGF_READONLY)) == HGF_FAST) {
	if (hflock(fd, 0, XLOCK_EX|XLOCK_NB) < 0) {
	    if (errno != EAGAIN && errno != EACCES)
		return(openLockFailed(fd, fileName));
	    logit(LOG_ERR, "%s is in use by another process: fast mode needs it to itself",
		fileName);
	    fprintf(stderr, "%s is in use by another process: fast mode needs it to itself\n",
		fileName);
	    exit(1);
	}
	openLock = XLOCK_EX;
    }

    /*
     * initial history file creation, if necessary
     */

    bzero(&HHead, sizeof(HHead));

    if (st.st_size == 0 || 
	read(fd, &HHead, sizeof(HHead)) != sizeof(HHead) ||
	HHead.hmagic != HMAGIC
    ) {
	/*
	 * Is this history an old one and we need to wait for a new one?
	 * dhisexpire marks it dead while it copies the last records; a
	 * first open waits for the new file by name, as a reopen does.
	 * Without a dhisexpire at work (it holds HDEADLOCK) and with the
	 * file still at its name, no new one is coming: fail at once.
	 */
	if (HHead.hmagic == HDEADMAGIC && (HFlags & HGF_FAST) == 0) {
	    int replacing = deadMarkerHeld(fd) || fileReplaced(fd, fileName);

	    /*
	     * dhisexpire drops the marker only after the history is valid
	     * again (a failure) or another file is at the name, but it can
	     * be between those steps: look once more before giving up.
	     */
	    if (!replacing) {
		if (stillDead(fd))
		    usleep(100000);
		replacing = !stillDead(fd) || deadMarkerHeld(fd) ||
		    fileReplaced(fd, fileName);
	    }
	    close(fd);
	    if (!replacing) {
		logit(LOG_ERR, "%s is marked dead and no dhisexpire is running: run dhisexpire -u",
		    fileName);
		fprintf(stderr, "%s is marked dead and no dhisexpire is running: run dhisexpire -u\n",
		    fileName);
		if (DoingReOpen)
		    return(-3);
		exit(1);
	    }
	    if (DoingReOpen)
		return(-2);
	    logit(LOG_NOTICE, "%s is marked dead, waiting for the new history (dhisexpire -u revives it)",
		fileName);
	    fprintf(stderr, "%s is marked dead, waiting for the new history (dhisexpire -u revives it)\n",
		fileName);
	    reopenWait();
	    return(0);
	}

	/*
	 * lock after finding the history file to be invalid and recheck.
	 * A read-only descriptor can only take a shared lock (an exclusive
	 * one fails at once with EBADF); it still waits for a creator.
	 * So does a writer that cannot get the exclusive lock at once: the
	 * creator keeps a shared lock while it has the file open, and an
	 * exclusive wait would last until it closes.
	 */

	while (openLock == 0) {
	    HistHead hh;

	    if ((HFlags & HGF_READONLY) == 0 &&
		hflock(fd, 0, XLOCK_EX|XLOCK_NB) == 0) {
		openLock = XLOCK_EX;
		break;
	    }
	    if ((HFlags & HGF_READONLY) == 0 &&
		errno != EAGAIN && errno != EACCES)
		return(openLockFailed(fd, fileName));
	    if (hflock(fd, 0, XLOCK_SH) < 0)
		return(openLockFailed(fd, fileName));
	    /*
	     * No creator is at work while we hold the shared lock: a file
	     * with data but no valid magic is not going to change (it fails
	     * below), and only an empty one is worth another try.
	     */
	    if ((HFlags & HGF_READONLY) ||
		(pread(fd, &hh, sizeof(hh), 0) == sizeof(hh) &&
		hh.hmagic == HMAGIC) ||
		fstat(fd, &st) < 0 || st.st_size > 0) {
		openLock = XLOCK_SH;
		break;
	    }
	    /* still not created: try for the creation lock again */
	    if (hflock(fd, 0, XLOCK_UN) < 0)
		return(openLockFailed(fd, fileName));
	    usleep(10000);
	}

	fstat(fd, &st);
	lseek(fd, 0L, 0);

	if (st.st_size == 0 || 
	    read(fd, &HHead, sizeof(HHead)) != sizeof(HHead) ||
	    HHead.hmagic != HMAGIC
	) {
	    uint32 n;
	    uint32 b;
	    char *z = calloc(8192, 1);

	    if (z == NULL)
		HistoryFatalIO("allocation of initialization buffer");

	    /*
	     * check for old version of history file
	     */

	    if (st.st_size) {
		logit(LOG_ERR, "Incompatible history file version or corrupted history file\n");
		fprintf(stderr, "%s: incompatible history file version or corrupted history file\n",
		    fileName);
		exit(1);
	    }

	    if (HFlags & HGF_READONLY) {
		logit(LOG_ERR, "Unable to create %s - read-only mode\n",
							fileName);
		fprintf(stderr, "Unable to create %s - read-only mode\n",
							fileName);
		exit(1);
	    }

	    /*
	     * create new history file
	     */

	    logit(LOG_INFO, "Creating history file");

	    if (ftruncate(fd, 0) < 0)
		HistoryFatalIO("initial truncate");
	    bzero(&HHead, sizeof(HHead));

	    HHead.hashSize = NewHSize;
	    HHead.version  = HVERSION;
	    HHead.henSize  = sizeof(History);
	    HHead.headSize = sizeof(HHead);

	    if (HistoryWriteAt(fd, &HHead, sizeof(HHead), 0) < 0)
		HistoryFatalIO("initial header write");

	    /*
	     * write out the hash table
	     */

	    n = 0;
	    b = HHead.hashSize * sizeof(HistIndex);

		printf("n=%u  b=%u HHead.hashSize=%u\n", n, b, HHead.hashSize);
	    while (n < b) {
		uint32 r = (b - n > 8192) ? 8192 : b - n;

		if (HistoryWriteAt(fd, z, r, sizeof(HHead) + (off_t)n) < 0)
		    HistoryFatalIO("initial hash table write");
		n += r;
	    }
	    /*
	     * Write out a dummy history entry so we don't use zero offset
	     */
	    if (HHead.version > 1) {
		History h = { 0 };
		if (HistoryWriteAt(fd, &h, sizeof(h),
		    sizeof(HHead) + (off_t)b) < 0)
		    HistoryFatalIO("dummy record write");
	    }

	    /* Commit the initialized contents before making the magic valid. */
	    if (fsync(fd) < 0)
		HistoryFatalIO("initial sync");

	    /* Mark the database valid only after its contents are complete. */
	    HHead.hmagic = HMAGIC;
	    if (HistoryWriteAt(fd, &HHead, sizeof(HHead), 0) < 0)
		HistoryFatalIO("final header write");
	    if (fsync(fd) < 0)
		HistoryFatalIO("final header sync");

	    free(z);
	    logit(LOG_INFO, "History file creation complete");
	}

	/*
	 * Fast mode keeps its exclusive lock for the whole run; other
	 * openers turn the creation lock into their shared one, so that
	 * no fast-mode writer gets in between.
	 */
	if ((HFlags & HGF_FAST) == 0 && openLock == XLOCK_EX) {
	    if (hflock(fd, 0, XLOCK_SH) < 0)
		return(openLockFailed(fd, fileName));
	    openLock = XLOCK_SH;
	}
    }

    if (openLock == 0) {
	if (HistoryShareLock(fd, fileName) < 0)
	    return(openLockFailed(fd, fileName));
	openLock = XLOCK_SH;
    }

    if (HHead.version < 1) {
	logit(LOG_ERR, "DHistory version %d, expected %d, use the biweekly adm script to regenerate the history file",
	    HHead.version,
	    HVERSION
	);
	fprintf(stderr, "DHistory version %d, expected %d, use the biweekly adm script to regenerate the history file\n",
	    HHead.version,
	    HVERSION
	);
	exit(1);
    }
    if (HHead.version > HVERSION) {
	logit(LOG_ERR, "DHistory version %d, expected %d, YOU ARE RUNNING AN OLD DIABLO ON A NEW HISTORY FILE!",
	    HHead.version,
	    HVERSION
	);
	exit(1);
    }
    /*
     * Version 2 records are walked at a fixed sizeof(History) stride, so an
     * entry size that does not match this build (a different ABI, or a
     * damaged header) must not be read at the wrong stride.  The hash table
     * is mapped at HHead.headSize, which the format lets grow: only a header
     * smaller than this build's is refused.  Version 1 has no such fields.
     */
    if (HHead.version > 1 &&
	(HHead.henSize != sizeof(History) || HHead.headSize < sizeof(HHead))) {
	logit(LOG_ERR, "DHistory entry size %d or header size %d does not match this build (%d, %d); use the biweekly adm script to regenerate the history file",
	    (int)HHead.henSize, (int)HHead.headSize,
	    (int)sizeof(History), (int)sizeof(HHead));
	fprintf(stderr, "DHistory entry size %d or header size %d does not match this build (%d, %d); use the biweekly adm script to regenerate the history file\n",
	    (int)HHead.henSize, (int)HHead.headSize,
	    (int)sizeof(History), (int)sizeof(HHead));
	exit(1);
    }
    if (HHead.hmagic != HMAGIC) {
	logit(LOG_NOTICE, "DHistory magic mismatch");
	fprintf(stderr, "DHistory magic mismatch\n");
    }

    /*
     * Map the history header so that we can check it quickly
     */

    HHeadMap = xmap(NULL, HHead.headSize, PROT_READ, MAP_SHARED, fd, 0);
    if (HHeadMap == 0) {
	if (fd >= 0)
	    close(fd);
	logit(LOG_CRIT, "dhistory header mmap error: %s", strerror(errno));
	exit(1);
    }

    /*
     * Map history file
     */

    HSize = HHead.hashSize;
    if (HSize == 0 || (HSize & (HSize - 1)) != 0 ||
	fstat(fd, &st) != 0 || st.st_size < HHead.headSize ||
	(off_t)HSize * (off_t)sizeof(HistIndex) > st.st_size - HHead.headSize
    ) {
	logit(LOG_CRIT, "Invalid history hash size %u", (unsigned)HSize);
	exit(1);
    }
    HMask = HSize - 1;

    /*
     * In FAST mode we hold the history file locked (above) in order to
     * cache the hash table array at the beginning, which in turn
     * allows us to essentially append new entries to the end of
     * the file without having to seek back and forth updating
     * the hash table.
     *
     * When we aren't in FAST mode, we memory-map the hash table
     * portion of the history file.
     */

    if ((HFlags & (HGF_FAST|HGF_READONLY)) == (HGF_FAST|HGF_READONLY))
	fprintf(stderr, "Ignoring fast mode for readonly history open\n");

    if (HFlags & HGF_FAST) {
	HAry = calloc(HSize, sizeof(HistIndex));
	if (HAry == NULL) {
	    perror("calloc");
	    exit(1);
	}
	lseek(fd, HHead.headSize, 0);
	if (read(fd, HAry, (size_t)HSize * sizeof(HistIndex)) != (size_t)HSize * sizeof(HistIndex)) {
	    perror("read");
	    exit(1);
	}
    } else {
	int mapflags = PROT_READ;

	if ((HFlags & HGF_READONLY) == 0)
	    mapflags |= PROT_WRITE;
	HAry = xmap(NULL, (size_t)HSize * sizeof(HistIndex), mapflags, MAP_SHARED, fd, HHead.headSize);
	if (HFlags & HGF_MLOCK)
	    mlock(HAry, HSize * sizeof(HistIndex));
    }

    if (HAry == NULL || HAry == (HistIndex *)-1) {
	if (fd >= 0)
	    close(fd);
	logit(LOG_CRIT, "dhistory mmap error: %s", strerror(errno));
	exit(1);
    }
    HFd = fd;
    HEntryOff = HHead.headSize + HHead.hashSize * sizeof(HistIndex);
    return(0);
}

/*
 * On close, we have to commit the hash table if we were in
 * FAST mode, otherwise we need only unmap the file before
 * closing it.
 */

int
HistoryClose(void)
{
    int r = RCOK;

    if (HFd >= 0 && !(HFlags & HGF_READONLY)) {
	if (HFlags & HGF_FAST) {
	    /* synced too: the file is often renamed into place next */
	    if (HistoryWriteAt(HFd, HAry,
		(size_t)HSize * sizeof(HistIndex), HHead.headSize) < 0 ||
		fsync(HFd) < 0) {
		logit(LOG_ERR, "dhistory fast-mode index write failed: %s",
		    strerror(errno));
		r = RCTRYAGAIN;
	    } else {
		free(HAry);
		HAry = NULL;
	    }
	} else {
	    if (HAry && HAry != (HistIndex *)-1) {
		if (HFlags & HGF_MLOCK)
		    munlock(HAry, HSize * sizeof(HistIndex));
		xunmap((void *)HAry, HSize * sizeof(HistIndex));
		HAry = NULL;
	    }
	}
    }
    if (r == RCOK) {
	if (HAry && HAry != (HistIndex *)-1) {
	    if (HFlags & HGF_MLOCK)
		munlock(HAry, HSize * sizeof(HistIndex));
	    xunmap((void *)HAry, HSize * sizeof(HistIndex));
	    HAry = NULL;
	}
	if (HHeadMap != NULL) {
	    xunmap((void *)HHeadMap, HHead.headSize);
	    HHeadMap = NULL;
	}
	if (HFd >= 0) {
	    hflock(HFd, 0, XLOCK_UN);
	    close(HFd);
	}
	HFd = -1;
    }
    return(r);
}

void
historyReOpen(void)
{
    DoingReOpen = 1;
    HistoryClose();
    reopenWait();
}

/* Open HistoryFileName again, waiting up to 6 minutes for a new file. */
static void
reopenWait(void)
{
    int count = 0;
    int r;

    DoingReOpen = 1;
    while ((r = HistoryOpen(HistoryFileName, HFlags)) == -2 && count++ < 360)
	sleep(1);
    if (r == -3)	/* dead and no dhisexpire: HistoryOpen said so */
	exit(1);
    if (count++ >= 360) {
	if (ReOpenLockError) {
	    fprintf(stderr, "Unable to reopen history file: lock failed (%s)\n",
		strerror(ReOpenLockError));
	    logit(LOG_ERR, "Unable to reopen history file: lock failed (%s)",
		strerror(ReOpenLockError));
	} else {
	    fprintf(stderr, "Unable to reopen history file due to old magic\n");
	    logit(LOG_ERR, "Unable to reopen history file due to old magic");
	}
	exit(1);
    }
    DoingReOpen = 0;
}

void
PrintHistory(History *h)
{
    printf(" [hv=%08x.%08x gm=%d ex=%d off=%d len=%d F=%s]\n",
		h->hv.h1,
		h->hv.h2,
		(int)h->gmt,
		(int)h->exp,
		(int)h->boffset,
		(int)h->bsize,
		((h->exp & EXPF_HEADONLY) ? "H" : "")
    );
}

int
HistoryLookup(const char *msgid, History *nh)
{
    hash_t hv;
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    History h = { 0 };
    static int HLAlt = 0;
    int r = -1;
    int counter = 0;
    int statfailed = 0;

    if (HHeadMap->hmagic != HMAGIC)
	historyReOpen();

    hv = hhash(msgid);
    hi = (hv.h1 ^ hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    index = HAry[hi];

    while (index) {
	if (HHead.version > 1)
	    off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	else 
	    off = index;
	if (pread(HFd, &h, sizeof(h), off) != sizeof(h)) {
	    if ((LoggedDHistCorrupt & 1) == 0 || DebugOpt) {
		LoggedDHistCorrupt |= 1;
		logit(LOG_ERR, "dhistory file corrupted on lookup @ %d->%d chain %d  offset %ld  msgid %s  counter=%d",
					pindex, index,
					(int)((hv.h1 ^ hv.h2) & HMask),
					off, msgid, counter);
		sleep(1);
	    }
	    index = 0;	/* unreadable record: a miss, not a hit */
	    break;
	}
	if (h.hv.h1 == hv.h1 && h.hv.h2 == hv.h2)
	    break;
	pindex = index;
	index = h.next;
	if (counter++ > 5000) {
	    logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d (%s)",
					(int)pindex, (int)index,
					(int)((hv.h1 ^ hv.h2) & HMask),
					msgid);
	    index = 0;
	}
    }
    if (index != 0)
	r = 0;
    /*
     * On failure, try alternate hash method (for lookup only)
     */
    if (r < 0 && DOpts.CompatHashMethod >= 0 && HLAlt == 0 && !statfailed) {
	int save = DOpts.HashMethod;

	DOpts.HashMethod = DOpts.CompatHashMethod;
	HLAlt = 1;
	r = HistoryLookup(msgid, nh);
	DOpts.HashMethod = save;
	HLAlt = 0;
    }

    if (r == 0 && nh != NULL)
	memcpy(nh, &h, sizeof(h));

    return(r);
}

int
HistoryLookupByHash(hash_t hv, History *h)
{
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    int counter = 0;

    if (HHeadMap->hmagic != HMAGIC)
	historyReOpen();

    hi = (hv.h1 ^ hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    index = HAry[hi];

    while (index) {
	if (HHead.version > 1)
	    off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	else 
	    off = index;
	if (pread(HFd, h, sizeof(*h), off) != sizeof(*h)) {
	    if ((LoggedDHistCorrupt & 1) == 0 || DebugOpt) {
		LoggedDHistCorrupt |= 1;
		logit(LOG_ERR, "dhistory file corrupted on lookup");
		sleep(1);
	    }
	    index = 0;	/* unreadable record: a miss, not a hit */
	    break;
	}
	if (h->hv.h1 == hv.h1 && h->hv.h2 == hv.h2)
	    break;
	pindex = index;
	index = h->next;
	if (counter++ > 5000) {
	    logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d",
			(int)pindex, (int)index, (int)((hv.h1 ^ hv.h2) & HMask));
	    index = 0;
	}
    }
    if (index != 0) {
	return(0);
    }
    return(-1);
}

HistIndex
HistoryPosLookupByHash(hash_t hv, History *h)
{
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    int counter = 0;

    hi = (hv.h1 ^ hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    index = HAry[hi];

    while (index) {
	if (HHead.version > 1)
	    off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	else 
	    off = index;
	if (pread(HFd, h, sizeof(*h), off) != sizeof(*h)) {
	    if ((LoggedDHistCorrupt & 1) == 0 || DebugOpt) {
		LoggedDHistCorrupt |= 1;
		logit(LOG_ERR, "dhistory file corrupted on lookup");
		sleep(1);
	    }
	    index = 0;	/* unreadable record: a miss, not a hit */
	    break;
	}
	if (h->hv.h1 == hv.h1 && h->hv.h2 == hv.h2)
	    break;
	pindex = index;
	index = h->next;
	if (counter++ > 5000) {
	    logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d",
			(int)pindex, (int)index, (int)((hv.h1 ^ hv.h2) & HMask));
	    index = 0;
	}
    }
    if (index != 0) {
	return(index);
    }
    return(-1);
}

#define HIST_LOCK_TRIES	5	/* tries for a chain lock that fails with ENOLCK */

/*
 * After an update under the chain lock: was the history marked dead in
 * the meantime?  dhisexpire copied the record long before, so the update
 * must be done again in the new history.  Fast mode has its own file.
 */
static int
updateLost(void)
{
    return((HFlags & HGF_FAST) == 0 && HHeadMap->hmagic != HMAGIC);
}

/* File offset of the record at index. */
static off_t
recordOffset(HistIndex index)
{
    if (HHead.version > 1)
	return((off_t)HEntryOff + (off_t)index * sizeof(History));
    return((off_t)index);
}

static void
unlockChain(off_t chainlock)
{
    if (chainlock > 0 && hflock(HFd, chainlock, XLOCK_UN) < 0)
	HistoryFatalIO("hash chain unlock");
}

/*
 * Lock the hash chain of hv for an add or a record update.  Returns the
 * lock offset, 0 in fast mode (no lock, it has the file to itself), or
 * -1.  dhisexpire marks the history dead and then waits for every chain
 * lock before it copies the last records: a history found dead under the
 * lock is reopened, so no write goes to the old file after that copy.
 */
static off_t
lockChain(hash_t hv)
{
    int tries = 0;

    for (;;) {
	off_t chainlock;

	if (HHeadMap->hmagic != HMAGIC)
	    historyReOpen();
	if (HFlags & HGF_FAST)
	    return(0);
	chainlock = HHead.headSize +
	    (off_t)((hv.h1 ^ hv.h2) & HMask) * sizeof(HistIndex);
	while (hflock(HFd, chainlock, XLOCK_EX) < 0) {
	    int error = errno;

	    /* ENOLCK can pass (as in KPDBLockRec); other errors do not */
	    if (error == ENOLCK && ++tries < HIST_LOCK_TRIES) {
		usleep(100000);
		continue;
	    }
	    logit(LOG_ERR, "Unable to lock dhistory chain: %s",
		strerror(error));
	    errno = error;
	    return(-1);
	}
	if (HHeadMap->hmagic == HMAGIC)
	    return(chainlock);
	unlockChain(chainlock);
    }
}

/*
 * This is a new and simplifed HistoryAdd that locks an append lock
 * (pos 4) and just appends the single history record and unlocks.
 * It then updates the hash chain (already locked) by adding the
 * new history entry at the beginning as usual.
 */
int
HistoryAdd(const char *msgid, History *h)
{
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    off_t chainlock;
    int r = RCOK;

    /*
     * This should not happen, but if it does, log it and pretend we did it
     */
    if (h->gmt == 0) {
	logit(LOG_ERR, "Not adding history entry with gmt=0");
	return(RCOK);
    }
    /*
     * record lock, search for message id
     *
     */

    if ((chainlock = lockChain(h->hv)) < 0)
	return(RCTRYAGAIN);
    hi = (h->hv.h1 ^ h->hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);

    /*
     * make sure message-id is not already in hash table
     */
    if ((HFlags & HGF_NOSEARCH) == 0) {
	int counter = 0;

	index = HAry[hi];
	while (index) {
	    static History ht;

	    if (HHead.version > 1)
		off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	    else
		off = index;
	    if (pread(HFd, &ht, sizeof(ht), off) != sizeof(ht)) {
		if ((LoggedDHistCorrupt & 2) == 0 || DebugOpt) {
		    LoggedDHistCorrupt |= 2;
		    logit(LOG_ERR, "dhistory file corrupted @ %u (%s)",
					index, strerror(errno));
		    sleep(1);
		}
		break;
	    }
	    if (ht.hv.h1 == h->hv.h1 && ht.hv.h2 == h->hv.h2) {
		r = RCALREADY;
		break;
	    }
	    pindex = index;
	    index = ht.next;
	    if (counter++ > 5000) {
		logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d (%s)",
					(int)pindex, (int)index,
					(int)((ht.hv.h1 ^ ht.hv.h2) & HMask),
					msgid ? msgid : "no message-id");
		index = 0;
	    }
	}
    }

    if (r == RCOK) {
	off_t writePos;
	HistIndex oldIndex = HAry[hi];

	h->next = oldIndex;

	if ((HFlags & HGF_FAST) == 0 &&
	    hflock(HFd, 4, XLOCK_EX) < 0) {
	    logit(LOG_ERR, "Unable to lock dhistory append: %s",
		strerror(errno));
	    r = RCTRYAGAIN;
	} else {
	    writePos = lseek(HFd, 0L, SEEK_END);
	    if (writePos < 0) {
		logit(LOG_ERR, "Unable to seek dhistory end: %s",
		    strerror(errno));
		r = RCTRYAGAIN;
	    } else if (HHead.version > 1 &&
		(writePos - (off_t)HEntryOff) % (off_t)sizeof(History) != 0) {
		/*
		 * The file ends inside a record (a crash in the middle of an
		 * append).  Round up to the next record boundary instead of
		 * truncating down: that never loses a complete record, and
		 * the stray bytes become a gap that no chain points to.
		 * Without this every later index is skewed.
		 */
		off_t odd = (writePos - (off_t)HEntryOff) % (off_t)sizeof(History);
		off_t newPos = writePos + ((off_t)sizeof(History) - odd);
		static const char zeros[sizeof(History)] = { 0 };

		logit(LOG_ERR, "dhistory file ends inside a record, skipping %d stray bytes",
		    (int)odd);
		/* make the gap an all-zero record (gmt 0) for sequential scans */
		(void)HistoryWriteAt(HFd, zeros, (size_t)odd, writePos - odd);
		writePos = newPos;
	    }
	    if (r == RCOK && HistoryWriteAt(HFd, h, sizeof(*h), writePos) < 0) {
		int error = errno;

		/* Do not leave a partial, unindexed record on disk. */
		if (ftruncate(HFd, writePos) < 0)
		    HistoryFatalIO("rollback of incomplete record");
		logit(LOG_ERR, "Unable to append dhistory record: %s",
		    strerror(error));
		r = RCTRYAGAIN;
	    }

	    if ((HFlags & HGF_FAST) == 0 &&
		hflock(HFd, 4, XLOCK_UN) < 0)
		HistoryFatalIO("append unlock");

	    if (r == RCOK) {
		if (HHead.version > 1)
		    index = (HistIndex)((writePos - HEntryOff) / sizeof(History));
		else
		    index = (HistIndex)writePos;

		if ((HFlags & HGF_FAST) == 0) {
		    off_t indexPos = HHead.headSize +
			(off_t)hi * sizeof(HistIndex);

		    if (HistoryWriteAt(HFd, &index, sizeof(index), indexPos) < 0) {
			int error = errno;

			/* Restore the previous chain head on partial writes. */
			if (HistoryWriteAt(HFd, &oldIndex,
			    sizeof(oldIndex), indexPos) < 0)
			    HistoryFatalIO("restoring hash chain head");
			logit(LOG_ERR, "Unable to update dhistory chain: %s",
			    strerror(error));
			r = RCTRYAGAIN;
		    }
		} else {
		    HAry[hi] = index;
		}
	    }
	}
    }

    unlockChain(chainlock);

    return(r);
}

/*
 * Replace the record of h->hv with *h, under its chain lock.  The link
 * and an expired flag on disk are kept: *h can be an old copy, read
 * before the history was replaced or the record was expired.  Returns
 * 1 when stored, 0 when not found, -1 when the lock failed (errno).
 */
int
HistoryStore(History *h)
{
    HistIndex index;
    History th;
    off_t chainlock;
    int lost;

    do {
	if ((chainlock = lockChain(h->hv)) < 0)
	    return(-1);
	index = HistoryPosLookupByHash(h->hv, &th);
	if (index != (HistIndex)-1) {
	    History nh = *h;

	    nh.next = th.next;
	    if (H_EXPIRED(th.exp))
		nh.exp |= EXPF_EXPIRED;
	    if (HistoryWriteAt(HFd, &nh, sizeof(nh), recordOffset(index)) < 0)
		HistoryFatalIO("record update");
	}
	lost = updateLost();
	unlockChain(chainlock);
    } while (lost);
    return(index != (HistIndex)-1);
}

/* Write exp into the record at index; the caller holds its chain lock. */
static void
storeExp(uint16 exp, HistIndex index)
{
    if (HistoryWriteAt(HFd, &exp, sizeof(exp),
	recordOffset(index) + offsetof(History, exp)) < 0)
	HistoryFatalIO("expiration update");
}

/*
 * Store h->exp in the record at index (from a lookup of h->hv), under
 * its chain lock.  When that record is no longer h->hv (the history was
 * replaced since), it is looked up again.  Returns 1 when stored, 0
 * when not found, -1 when the lock failed (errno).
 */
int
HistoryStoreExp(History *h, HistIndex index)
{
    History th;
    off_t chainlock;
    int lost;

    do {
	if ((chainlock = lockChain(h->hv)) < 0)
	    return(-1);
	if (pread(HFd, &th, sizeof(th), recordOffset(index)) != sizeof(th) ||
	    th.hv.h1 != h->hv.h1 || th.hv.h2 != h->hv.h2)
	    index = HistoryPosLookupByHash(h->hv, &th);
	if (index != (HistIndex)-1)
	    storeExp(h->exp, index);
	lost = updateLost();
	unlockChain(chainlock);
    } while (lost);
    return(index != (HistIndex)-1);
}

/*
 * Cancel/expire article in history.  The lookup and the update are
 * done under the chain lock.  Returns 1 when found, 0 when not, 2 when
 * it was found but dhisexpire replaced the history meanwhile and the
 * new one has no such entry, -1 when the lock failed (errno).
 */
int
HistoryExpire(const char *msgid, History *h, int unexp)
{
    HistIndex index;
    hash_t hv;
    off_t chainlock;
    int lost;
    int found = 0;

    if (msgid == NULL)
	hv = h->hv;
    else
	hv = hhash(msgid);
    do {
	if ((chainlock = lockChain(hv)) < 0)
	    return(-1);
	index = HistoryPosLookupByHash(hv, h);
	if (index != (HistIndex)-1) {
	    found = 1;
	    if (unexp && H_EXPIRED(h->exp)) {
		h->exp &= ~EXPF_EXPIRED;
		storeExp(h->exp, index);
	    } else if (!unexp && !H_EXPIRED(h->exp)) {
		h->exp |= EXPF_EXPIRED;
		storeExp(h->exp, index);
	    }
	}
	lost = updateLost();
	unlockChain(chainlock);
    } while (lost);
    if (index == (HistIndex)-1)
	return(found ? 2 : 0);
    return(1);
}
