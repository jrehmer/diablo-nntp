/*
 * DEXPIRECACHE.C    - dreaderd cache expire.
 *
 * (c)Copyright 2002, Francois Petillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"
#include <sys/param.h>
#if !defined(_AIX) && !USE_SYS_VFS
#include <sys/mount.h>
#endif
#ifdef _AIX
#include <sys/statfs.h>
#endif

#if USE_SYSV_STATFS
#include <sys/statfs.h>
#define f_bavail f_bfree
#endif

/*
 * The solaris statvfs is rather limited, but we suffer with reduced
 * capability (and hence take a possible performance hit).
 */
#if USE_SUN_STATVFS
#include <sys/statvfs.h>	/* god knows if this hack will work */
#define f_bsize	f_frsize	/* god knows if this hack will work */
#define fsid_t u_long
#define statfs statvfs
#endif

#if USE_SYS_VFS			/* this is mainly for linux	*/
#include <sys/vfs.h>
#endif

char *basepath=NULL;
int freeblocks=0, freeinodes=0, treeblocks=0;
time_t oldest,oldestremoved,newestremoved=0, now=0;
int unread=0,reread=0,unreadremoved=0,readremoved=0,filled=0,lazy=0,lazyremoved=0,cleaned=0;
int linksremoved=0;	/* empty files and links unlinked (-i, two days old) */
int verbose=0, trnct=0;

struct FileQueue {
	struct FileQueue *h,*l;
	char *name;
	long size;
	time_t atime;
	time_t mtime;
} FileQueue;

struct InodeQueue {
	char *name;
	time_t ctime;
} InodeQueue;

struct FileQueue *fileBeg=NULL, *fileFree=NULL;
struct InodeQueue *inodeHeap=NULL;	/* the -i entries kept, see addSLink() */
long inodeLen=0, inodeCap=0;
time_t fileEnd;

int
removeFiles(struct FileQueue *node, int val) {
	if (!node) return val;

	val = removeFiles(node->l, val);

	if (val<freeblocks) {
		if (trnct && node->size) {
			if (truncate(node->name,0) < 0)
				unlink(node->name);
		} else {
			unlink(node->name);
		}
		cleaned++;
		if(node->size) {
			if (node->atime==node->mtime) {
				unreadremoved++;
			} else {
				readremoved++;
			}
			if (node->mtime<oldestremoved) oldestremoved=node->mtime;
			if (node->mtime>newestremoved) newestremoved=node->mtime;
		} else {
			lazyremoved++;
		}
	 	return removeFiles(node->h, val+node->size);
	} else {
		return val;
	}
}

static int inodeCmp(const void *a, const void *b);

void
removeLinks(void) {
	long i;

	/* oldest first, as inodeCmp() orders them */
	if (inodeLen > 1)
		qsort(inodeHeap, inodeLen, sizeof(struct InodeQueue), inodeCmp);
	for (i = 0; i < inodeLen; i++) {
		if (verbose) printf("Removing %s\n", inodeHeap[i].name);
		/* one already gone (two days old, addSLink()) is not counted again */
		if (unlink(inodeHeap[i].name) == 0)
			linksremoved++;
	}
}

void
dropTree (struct FileQueue **pnode) {
	struct FileQueue *node = *pnode;

	if (!node) return;

	dropTree(&node->l);
	dropTree(&node->h);

	free(node->name);
	node->h = fileFree;
	fileFree = node;
	
	*pnode = NULL;
}

int
cleanTree (struct FileQueue **pnode, int *val) {
	struct FileQueue *node = *pnode;
	int hd=0, ld=0;

	if (!node) return 0;
	
	ld = cleanTree(&node->l, val);

	if (*val>freeblocks) {
		/* I am out */
		if (node->atime < fileEnd) {
			fileEnd = node->atime;
		}
		dropTree(&node->h);
		free(node->name);
		node->h = fileFree;
		fileFree = node;
		*pnode = node->l;
		return ld;
	}

	/* I am in */
	*val += node->size;
	if (*val>freeblocks) {
		/* high tree is out */
		dropTree(&node->h);
		hd = 0;
	} else {
		hd = cleanTree(&node->h, val);
	}

	/* rebalance the tree */
	if (ld>hd+1) {
		*pnode = node->l;
		node->l = (*pnode)->h;
		(*pnode)->h = node;
		ld--; hd++;
	} else if (hd>ld+1) {
		*pnode = node->h;
		node->h = (*pnode)->l;
		(*pnode)->l = node;
		hd--; ld++;
	}

	if (hd>ld)
		return hd+1;
	return ld+1;
}

int
statTree(struct FileQueue *node, int *size, int *el) {
	int hd, ld;

	if (!node) return 0;

	*el += 1;
	*size += node->size;
	ld = statTree(node->l, size, el)+1;
	hd = statTree(node->h, size, el)+1;

	if (ld>hd) return ld;
	return hd;
}

void
addFile(char *name, long size, time_t atime, time_t mtime) {
	struct FileQueue *ftmp;
	int s;

	/* to manage lazy cache */
	if (atime < mtime) atime = mtime;

	if (size) {
		filled++;
		if (atime==mtime) {
			unread++;
		} else {
			reread++;
		}
		if (mtime<oldest) oldest=mtime;
	} else {
		lazy++;
	}

	if (!freeblocks || (atime > fileEnd))
		return;

	treeblocks += size;
	s = strlen(name)+1;

	if (!fileFree) {
		struct FileQueue *fq;
		char *f;
		int i;
		f = malloc(1024*sizeof(struct FileQueue));
		fileFree = (struct FileQueue*)f;
		for (i=0; i<1024 ; i++) {
			fq = (struct FileQueue*) f;
			f += sizeof(struct FileQueue);
			fq->h = (struct FileQueue*) f;
		}
		fq->h = NULL;
	}
	ftmp = fileFree;
	fileFree = ftmp->h;
	ftmp->name = (char*) malloc(s);
	memcpy(ftmp->name, name, s);
	ftmp->size = size;
	ftmp->atime = atime;
	ftmp->mtime = mtime;
	ftmp->h = NULL;
	ftmp->l = NULL;

	if (!fileBeg) {
		fileBeg = ftmp;
		return;
	} else {
		struct FileQueue *i=fileBeg;
		while(i) {
			if (atime < i->atime) {
				if (i->l) {
					i = i->l;
				} else {
					i->l = ftmp;
					i = NULL;
				}
			} else {
				if (i->h) {
					i = i->h;
				} else {
					i->h = ftmp;
					i = NULL;
				}
			}
		}
	}
	if (treeblocks>2*freeblocks) {
		treeblocks = 0;
		if(verbose>1) {
			int size=0, el=0, depth;
			depth = statTree(fileBeg, &size, &el);
			printf("\nResize  : %i elements %i octets (%i/%i#%i)\n", el, size, filled, lazy, depth);
		}
		cleanTree(&fileBeg, &treeblocks);
		if(verbose>1) {
			int size=0, el=0, depth;
			depth = statTree(fileBeg, &size, &el);
			printf("Resized : %i elements %i octets (%is#%i)\n", el, size, (int)fileEnd, depth);
		}
	}
}

/*
 * The order of the -i entries: by ctime, oldest first, and by name where the
 * ctimes are the same, so that ties do not depend on the directory order.
 */
static int
inodeOlder(time_t ctime, const char *name, const struct InodeQueue *e)
{
	return ctime < e->ctime || (ctime == e->ctime && strcmp(name, e->name) < 0);
}

/* for qsort(): oldest first (no two entries have the same name) */
static int
inodeCmp(const void *a, const void *b)
{
	const struct InodeQueue *x = a, *y = b;

	if (x->ctime != y->ctime)
		return x->ctime < y->ctime ? -1 : 1;
	return strcmp(x->name, y->name);
}

/* move e up from the free place k until its parent is newer */
static void
inodeSiftUp(struct InodeQueue e, long k)
{
	while (k > 0) {
		long p = (k-1)/2;
		if (inodeCmp(&inodeHeap[p], &e) >= 0)
			break;
		inodeHeap[k] = inodeHeap[p];
		k = p;
	}
	inodeHeap[k] = e;
}

/* move e down from the free place k until both children are older */
static void
inodeSiftDown(struct InodeQueue e, long k)
{
	long c;

	while ((c = 2*k+1) < inodeLen) {
		if (c+1 < inodeLen && inodeCmp(&inodeHeap[c+1], &inodeHeap[c]) > 0)
			c++;
		if (inodeCmp(&inodeHeap[c], &e) <= 0)
			break;
		inodeHeap[k] = inodeHeap[c];
		k = c;
	}
	inodeHeap[k] = e;
}

/*
 * Keep the freeinodes oldest empty files and symbolic links.  They are a
 * binary heap in inodeHeap[0..inodeLen-1] with the newest kept entry at the
 * root: a new entry is added while there is room, or replaces the root when
 * it is older, so each entry costs O(log freeinodes).  removeLinks() sorts
 * the heap, oldest first.
 */
void
addSLink(char *name, time_t ctime) {
	struct InodeQueue e;
	int s;

	if (ctime < time(NULL)-2*24*3600) {
		if (unlink(name) == 0)
			linksremoved++;
	}

	if (!freeinodes)
		return;
	/* the heap is full and this one is not older than the newest in it */
	if (inodeLen >= freeinodes && !inodeOlder(ctime, name, &inodeHeap[0]))
		return;

	s = strlen(name)+1;
	e.name = (char*) malloc(s);
	if (!e.name) {
		fprintf(stderr, "dexpirecache: out of memory\n");
		exit(1);
	}
	memcpy(e.name, name, s);
	e.ctime = ctime;

	if (inodeLen >= freeinodes) {
		/* replace the newest kept entry */
		free(inodeHeap[0].name);
		inodeSiftDown(e, 0);
		return;
	}
	if (inodeLen == inodeCap) {
		struct InodeQueue *h;
		long cap = inodeCap ? 2*inodeCap : 1024;

		if (cap > freeinodes)
			cap = freeinodes;
		h = (struct InodeQueue*) realloc(inodeHeap, cap*sizeof(struct InodeQueue));
		if (!h) {
			fprintf(stderr, "dexpirecache: out of memory\n");
			exit(1);
		}
		inodeHeap = h;
		inodeCap = cap;
	}
	inodeSiftUp(e, inodeLen++);
}

void
parseDir(char *path) {
	DIR *dir = NULL;
	struct dirent *entry;

	if ((dir = opendir(path)) == NULL) {
		fprintf(stderr, "Can not parse %s\n", path);
		return;
	}
	while((entry=readdir(dir)) != NULL) {
		char name[PATH_MAX];
		struct stat st;

		if (!strncmp(entry->d_name, ".", 2)
				|| !strncmp(entry->d_name, "..", 3)) {
			continue;
		}

		snprintf(name, sizeof(name),"%s/%s", path, entry->d_name);
		if (lstat(name, &st)) {
			fprintf(stderr, "Can not stat %s\n", name);
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			parseDir(name);
		} else if (S_ISLNK(st.st_mode)) {
			addSLink(name, st.st_ctime);
		} else if (S_ISREG(st.st_mode)) {
			if (st.st_blocks) {
				addFile(name, st.st_blocks/2, st.st_atime, st.st_mtime);
			} else {
				if (strncmp(name+(strlen(name)-4),".tmp",4)) {
					addSLink(name, st.st_ctime);
				} else {
					addFile(name, 0, st.st_ctime, st.st_ctime);
				}
			}
		}
	}
	closedir(dir);
}

int
main(int ac, char **av)
{
	int i;

	now=oldest=oldestremoved=fileEnd = time(NULL);
	LoadDiabloConfig(ac, av);
	for (i = 1; i < ac; ++i) {
        char *ptr = av[i];

        if (*ptr != '-') {
            fprintf(stderr, "Unexpected option: %s\n", ptr);
            exit(1);
        }
        ptr += 2;

		switch(ptr[-1]) {
			case 'C':		/* parsed by LoadDiabloConfig */
				if (*ptr == 0)
					++i;
				break;
			case 'd':
				basepath = (*ptr) ? ptr : av[++i];
				break;
			case 'f':
				freeblocks = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
				break;
			case 'i':
				freeinodes = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
				break;
			case 't' :
				trnct = 1;
				break;
			case 'v' :
				verbose++;
				break;
			default:
				fprintf(stderr, "unknown option: %s\n", ptr - 2);
				exit(1);
		}
	}
	if (basepath == NULL)
		basepath = strdup(PatExpand(CacheHomePat));

	if (chdir(basepath) == -1) {
		fprintf(stderr, "Unable to change to `%s' (%s).\n",basepath,strerror(errno));
		exit(1);
	}
	{ /* stat fs */
		struct statfs fs;
		long long fb;
		if (statfs(".", &fs)) {
			fprintf(stderr, "Can not stat fs (%s).\n",strerror(errno));
			exit(1);
		}
		/* in 64 bits: an int overflows above 2 TB free */
		fb = (long long)fs.f_bavail * fs.f_bsize / 1024;
		if (fb<freeblocks) {
			freeblocks -= fb;
		} else {
			freeblocks = 0;
		}
		/*
		 * A file system that has no fixed number of inodes (btrfs)
		 * reports f_files and f_ffree as 0: its free inodes are
		 * unknown, not none, so -i is not applied there.  -f still is.
		 */
		if (freeinodes && fs.f_files == 0) {
			fprintf(stderr, "dexpirecache: %s reports no inode count: not expiring for -i there\n", basepath);
			freeinodes = 0;
		} else if ((long long)fs.f_ffree<freeinodes) {
			freeinodes -= fs.f_ffree;
		} else {
			freeinodes = 0;
		}
	}
	if (freeblocks || freeinodes) {
		if (verbose) printf("Base path %s\nFree Space needed : %i\nFree inodes needed : %i\n", basepath, freeblocks, freeinodes);
		parseDir(".");
		removeFiles(fileBeg,0);
		removeLinks();
		printf("\nStats :\n* %7i cache files found\n\t%7i reread\n\t%7i unread\n* %7i pre-cache files found\n* %7i files have been cleaned\n\t%7i reread\n\t%7i unread\n\t%7i pre-cache\n", filled, reread, unread, lazy, cleaned, readremoved, unreadremoved, lazyremoved);
		printf("* %7i empty files and links removed\n", linksremoved);
		/* the ages are kept only for removed cache files with content */
		if (readremoved + unreadremoved) {
			printf("Oldest removed file was created %s ago\n", dtlenstr(now-oldestremoved));
			printf("Newest removed file was created %s ago\n", dtlenstr(now-newestremoved));
		}
		printf("Oldest file found was created %s ago\n", dtlenstr(now-oldest));
		printf("DexpireCache has run for %is\n",(int)(time(NULL)-now));
	} else {
		if (verbose) printf("No need to expire cache\n");
	}
	return 0;
}

