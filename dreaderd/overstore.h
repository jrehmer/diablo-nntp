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
 */
int OverStoreReadRange(OverStoreRange *range);

#endif
