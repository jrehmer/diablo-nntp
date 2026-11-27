
/*
 * LIB/HASH.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * base32 encode is based on code that is
 * Copyright (c) 1995, 1996, 1997 Kungliga Tekniska H<F6>gskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * CRC hash generator
 *
 * NOTE!  If using this CRC code in other projects, please fix the folding
 * code (see 'folding' below).  The '| 1' should be '^ 1'.  It cannot be
 * changed in Diablo, unfortunately, without putting everying through the
 * wringer again which I do not want to do.
 */

#include "defs.h"

Prototype int quickhash(const char *st);
Prototype hash_t hhash(const char *msgid);
Prototype uint64_t siphash24(const void *in, size_t len, uint64_t k0, uint64_t k1);
Prototype hash_t msgidhash(const char *msgid);
Prototype const char *MsgIdHashName(int method);
Prototype void bhash_init(void);
Prototype void bhash_update(const char *p, int len);
Prototype void bhash_final(md5hash_t *h);
Prototype md5hash_t *md5hash(const char *st);
Prototype char *md5hashstr(md5hash_t *hash, char *buf);
Prototype void bhash(hash_t *h, const char *p, int len);
Prototype int GFIndex(char c);
Prototype const char *GFHash(const char *group, GroupHashType *method);
Prototype const char *GFName(const char *group, int ftype, artno_t artBase, int wantdir, int iter, GroupHashType *method);
static void FindGroupDir(const char *group, char *path, size_t pathsize);
Prototype int ExtractGroupHashInfo(const char *name, char *Hash, artno_t *artBase, int *iter);
Prototype int ExtractGroupHashInfoGz(const char *name, char *Hash, char *HashZ, artno_t *artBase, int *iter);
Prototype char *GetHash(GroupHashType *Hash, char *hashStr);
Prototype void SetCacheHash(struct CacheHash_t* ch, const char *group, int iter, GroupHashType *method);
Prototype int CacheHashEqual(const struct CacheHash_t *a, const struct CacheHash_t *b);
Prototype int CacheHitsUsable(int fd, const struct CacheHitHead *chh, int r);

#define P1	7834891
#define P2	6017489

#define HINIT1  0xFAC432B1
#define HINIT2  0x0CD5E44A

#define POLY1   0x00600340UL
#define POLY2   0x00F0D50BUL
#define OPOLY2  0x00F0D50AUL

#ifdef TEST
int DOpts.HashMethod = HASH_CRC;
#endif

hash_t CrcXor[3][256];
int    DidCrcInit;

struct diablo_MD5Context HMD5ctx;
struct diablo_MD5Context BMD5ctx;	/* article body MD5 context */

int
quickhash(const char *st)
{
    register int rval = 0;
    register unsigned char *p = (unsigned char *)st;

    while (*p)
	rval += *p++;
    return(rval);
}

/*
 * The CRC and hash arithmetic below is done in uint32 and stored back into
 * the int32 fields of hash_t.  Shifting a signed int left into or past its
 * sign bit is undefined in C; the unsigned form gives the same bits the
 * compilers always produced, so hashes already on disk (dhistory, the
 * overview index, the reader's group files) are unchanged.  Right shifts
 * of the signed values are kept as they were.
 */
void
crcinit(int method)
{
    int i;

    for (i = 0; i < 256; ++i) {
        int j;
        int v = i;
        uint32 h1 = 0;
        uint32 h2 = 0;

        for (j = 0; j < 8; ++j, (v <<= 1)) {
            if (v & 0x80) {
                h1 ^= POLY1;
                h2 ^= (method == HASH_CRC) ? POLY2 : OPOLY2;
            }
            h2 = (h2 << 1);
            if (h1 & 0x80000000)
                h2 |= 1;
            h1 <<= 1;
        }
        CrcXor[method][i].h1 = (int32)h1;
        CrcXor[method][i].h2 = (int32)h2;
    }
    DidCrcInit = 1;
}

hash_t
hhash(const char *msgid)
{
    hash_t t;

    if (DOpts.HashMethod == HASH_CRC || DOpts.HashMethod == HASH_OCRC) {
	/*
	 * HASH_CRC  - CRC64
	 */
	uint32 h1 = HINIT1;
	uint32 h2 = HINIT2;

	if (DidCrcInit == 0) {
	    crcinit(HASH_CRC);
	    crcinit(HASH_OCRC);
	}
	while (*msgid) {
	    int i = (h1 >> 24) & 255;
	    h1 = (h1 << 8) ^ (h2 >> 24) ^ (uint32)CrcXor[DOpts.HashMethod][i].h1;
	    h2 = (h2 << 8) ^ (uint8)*msgid ^ (uint32)CrcXor[DOpts.HashMethod][i].h2;
	    ++msgid;
	}
	/*
	 * Fold the generated CRC.  Note, this is buggy but it is too late
	 * for me to change it now.  I should have XOR'd the 1 in, not OR'd
	 * it when folding the bits.
	 */
	if (h1 & 0x80000000)
	    h1 = (h1 & 0x7FFFFFFF) | 1;
	if (h2 & 0x80000000)
	    h2 = (h2 & 0x7FFFFFFF) | 1;
	h1 |= 0x80000000;	/* indicate CRC64 hash */
	t.h1 = (int32)h1;
	t.h2 = (int32)h2;
    } else {
	/*
	 * HASH_PRIME
	 */
	int h1 = 0x0034629D;
	int h2 = 0x007395DD;
	int hv = 0x1A3F5C4F;

	while (*msgid) {
	    h1 = h1 * *(const unsigned char *)msgid % P1;
	    h2 = h2 * *(const unsigned char *)msgid % P2;
	    hv = (int)(((uint32)hv << 5) ^ *(const unsigned char *)msgid ^
		(uint32)(hv >> 23));
	    ++msgid;
	}
	/* bit 31 reserved */
	t.h1 = (int32)(((uint32)h1 ^ ((uint32)hv << 14)) & 0x7FFFFFFF);
	t.h2 = (int32)(((uint32)h2 ^ ((uint32)hv << 20)) & 0x7FFFFFFF);
    }
    return(t);
}

/*
 * siphash24() - SipHash-2-4 (Aumasson and Bernstein) of len bytes with the
 * key k0, k1.  The bytes are read as little-endian words on every host, so
 * the result does not depend on the byte order.
 */
#define SIP_ROTL(x, b)	(((x) << (b)) | ((x) >> (64 - (b))))
#define SIP_ROUND do {						\
	v0 += v1; v1 = SIP_ROTL(v1, 13); v1 ^= v0;		\
	v0 = SIP_ROTL(v0, 32);					\
	v2 += v3; v3 = SIP_ROTL(v3, 16); v3 ^= v2;		\
	v0 += v3; v3 = SIP_ROTL(v3, 21); v3 ^= v0;		\
	v2 += v1; v1 = SIP_ROTL(v1, 17); v1 ^= v2;		\
	v2 = SIP_ROTL(v2, 32);					\
    } while (0)

uint64_t
siphash24(const void *in, size_t len, uint64_t k0, uint64_t k1)
{
    const unsigned char *p = in;
    const unsigned char *end = p + (len & ~(size_t)7);
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    uint64_t b = (uint64_t)len << 56;
    uint64_t m;

    for (; p != end; p += 8) {
	m = (uint64_t)p[0] | (uint64_t)p[1] << 8 | (uint64_t)p[2] << 16 |
	    (uint64_t)p[3] << 24 | (uint64_t)p[4] << 32 |
	    (uint64_t)p[5] << 40 | (uint64_t)p[6] << 48 |
	    (uint64_t)p[7] << 56;
	v3 ^= m;
	SIP_ROUND;
	SIP_ROUND;
	v0 ^= m;
    }
    switch (len & 7) {
	case 7: b |= (uint64_t)p[6] << 48;	/* FALLTHROUGH */
	case 6: b |= (uint64_t)p[5] << 40;	/* FALLTHROUGH */
	case 5: b |= (uint64_t)p[4] << 32;	/* FALLTHROUGH */
	case 4: b |= (uint64_t)p[3] << 24;	/* FALLTHROUGH */
	case 3: b |= (uint64_t)p[2] << 16;	/* FALLTHROUGH */
	case 2: b |= (uint64_t)p[1] << 8;	/* FALLTHROUGH */
	case 1: b |= (uint64_t)p[0];
    }
    v3 ^= b;
    SIP_ROUND;
    SIP_ROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIP_ROUND;
    SIP_ROUND;
    SIP_ROUND;
    SIP_ROUND;
    return(v0 ^ v1 ^ v2 ^ v3);
}

/*
 * msgidhash() - the hash of a Message-ID for the history, the overview
 * index (oa_MsgHash) and the Message-ID caches (D-210).
 *
 * MSGIDHASH_HASH (msgidhash hash, the default) is hhash(), the method of the
 * "hash" option, as before.  With hash crc its fold loses bit 0 of h1, so two
 * Message-IDs of the same length that differ only in bit 0 of the 5th byte
 * from the end share a hash about half the time; the history keeps only the
 * hash, so the second one is refused as a duplicate.
 *
 * MSGIDHASH_SIPHASH (msgidhash siphash) is SipHash-2-4 with a fixed key, so
 * every site gets the same hash for a Message-ID (a history file can move
 * between hosts) and needs no key of its own.  The 64 bits are kept as 62 in
 * the hash_t layout of the crc method: bit 31 of h1 set, bit 31 of h2 clear.
 * It is about three times faster than hhash() crc.
 *
 * The group-name hashes (GFHash() and the names made from it) stay on
 * hhash(): they name the reader's files on disk.
 */
#define MSGID_K0	0x6d206f6c62616944ULL	/* "Diablo m", little-endian */
#define MSGID_K1	0x79656b2064696773ULL	/* "sgid key" */

hash_t
msgidhash(const char *msgid)
{
    hash_t t;

    if (DOpts.MsgIdHashMethod == MSGIDHASH_SIPHASH) {
	uint64_t m = siphash24(msgid, strlen(msgid), MSGID_K0, MSGID_K1);

	t.h1 = (int32)((uint32)(m >> 32) | 0x80000000);
	t.h2 = (int32)((uint32)m & 0x7FFFFFFF);
	return(t);
    }
    return(hhash(msgid));
}

const char *
MsgIdHashName(int method)
{
    switch (method) {
	case MSGIDHASH_HASH:
	    return("hash");
	case MSGIDHASH_SIPHASH:
	    return("siphash");
    }
    return("unknown");
}

/*
 * Slightly different and faster hash algorithm to handle message bodies.
 * This is simpler.
 */

void 
bhash(hash_t *h, const char *p, int len)
{
    while (len) {
	/* simple checksum; uint32 as in hhash() */
	h->h1 = (int32)((uint32)h->h1 + *(const unsigned char *)p);
	h->h2 = (int32)(((uint32)h->h2 << 5) ^ (uint32)h->h1 ^
	    (uint32)(h->h2 >> 27));
	++p;
	--len;
    }
}

void 
bhash_init(void)
{
    diablo_MD5Init(&BMD5ctx);
}

void 
bhash_update(const char *p, int len)
{
    diablo_MD5Update(&BMD5ctx, (const unsigned char *)p, len);
}

void 
bhash_final(md5hash_t *h)
{
    diablo_MD5Final((unsigned char *)h, &BMD5ctx);
}

/*
 * The digest has to be defined as md5hash_t to get memory alignments
 * right, otherwise we get bus errors.
 */
md5hash_t *
md5hash(const char *st)
{
    static md5hash_t digest;

    diablo_MD5Init(&HMD5ctx);
    diablo_MD5Update(&HMD5ctx, (const unsigned char *)st, strlen(st));
    diablo_MD5Final((unsigned char *)&digest, &HMD5ctx);
    return((md5hash_t *)&digest);
}


char *
md5hashstr(md5hash_t *hash, char *buf)
{
    int i;
    unsigned char *digest;
    static const char hex[] = "0123456789abcdef";

    digest = (unsigned char *)hash;
    for (i = 0; i < 16; i++) {
        buf[i+i] = hex[digest[i] >> 4];
        buf[i+i+1] = hex[digest[i] & 0x0f];
    }
    buf[i+i] = '\0';
    return(buf);
}


/*
 * Use a base32 algorithm to encode the MD5 of a newsgroup name into
 * a filename
 */

static char base32[] = "abcdefghijklmnopqrstuvwxyz012345"
		       "abcdefghijklmnopqrstuvwxyz012345";
static char base64[] = "abcdefghijklmnopqrstuvwxyz0123456789"
		       "ABCDEFGHIJKLMNOPQRSTUVWXYZ+-";

int base_encode(const void *data, int size, char **str, char *base)
{
  static char tmpbuf[32];
  char *p;
  char *s;
  int i;
  int c;
  unsigned char *q;

  p = s = tmpbuf;
  q = (unsigned char*)data;
  i=0;
  for(i = 0; i < size;){
    c=q[i++];
    c*=256;
    if(i < size)
      c+=q[i];
    i++;
    c*=256;
    if(i < size)
      c+=q[i];
    i++;
    p[0]=base[(c&0x00fc0000) >> 18];
    p[1]=base[(c&0x0003f000) >> 12];
    p[2]=base[(c&0x00000fc0) >> 6];
    p[3]=base[(c&0x0000003f) >> 0];
    if(i > size)
      p[3]='=';
    if(i > size+1)
      p[2]='=';
    p+=4;
  }
  *p=0;
  while (*--p == '=')
	*p = 0;
  *str = s;
  return strlen(s);
}

int
GFIndex(char c)
{
    return(index(base64, c) - base64);
}

/*
 * Newsgroup hash types and how the directories are created
 *
 * crc           (default)
 *	This is a simple hhash() of the newsgroup name printed as 2
 *	8 byte hex values separated by a '.'. e.g: f5ca1300.3d9d520d
 *
 * md5-32[:B]/N[/N][/N]
 * md5-32[:B]\N[\N][\N]
 *	This is a 32-bit conversion of a base64 type encoding of the
 *	newsgroup name. The base64 character set is shown above.
 *
 *	The 'B' value indicates the number of bytes of the hash (from the
 *	beginning) to be used for the newsgroup filename
 *	(defaults to all = 22). Lower values can be used to reduce the
 *	filename length and possibly the directory size.
 *
 *	The '/N' values sets the number of directories to create. Multiple
 *	'/N' values can be specified to indicate multiple levels of that
 *	number of directories. The directory name is made by taking the
 *	a hhash() of the hash string, adding the 2 integers of the hash
 *	and applying a % N to it (padding the result with leading '0's
 *	to make it a consistent length. There is no easy way to calculate
 *	the directory name other than to use 'dgrpctl listgrouphashfile'.
 *	
 *	The '\N' value represents the number of characters of the hash to
 *	use for the directory name (max of 9). e.g: for \1\2 on a hash of
 *	'xhpgbjwwlrq2tl1pfq1epq', the directory structure would be:
 *
 *		/news/spool/group/x/hp/
 *
 *	giving 32 directories at the first level and 32*32 = 1024 dirs
 *	at the second level. This gives an easy way to calculate directories
 *	but has a less even spread of groups per directory.
 *
 *	A maximum of 9 directory levels can be created, although no more
 *	than 3 is strictly necessary.
 *
 * md5-64[:B]/N[/N]
 * md5-64[:B]\N[\N]
 *	This is a 64-bit base64 type encoding ('/' converted to a '-').
 *
 *	The 'B' and 'N' values are as the md-32 option above, with the
 *	number of directories for a '\1' directory level set to 64 and
 *	'\2' set to 4096.
 *
 * hierarchy
 *	Use the newsgroup name for the filename and the directory is made
 *	from the '.' separated components of the newsgroup name. This
 *	option can lead to a very uneven spread of newsgroups per directory.
 *
 */

const char *
GFHash(const char *group, GroupHashType *hashtype)
{
    static char GFBuf[PATH_MAX];

    switch (hashtype->gh_type) {
	case HASHGRP_CRC: {
	    hash_t hv = hhash(group);
	    snprintf(GFBuf, hashtype->gh_sigbytes, "%08x.%08x", (uint32)hv.h1, (uint32)hv.h2 );
	    break;
	}
	case HASHGRP_32MD5: case HASHGRP_64MD5: {
	    char buf[34];
	    char *p = buf;
	    unsigned char digest[16];
	    diablo_MD5Init(&HMD5ctx);
	    diablo_MD5Update(&HMD5ctx, (const unsigned char *)group, strlen(group));
	    diablo_MD5Final(digest, &HMD5ctx);
	    base_encode(digest, 16, &p,
		(hashtype->gh_type == HASHGRP_64MD5) ?  base64 : base32);
	    snprintf(GFBuf, hashtype->gh_sigbytes, "%s", p);
	    break;
	}
	case HASHGRP_HIER: {
	    snprintf(GFBuf, hashtype->gh_sigbytes, "%s", group);
	}
    }

    return(GFBuf);
}

/*
 * Hash a string to an integer. Conflicts are not important as this
 * is only used to calculate a directory from a hash
 */
unsigned int
hashNum(char *st)
{
    hash_t h = hhash(st);
    return(abs(h.h1 + h.h2));
}

const char *
GFName(const char *group, int gftype, artno_t artBase, int wantdir, int iter, GroupHashType *method)
{
    static char GFBuf[PATH_MAX];
    int n = -1;
    char ftype[16];
    char itbuf[16];
    char *hashstr = (char *)GFHash(group, method);

    switch (method->gh_type) {
	case HASHGRP_CRC: {
	    hash_t hv = hhash(group);
	    switch (gftype) {
		case GRPFTYPE_OVER:
		    strcpy(ftype, "over");
		    break;
		case GRPFTYPE_DATA:
		    strcpy(ftype, "data");
		    break;
	    }
	    if (iter > 0)
		snprintf(itbuf, sizeof(itbuf), ".%d", iter);
	    else
		itbuf[0] = 0;
	    if (wantdir == 2)
		n = snprintf(GFBuf, sizeof(GFBuf), "%s/%02x/%s.%lld.%s%s",
						PatExpand(GroupHomePat),
						(uint8)hv.h1,
						ftype,
						artBase,
						hashstr,
						itbuf
		);
	    else if (wantdir == 1)
		n = snprintf(GFBuf, sizeof(GFBuf), "%02x/%s.%lld.%s%s",
						(uint8)hv.h1,
						ftype,
						artBase,
						hashstr,
						itbuf
		);
	    else
		n = snprintf(GFBuf, sizeof(GFBuf), "%s.%lld.%s%s",
						ftype,
						artBase,
						hashstr,
						itbuf
		);
	    break;
	}
	case HASHGRP_32MD5: case HASHGRP_64MD5: {
	    char dirbuf[PATH_MAX];
	    char tmpbuf[22];
	    int i;
	    switch (gftype) {
		case GRPFTYPE_OVER:
		    strcpy(ftype, "o");
		    break;
		case GRPFTYPE_DATA:
		    strcpy(ftype, "d");
		    break;
	    }
	    switch (method->gh_type) {
		case HASHGRP_32MD5:
		    strcat(ftype, ".32");
		    break;
		case HASHGRP_64MD5:
		    strcat(ftype, ".64");
		    break;
	    }
	    dirbuf[0] = 0;
	    for (i = 0; i < method->gh_dirlvl; i++) {
		switch (method->gh_dirtype) {
		    case HASHGRP_DIR_NUM: {
			char format[16];
			int formsize = 0;
			int c = method->gh_dirinfo[i];
			while (c > 0) {
			    formsize++;
			    c = (c - 1) / 10;
			}
			sprintf(format,"%%0%dx", formsize);
			sprintf(tmpbuf, format,
				hashNum(&hashstr[i]) % method->gh_dirinfo[i]);
			if (i > 0)
			    strcat(dirbuf, "/");
			strcat(dirbuf, tmpbuf);
			break;
		    }
		    case HASHGRP_DIR_BIT:
			snprintf(tmpbuf, method->gh_dirinfo[i] + 1, "%s", hashstr);
			if (i > 0)
			    strcat(dirbuf, "/");
			strcat(dirbuf, tmpbuf);
			break;
		}
	    }
	    snprintf(itbuf, sizeof(itbuf), ".%d", iter);
	    if (wantdir == 2)
		n = snprintf(GFBuf, sizeof(GFBuf), "%s/%s/%s%s.%s.%lld",
						PatExpand(GroupHomePat),
						dirbuf,
						hashstr,
						itbuf,
						ftype,
						artBase
		);
	    else if (wantdir == 1)
		n = snprintf(GFBuf, sizeof(GFBuf), "%s/%s%s.%s.%lld",
						dirbuf,
						hashstr,
						itbuf,
						ftype,
						artBase
		);
	    else
		n = snprintf(GFBuf, sizeof(GFBuf), "%s%s.%s.%lld",
						hashstr,
						itbuf,
						ftype,
						artBase
		);
	    break;
	}
	case HASHGRP_HIER: {
	    char groupPath[PATH_MAX];
	    switch (gftype) {
		case GRPFTYPE_OVER:
		    strcpy(ftype, "o");
		    break;
		case GRPFTYPE_DATA:
		    strcpy(ftype, "d");
		    break;
	    }
	    if (wantdir) {
		FindGroupDir(group, groupPath, sizeof(groupPath));
		n = snprintf(GFBuf, sizeof(GFBuf), "%s%s.%lld.%s",
						groupPath,
						ftype,
						artBase,
						hashstr
		);
	    } else {
		n = snprintf(GFBuf, sizeof(GFBuf), "%s.%lld.%s",
						ftype,
						artBase,
						hashstr
		);
	    }
	    break;
	}
    }
    if (n < 0 || (size_t)n >= sizeof(GFBuf))
	fatal("GFName: generated pathname too long for newsgroup %s", group);
    if (DebugOpt > 4)
	printf("GFName:%s\n", GFBuf);
    return(GFBuf);
}

void
SetCacheHash(struct CacheHash_t* ch, const char *group, int iter, GroupHashType *method)
{
    /* Clear the padding after iter too: the key is stored in cache.hits. */
    memset(ch, 0, sizeof(*ch));
    ch->iter = iter;
    switch (method->gh_type) {
	case HASHGRP_CRC: {
	    hash_t hv = hhash(group);
	    ch->h1 = (uint64_t) hv.h1;
	    ch->h2 = (uint64_t) hv.h2;
	    break;
	}
	case HASHGRP_32MD5:
	case HASHGRP_64MD5:
	case HASHGRP_HIER: { /* we just hope there will be no collision */
	    md5hash_t *hv = md5hash(group);
	    ch->h1 = hv->h1;
	    ch->h2 = hv->h2;
	    break;
	}
    }
}

/*
 * CacheHashEqual() - compare two cache.hits keys field by field.  Not
 * memcmp(): struct CacheHash_t has padding after iter, and entries written
 * before SetCacheHash() cleared it hold whatever was on the stack there.
 */
int
CacheHashEqual(const struct CacheHash_t *a, const struct CacheHash_t *b)
{
    return(a->h1 == b->h1 && a->h2 == b->h2 && a->iter == b->iter);
}

/*
 * CacheHitsUsable() - the header read from cache.hits (r bytes) describes a
 * scoreboard that fits in the file.
 */
int
CacheHitsUsable(int fd, const struct CacheHitHead *chh, int r)
{
    struct stat st;

    return(r == sizeof(*chh) &&
	chh->chh_magic == CHMAGIC &&
	chh->chh_version == CHVERSION &&
	fstat(fd, &st) == 0 &&
	chh->chh_hashSize != 0 &&
	chh->chh_end >= sizeof(*chh) &&
	chh->chh_newEntry >= sizeof(*chh) +
	    (uint64_t)chh->chh_hashSize * sizeof(uint32) &&
	chh->chh_end >= chh->chh_newEntry &&
	st.st_size >= chh->chh_end);
}

static void
FindGroupDir(const char *group, char *path, size_t pathsize)
{
    const char *p = group;
    const char *q = p;
    size_t used = 0;

    if (pathsize < 2)
	fatal("FindGroupDir: destination buffer too small");
    *path = 0;
    while ((p = strchr(p, '.')) != NULL) {
	size_t component = (size_t)(p - q);

	if (component <= 1) {
	    p++;
	    continue;
	}
	/* Need space for the component, trailing slash and NUL. */
	if (used > pathsize - 2 || component > pathsize - used - 2)
	    fatal("FindGroupDir: pathname too long for newsgroup %s", group);
	memcpy(path + used, q, component);
	used += component;
	path[used++] = '/';
	path[used] = 0;
	p++;
	q = p;
    }
}

/*
 * Given a hashed newsgroup header filename, extract the components
 * and return hash type.  The hash is returned as it stands in the name:
 * for a hierarchy data file "d.<n>.<group>.gz" it is "<group>.gz", which is
 * the name of the group of a plain data file when the group itself ends in
 * ".gz".  ExtractGroupHashInfoGz() tells the two readings apart (D-318).
 */
int
ExtractGroupHashInfo(const char *name, char *Hash, artno_t *artBase, int *iter)
{
    return(ExtractGroupHashInfoGz(name, Hash, NULL, artBase, iter));
}

/*
 * ExtractGroupHashInfoGz() - ExtractGroupHashInfo() for a caller that
 * handles the ambiguity of hierarchy data names.  dreaderd and dexpireover
 * name the compressed data file of a group by adding ".gz" to the name of
 * its plain data file, and a group name may end in ".gz" itself, so a
 * hierarchy data file "d.<n>.X.gz" is either the compressed data of group
 * "X" or the plain data of group "X.gz" (and "d.<n>.X.gz.gz" the compressed
 * data of "X.gz" or the plain data of "X.gz.gz", and so on).  The name
 * cannot tell which, so for such a name Hash is "X.gz" (the plain reading,
 * as ExtractGroupHashInfo() gives it) and HashZ is "X" (the compressed
 * reading); the caller must take the file as belonging to whichever of the
 * two groups exists and never remove it while either does.  For every other
 * name HashZ is the empty string.  HashZ must hold as much as Hash, or be NULL
 * for a caller that does not want the second reading.
 */
int
ExtractGroupHashInfoGz(const char *name, char *Hash, char *HashZ, artno_t *artBase, int *iter)
{
    int b;
    int type;

    if (HashZ)
	HashZ[0] = 0;

    /*
     * This is pure evil because we have too many overview file
     * formats to support. We start with what is likely to be
     * the most common. It gets even messier because random values
     * can be assigned to variables, even when the whole string doesn't
     * match.
     */

    /*
     * HASHGRP_32MD5 or HASHGRP_64MD5
     */
    if (sscanf(name, "%[^.].%d.d.%d.%lld.gz", Hash, iter, &b, artBase) == 4)
	return(((b == 64) ? HASHGRP_64MD5 : HASHGRP_32MD5) | HASHGRPTYPE_DATA);
    *iter = 0;
    if (sscanf(name, "%[^.].%d.d.%d.%lld", Hash, iter, &b, artBase) == 4)
	return(((b == 64) ? HASHGRP_64MD5 : HASHGRP_32MD5) | HASHGRPTYPE_DATA);
    *iter = 0;
    if (sscanf(name, "%[^.].%d.o.%d.%lld", Hash, iter, &b, artBase) == 4)
	return(((b == 64) ? HASHGRP_64MD5 : HASHGRP_32MD5) | HASHGRPTYPE_OVER);
    *iter = 0;

    /*
     * HASHGRP_CRC with iteration
     */
    if (sscanf(name, "over.%lld.%17[0-9a-f.].%d", artBase, Hash, iter) == 3)
	return(HASHGRP_CRC | HASHGRPTYPE_OVER);
    *iter = 0;
    if (sscanf(name, "data.%lld.%17[0-9a-f.].%d.gz", artBase, Hash, iter) == 3)
	return(HASHGRP_CRC | HASHGRPTYPE_DATA);
    *iter = 0;
    if (sscanf(name, "data.%lld.%17[0-9a-f.].%d", artBase, Hash, iter) == 3)
	return(HASHGRP_CRC | HASHGRPTYPE_DATA);
    *iter = 0;

    /*
     * HASHGRP_CRC
     */
    if (sscanf(name, "over.%lld.%17[0-9a-f.]", artBase, Hash) == 2)
	return(HASHGRP_CRC | HASHGRPTYPE_OVER);
    *iter = 0;
    if (sscanf(name, "data.%lld.%17[0-9a-f.].gz", artBase, Hash) == 2)
	return(HASHGRP_CRC | HASHGRPTYPE_DATA);
    *iter = 0;
    if (sscanf(name, "data.%lld.%17[0-9a-f.]", artBase, Hash) == 2)
	return(HASHGRP_CRC | HASHGRPTYPE_DATA);
    *iter = 0;

    /*
     * HASHGRP_HIER
     */
    if (sscanf(name, "o.%lld.%[^/]", artBase, Hash) == 2)
	return(HASHGRP_HIER | HASHGRPTYPE_OVER);
    *iter = 0;
    /*
     * A trailing ".gz" cannot be told from a part of the group name (see
     * above): "%[^/]" takes it, and a "d.%lld.%[^/].gz" pattern would
     * return 2 as well, as the literal after a scanset is not counted
     * (D-318).  Give both readings.
     */
    if (sscanf(name, "d.%lld.%[^/]", artBase, Hash) == 2) {
	size_t len = strlen(Hash);

	type = HASHGRP_HIER | HASHGRPTYPE_DATA;
	if (HashZ && len > 3 && strcmp(Hash + len - 3, ".gz") == 0) {
	    memcpy(HashZ, Hash, len - 3);
	    HashZ[len - 3] = 0;
	}
	return(type);
    }
    *iter = 0;
    return(HASHGRP_NONE);
}

char *
GetHash(GroupHashType *Hash, char *hashStr)
{
    int i;
    char ch;
    char numSt[16];

    switch (Hash->gh_type) {
	case HASHGRP_NONE:
	    strcpy(hashStr, "NONE");
	    break;
	case HASHGRP_CRC:
	    strcpy(hashStr, "crc");
	    break;
	case HASHGRP_32MD5:
	    strcpy(hashStr, "md5-32");
	    break;
	case HASHGRP_64MD5:
	    strcpy(hashStr, "md5-64");
	    break;
	case HASHGRP_HIER:
	    strcpy(hashStr, "hierarchy");
	    break;
	default:
	    strcpy(hashStr, "UNKNOWN\n");
    }
    sprintf(numSt, ":%d", Hash->gh_sigbytes);
    strcat(hashStr, numSt);
    switch (Hash->gh_dirtype) {
	case HASHGRP_DIR_NUM:
	    ch = '/';
	    break;
	case HASHGRP_DIR_BIT:
	    ch = '\\';
	    break;
	default:
	    ch = '?';
    }
    for (i = 0; i < Hash->gh_dirlvl; i++) {
	sprintf(numSt, "%c%d", ch, Hash->gh_dirinfo[i]);
	strcat(hashStr, numSt);
    }
    return(hashStr);
}

#ifdef TEST

int
main(int ac, char **av)
{
    int i;

    for (i = 1; i < ac; ++i) {
	hash_t hv = hhash(av[i]);
	printf("%08x.%08x\t%s\n", hv.h1, hv.h2, av[i]);
    }
    return(0);
}

#endif

