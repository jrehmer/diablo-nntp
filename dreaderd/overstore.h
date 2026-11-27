#ifndef DIABLO_OVERSTORE_H
#define DIABLO_OVERSTORE_H

/* Include after defs.h for artno_t. */
struct TimeRestrict;

typedef struct OverStoreRange {
    const char *group;
    artno_t next;
    artno_t last;
    struct TimeRestrict *timeRestrict;
    int (*canContinue)(void *context);
    void (*emit)(void *context, artno_t number, const char *header,
		    int headerLen, int articleSize, int lines);
    void *context;
} OverStoreRange;

/*
 * Visit readable rows in order through last, inclusive, with next advanced
 * past visited numbers and holes. The caller can pause before each scan
 * step, which may visit several rows when the legacy mmap path is enabled.
 * Header bytes are borrowed for the emit call only. Return zero if the
 * group has no readable index, or one after scanning or pausing.
 *
 * Contract, as the reader uses it:
 * - Both callbacks must be set. canContinue is called once before each
 *   step and counts steps (and may FD_SET the descriptor), so call it
 *   exactly once per step. A step is one number, or one datafile
 *   window with USE_OVER_MADVISE. It returns zero to stop at once;
 *   next then names the first number not yet visited.
 * - The header is NUL-terminated at headerLen: OutputOverview scans each
 *   line to the NUL, not to headerLen. It is valid only inside emit,
 *   before any later remap or PutOverInfo.
 * - lines is -1 for none. OVER prints articleSize + 1 as :bytes when
 *   the stored header has no Bytes line.
 * - The legacy store skips a number with no record, a record over
 *   OVER_HMAPSIZE / 2, one older than ov_LimitSecs, one received before
 *   timeRestrict, one without data, and one failing the NUL checks. The
 *   USE_OVER_MADVISE scan applies timeRestrict and the NUL checks but
 *   not the ov_LimitSecs age or the OVER_HMAPSIZE / 2 size check.
 * - The return value does not tell done from paused: compare next with
 *   last. The reader allows 10000 steps per pass, 50 for XPAT.
 */
int OverStoreReadRange(OverStoreRange *range);

#endif
