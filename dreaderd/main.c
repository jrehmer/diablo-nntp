
/*
 * DREADERD/MAIN.C - diablo newsreader
 *
 *	This is the startup for the dreaderd daemon.
 *
 *	The diablo newsreader requires one or remote diablo news boxes to 
 *	obtain articles from, but keeps track of article assignments to groups
 *	and overview information locally.
 *
 *	Operationally, dreaderd runs as a standalone multi-forking, 
 *	multi-threaded daemon.  It forks X resolvers and Y readers.  Each
 *	reader can support N connections (NxY total connections) and maintains
 *	its own links to one or more news servers (diablo, INN, whatever). 
 *	Overview information is utilized and articles are accessed by 
 *	message-id.
 *
 *	The diablo newsreader is also able to accept header-only feeds from
 *	one or more diablo servers via 'mode headfeed'.  Processing of these
 *	feeds is not multi-threaded, only processing of newsreader functions is
 *	multithreaded.  The diablo newsreader does not operate with a dhistory
 *	file.  If taking a feed from several sources you must also run the
 *	diablo server on the same box and use that to feed the dreaderd system,
 *	though the diablo server need not store any articles itself.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE	/* sched_getaffinity(), CPU_COUNT() */
#endif

#include "defs.h"
#include <sys/resource.h>

#ifdef __linux__
#include <sched.h>
#endif
#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/cpuset.h>
#endif

#if NEED_TERMIOS
#include <sys/termios.h>
#endif

Prototype void RTClearStatus(int index, int count);
Prototype void RTUpdateStatus(int slot, const char *ctl, ...);
Prototype pid_t ForkPipe(int *pfd, void (*task)(int fd, const char *id), const char *description, int sockType);
Prototype void ValidateTcpBufferSize(int *psize);

Prototype int NumReaderForks;
Prototype int ThisReaderFork;
Prototype int FeedOnlyServer;
Prototype int CoreDebugOpt;
Prototype int FastCopyOpt;
Prototype pid_t *ReaderPids;
Prototype char *RTStatus;
Prototype char *MyGroupHome;

void HandleDnsThread(ForkDesc *desc);
void deleteDnsResHash(DnsRes *dres, ForkDesc *desc);
int getReaderForkSlot(void);
void delReaderForkSlot(pid_t pid);
static int AvailableCpus(void);
#ifdef __linux__
static int CgroupCpuLimit(void);
#endif

#define READERFORKS_AUTO_CAP	64	/* most forks 'readerforks auto' starts */

/*
 * Listen backlog.  SOMAXCONN is the C library's constant, not the
 * kernel's limit: musl has 128 while Linux allows 4096 by default.  A
 * burst of new clients past the backlog loses SYNs, and each of those
 * clients waits for a TCP retransmit (1 s, then 3 s) before dreaderd can
 * even refuse it.  Ask for more and let the kernel cut it to its own
 * limit (net.core.somaxconn, kern.ipc.soacceptqueue); 65535 because old
 * Linux kernels keep the backlog in 16 bits.
 */
#define READER_LISTEN_BACKLOG	65535

#ifdef INET6
void SendDns(int fd, struct sockaddr_storage *rsin, DnsRes *dres);
#else
void SendDns(int fd, struct sockaddr_in *rsin, DnsRes *dres);
#endif
void DnsQueueRun(void);
void FinishRetain(int what);
void DoListen(ForkDesc *desc, time_t t);
void DoCommand(int fd);
void getVStats(FILE*fo);
void getStats(FILE*fo, int raw);
#ifdef	READER_BAN_LISTS
void SetBanList(char *ban);
int Banned(char *ip, time_t t);
void DumpBannedLists(FILE *fo);
void DumpBannedConfigs(FILE *fo);
#endif

char *RTStatus = NULL;
char *MyGroupHome;
char *PidFile;
char AdminVersion;
const char *SysLogDesc;
int CoreDebugOpt = 0;
int FastCopyOpt = 1;
int TxBufSize = 0;
int RxBufSize = 0;
int MaxSDnsForks = 0;		/* future				*/
int MaxConnects;
int ConnectCount;
int FailCount;
int ListenersEnabled = 1;
time_t TimeStart;
#ifdef	DREADER_CLIENT_TIMING
float ticksPerSecond;
#endif

int NumReaderFeedForks;
int NumReaderForks;
int NumDnsForks;
int NumSDnsForks;
int NumPending;			/* pending connections	*/
int NumActive;			/* active connections	*/

/*
 * Socket type of the DNS/auth IPC socketpair.  SOCK_SEQPACKET keeps the
 * fixed-size DnsReq and DnsRes messages apart; SOCK_STREAM is the fallback
 * for systems whose AF_UNIX does not implement SOCK_SEQPACKET (illumos and
 * Solaris document it as not implemented for any protocol family).
 */
static int DnsSockType = SOCK_STREAM;

/*
 * Requests that found every DNS worker busy wait here, in order, until one
 * is free.  AUTHINFO logins of greeted clients have their own queue, served
 * first.  New connections (and a proxied client's first lookup) are queued
 * only while the expected wait is within half of readerdnswait, up to
 * readerdnsqueue of them and below a fd cap; otherwise they get 400 at
 * once.  Nothing waits longer than readerdnswait.
 */
typedef struct DnsQ {
    struct DnsQ *dq_Next;
    int dq_Fd;
#ifdef INET6
    struct sockaddr_storage dq_RSin;
#else
    struct sockaddr_in dq_RSin;
#endif
    int dq_HasRes;
    DnsRes dq_Res;
    double dq_When;		/* MonoMs() when queued */
    int dq_Known;		/* lookup result known: no lookups */
} DnsQ;
typedef struct DnsQList {
    DnsQ *ql_Head;
    DnsQ **ql_Tail;
    int ql_Len;
    int ql_Known;		/* entries with dq_Known */
} DnsQList;
static DnsQList DnsQAuth = { NULL, &DnsQAuth.ql_Head, 0, 0 };
static DnsQList DnsQNew = { NULL, &DnsQNew.ql_Head, 0, 0 };
static int DnsQFdCap;		/* queue new connections only below this fd */
static int DnsQAuthFdCap;	/* queue AUTHINFO logins only below this fd */
static double ListenPauseUntil;	/* MonoMs(): listeners off until then */
#define DQR_FULL	1	/* readerdnsqueue reached		*/
#define DQR_WAIT	2	/* estimated wait too long		*/
#define DQR_FDS		3	/* no descriptor headroom		*/
#define DQR_NOWORKER	4	/* no DNS worker			*/
#define DQR_EXPIRED	5	/* waited readerdnswait			*/
#define DQR_MAXFDS	6	/* accepted fd beyond MAXFDS		*/
#define DQR_ACCEPT	7	/* accept() out of descriptors/memory	*/
#define DQR_NOFD	8	/* hand-back arrived without its fd	*/
#define DQR_INFLIGHT	9	/* fd pass to a reader: ETOOMANYREFS	*/
#define DQR_N		10	/* reasons are 1..9; 0 = admitted */
static double MonoMs(void);
static void DnsRefuseLog(int r);
static void DnsQInitLimits(void);

double TotalClientGroups = 0.0;
double TotalClientArticles = 0.0;
double TotalClientArtBytes = 0.0;
double TotalClientPosts = 0.0;
double TotalClientPostFail = 0.0;
double TotalClientPostBytes = 0.0;
double TotalSpoolArticles = 0.0;
double TotalSpoolBytes = 0.0;
double TotalFeedArticles = 0.0;
double TotalFeedBytes = 0.0;
double TotalFeedArtFail = 0.0;

int ufd;			/* drcmd listen fd */

int ThisReaderFork;
int FeedOnlyServer = -1;
pid_t *ReaderPids;

MemPool *DnsResPool;
DnsRes  *DnsResHash[DRHSIZE];

BindList *BindBase = NULL;

typedef struct PendingClose {
    int		fd;
    time_t	when;
    int		quickclose;
    char	msgbuf[1024];
    struct PendingClose *next;
} PendingClose;

PendingClose	*NeedClosing = NULL;
MemPool 	*PendingMemPool;
int		DelayedClose = 0;

#ifdef	READER_BAN_LISTS

typedef struct BanInfo {
    IPList *BanList;
    int BanHashSize;
    int BanLinkSize;
    int BanCount;
    time_t BanTime;
} BanInfo;

BanInfo	HostBan;
BanInfo	UserBan;
BanInfo	GroupBan;
BanInfo	GlobalBan;
char *LastBan = NULL;

#endif

#define RET_CLOSE       1
#define RET_PAUSE       2
#define RET_LOCK        3

typedef struct Retain {   
    struct Retain *re_Next;
    FILE        *re_Fi;
    FILE        *re_Fo;
    int         re_What;
} Retain;


Retain	*ReBase;
int	PausedCount = 0;
int	Exiting = 0;
MemPool	*ReMemPool;

void
Usage(void)
{
    fprintf(stderr, "Usage: dreaderd -p pathhost [-A admin] [-B bindip[:port] [-b fd][-C file]\n");
    fprintf(stderr, "                [-c delaysecs] [-d [debugval] [-D dnsprocs ] [-F feedprocs]\n");
    fprintf(stderr, "                [-f val] [-h hostname ] [-M readerprocs ] [-N readthreads]\n");
    fprintf(stderr, "                [-P [bindip:]port ] [-R rxbufsize ]\n");
    fprintf(stderr, "                [-r rtstatusfile ] [ -S[T|R]# ] [-s statusline ]\n");
    fprintf(stderr, "                [-T txbufsize] [ -V ] [-X xrefhost ] [ -x xrefslavehost ] [ -Z ]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "where:\n");
    fprintf(stderr, "   -p pathhost      Set the default Path: entry for this host:\n");
    fprintf(stderr, "   -A admin         Set the default newsadmin email for this host:\n");
    fprintf(stderr, "   -B bindip[:port] Bind to a specific interface\n");
    fprintf(stderr, "   -b fd            Provide an open socket to listen on\n");
    fprintf(stderr, "	-C file		specify diablo.config to use\n");
    fprintf(stderr, "   -c delaysecs     Delay closing of failed connections\n");
    fprintf(stderr, "   -d [debugval]    Enable debugging\n");
    fprintf(stderr, "   -D dnsprocs      Specify the number of DNS processes to start\n");
    fprintf(stderr, "   -F feedprocs     Specify the number of dedicated feed processes to start\n");
    fprintf(stderr, "   -f [0|1]         Disable/enable fast copy of data from spool to client\n");
    fprintf(stderr, "   -h hostname      Set the default banner hostname for this server\n");
    fprintf(stderr, "   -M readprocs     Specify the number of reader processes to start\n");
    fprintf(stderr, "   -N readthreads   Specify the number of threads per reader process\n");
    fprintf(stderr, "   -P [bindip]:port Bind to a specific port\n");
    fprintf(stderr, "   -R rxbufsize     Set the TCP rcv buffer size\n");
    fprintf(stderr, "   -r rtstatusfile  Set the filename to store realtime status data\n");
    fprintf(stderr, "   -S[T:R]#         Set TCP rx/tx buffer sizes\n");
    fprintf(stderr, "   -s statusline    Use this area of process space to write status data\n");
    fprintf(stderr, "   -T txbufsize     Set the TCP txmit buffer size\n");
    fprintf(stderr, "   -V               Print version\n");
    fprintf(stderr, "   -X               Set the default Xref: hostname shown to clients\n");
    fprintf(stderr, "   -x               Set the Xref: hostname that must match incoming\n");
    fprintf(stderr, "                       feed Xref: lines\n");
    fprintf(stderr, "   -Z               Enable core file debugging\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    int passedfd = -1;

    LoadDiabloConfig(ac, av);

#ifdef DEBUG_ZALLOC
    dbg_install_sighandler();
#endif

    if (strcmp(DRIncomingLogPat, "NONE") == 0)
	DRIncomingLogPat = NULL;
    else
	IncomingLogPat = DRIncomingLogPat;

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	int v;

	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected option: %s\n", ptr);
	    exit(1);
	}
	ptr += 2;

	v = (*ptr) ? strtol(ptr, NULL, 0) : 1;

	switch(ptr[-1]) {
	case 'A':
	    strdupfree(&DOpts.NewsAdmin, (*ptr) ? ptr : av[++i], "");
	    break;
	case 'B':
	    {
		BindList *bl = calloc(1, sizeof(BindList));

		bl->bl_Port = NULL;
		bl->bl_Host = NULL;

		if (*ptr == 0)
		    ptr = av[++i];
		if (*ptr == '[') {
		    char *p = strchr(ptr, ']');
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		    if (p != NULL && (p = strrchr(p, ':')) != NULL)
			bl->bl_Port = strdup(p + 1);
		} else if (strrchr(ptr, ':')) {
		    char *p = strrchr(ptr, ':');

		    bl->bl_Port = strdup(p + 1);
		    *p = 0;	/* host:port - the host ends at the colon */
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		} else {
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		}
		bl->bl_Next = BindBase;
		BindBase = bl;
	    }
	    break;
	case 'b':
	    passedfd = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'c':
	    DelayedClose = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'd':
	    DebugOpt = v;
	    break;
	case 'D':
	    DOpts.ReaderDns = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'F':
	    DOpts.ReaderFeedForks = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'f':
	    FastCopyOpt = v;
	    break;
	case 'h':
	    strdupfree(&DOpts.ReaderHostName, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'M':
	    if (*ptr == 0)
		ptr = av[++i];
	    if (ptr == NULL || SetReaderForks(ptr, &DOpts.ReaderForks) != 0) {
		fprintf(stderr, "-M: readerforks must be auto or 0..%d\n",
							READERFORKS_MAX);
		exit(1);
	    }
	    break;
	case 'N':
	    DOpts.ReaderThreads = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'P':
	    {
		BindList *bl = calloc(1, sizeof(BindList));

		bl->bl_Port = NULL;
		bl->bl_Host = NULL;

		if (*ptr == 0)
		    ptr = av[++i];
		if (*ptr == '[') {
		    char *p = strchr(ptr, ']');
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		    if (p != NULL && (p = strrchr(p, ':')) != NULL)
			bl->bl_Port = strdup(p + 1);
		} else if (strrchr(ptr, ':')) {
		    char *p = strrchr(ptr, ':');

		    bl->bl_Port = strdup(p + 1);
		    *p = 0;	/* host:port - the host ends at the colon */
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		} else {
		    bl->bl_Port = strdup(ptr);
		}
		bl->bl_Next = BindBase;
		BindBase = bl;
	    }
	    break;
	case 'p':
	    strdupfree(&DOpts.ReaderPathHost, (*ptr) ? ptr : av[++i], "");
	    break;
	case 'R':
	    RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'r':
	    strdupfree(&RTStatus, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'S':
	    switch(*ptr) {
	    case 'T':
		++ptr;
		TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    case 0:
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		/* -S# sets both buffers */
		TxBufSize = RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    case 'R':
		++ptr;
		RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    default:
		fatal("bad option: -S (must be R#, T#, or #)");
		break;
	    }
	    break;
	case 's':
	    ptr = (*ptr) ? ptr : av[++i];
	    SetStatusLine(ptr, strlen(ptr));
	    break;
	case 'T':
	    TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'X':
	    strdupfree(&DOpts.ReaderXRefHost, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'x':
	    strdupfree(&DOpts.ReaderXRefSlaveHost, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'Z':
	    CoreDebugOpt = 1;
	    break;
	default:
	    fprintf(stderr, "unknown option: %s\n", ptr - 2);
	    Usage();
	}
    }
    ValidateTcpBufferSize(&RxBufSize);
    ValidateTcpBufferSize(&TxBufSize);

    if (BindBase == NULL) {
	BindList *bl = calloc(1, sizeof(BindList));

	bl->bl_Next = BindBase;
	BindBase = bl;
    }

    /*
     * -B host and -P port take the missing half from diablo.config
     * (readerbindhost, readerbindport)
     */
    {
	BindList *bl;

	for (bl = BindBase; bl; bl = bl->bl_Next) {
	    if (bl->bl_Host == NULL)
		bl->bl_Host = DOpts.ReaderBindHost;
	    if (bl->bl_Port == NULL)
		bl->bl_Port = DOpts.ReaderPort;
	}
    }

    MyGroupHome = strdup(PatExpand(GroupHomePat));

    /*
     * To stdout, not an error, no arguments == user requesting 
     * options list.  We can do this because the -p option is mandatory.
     */

    if (DOpts.ReaderPathHost == NULL)
	Usage();

    /*
     * Cancel-Lock checks need the target's headers from the overview data,
     * which 'readerxover trackonly' doesn't store.  Mode 'none' refuses every
     * cancel before it needs a header, and 'all' checks nothing.
     */
    if (DOpts.ReaderXOverMode == 2 && DOpts.ReaderCanLock != CANLOCK_ALL &&
				DOpts.ReaderCanLock != CANLOCK_NONE) {
	fprintf(stderr, "dreaderd: readercanlock %s needs article headers, "
	    "but readerxover is trackonly; set readercanlock all or none\n",
	    CanLockModeName(DOpts.ReaderCanLock));
	exit(1);
    }

    /*
     * Can't do OpenLog() yet, but we do want to be able to log
     * errors, so initialize standard syslog.
     */
    openlog("dreaderd", (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID,
            LOG_NEWS);

    if (DOpts.ReaderForks == READERFORKS_AUTO) {
	int cpus = AvailableCpus();

	DOpts.ReaderForks = cpus;
	if (DOpts.ReaderForks > READERFORKS_AUTO_CAP)
	    DOpts.ReaderForks = READERFORKS_AUTO_CAP;
	logit(LOG_NOTICE, "readerforks auto: %d (%d CPUs available)",
						DOpts.ReaderForks, cpus);
    }
    MaxConnects = DOpts.ReaderThreads * DOpts.ReaderForks;

#ifndef	CANLOCK_ENABLED
    /*
     * Without libcanlock no Cancel-Key can be verified: say what the mode
     * then does, so that a build without the library is not a surprise.
     */
    if (DOpts.ReaderCanLock == CANLOCK_REQUIRE_AUTH ||
				DOpts.ReaderCanLock == CANLOCK_AUTH) {
	logit(LOG_WARNING, "readercanlock %s: this build has no libcanlock "
	    "(CANLOCK_ENABLED), so no Cancel-Key can be verified and %s",
	    CanLockModeName(DOpts.ReaderCanLock),
	    (DOpts.ReaderCanLock == CANLOCK_AUTH) ?
		"only articles without a Cancel-Lock can be cancelled" :
		"no cancel or supersede is executed");
    }
#endif

    /*
     * bind to our socket prior to changing uid/gid
     */

    if (passedfd < 0)
    {
	BindList *bl;

	for (bl = BindBase; bl; bl = bl->bl_Next) {
	    int lfd;

#ifdef INET6
	    int error;
	    struct addrinfo hints;
	    struct addrinfo *res;
	    struct addrinfo *ai;

	    /*
	     * Open a wildcard listening socket
	     */
	    bzero(&hints, sizeof(hints));
	    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
	    hints.ai_family = PF_UNSPEC;
	    hints.ai_socktype = SOCK_STREAM;
	    error = getaddrinfo(bl->bl_Host, bl->bl_Port, &hints, &res);
	    if (error != 0) {
		fprintf(stderr, "getaddrinfo: %s:%s: %s\n",
					bl->bl_Host ? bl->bl_Host : "ALL",
					bl->bl_Port, gai_strerror(error));
		exit(1);
	    }

	    for (ai = res; ai; ai = ai->ai_next) {

	    lfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	    if (ai->ai_family == AF_INET6 && (bl->bl_Host || res->ai_next)) {
		/*
		 * set INET6 socket to V6ONLY if we bind to a specific
		 * host or getaddrinfo tells us to use seperate wildcard
		 * listening sockets for v4 and v6.
		 */
		int on = 1;
		setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, 
			   (void *) &on, sizeof (on));
	    }
#else
	    struct sockaddr_in lsin;

	    lfd = socket(AF_INET, SOCK_STREAM, 0);
	    bzero(&lsin, sizeof(lsin));
	    if (bl->bl_Port == NULL)
		bl->bl_Port = DOpts.ReaderPort;
	    lsin.sin_port = strtol(bl->bl_Port, NULL, 0);
	    if (lsin.sin_port == 0) {
		struct servent *se;
		se = getservbyname(bl->bl_Port, NULL);
		if (se == NULL) {
		    fprintf(stderr, "Unknown service name: %s\n", bl->bl_Port);
		    exit(1);
		}
		lsin.sin_port = se->s_port;
	    } else {
		lsin.sin_port = htons(lsin.sin_port);
	    }

	    lsin.sin_family = AF_INET;

	    lsin.sin_addr.s_addr = INADDR_ANY;
	    if (bl->bl_Host != NULL) {
		struct hostent *he;

		if ((he = gethostbyname(bl->bl_Host)) != NULL) {
		    lsin.sin_addr = *(struct in_addr *)he->h_addr;
		} else {
		    lsin.sin_addr.s_addr = inet_addr(bl->bl_Host);
		    if (lsin.sin_addr.s_addr == INADDR_NONE) {
			perror("gethostbyname(BindHost)");
			exit(1);
		    }
		}
	    }
#endif
	    {
		int on = 1;

		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
		setsockopt(lfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	    }

	    if (TxBufSize > 0)
	        if (setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_SNDBUF to %d on listen fd %d: %m", 
							TxBufSize, lfd);
	        }
	    if (RxBufSize > 0)
	        if (setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_RCVBUF to %d on listen fd %d: %m", 
							RxBufSize, lfd);
	        }

#ifdef INET6
	    if (bind(lfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		perror("bind");
		exit(1);
	    }
#else
	    if (bind(lfd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		perror("bind");
		exit(1);
	    }
#endif
	    listen(lfd, READER_LISTEN_BACKLOG);
	    if (bl->bl_Host != NULL && strchr(bl->bl_Host, ':') != NULL)
		syslog(LOG_INFO, "Listening on [%s]:%s",
					bl->bl_Host ? bl->bl_Host : "ALL",
					bl->bl_Port);
	    else
		syslog(LOG_INFO, "Listening on %s:%s",
					bl->bl_Host ? bl->bl_Host : "ALL",
					bl->bl_Port);
	    AddThread("acceptor", lfd, -1, THREAD_LISTEN, -1, 0);
	}
#ifdef INET6
        freeaddrinfo(res);
        }
#endif
    } else {
	    {
		int on = 1;

		setsockopt(passedfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
		setsockopt(passedfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	    }
	    if (TxBufSize > 0)
	        if (setsockopt(passedfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_SNDBUF to %d on pass fd %d: %m", 
							TxBufSize, passedfd);
	        }
	    if (RxBufSize > 0)
	        if (setsockopt(passedfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_RCVBUF to %d on pass fd %d: %m", 
							RxBufSize, passedfd);
	        }

	    listen(passedfd, READER_LISTEN_BACKLOG);
	    AddThread("acceptor", passedfd, -1, THREAD_LISTEN, -1, 0);
    }

    /*
     * Fork if not in debug mode and detach from terminal
     */

    if (DebugOpt == 0) {
	pid_t pid;
	int fd;

	fflush(stdout);
	fflush(stderr);

	if ((pid = fork()) != 0) {
	    if (pid < 0) {
		perror("fork");
		exit(1);
	    }
	    exit(0);
	}
	/*
	 * child continues, close stdin, stdout, stderr, detach tty,
	 * detach process group.
	 */
	{
	    int fd = open("/dev/null", O_RDWR);
	    if (fd != 0)
		dup2(fd, 0);
	    if (fd != 1)
		dup2(fd, 1);
	    if (fd != 2)
		dup2(fd, 2);
	    if (fd > 2)
		close(fd);
	}
#if USE_TIOCNOTTY
	if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
	    ioctl(fd, TIOCNOTTY, 0);
	    close(fd);
	}
#endif
#if USE_SYSV_SETPGRP
        setpgrp();
#else
	setpgrp(0, 0);
#endif
    }

    /*
     * Ignore SIGPIPE, in case a write() fails we do not
     * want to segfault.
     */
    rsignal(SIGPIPE, SIG_IGN);

    /*
     * change my uid/gid
     */

    {
        struct passwd *pw = getpwnam("news");
        struct group *gr = getgrnam("news");
        gid_t gid;

        if (pw == NULL) {
            perror("getpwnam('news')");
            exit(1);
        }
        if (gr == NULL) {
            perror("getgrnam('news')");
            exit(1);
        }
        gid = gr->gr_gid;
        /*
         * Only root can change identity.  A non-root start keeps running
         * as the invoking user, as it always has; a failure while root
         * must not leave the daemon running with root privileges.
         */
        if (geteuid() == 0) {
            if (setgroups(1, &gid) < 0) {
                syslog(LOG_CRIT, "Unable to set supplementary groups to news: %s",
                       strerror(errno));
                exit(1);
            }
            if (setgid(gr->gr_gid) < 0) {
                syslog(LOG_CRIT, "Unable to set group ID to news: %s",
                       strerror(errno));
                exit(1);
            }
            if (setuid(pw->pw_uid) < 0) {
                syslog(LOG_CRIT, "Unable to set user ID to news: %s",
                       strerror(errno));
                exit(1);
            }
        }
    }

#ifdef	USE_PRCTL
    {
	struct rlimit rlp = { RLIM_INFINITY, RLIM_INFINITY };

	if (setrlimit(RLIMIT_CORE, &rlp) < 0) {
	    perror("setrlimit(RLIMIT_CORE)");
	}
	getrlimit(RLIMIT_CORE, &rlp);
	if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
	    perror("prctl(PR_SET_DUMPABLE)");
	}
    }
#endif

    if (DOpts.ReaderHostName == NULL)
	SetMyHostName(&DOpts.ReaderHostName);
    if (*DOpts.ReaderHostName == 0) {
	free(DOpts.ReaderHostName);
	DOpts.ReaderHostName = NULL;
    }
    if (DOpts.NewsAdmin == NULL)
	SetNewsAdmin(&DOpts.NewsAdmin, DOpts.ReaderHostName);
    if (*DOpts.NewsAdmin == 0) {
	free(DOpts.NewsAdmin);
	DOpts.NewsAdmin = NULL;
    }
    if (DOpts.ReaderXRefHost == NULL)
	strdupfree(&DOpts.ReaderXRefHost, DOpts.ReaderXRefSlaveHost, NULL);
    if (DOpts.ReaderXRefHost == NULL)
	strdupfree(&DOpts.ReaderXRefHost, DOpts.ReaderPathHost, NULL);
    if (DOpts.ReaderXRefHost == NULL)
	strdupfree(&DOpts.ReaderXRefHost, DOpts.ReaderHostName, NULL);

    /*
     * Setup realtime status file name and remove file in case
     * dreaderd's are left over from a previous run.
     */

    if (RTStatus == NULL) {
	RTStatus = malloc(strlen(PatExpand(LogHomePat)) + 32);
	sprintf(RTStatus, "%s/dreaderd.status", PatExpand(LogHomePat));
    }
    remove(RTStatus);

    /*
     * RTStatus file initial open
     */

    RTStatusOpen(RTStatus, 0, 1);

    /*
     * open syslog
     */

    closelog();
    OpenLog("dreaderd", (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID);
    SysLogDesc = "dreaderd";
    DnsQInitLimits();

    /*
     * Save PID
     */

    {
	int fd;
	char buf[32];

	sprintf(buf, "%d\n", (int)getpid());

	PidFile = malloc(strlen(PatExpand(LogHomePat)) + 32);
	sprintf(PidFile, "%s/dreaderd.pid", PatExpand(LogHomePat));
	remove(PidFile);
	if ((fd = open(PidFile, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
	    logit(LOG_ERR, "unable to create %s\n", PidFile);
	    exit(1);
	}
	if (WriteAll(fd, buf, strlen(buf)) < 0)
	    logit(LOG_ERR, "unable to write %s: %s", PidFile, strerror(errno));
	close(fd);
    }

#ifdef	DREADER_CLIENT_TIMING
    ticksPerSecond=sysconf(_SC_CLK_TCK);
#endif

    /*
     * Cancel cache (inherited by children)
     */

    InitCancelCache();

    InstallAccessCache();

#ifdef	READER_BAN_LISTS
    bzero(&UserBan, sizeof(UserBan));
    bzero(&HostBan, sizeof(HostBan));
    bzero(&GroupBan, sizeof(GroupBan));
    bzero(&GlobalBan, sizeof(GlobalBan));
#endif

    if (DOpts.ReaderForks < 0 || DOpts.ReaderForks > 1024 ||
	DOpts.ReaderFeedForks < 0 || DOpts.ReaderFeedForks > 1024
    ) {
	fprintf(stderr, "readerforks/readerfeedforks outside 0..1024\n");
	exit(1);
    }
    ReaderPids = malloc((DOpts.ReaderForks + DOpts.ReaderFeedForks) * sizeof(pid_t));
    if (ReaderPids == NULL) {
	perror("malloc ReaderPids");
	exit(1);
    }
    bzero(ReaderPids, (DOpts.ReaderForks + DOpts.ReaderFeedForks) * sizeof(pid_t));

    /*
     *  Add the UNIX domain socket listener for drcmd
     */
    {
        struct sockaddr_un soun;
        const char *sockpath;

        bzero(&soun, sizeof(soun));

        if ((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
            perror("udom-socket");
            exit(1);
        }
        soun.sun_family = AF_UNIX;

        sockpath = PatRunExpand(DReaderSocketPat);
        if (strlen(sockpath) >= sizeof(soun.sun_path)) {
            fprintf(stderr, "socket path too long: %s\n", sockpath);
            exit(1);
        }
        snprintf(soun.sun_path, sizeof(soun.sun_path), "%s", sockpath);
        remove(soun.sun_path);
        if (bind(ufd, (struct sockaddr *)&soun,
			offsetof(struct sockaddr_un,
				sun_path[strlen(soun.sun_path)+1])) < 0) {
            perror("udom-bind");
            exit(1);
        }
        chmod(soun.sun_path, 0770);

#if NONBLK_ACCEPT_BROKEN
        /* HPUX is broken, see lib/config.h */
#else
        fcntl(ufd, F_SETFL, O_NONBLOCK);
#endif

        if (listen(ufd, 10) < 0) {
            perror("udom-listen");
            exit(1);
        }

	AddThread("drcmd", ufd, -1, THREAD_DRCMD, -1, 0);
    }
    /*
     * accept()/pipe-processing loop, fork child processes as required
     * to maintain requested parameters (initial fork is done as part of
     * the nominal re-fork code when a child dies).
     */

    logit(LOG_NOTICE, "Waiting for connections");
    TimeStart = time(NULL);
    for (;;) {
	/*
	 * handle required forks and child process cleanup
	 *
	 * (A) reader NNTP processes
	 */

	while (!Exiting && NumReaderForks < DOpts.ReaderForks) {
	    pid_t pid;
	    int fd;

	    ThisReaderFork = getReaderForkSlot();
	    FeedOnlyServer = 0;
	    pid = ForkPipe(&fd, ReaderTask, "reader", SOCK_SEQPACKET);
	    FeedOnlyServer = -1;
	    if (pid < 0)
		break;
	    ReaderPids[ThisReaderFork] = pid;
	    AddThread("reader", fd, pid, THREAD_READER, -1, 0);
	    ++NumReaderForks;
	}

	/*
	 * (B) reader feeder-only processes ('mode reader' not available)
	 */

	while (!Exiting && NumReaderFeedForks < DOpts.ReaderFeedForks) {
	    pid_t pid;
	    int fd;

	    ThisReaderFork = getReaderForkSlot();
	    FeedOnlyServer = 1;
	    pid = ForkPipe(&fd, ReaderTask, "feed", SOCK_SEQPACKET);
	    FeedOnlyServer = -1;
	    if (pid < 0)
		break;
	    ReaderPids[ThisReaderFork] = pid;
	    AddThread("feed", fd, pid, THREAD_FEEDER, -1, 0);
	    ++NumReaderFeedForks;
	}

	/*
	 * (C) standard DNS processes
	 */

	while (!Exiting && NumDnsForks < DOpts.ReaderDns) {
	    pid_t pid;
	    int fd;

	    if ((pid = ForkPipe(&fd, DnsTask, "reader-dns", SOCK_SEQPACKET)) < 0)
		break;
	    AddThread("dns", fd, pid, THREAD_DNS, -1, 0);
	    ++NumDnsForks;
	}

#ifdef NOTDEF
	/*
	 * (D) server hostname resolver	(future)
	 */

	while (!Exiting && NumSDnsForks < MaxSDnsForks) {
	    pid_t pid;
	    int fd;

	    if ((pid = ForkPipe(&fd, DnsTask, "reader-sdns", SOCK_SEQPACKET)) < 0)
		break;
	    AddThread("sdns", fd, pid, THREAD_SDNS, -1, 0);
	    ++NumSDnsForks;
	}
#endif

	/*
	 * (E) dead processes
	 */

	{
	    pid_t pid;

	    while ((pid = wait3(NULL, WNOHANG, NULL)) > 0) {
		ForkDesc *desc;

		if ((desc = FindThread(-1, pid)) != NULL) {
		    if (desc->d_Type == THREAD_DNS) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost dns resolver %d", (int)pid);
			--NumDnsForks;
			if (desc->d_FdPend >= 0) {
			    logit(LOG_NOTICE, "pending descriptor from %s on dead DNS pid closed",
				NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 1, 1)
			    );
			    close(desc->d_FdPend);
			    desc->d_FdPend = -1;
			}
			/* also for an in-flight accounting request (no pending descriptor) */
			NumPending -= desc->d_Count; /* note: d_Count may already be 0 */
			NumActive -= desc->d_Count;
			desc->d_Count = 0;
		    } else if (desc->d_Type == THREAD_READER) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost reader process %d", (int)pid);
			--NumReaderForks;
			delReaderForkSlot(pid);
			NumActive -= desc->d_Count;
			deleteDnsResHash(NULL, desc);
		    } else if (desc->d_Type == THREAD_FEEDER) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost feeder process %d", (int)pid);
			--NumReaderFeedForks;
			delReaderForkSlot(pid);
			NumActive -= desc->d_Count;
			deleteDnsResHash(NULL, desc);
		    } else if (desc->d_Type == THREAD_SDNS) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost sdns process %d", (int)pid);
			--NumSDnsForks;
		    } else {
			fatal("main loop, unknown thread type %d pid %d\n", desc->d_Type, (int)pid);
		    }
		    DelThread(desc);
		} else {
		    fatal("main loop, wait3() pid %d, pid is not known to me!", (int)pid);
		}
	    }
	}

#ifdef	READER_BAN_LISTS
	/*
	 * Check for changes in the Ban list
	 */
	if (DOpts.ReaderBan != NULL && LastBan != DOpts.ReaderBan) {
	    logit(LOG_INFO, "Setting ban list");
	    SetBanList(DOpts.ReaderBan);
	    LastBan = DOpts.ReaderBan;
	}
#endif
	/*
	 * (F) select core
	 *
	 * select on descriptors (listen socket and pipe sockets only),
	 * process results
	 */
	{
	    struct timeval tv;
	    fd_set rfds = SFds;
	    int i;

	    if (Exiting)
		NextTimeout(&tv, 50);
	    else
	    {
		int ms = (DnsQAuth.ql_Head || DnsQNew.ql_Head) ? 250 : 10 * 1000;

		if (ListenPauseUntil > 0) {
		    double left = ListenPauseUntil - MonoMs();

		    if (left < ms)
			ms = (left < 1) ? 1 : (int)left + 1;
		}
		NextTimeout(&tv, ms);
	    }

	    stprintf("Connect=%d Failed=%d Dns=%d/%d Act=%d/%d", 
		ConnectCount, FailCount,
		NumPending, DOpts.ReaderDns, 
		NumActive, MaxConnects
	    );
	    RTStatusUpdate(0, "Connect=%d Failed=%d Dns=%d/%d Act=%d/%d", 
		ConnectCount, FailCount,
		NumPending, DOpts.ReaderDns, 
		NumActive, MaxConnects
	    );

	    select(MaxFds, &rfds, NULL, NULL, &tv);
	    gettimeofday(&CurTime, NULL);

	    for (i = 0; i < MaxFds; ++i) {
		if (FD_ISSET(i, &rfds)) {
		    ForkDesc *desc;

		    if ((desc = FindThread(i, -1)) != NULL) {
#ifdef	DREADER_CLIENT_TIMING
			struct tms start,end;

		    	times(&start);
#endif
			switch(desc->d_Type) {
			case THREAD_LISTEN:
			    DoListen(desc, CurTime.tv_sec);
			    break;
			case THREAD_DNS:
			    /*
			     * read query result from DNS handler, then
			     * issue appropriate action to descriptor.
			     */
			    HandleDnsThread(desc);
			    break;
			case THREAD_READER:
			case THREAD_FEEDER:
			    /*
			     * reader process returns a dres struc for each
			     * closed connection.  Update our ref counts.
			     */
			    {
				DnsRes dres;
				int n;
				int recv_fd;

				while ((n = RecvMsg(desc->d_Fd, &recv_fd, &dres)) == sizeof(dres)) {
				    if (dres.dr_ResultFlags == DR_SERVER_STATS) {
					if (recv_fd >= 0)
					    close(recv_fd);
					TotalSpoolArticles += dres.dr_ArtCount;
					TotalSpoolBytes += dres.dr_ByteCount;
				    } else {
#ifdef	INET6
					struct sockaddr_storage rsin;
					bzero((void *)&rsin, sizeof(rsin));

					memcpy(&rsin, &dres.dr_Addr,
						sizeof(struct sockaddr_storage));
#else
					struct sockaddr_in rsin;

					bzero(&rsin, sizeof(rsin));
					memcpy(&rsin, &dres.dr_Addr, sizeof(rsin));
#endif
					if (recv_fd < 0) {
					    /* the descriptor was lost (limit); still account */
					    DnsRefuseLog(DQR_NOFD);
					} else if (dres.dr_ResultFlags == DR_REQUIRE_DNS) {
					    SendDns(recv_fd, &rsin, &dres);
					} else {
#ifdef	NETREMOTE_ENABLED
					    /* session-exit accounting: only netremote uses it */
					    if (dres.dr_AuthUser[0] && dres.dr_AuthPass[0]) {
						dres.dr_ResultFlags = DR_SESSEXIT_RPT;
						SendDns(recv_fd, &rsin, &dres);
					    }
#endif
					    close(recv_fd);
					}
					--NumActive;
					--desc->d_Count;
					deleteDnsResHash(&dres, desc);
				    }
				}
			    }
			    break;
			case THREAD_DRCMD:
			    DoCommand(desc->d_Fd);
			    break;
			default:
			    fatal("Unknown descriptor type %d", desc->d_Type);
			    break;
			}
#ifdef	DREADER_CLIENT_TIMING
			if ((desc = FindThread(i, -1)) != NULL) {
			    times(&end);
			    desc->d_utime+=end.tms_utime-start.tms_utime;
			    desc->d_stime+=end.tms_stime-start.tms_stime;
			}
#endif
		    } else {
			fatal("select returned unknown descriptor %d", i);
		    }
		}
	    }
	    (void)ScanTimers(1, 0);
	}
	DnsQueueRun();

	/*
	 * Disable listen socket if we are full up or out of descriptors,
	 * enable listen socket otherwise.  Busy DNS workers do not turn it
	 * off: SendDns() queues or refuses the connection.  NOTE: NumPending
	 * is included in the NumActive number.
	 */
	if (ListenPauseUntil > 0 && MonoMs() >= ListenPauseUntil)
	    ListenPauseUntil = 0;
	if (ListenPauseUntil == 0 &&
	    NumActive < NumReaderForks * DOpts.ReaderThreads
	) {
	    if (ListenersEnabled == 0) {
		int i;

		ListenersEnabled = 1;
		for (i = 0; i < MaxFds; ++i) {
		    ForkDesc *desc;

		    if ((desc = FindThread(i, -1)) != NULL && 
			desc->d_Fd >= 0 &&
			desc->d_Type == THREAD_LISTEN
		    ) {
			FD_SET(desc->d_Fd, &SFds);
		    }
		}
	    }
	} else {
	    if (ListenersEnabled == 1) {
		int i;

		ListenersEnabled = 0;
		for (i = 0; i < MaxFds; ++i) {
		    ForkDesc *desc;

		    if ((desc = FindThread(i, -1)) != NULL && 
			desc->d_Fd >= 0 &&
			desc->d_Type == THREAD_LISTEN
		    ) {
			FD_CLR(desc->d_Fd, &SFds);
		    }
		}
	    }
	}
	InstallAccessCache();
	/* Clean out the delayed closes */
	if (NeedClosing != NULL) {
	    PendingClose *pc;
	    PendingClose *ppc = NULL;
	    time_t timenow = time(NULL);

	    for (pc = NeedClosing; pc != NULL; ppc = pc, pc = pc->next) {
		if (pc->when > timenow)
		    continue;
		WriteBestEffort(pc->fd, pc->msgbuf, strlen(pc->msgbuf));
		close(pc->fd);
		pc->fd = -1;
		++FailCount;
		if (!pc->quickclose) {
		    --NumActive;
		}
		if (pc == NeedClosing)
		    NeedClosing = pc->next;
		if (ppc != NULL)
		    ppc->next = pc->next;
		zfree(&PendingMemPool, pc, sizeof(PendingClose));
	    }
	}
	if (Exiting && !NumReaderFeedForks &&
		!NumReaderForks && !NumDnsForks && !NumSDnsForks)
	    break;
    }
    FinishRetain(RET_CLOSE);
    RTStatusClose();
    logit(LOG_NOTICE, "Exiting");
    return(0);
}

void
FinishRetain(int what)
{
    Retain *ret;
    Retain **pret;

    for (pret = &ReBase; (ret = *pret) != NULL; ) {
        if (ret->re_What == what) {
            if (ret->re_Fo) 
                fprintf(ret->re_Fo, "200 Operation Complete\n.\n");
            *pret = ret->re_Next;
            if (ret->re_Fi)
                fclose(ret->re_Fi);
            if (ret->re_Fo)
                fclose(ret->re_Fo);
            zfree(&ReMemPool, ret, sizeof(Retain));
        } else {
            pret = &ret->re_Next;
        }
    }
} 

void
DoListen(ForkDesc *desc, time_t t)
{

    /*
     * accept new descriptor, depend on socket
     * buffer for write to not block if descriptor
     * beyond what we can handle.
     */
    {
        char errbuf[1024];
#ifdef INET6
	struct sockaddr_storage rsin;
#else
	struct sockaddr_in rsin;
#endif
	socklen_t rslen = sizeof(rsin);
	int fd;

	bzero(&rsin, sizeof(rsin));
	fd = accept(desc->d_Fd, (struct sockaddr *)&rsin, &rslen);

	if (fd < 0 && (errno == EMFILE || errno == ENFILE ||
			errno == ENOBUFS || errno == ENOMEM)) {
	    /* the client stays in the backlog; retry in a second */
	    DnsRefuseLog(DQR_ACCEPT);
	    ListenPauseUntil = MonoMs() + 1000;
	    return;
	}

	if (PausedCount) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Server is temporarily unavailable, please try later%s%s\r\n",
					DOpts.ReaderHostName,
					(DOpts.NewsAdmin != NULL) ? " - " : "",
					safestr(DOpts.NewsAdmin, ""));
	    WriteBestEffort(fd, errbuf, strlen(errbuf));
	    close(fd);
	    fd = -1;
	    ++ConnectCount;
	    ++FailCount;
	    return;
	}

	if (fd >= MAXFDS) {
	    DnsRefuseLog(DQR_MAXFDS);
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Too many open descriptors, please try later%s%s\r\n",
					DOpts.ReaderHostName,
					(DOpts.NewsAdmin != NULL) ? " - " : "",
					safestr(DOpts.NewsAdmin, ""));
	    WriteBestEffort(fd, errbuf, strlen(errbuf));
	    close(fd);
	    fd = -1;
	    ++ConnectCount;
	    ++FailCount;
	    return;
	}

	if (fd >= 0) {
#ifdef	READER_BAN_LISTS
	    if (Banned(inet_ntoa(rsin.sin_addr), t)) {
		PendingClose *pc;
		logit(LOG_INFO, "rejected auto-banned IP %s", inet_ntoa(rsin.sin_addr));
		snprintf(errbuf, sizeof(errbuf), "502 %s: Access denied to your node%s%s\r\n",
					DOpts.ReaderHostName,
					(DOpts.NewsAdmin != NULL) ? " - " : "",
					safestr(DOpts.NewsAdmin, ""));
		if (DelayedClose) {
		    for (pc = NeedClosing; pc != NULL && pc->next != NULL;
								pc = pc->next);
		    if (pc == NULL) {
			pc = zalloc(&PendingMemPool, sizeof(PendingClose));
			NeedClosing = pc;
		    } else {
			pc->next = zalloc(&PendingMemPool, sizeof(PendingClose));
			pc = pc->next;
		    }
		    pc->fd = fd;
		    strncpy(pc->msgbuf, errbuf, sizeof(pc->msgbuf) - 1);
		    pc->msgbuf[sizeof(pc->msgbuf) - 1] = '\0';
		    pc->when = time(NULL) + DelayedClose;
		    pc->quickclose = 1;
		    pc->next = NULL;
		} else {
		    write(fd, errbuf, strlen(errbuf));
		    close(fd);
		    fd = -1;
		}
		++ConnectCount;
		++FailCount;
		return;
	    }
#endif
	    /*
	     * queue descriptor information to DNS
	     * resolver and put descriptor on hold.
	     */
	    SendDns(fd, &rsin, NULL);
	}
    }
}

/*
 * DOCOMMAND() - handles a control connection
 */

void
DoCommand(int ufd)
{
    int fd;
    int retain = 0;
#ifdef INET6
    struct sockaddr_storage asin;
#else
    struct sockaddr_in asin;
#endif
    ACCEPT_ARG3_TYPE alen = sizeof(asin);

    /*
     * This can be a while() or an if(), but apparently some OS's fail to
     * properly handle O_NONBLOCK for accept() on unix domain sockets so...
     */

    if ((fd = accept(ufd, (struct sockaddr *)&asin, &alen)) > 0) {
	FILE *fi;
	FILE *fo;

	fcntl(fd, F_SETFL, 0);
	fo = fdopen(dup(fd), "w");
	fi = fdopen(fd, "r");

	if (fi && fo) {
	    char buf[MAXLINE];

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		char *s1;
		char *s2;

		if (DebugOpt)
		    printf("%d << %s", (int)getpid(), buf);

		if ((s1 = strtok(buf, " \t\r\n")) == NULL)
		    break;
		s2 = strtok(NULL, "\n");
		if (strcmp(s1, "status") == 0) {
		    fprintf(fo, "Connect=%d Failed=%d Dns=%d/%d Act=%d/%d\n",
			ConnectCount, FailCount,
			NumPending, DOpts.ReaderDns, 
			NumActive, MaxConnects
		    );
		} else if (strcmp(s1, "version") == 0) {
		    fprintf(fo, "200 DREADERD Version %s - %s\n", VERS, SUBREV);
		} else if (strcmp(s1, "dnscache") == 0) {
		    char *a2 = (s2 != NULL) ? strtok(s2, " \t\r") : NULL;

		    if (a2 != NULL && strcmp(a2, "flush") == 0) {
			DnsCacheFlush();
			fprintf(fo, "200 DNS cache flushed\n");
		    } else {
			DnsCacheStats(fo);
		    }
		} else if (strcmp(s1, "vstats") == 0) {
		    getVStats(fo);
		} else if (strcmp(s1, "stats") == 0) {
		    getStats(fo, 0);
		} else if (strcmp(s1, "rawstats") == 0) {
		    getStats(fo, 1);
#ifdef	READER_BAN_LISTS
		} else if (strcmp(s1, "banlist") == 0) {
		    DumpBannedLists(fo);
		} else if (strcmp(s1, "banconfig") == 0) {
		    DumpBannedConfigs(fo);
#endif
		} else if (strcmp(s1, "pause") == 0) {
		    /* retain = RET_PAUSE; */
		    ++PausedCount;
		    fprintf(fo, "200 Pause, count %d.\n", PausedCount);
		} else if (strcmp(s1, "go") == 0) {
		    if (PausedCount)
			--PausedCount;
		    fprintf(fo, "200 Resume, count %d\n", PausedCount);
		} else if (strcmp(s1, "quit") == 0) {
		    fprintf(fo, "200 Goodbye\n");
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "exit") == 0 || strcmp(s1, "aexit")==0) {
		    int i;

		    if (s1[0] == 'e')
			retain = RET_CLOSE;

		    fprintf(fo, "211 Exiting\n");
		    fflush(fo);

		    if (Exiting == 0) {
			ForkDesc *desc;
			Exiting = 1;

			logit(LOG_INFO, "Shutting down sub-processes");
			for (i = 0; i < MaxFds; ++i)
			if ((desc = FindThread(i, -1)) != NULL) {
			    switch(desc->d_Type) {
			    case THREAD_DNS:
				if (desc->d_Pid)
				    kill(desc->d_Pid, SIGKILL);
				break;
			    case THREAD_READER:
			    case THREAD_FEEDER:
				if (desc->d_Pid)
				    kill(desc->d_Pid, SIGHUP);
				break;
			    case THREAD_LISTEN:
				close(desc->d_Fd);
				break;
			    case THREAD_DRCMD:
				break;
			    }
			}
		    } else {
			fprintf(fo, "200 Exit is already in progress\n");
		    }
		} else if (strcmp(s1, "config") == 0) {
		    if (s2 != NULL) {
			char *opt;
			char *cmd;
			int cmdErr = 0;
			if ((cmd = strtok(s2, " \t\n")) != NULL &&
			    (opt = strtok(NULL, " \t\n")) != NULL) {
			    if (SetCommand(fo, cmd, opt, &cmdErr) == 0) {
				CheckConfigOptions(1);
				fprintf(fo, "Set '%s' to '%s'\n", cmd, opt);
			    } else if (cmdErr == 1) {
				fprintf(fo, "Invalid config command: %s %s\n",
								cmd, opt);
				fprintf(fo, "Usage: config command option\n");
			    } else {
				fprintf(fo, "Invalid option for config command: %s %s\n",
								 cmd, opt);
				fprintf(fo, "Usage: config command option\n");
			    }
			} else {
			    DumpConfigOptions(fo, s2, CONF_READER);
			}
		    } else {
			DumpConfigOptions(fo, NULL, CONF_READER);
		    }
		} else if (strcmp(s1, "help") == 0) {
		    fprintf(fo,"Supported commands:\n");
		    fprintf(fo,"   help      you are looking at it\n");
		    fprintf(fo,"   status    short status line\n");
		    fprintf(fo,"   version   return dreaderd version\n");
		    fprintf(fo,"   dnscache  DNS cache counters; 'dnscache flush' empties it\n");
		    fprintf(fo,"   config    view/set run-time config\n");
		    fprintf(fo,"   stats     general server statistics\n");
		    fprintf(fo,"   rawstats  general server statistics (without pretty printing)\n");
		    fprintf(fo,"   vstats    virtual host statistics\n");
#ifdef	READER_BAN_LISTS
		    fprintf(fo,"   banconfig show current ban configs\n");
		    fprintf(fo,"   banlist   show the current ban lists\n");
#endif
		    fprintf(fo,"   go        resume accepting new connections\n");
		    fprintf(fo,"   exit      close all connections and exit\n");
		} else {
		    fprintf(fo, "500 Unrecognized command: %s\n", s1);
		}
		if (retain == 0)
		    fprintf(fo, ".\n");
		fflush(fo);
		/* if (retain) */
		    break;
	    }
	}
	if (retain) {
	    Retain *ret = zalloc(&ReMemPool, sizeof(Retain));

	    ret->re_Next = ReBase;
	    ret->re_Fi = fi;
	    ret->re_Fo = fo;
	    ret->re_What = retain;
	    ReBase = ret;
	} else {
	    if (fi)
		fclose(fi);
	    if (fo)
		fclose(fo);
	}
    }
}

static void
DnsTooBusy(int fd)
{
    char errbuf[1024];

    snprintf(errbuf, sizeof(errbuf), "400 %s: Server too busy - please try later%s%s\r\n",
	    DOpts.ReaderHostName,
	    (DOpts.NewsAdmin != NULL) ? " - " : "",
	    safestr(DOpts.NewsAdmin, ""));
    WriteBestEffort(fd, errbuf, strlen(errbuf));
    close(fd);
    ++FailCount;
}

/*
 * Give the request to a free DNS worker.  Returns 0 if there is none; the
 * caller keeps the descriptor then.
 */
#ifdef INET6
static int
TrySendDns(int fd, struct sockaddr_storage *rsin, DnsRes *dres)
#else
static int
TrySendDns(int fd, struct sockaddr_in *rsin, DnsRes *dres)
#endif
{
    ForkDesc *best;
#ifdef INET6
    struct sockaddr_storage lsin;
#else
    struct sockaddr_in lsin;
#endif
    socklen_t lslen = sizeof(lsin);

    /*
     * Check for auth changes
     */
    InstallAccessCache();

    bzero(&lsin, sizeof(lsin));

    if (getsockname(fd, (struct sockaddr *)&lsin, &lslen) < 0) {
	logit(LOG_ERR, "getsockname(%d) call failed: %s", fd, strerror(errno));
	close(fd);
	return(1);
    }

    best = FindLeastUsedThread(THREAD_DNS, 1, 0, NULL, -1, NULL, NULL);

    if (best != NULL) {
	DnsReq dreq;

	++NumActive;
	++NumPending;
	++best->d_Count;

	bzero(&dreq, sizeof(dreq));
	bcopy(rsin, &dreq.dr_RSin, sizeof(dreq.dr_RSin));
	bcopy(&lsin, &dreq.dr_LSin, sizeof(dreq.dr_LSin));
	if (dres != NULL) {
	    dreq.dr_ResultFlags = dres->dr_ResultFlags;
	    dreq.dr_Flags = dres->dr_Flags & DF_RESREQRES;
	    diablo_strlcpy(dreq.dr_AuthUser, dres->dr_AuthUser, sizeof(dres->dr_AuthUser), sizeof(dreq.dr_AuthUser));
	    diablo_strlcpy(dreq.dr_AuthPass, dres->dr_AuthPass, sizeof(dres->dr_AuthPass), sizeof(dreq.dr_AuthPass));
	    dreq.dr_ByteCount = dres->dr_ByteCount;
	    dreq.dr_GrpCount = dres->dr_GrpCount;
	    dreq.dr_ArtCount = dres->dr_ArtCount;
	    dreq.dr_PostBytes = dres->dr_PostBytes;
	    dreq.dr_PostCount = dres->dr_PostCount;
	    dreq.dr_PostFailCount = dres->dr_PostFailCount;
	    dreq.dr_ByteCountArticle = dres->dr_ByteCountArticle;
	    dreq.dr_ByteCountHead = dres->dr_ByteCountHead;
	    dreq.dr_ByteCountBody = dres->dr_ByteCountBody;
	    dreq.dr_ByteCountList = dres->dr_ByteCountList;
	    dreq.dr_ByteCountXover = dres->dr_ByteCountXover;
	    dreq.dr_ByteCountXhdr = dres->dr_ByteCountXhdr;
	    dreq.dr_ByteCountOther = dres->dr_ByteCountOther;
	    dreq.dr_SessionLength = time(NULL) - dres->dr_TimeStart;
	}

	/*
	 * Pass a known name lookup result so the worker skips the lookups:
	 * the connect-time result of this connection (AUTHINFO), else the
	 * DNS cache.
	 */
	if (dres == NULL || dres->dr_ResultFlags != DR_SESSEXIT_RPT) {
	    dreq.dr_CacheGen = DnsCacheGen();
	    if (dres != NULL && (dres->dr_Lookup & DL_MASK) != DL_NONE &&
		    (dres->dr_Lookup & DL_TEMP) == 0 &&
		    memchr(dres->dr_RevHost, 0, sizeof(dres->dr_RevHost)) &&
		    DnsCacheSameAddr((struct sockaddr *)&dres->dr_LookupAddr,
					(struct sockaddr *)rsin)) {
		dreq.dr_Lookup = dres->dr_Lookup & DL_MASK;
		memcpy(dreq.dr_RevHost, dres->dr_RevHost, sizeof(dreq.dr_RevHost));
	    } else {
		DnsCacheGet((struct sockaddr *)rsin, &dreq.dr_Lookup,
				dreq.dr_RevHost, sizeof(dreq.dr_RevHost));
	    }
	}

	/*
	 * An accounting hand-back (DR_SESSEXIT_RPT) is closed by the caller
	 * right after this; don't keep its number as a pending descriptor
	 * that a dying worker's cleanup would close later.
	 */
	if (dres != NULL && dres->dr_ResultFlags == DR_SESSEXIT_RPT)
	    best->d_FdPend = -1;
	else
	    best->d_FdPend = fd;
	bcopy(rsin, &best->d_SaveRSin, sizeof(best->d_SaveRSin));
	/*
	 * The worker never received a whole request, so no reply is coming:
	 * undo the accounting and let the caller queue the request again.
	 * With SOCK_SEQPACKET a failed write sent nothing at all.  On the
	 * SOCK_STREAM fallback a short write may already have left half a
	 * request in the pipe, so retire the worker instead of feeding it
	 * the next request at a wrong offset.
	 */
	best->d_SendMs = MonoMs();
	best->d_SendKnown = (dres != NULL && dres->dr_ResultFlags == DR_SESSEXIT_RPT) ||
			    (dreq.dr_Lookup & DL_MASK) != DL_NONE;
	if (WriteAll(best->d_Fd, &dreq, sizeof(dreq)) < 0) {
	    int err = errno;

	    logit(LOG_ERR, "DNS request to worker failed: %s", strerror(err));
	    best->d_FdPend = -1;
	    --best->d_Count;
	    --NumPending;
	    --NumActive;
	    if (DnsSockType != SOCK_SEQPACKET) {
		best->d_Dead = 1;
		if (best->d_Pid > 0)
		    kill(best->d_Pid, SIGKILL);
	    }
	    return(0);
	}
	return(1);
    }
    return(0);
}

/*
 * Refusals are logged per reason, at most once per 10 seconds each, with
 * the count of the ones not logged; DnsRefuseFlush() logs what is left.
 */

static struct {
    time_t last;
    int skipped;
} DnsRefuseLogs[DQR_N];

static void
DnsRefuseText(int r, char *buf, size_t len)
{
    switch (r) {
    case DQR_FULL:
	snprintf(buf, len, "Unable to find spare DNS thread, queue full - rejecting connection");
	break;
    case DQR_WAIT:
	snprintf(buf, len, "Unable to find spare DNS thread, estimated wait over %d seconds - rejecting connection",
	    DOpts.ReaderDnsWait);
	break;
    case DQR_FDS:
	snprintf(buf, len, "Unable to find spare DNS thread, no descriptors left to queue - rejecting connection");
	break;
    case DQR_NOWORKER:
	snprintf(buf, len, "No DNS thread running - rejecting connection");
	break;
    case DQR_EXPIRED:
	snprintf(buf, len, "Unable to find spare DNS thread in %d seconds - rejecting connection",
	    DOpts.ReaderDnsWait);
	break;
    case DQR_MAXFDS:
	snprintf(buf, len, "fd beyond MAXFDS (%d), closing", MAXFDS);
	break;
    case DQR_ACCEPT:
	snprintf(buf, len, "accept: out of descriptors or memory - listener paused");
	break;
    case DQR_INFLIGHT:
	snprintf(buf, len, "connect: too many descriptors in flight to the readers - rejecting connection");
	break;
    default:
	snprintf(buf, len, "reader handed back a connection without its descriptor (descriptor limit?)");
	break;
    }
}

static void
DnsRefuseLog(int r)
{
    char buf[256];
    time_t now = time(NULL);

    if (DnsRefuseLogs[r].last != 0 && now - DnsRefuseLogs[r].last < 10) {
	++DnsRefuseLogs[r].skipped;
	return;
    }
    DnsRefuseText(r, buf, sizeof(buf));
    if (DnsRefuseLogs[r].skipped)
	logit(LOG_CRIT, "%s (%d more since the last message)", buf,
	    DnsRefuseLogs[r].skipped);
    else
	logit(LOG_CRIT, "%s", buf);
    DnsRefuseLogs[r].last = now;
    DnsRefuseLogs[r].skipped = 0;
}

static void
DnsRefuseFlush(void)
{
    char buf[256];
    time_t now = time(NULL);
    int r;

    for (r = 1; r < DQR_N; ++r) {
	if (DnsRefuseLogs[r].skipped == 0 || now - DnsRefuseLogs[r].last < 10)
	    continue;
	DnsRefuseText(r, buf, sizeof(buf));
	logit(LOG_CRIT, "%s (%d more since the last message)", buf,
	    DnsRefuseLogs[r].skipped);
	DnsRefuseLogs[r].last = now;
	DnsRefuseLogs[r].skipped = 0;
    }
}

/*
 * Milliseconds on a clock that does not jump with the time of day.
 */
static double
MonoMs(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
	return(ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0);
#endif
    return(time(NULL) * 1000.0);
}

/*
 * Descriptor limits, at startup: raise the soft RLIMIT_NOFILE up to the
 * hard limit, but not past FD_SETSIZE (the readers select() on their
 * clients).  The limit used is the smaller of MAXFDS and RLIMIT_NOFILE.
 * New connections are queued only below three quarters of it; AUTHINFO
 * logins up to 32 below it (never less than new connections), so that a
 * descriptor a reader hands back can still be received.
 */
static void
DnsQInitLimits(void)
{
    struct rlimit rl;
    rlim_t lim = MAXFDS;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
	if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < (rlim_t)FD_SETSIZE &&
		(rl.rlim_max == RLIM_INFINITY || rl.rlim_cur < rl.rlim_max)) {
	    rlim_t old = rl.rlim_cur;

	    rl.rlim_cur = (rl.rlim_max == RLIM_INFINITY ||
			    rl.rlim_max > (rlim_t)FD_SETSIZE) ?
			    (rlim_t)FD_SETSIZE : rl.rlim_max;
	    if (setrlimit(RLIMIT_NOFILE, &rl) == 0)
		logit(LOG_NOTICE, "raised the descriptor limit from %lu to %lu",
		    (unsigned long)old, (unsigned long)rl.rlim_cur);
	    else
		rl.rlim_cur = old;
	}
	if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < lim)
	    lim = rl.rlim_cur;
    }
    DnsQFdCap = (int)(lim * 3 / 4);
    DnsQAuthFdCap = (int)lim - 32;
    if (DnsQAuthFdCap < DnsQFdCap)
	DnsQAuthFdCap = DnsQFdCap;
}

/*
 * Worker time of requests that need lookups and of requests that do not
 * (a DNS cache hit, AUTHINFO with its connection's result): the larger of
 * a moving average and the median of the last three samples, so a step
 * up shows after two samples while a single slow one does not.  The
 * estimate never goes below DNSQ_FLOOR_LOOKUP / DNSQ_FLOOR_KNOWN ms, so a
 * burst right after startup or after only cache hits is not taken whole.
 */
#define DNSQ_FLOOR_LOOKUP	50.0
#define DNSQ_FLOOR_KNOWN	5.0

typedef struct DnsTime {
    double dt_Avg;
    double dt_Last[3];
    int dt_N;
} DnsTime;

static DnsTime DnsTimeLookup;
static DnsTime DnsTimeKnown;
static int DnsEstChanged;	/* a new sample: recheck the queue */

static void
DnsSample(int known, double ms)
{
    DnsTime *dt = known ? &DnsTimeKnown : &DnsTimeLookup;

    if (ms < 0)
	ms = 0;
    dt->dt_Avg = (dt->dt_N == 0) ? ms : dt->dt_Avg + (ms - dt->dt_Avg) / 8;
    dt->dt_Last[dt->dt_N % 3] = ms;
    ++dt->dt_N;
    if (dt->dt_N >= 1000000)
	dt->dt_N = 3 + dt->dt_N % 3;	/* keep the slot order */
    DnsEstChanged = 1;
}

static double
DnsTimeEst(const DnsTime *dt, double floor)
{
    const double *l = dt->dt_Last;
    double m = dt->dt_Avg;
    double med;

    if (dt->dt_N >= 3) {
	med = (l[0] > l[1]) ?
	    ((l[1] > l[2]) ? l[1] : (l[0] > l[2]) ? l[2] : l[0]) :
	    ((l[0] > l[2]) ? l[0] : (l[1] > l[2]) ? l[2] : l[1]);
    } else {
	med = (dt->dt_N == 2 && l[1] > l[0]) ? l[1] : l[0];
    }
    if (med > m)
	m = med;
    return((m > floor) ? m : floor);
}

/*
 * The youngest request a worker is busy with: when even that one is older
 * than the average, the workers have slowed down and the average lags.
 */
static double DnsYoungest;	/* send time in ms, 0 = none busy */

static void
DnsYoungestScan(ForkDesc *desc)
{
    if (desc->d_Count > 0 && !desc->d_Dead && desc->d_SendMs > DnsYoungest)
	DnsYoungest = desc->d_SendMs;
}

static void
DnsReqMs(double now, double *lookup, double *known)
{
    *lookup = DnsTimeEst(&DnsTimeLookup, DNSQ_FLOOR_LOOKUP);
    *known = DnsTimeEst(&DnsTimeKnown, DNSQ_FLOOR_KNOWN);
    DnsYoungest = 0;
    ScanThreads(THREAD_DNS, DnsYoungestScan);
    if (DnsYoungest > 0 && now - DnsYoungest > *lookup)
	*lookup = now - DnsYoungest;
}

/* worker time of the requests in a queue */
static double
DnsQWork(const DnsQList *ql, double lookup, double known)
{
    return(ql->ql_Known * known + (ql->ql_Len - ql->ql_Known) * lookup);
}

/*
 * May a new connection (or the first lookup of a proxied client) wait
 * for a worker?  Not when the queue is at readerdnsqueue, its descriptor
 * is above the cap, or the expected wait is beyond half of readerdnswait:
 * the work queued ahead of it and its own over the workers, or the age of
 * the oldest one waiting.  Returns 0 or the DQR_ reason.
 */
static int
DnsQAdmit(int fd, int known)
{
    double now = MonoMs();
    double lookup;
    double kms;
    double est;

    if (NumDnsForks <= 0)
	return(DQR_NOWORKER);
    if (fd >= DnsQFdCap)
	return(DQR_FDS);
    if (DnsQNew.ql_Len >= DOpts.ReaderDnsQueue)
	return(DQR_FULL);
    DnsReqMs(now, &lookup, &kms);
    est = (DnsQWork(&DnsQAuth, lookup, kms) + DnsQWork(&DnsQNew, lookup, kms) +
	    (known ? kms : lookup)) / NumDnsForks;
    if (DnsQNew.ql_Head != NULL && now - DnsQNew.ql_Head->dq_When > est)
	est = now - DnsQNew.ql_Head->dq_When;
    /* half the limit: the estimate lags when the workers slow down */
    if (est > DOpts.ReaderDnsWait * 500.0)
	return(DQR_WAIT);
    return(0);
}

#ifdef INET6
static int
DnsQAdd(DnsQList *ql, int fd, struct sockaddr_storage *rsin, DnsRes *dres, int known)
#else
static int
DnsQAdd(DnsQList *ql, int fd, struct sockaddr_in *rsin, DnsRes *dres, int known)
#endif
{
    DnsQ *dq;

    if ((dq = malloc(sizeof(*dq))) == NULL)
	return(0);
    dq->dq_Next = NULL;
    dq->dq_Fd = fd;
    bcopy(rsin, &dq->dq_RSin, sizeof(dq->dq_RSin));
    dq->dq_HasRes = (dres != NULL);
    if (dres != NULL)
	dq->dq_Res = *dres;
    dq->dq_When = MonoMs();
    dq->dq_Known = known;
    *ql->ql_Tail = dq;
    ql->ql_Tail = &dq->dq_Next;
    ++ql->ql_Len;
    ql->ql_Known += known;
    return(1);
}

/*
 * Refusals of new connections per 100 ms: past DNSQ_REFUSE_BUDGET the
 * listener pauses until the tick ends, so a reconnect storm costs little
 * CPU and its clients wait at most 100 ms in the backlog for their 400.
 */
#define DNSQ_REFUSE_BUDGET	200
static void
DnsRefuseNew(int fd, int r)
{
    static double tick;
    static int count;
    double now = MonoMs();

    DnsRefuseLog(r);
    DnsTooBusy(fd);
    if (now - tick >= 100) {
	tick = now;
	count = 0;
    }
    if (++count >= DNSQ_REFUSE_BUDGET && ListenPauseUntil < tick + 100)
	ListenPauseUntil = tick + 100;
}

/*
 * Send a new connection or an AUTHINFO login to a DNS worker; when all are
 * busy, queue it (D-51) or refuse it.
 */
#ifdef INET6
void
SendDns(int fd, struct sockaddr_storage *rsin, DnsRes *dres)
#else
void
SendDns(int fd, struct sockaddr_in *rsin, DnsRes *dres)
#endif
{
    int known;
    int r;

    ++ConnectCount;

    if (DnsQFdCap == 0)
	DnsQInitLimits();

    /*
     * Session-exit accounting is never queued (see below), so it doesn't wait
     * behind the queue either.
     */
    if (dres != NULL && dres->dr_ResultFlags == DR_SESSEXIT_RPT) {
	if (TrySendDns(fd, rsin, dres))
	    return;
	/*
	 * Only the end-of-session accounting of a finished session: the
	 * client already got its 205.  Never write to it; the caller
	 * closes the descriptor.
	 */
	logit(LOG_INFO, "No spare DNS thread - session accounting for %s skipped",
	    NetAddrToSt(0, (struct sockaddr *)rsin, 1, 1, 1));
	++FailCount;
	return;
    }

    /*
     * An AUTHINFO login of a greeted client waits only behind other
     * AUTHINFO logins, never behind new connections.  There is at most
     * one per connection, so the queue needs no length limit, only
     * descriptor headroom.
     */
    if (dres != NULL && dres->dr_AuthUser[0] &&
	    !((dres->dr_Flags & DF_ISPROXIED) &&
	      (dres->dr_Lookup & DL_MASK) == DL_NONE)) {
	if (DnsQAuth.ql_Head == NULL && TrySendDns(fd, rsin, dres))
	    return;
	if (fd >= DnsQAuthFdCap) {
	    DnsRefuseLog(DQR_FDS);
	    DnsTooBusy(fd);
	} else if (!DnsQAdd(&DnsQAuth, fd, rsin, dres, 1)) {
	    DnsRefuseLog(DQR_FULL);
	    DnsTooBusy(fd);
	}
	return;
    }

    /*
     * A new connection, or the first lookup of a proxied client's real
     * address (PROXY or "proxied", no AUTHINFO yet): admission and the
     * new-connection queue.
     */
    if (DnsQAuth.ql_Head == NULL && DnsQNew.ql_Head == NULL &&
	    TrySendDns(fd, rsin, dres))
	return;
    if (dres != NULL)
	known = (dres->dr_Lookup & DL_MASK) != DL_NONE &&
		(dres->dr_Lookup & DL_TEMP) == 0;
    else
	known = DnsCacheHas((struct sockaddr *)rsin);
    if ((r = DnsQAdmit(fd, known)) != 0)
	DnsRefuseNew(fd, r);
    else if (!DnsQAdd(&DnsQNew, fd, rsin, dres, known))
	DnsRefuseNew(fd, DQR_FULL);
}

/*
 * Called every pass of the main loop: drop queued clients that went away or
 * waited too long, refuse the ones that can no longer make it in time, and
 * hand the oldest ones to free workers, AUTHINFO logins first.
 */
static void
DnsQRemove(DnsQList *ql, DnsQ **pdq)
{
    DnsQ *dq = *pdq;

    if ((*pdq = dq->dq_Next) == NULL)
	ql->ql_Tail = pdq;
    --ql->ql_Len;
    ql->ql_Known -= dq->dq_Known;
    free(dq);
}

static void
DnsQExpire(DnsQList *ql, double now)
{
    DnsQ *dq;

    while ((dq = ql->ql_Head) != NULL &&
	    (Exiting || now - dq->dq_When >= DOpts.ReaderDnsWait * 1000.0)) {
	DnsRefuseLog(DQR_EXPIRED);
	DnsTooBusy(dq->dq_Fd);
	DnsQRemove(ql, &ql->ql_Head);
    }
}

/*
 * After a new worker time sample: a queued new connection that would get
 * its worker only after readerdnswait gets its 400 now.
 */
static void
DnsQTrim(double now)
{
    double lookup;
    double kms;
    double work;
    DnsQ **pdq;
    DnsQ *dq;

    if (NumDnsForks <= 0)
	return;
    DnsReqMs(now, &lookup, &kms);
    work = DnsQWork(&DnsQAuth, lookup, kms);
    for (pdq = &DnsQNew.ql_Head; (dq = *pdq) != NULL; ) {
	double ms = dq->dq_Known ? kms : lookup;

	if (now - dq->dq_When + (work + ms) / NumDnsForks >
		DOpts.ReaderDnsWait * 1000.0) {
	    DnsRefuseLog(DQR_WAIT);
	    DnsTooBusy(dq->dq_Fd);
	    DnsQRemove(&DnsQNew, pdq);
	} else {
	    work += ms;
	    pdq = &dq->dq_Next;
	}
    }
}

/*
 * Hand queued requests to free workers.  Returns 0 when a worker ran out.
 */
static int
DnsQDispatch(DnsQList *ql)
{
    DnsQ *dq;

    while ((dq = ql->ql_Head) != NULL) {
	char c;

	if (recv(dq->dq_Fd, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
	    /* the client closed while waiting */
	    close(dq->dq_Fd);
	    DnsQRemove(ql, &ql->ql_Head);
	} else if (TrySendDns(dq->dq_Fd, (void *)&dq->dq_RSin,
				dq->dq_HasRes ? &dq->dq_Res : NULL)) {
	    DnsQRemove(ql, &ql->ql_Head);
	} else {
	    return(0);		/* no free worker; keep the order */
	}
    }
    return(1);
}

void
DnsQueueRun(void)
{
    double now;

    DnsRefuseFlush();
    if (DnsQAuth.ql_Head == NULL && DnsQNew.ql_Head == NULL)
	return;
    now = MonoMs();
    DnsQExpire(&DnsQAuth, now);
    DnsQExpire(&DnsQNew, now);
    if (DnsQDispatch(&DnsQAuth))
	DnsQDispatch(&DnsQNew);
    if (DnsEstChanged && DnsQNew.ql_Head != NULL)
	DnsQTrim(now);
    DnsEstChanged = 0;
}

pid_t
ForkPipe(int *pfd, void (*task)(int fd, const char *id), const char *description, int sockType)
{
    int fds[2] = { 0, 0 };
    pid_t pid;

    *pfd = -1;

    /*
     * NOTE: must use socketpair in order to be able to pass descriptors.
     *
     * The DNS workers exchange fixed-size DnsReq and DnsRes structs.  A
     * SOCK_SEQPACKET pair preserves their message boundaries, so a short
     * write cannot leave a fragment that the worker reads together with
     * the next request.  Fall back to a stream where SEQPACKET is not
     * implemented; the protocol still works there, it just relies on the
     * writer completing every message.
     */

    if (socketpair(PF_UNIX, sockType, IPPROTO_IP, fds) < 0) {
	if (sockType == SOCK_STREAM) {
	    logit(LOG_ERR, "socketpair() call failed");
	    return((pid_t)-1);
	}
	logit(LOG_NOTICE, "socketpair(SOCK_SEQPACKET) failed (%s), using SOCK_STREAM",
		strerror(errno));
	sockType = SOCK_STREAM;
	if (socketpair(PF_UNIX, SOCK_STREAM, IPPROTO_IP, fds) < 0) {
	    logit(LOG_ERR, "socketpair() call failed");
	    return((pid_t)-1);
	}
    }
    if (task == DnsTask && DnsSockType != sockType) {
	DnsSockType = sockType;
	logit(LOG_INFO, "DNS worker IPC uses %s",
		sockType == SOCK_SEQPACKET ? "SOCK_SEQPACKET" : "SOCK_STREAM");
    }
    if (fds[0] > MAXFDS || fds[1] > MAXFDS) {
	logit(LOG_ERR, "pipe desc > MAXFDS: %d/%d/%d", fds[0], fds[1], MAXFDS);
	close(fds[0]);
	close(fds[1]);
	return((pid_t)-1);
    }

    /*
     * Raise the IPC socketpair buffers above FreeBSD's 8 KiB AF_UNIX default
     * so a readerthreads-sized burst of DnsRes messages does not stall.
     */
    {
	int i;
	int opt;

	for (i = 0; i < 2; ++i) {
	    for (opt = 0; opt < 2; ++opt) {
		int name = (opt == 0) ? SO_SNDBUF : SO_RCVBUF;
		int sz = 256 * 1024;

		if (setsockopt(fds[i], SOL_SOCKET, name, &sz, sizeof(sz)) < 0) {
		    sz = 64 * 1024;
		    (void)setsockopt(fds[i], SOL_SOCKET, name, &sz, sizeof(sz));
		}
	    }
	}
    }

    *pfd = fds[0];

    fflush(stdout);
    fflush(stderr);

    /*
     * Some systems can't deal with syslog through a
     * fork.
     */

    CloseLog(NULL, 1);
    pid = fork();
    if (pid == 0) {
	OpenLog(description, (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID);
	SysLogDesc = description;
    } else {
	OpenLog(SysLogDesc, (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID);
    }

    if (pid == 0) {
	freePool(&DnsResPool);
	stprintf("%s startup", description);
	/* note: DnsResHash not longer valid in children */
	/* note: fds[1] is a normal descriptor, not a non-blocking desc */
	ResetThreads();
	close(*pfd);
	task(fds[1], description);
	_exit(1);
    } else if (pid < 0) {
	logit(LOG_ERR, "fork() failed: %s", strerror(errno));
	close(*pfd);
	close(fds[1]);
	*pfd = -1;
    } else if (pid > 0) {
	close(fds[1]);
	if (DebugOpt)
	    printf("forked %s child process %d\n", description, (int)pid);
    }
    return(pid);
}


/*
 * DNS replies arrive over a nonblocking SOCK_STREAM socket.  Preserve
 * partial fixed-size replies between select() wakeups.  Return 0 if more
 * bytes are needed, 1 when a complete response is ready, or -1 on error.
 */
static int
ReadDnsResponse(ForkDesc *desc, DnsRes *dres)
{
    DnsRes *partial = desc->d_DnsRes;

    while (desc->d_DnsRead < sizeof(*partial)) {
	size_t remaining = sizeof(*partial) - desc->d_DnsRead;
	ssize_t n = read(desc->d_Fd, (char *)partial + desc->d_DnsRead,
			 remaining);

	if (n > 0) {
	    desc->d_DnsRead += (size_t)n;
	    continue;
	}
	if (n < 0 && errno == EINTR)
	    continue;
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
	    return(0);
	if (n == 0)
	    errno = EPIPE;	/* worker closed before completing its reply */
	return(-1);
    }

    memcpy(dres, partial, sizeof(*dres));
    desc->d_DnsRead = 0;
    return(1);
}

void
HandleDnsThread(ForkDesc *desc)
{
    DnsRes dres;
    ForkDesc *reader = NULL;
    static int RandCounter = 0;
    const char *error1 = NULL;
    const char *error2 = NULL;
    const char *vsname = "-";	/* only known once SetAuthDetails() ran */
    char errbuf[1024], *p;
    int r;
#ifdef READER_BAN_LISTS
    int bantime;
#endif
    char tmpaddr[NI_MAXHOST];

    r = ReadDnsResponse(desc, &dres);
    if (r == 0)
	return;
    if (r < 0) {
	int err = errno;

	/* A worker that closes between replies (e.g. killed at shutdown)
	 * is routine; an unexpected death is reported by wait3() as "Lost
	 * dns resolver".  Keep LOG_CRIT for partial replies and errors.
	 */
	logit((desc->d_DnsRead == 0 && err == EPIPE) ? LOG_INFO : LOG_CRIT,
		"DNS subprocess %d response failed after %lu/%lu bytes: %s",
		(int)desc->d_Pid, (unsigned long)desc->d_DnsRead,
		(unsigned long)sizeof(dres), strerror(err));
	FD_CLR(desc->d_Fd, &SFds);	/* avoid select() spinning on EOF */
	if (desc->d_Pid > 0)
	    kill(desc->d_Pid, SIGKILL);
	/* Never hand this worker another client.  Leave d_Count, the
	 * counters and the pending descriptor to the wait3() handler,
	 * which closes d_FdPend and subtracts d_Count when it reaps it.
	 */
	desc->d_Dead = 1;
	return;
    }
    --desc->d_Count;
    --NumPending;

    /* worker time, for DnsQAdmit() */
    DnsSample(desc->d_SendKnown, MonoMs() - desc->d_SendMs);

    if (dres.dr_ResultFlags & DR_SESSEXIT_RPT) {
	/* we don't need the rest of this */
	--NumActive;
	return;
    }

    /* the worker did the lookups: remember the result */
    if ((dres.dr_Lookup & (DL_FRESH | DL_TEMP)) == DL_FRESH)
	DnsCacheStore((struct sockaddr *)&dres.dr_LookupAddr,
			dres.dr_Lookup & DL_MASK, dres.dr_RevHost,
			dres.dr_CacheGen);

    if (isdigit((unsigned char)dres.dr_ReaderName[0]) && isdigit((unsigned char)dres.dr_ReaderName[1]) && isdigit((unsigned char)dres.dr_ReaderName[2])) {
	p = dres.dr_ReaderName;
	if (! strncmp(p, "600 ", 4)) {
	    /* 
	     * Deep six a user via authentication 
	     * Return: 110 600 <time-in-mins> 500 Your connection is rejected
	     * Perpetuating the ugly return code chaining
	     * Requires reader ban lists to actually work,
	     * and actually only tweaks the host banned count.
	     */

	    p += 4;
#ifdef READER_BAN_LISTS
	    bantime = strtol(p, NULL, 10) * 60;
#endif
	    for (; *p && *p != ' '; p++) {
	    }
	    for (; *p && *p == ' '; p++) {
	    }

#ifdef	READER_BAN_LISTS
	    if (HostBan.BanCount > 0)
	        IPListAdd(&HostBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				HostBan.BanCount,
				time(NULL) + bantime,
				HostBan.BanHashSize,
				HostBan.BanLinkSize);
#endif

	}
	snprintf(errbuf, sizeof(errbuf), "%s\r\n", p);
	error1 = errbuf;
	/* Point error2 at errbuf + 4 to catch the specific error */
	/* error2 = "CustomError"; */
	error2 = errbuf + 4;
    }

    if (error1 == NULL) {
        if ((r = ReadAccessCache()) < 0) {
	    logit(LOG_CRIT, "Fatal: Unable to load access cache");
	    exit(1);
        }
        SetAuthDetails(&dres, dres.dr_ReaderName);
        vsname = dres.dr_VServerDef->vs_Name;
        dres.dr_TimeStart = time(NULL);
        if (r == 1)
	    ClearOldAccessMap();

        if (dres.dr_Code == 0 && dres.dr_AuthUser[0] && dres.dr_AuthPass[0]) {
	    /* AUTHINFO USER/PASS that no access entry accepts (RFC 4643) */
	    snprintf(errbuf, sizeof(errbuf), "481 Authentication failed\r\n");
	    error1 = errbuf;
	    error2 = "AuthFailed";
        } else if (dres.dr_Code == 0) {
	    snprintf(errbuf, sizeof(errbuf), "502 %s: Access denied to your node%s%s\r\n",
		    dres.dr_VServerDef->vs_HostName,
		    *dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
		    dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "Unauthorized";
        }

        if (dres.dr_Flags & DF_STATUS) {
            snprintf(errbuf, sizeof(errbuf), "180 %s status: connect=%d failed=%d dns=%d/%d act=%d/%d\r\n", dres.dr_VServerDef->vs_HostName,
		    ConnectCount, FailCount, 
		    NumPending, DOpts.ReaderDns,
		    NumActive, MaxConnects
		    );
	    WriteBestEffort(desc->d_FdPend, errbuf, strlen(errbuf));
	    close(desc->d_FdPend);
	    desc->d_FdPend = -1;
	    --NumActive;
	    deleteDnsResHash(&dres, desc);
	    return;
        }
    }

    /*
     * Pass the descriptor to the correct thread.  A DF_FEED only connection
     * can be passed to a feed-specific thread.
     */

    if (error1 == NULL) {
	if ((dres.dr_Flags & (DF_FEED|DF_READ|DF_POST)) == DF_FEED) {
	    reader = FindLeastUsedThread(THREAD_FEEDER, DOpts.ReaderThreads, 0, &RandCounter, -1, NULL, NULL);
	    dres.dr_Flags |= DF_FEEDONLY;
	} else {
	    reader = FindLeastUsedThread(THREAD_READER, DOpts.ReaderThreads, 0, &RandCounter, -1, NULL, NULL);
	}
	if (reader == NULL) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Global connection limit reached, please try later%s%s\r\n",
	        DOpts.ReaderHostName,
		(DOpts.NewsAdmin != NULL) ? " - " : "",
		safestr(DOpts.NewsAdmin, ""));
	    error1 = errbuf;
	    error2 = "ServerMaxedOut";
	}
    }

    /*
     * Add DnsRes to hash table
     */
    tmpaddr[0] = '\0';
    if (error1 == NULL) {
	const char *addr = NetAddrToSt(0, (struct sockaddr *)&dres.dr_Addr, 1, 0, 0);
	if (addr != NULL) {
	    snprintf(tmpaddr, sizeof(tmpaddr), "%s", addr);
	} else {
	    snprintf(errbuf, sizeof(errbuf),
		"400 %s: Unable to identify client address\r\n",
		dres.dr_VServerDef->vs_HostName);
	    error1 = errbuf;
	    error2 = "InvalidClientAddress";
	}
    }
    if (error1 == NULL) {
	int hi = shash(tmpaddr) & DRHMASK;
	int hmi;
	DnsRes *dr;
	int gcount = 0;
	int hcount = 0;
	int ucount = 0;
	int icount = 0;
	int vcount = 0;

	/*
	 * Until later, the counters only reflect the values of
	 * sessions prior to the current one. This makes it easier to
	 * calculate the rate limit.
	 */
	for (dr = DnsResHash[hi]; dr; dr = dr->dr_HNext) {
	    char *ipname = NetAddrToSt(0, (struct sockaddr *)&dr->dr_Addr, 1, 0, 0);
	    /* we count per-vserver, not global! */
	    if (ipname != NULL && strcmp(tmpaddr, ipname) == 0 &&
			strcmp(dres.dr_VServer, dr->dr_VServer) == 0) {
		++hcount;
		if (*dres.dr_IdentUser && *dr->dr_IdentUser &&
			strcmp(dres.dr_IdentUser, dr->dr_IdentUser) == 0)
		    ++icount;
	    }
	}
	if (dres.dr_ReaderDef->rd_MaxConnPerGroup ||
			dres.dr_ReaderDef->rd_MaxConnPerUser) {
	    for (hmi=0; hmi < DRHSIZE; hmi++) 
		for (dr = DnsResHash[hmi]; dr; dr = dr->dr_HNext) {
		    /* we count per-vserver, not global! */
		    if (strcmp(dres.dr_VServer, dr->dr_VServer) == 0) {
			++vcount;
			if (*dres.dr_AuthUser && *dr->dr_AuthUser &&
			    ((dres.dr_Flags ^ dr->dr_Flags) &
				DF_IGNORED_AUTH_LONG) == 0 &&
			    strcmp(dres.dr_AuthUser, dr->dr_AuthUser) == 0)
			    ++ucount;
			if (strcmp(dres.dr_ReaderName, dr->dr_ReaderName) == 0)
			    ++gcount;
		    }
		}
	}

	/*
	 * Tax the rate limit based on number of connections
	 */
	{
	    int count = 0;
	    if (ucount)
		count = ucount;
	    else if (icount)
		count = icount;
	    else if (hcount)
		count = hcount;
	    dres.dr_ConnCount = count;
	}

	/*
	 * Now we add the current connection before we print the totals
	 * and work out the limit restrictions
	 */
	gcount++;
	hcount++;
	ucount++;
	icount++;
	vcount++;

	if ((dres.dr_Flags & DF_QUIET) == 0)
	    logit(LOG_INFO, "counters %s%s%s%s%s%s %s %s tcount=%d/%d vcount=%d/%d gcount=%d/%d hcount=%d/%d ucount=%d/%d icount=%d byte=%d",
			(dres.dr_Flags & DF_IGNORED_AUTH_LONG ? "!" : ""),
			(dres.dr_AuthUser[0] ? dres.dr_AuthUser : ""),
			(dres.dr_AuthUser[0] ? "/" : ""),
			(dres.dr_IdentUser[0] ? dres.dr_IdentUser : ""),
			(dres.dr_IdentUser[0] ? "@" : ""),
			dres.dr_Host, dres.dr_ReaderDef->rd_Name,
			dres.dr_VServerDef->vs_Name,
			NumActive, dres.dr_ReaderDef->rd_MaxConnTotal,
			vcount, dres.dr_ReaderDef->rd_MaxConnPerVs,
			gcount, dres.dr_ReaderDef->rd_MaxConnPerGroup,
			hcount, dres.dr_ReaderDef->rd_MaxConnPerHost,
			ucount, dres.dr_ReaderDef->rd_MaxConnPerUser,
			icount, dres.dr_ReaderDef->rd_ByteLimit);

	if (dres.dr_ReaderDef->rd_MaxConnTotal &&
		NumActive > dres.dr_ReaderDef->rd_MaxConnTotal) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Server overload - too many connections (max %d)%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				dres.dr_ReaderDef->rd_MaxConnTotal,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromTotal";
#ifdef	READER_BAN_LISTS
	    if (GlobalBan.BanCount > 0)
		IPListAdd(&GlobalBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				GlobalBan.BanCount,
				time(NULL) + GlobalBan.BanTime,
				GlobalBan.BanHashSize,
				GlobalBan.BanLinkSize);
#endif
	}

	if (dres.dr_ReaderDef->rd_MaxConnPerVs &&
		vcount > dres.dr_ReaderDef->rd_MaxConnPerVs) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Vserver overload - too many connections (max %d)%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				dres.dr_ReaderDef->rd_MaxConnPerVs,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromVServer";
	}

	if (dres.dr_ReaderDef->rd_MaxConnPerGroup &&
		gcount > dres.dr_ReaderDef->rd_MaxConnPerGroup) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Too many connections in your group (max %d)%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				dres.dr_ReaderDef->rd_MaxConnPerGroup,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromGroup";
#ifdef	READER_BAN_LISTS
	    if (GroupBan.BanCount > 0)
		IPListAdd(&GroupBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				GroupBan.BanCount,
				time(NULL) + GroupBan.BanTime,
				GroupBan.BanHashSize,
				GroupBan.BanLinkSize);
#endif
	}

	if (dres.dr_ReaderDef->rd_MaxConnPerHost &&
		hcount > dres.dr_ReaderDef->rd_MaxConnPerHost) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Too many connections from your host (max %d)%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				dres.dr_ReaderDef->rd_MaxConnPerHost,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromHost";
#ifdef	READER_BAN_LISTS
	    if (HostBan.BanCount > 0)
		IPListAdd(&HostBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				HostBan.BanCount,
				time(NULL) + HostBan.BanTime,
				HostBan.BanHashSize,
				HostBan.BanLinkSize);
#endif
	}
	if (dres.dr_ReaderDef->rd_MaxConnPerUser &&
		(ucount > dres.dr_ReaderDef->rd_MaxConnPerUser ||
		 icount > dres.dr_ReaderDef->rd_MaxConnPerUser)) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Too many connections from your username (max %d)%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				dres.dr_ReaderDef->rd_MaxConnPerUser,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromUser";
#ifdef	READER_BAN_LISTS
	    if (UserBan.BanCount > 0)
		IPListAdd(&UserBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				UserBan.BanCount,
				time(NULL) + UserBan.BanTime,
				UserBan.BanHashSize,
				UserBan.BanLinkSize);
#endif
	}
	if (error1 == NULL && !(dres.dr_Flags & (DF_USEPROXIED|DF_HAPROXY))) {
	    dr = zalloc(&DnsResPool, sizeof(DnsRes));
	    *dr = dres;
	    dr->dr_HNext = DnsResHash[hi];
	    dr->dr_ReaderPid = reader->d_Pid;
	    DnsResHash[hi] = dr;
	}
    }

    /*
     * error return
     */

    if (error1 != NULL) {
	PendingClose *pc;
	if ((dres.dr_Flags & DF_QUIET) == 0) {
	    logit(
		LOG_NOTICE, 
		"connection to %s from %s%s%s (%s) rejected: %s", 
		vsname,
		(dres.dr_IdentUser[0] ? dres.dr_IdentUser : ""),
		(dres.dr_IdentUser[0] ? "@" : ""),
		dres.dr_Host,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 1, 1),
		error2
	    );
	}
	if (!DelayedClose) {
	    WriteBestEffort(desc->d_FdPend, error1, strlen(error1));
	    close(desc->d_FdPend);
	    desc->d_FdPend = -1;
	    ++FailCount;
	    --NumActive;
	    return;
	}

	/*
	 * Add the fd to the "pending close" list
	 * They will be closed after the DelayedClose timeout
	 */
	for (pc = NeedClosing; pc != NULL && pc->next != NULL; pc = pc->next);
	if (pc == NULL) {
	    pc = zalloc(&PendingMemPool, sizeof(PendingClose));
	    NeedClosing = pc;
	} else {
	    pc->next = zalloc(&PendingMemPool, sizeof(PendingClose));
	    pc = pc->next;
	}
	pc->fd = desc->d_FdPend;
	strncpy(pc->msgbuf, error1, sizeof(pc->msgbuf) - 1);
	pc->msgbuf[sizeof(pc->msgbuf) - 1] = '\0';
	pc->when = time(NULL) + DelayedClose;
	pc->quickclose = 0;
	pc->next = NULL;
	desc->d_FdPend = -1;
	deleteDnsResHash(&dres, desc);

	return;
    }

    /*
     * Leave NumActive bumped, transfer descriptor to subprocess
     *
     * pass the descriptor to an appropriate subprocess
     */

    if (SendMsg(reader->d_Fd, desc->d_FdPend, &dres) < 0) {
	char errbuf[1024];
#ifdef ETOOMANYREFS
	if (errno == ETOOMANYREFS) {
	    /* too many descriptors in flight: nothing was sent, the reader is fine */
	    DnsRefuseLog(DQR_INFLIGHT);
	    DnsTooBusy(desc->d_FdPend);
	    desc->d_FdPend = -1;
	    --NumActive;
	    deleteDnsResHash(&dres, reader);	/* stored under the reader */
	    return;
	}
#endif
	logit(LOG_ERR, 
	    "connect: error passing file descriptor: %s, killing reader pid %d", 
	    strerror(errno),
	    reader->d_Pid
	);
	if (DebugOpt)
	    printf("sendmsg() failed: %s\n", strerror(errno));
	snprintf(errbuf, sizeof(errbuf), "400 %s: Server error, fd pass failed%s%s\r\n",
	    DOpts.ReaderHostName,
	    (DOpts.NewsAdmin != NULL) ? " - " : "",
	    safestr(DOpts.NewsAdmin, ""));
	WriteBestEffort(desc->d_FdPend, errbuf, strlen(errbuf));
	close(desc->d_FdPend);
	desc->d_FdPend = -1;
	++FailCount;
	--NumActive;
	deleteDnsResHash(&dres, reader);

	if (reader->d_Pid > 0)
	    kill(reader->d_Pid, SIGQUIT);
	return;
    }
    close(desc->d_FdPend);
    desc->d_FdPend = -1;
    ++reader->d_Count;

    if ((dres.dr_Flags & DF_QUIET) == 0) {
	logit(LOG_INFO,
	    "connect to %s from %s%s%s%s%s%s (%s)%s",
	    dres.dr_VServerDef->vs_Name,
	    (dres.dr_Flags & DF_IGNORED_AUTH_LONG ? "!" : ""),
	    (dres.dr_AuthUser[0] ? dres.dr_AuthUser : ""),
	    (dres.dr_AuthUser[0] ? "/" : ""),
	    (dres.dr_IdentUser[0] ? dres.dr_IdentUser : ""),
	    (dres.dr_IdentUser[0] ? "@" : ""),
	    dres.dr_Host,
	    NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 1, 1),
	    (dres.dr_Flags & DF_ISPROXIED) > 0 ? " proxied=1" : ""
	);
    }
}

void
ValidateTcpBufferSize(int *psize)
{
    if (*psize != 0 && *psize < 512) {
	logit(LOG_INFO, "TCP buffer size of %d too small, setting to 512", *psize);
	*psize = 512;
    }
    if (*psize > 8*256*1024) {
	logit(LOG_INFO, "TCP buffer size of %d too big, setting to 2048K", *psize);
	*psize = 8*256*1024;
    }
}

void
deleteDnsResHash(DnsRes *dres, ForkDesc *desc)
{
    if (dres == NULL) {
	int i;

	for (i = 0; i < DRHSIZE; ++i) {
	    DnsRes **pdr = &DnsResHash[i];
	    DnsRes *dr;

	    while ((dr = *pdr) != NULL) {
		if (dr->dr_ReaderPid == desc->d_Pid) {
		    logit(
			LOG_NOTICE,
			"killed from %s%s%s%s%s%s (%s)",
			(dr->dr_Flags & DF_IGNORED_AUTH_LONG ? "!" : ""),
			(dr->dr_AuthUser[0] ? dr->dr_AuthUser : ""),
			(dr->dr_AuthUser[0] ? "/" : ""),
			(dr->dr_IdentUser[0] ? dr->dr_IdentUser : ""),
			(dr->dr_IdentUser[0] ? "@" : ""),
			dr->dr_Host,
			NetAddrToSt(0, (struct sockaddr *)&dr->dr_Addr, 1, 1, 1)
		    );
		    *pdr = dr->dr_HNext;
		    zfree(&DnsResPool, dr, sizeof(DnsRes));
		} else {
		    pdr = &dr->dr_HNext;
		}
	    }
	}
    } else {
	int hi = shash(NetAddrToSt(0, (struct sockaddr *)&dres->dr_Addr, 1, 0, 0)) & DRHMASK;
	DnsRes **pdr = &DnsResHash[hi];
	DnsRes *dr;

	while ((dr = *pdr) != NULL) {
	    if (dr->dr_ReaderPid == desc->d_Pid &&
		memcmp(&dr->dr_Addr, &dres->dr_Addr, sizeof(dr->dr_Addr)) == 0
	    ) {
		if ((dr->dr_Flags & DF_QUIET) == 0) {
		    logit(
			LOG_INFO,
#ifdef	DREADER_CLIENT_TIMING
			"closed from %s%s%s%s%s%s (%s) groups=%u arts=%u bytes=%.0f posts=%d postbytes=%d postfail=%d vserv=%s secs=%d utime=%.3f stime=%.3f%s",
#else
			"closed from %s%s%s%s%s%s (%s) groups=%u arts=%u bytes=%.0f posts=%d postbytes=%d postfail=%d vserv=%s secs=%d%s",
#endif
			(dr->dr_Flags & DF_IGNORED_AUTH_LONG ? "!" : ""),
			(dr->dr_AuthUser[0] ? dr->dr_AuthUser : ""),
			(dr->dr_AuthUser[0] ? "/" : ""),
			(dr->dr_IdentUser[0] ? dr->dr_IdentUser : ""),
			(dr->dr_IdentUser[0] ? "@" : ""),
			dr->dr_Host,
			NetAddrToSt(0, (struct sockaddr *)&dr->dr_Addr, 1, 1, 1),
			dres->dr_GrpCount,
			dres->dr_ArtCount,
			dres->dr_ByteCount,
			dres->dr_PostCount,
			dres->dr_PostBytes,
			dres->dr_PostFailCount,
			dres->dr_VServer,
#ifdef	DREADER_CLIENT_TIMING
			time(NULL) - dres->dr_TimeStart,
			desc->d_stime/ticksPerSecond,
			desc->d_utime/ticksPerSecond,
#else
			time(NULL) - dres->dr_TimeStart,
#endif
			(dres->dr_Flags & DF_ISPROXIED) > 0 ? " proxied=1" : ""
		    );
		}
		if (dr->dr_Flags & DF_FEEDONLY) {
		    TotalFeedArticles += dres->dr_PostCount;
		    TotalFeedBytes += dres->dr_PostBytes;
		    TotalFeedArtFail += dres->dr_PostFailCount;
		} else {
		    TotalClientGroups += dres->dr_GrpCount;
		    TotalClientArticles += dres->dr_ArtCount;
		    TotalClientArtBytes += dres->dr_ByteCount;
		    TotalClientPosts += dres->dr_PostCount;
		    TotalClientPostBytes += dres->dr_PostBytes;
		    TotalClientPostFail += dres->dr_PostFailCount;
		}
		*pdr = dr->dr_HNext;
		zfree(&DnsResPool, dr, sizeof(DnsRes));
		break;
	    } else {
		pdr = &dr->dr_HNext;
	    }
	}
    }
}

int
getReaderForkSlot(void)
{
    int i;

    for (i = 0; i < DOpts.ReaderForks + DOpts.ReaderFeedForks; ++i) {
	if (ReaderPids[i] == 0)
	    return(i);
    }
    return(0); /* XXX panic */
}

void
delReaderForkSlot(pid_t pid)
{
    int i;

    for (i = 0; i < DOpts.ReaderForks + DOpts.ReaderFeedForks; ++i) {
	if (ReaderPids[i] == pid)
	    ReaderPids[i] = 0;
    }
}

/*
 * AvailableCpus - CPUs this process may use, for 'readerforks auto'.
 *
 * Linux: the affinity mask (taskset, cpusets), lowered to a cgroup v2
 * CPU quota.  FreeBSD: the cpuset of the process.  Elsewhere, or when
 * that fails: the CPUs online.  At least 1.
 */
static int
AvailableCpus(void)
{
    int n = 0;

#if defined(__linux__) && defined(CPU_COUNT)
    {
	cpu_set_t set;

	CPU_ZERO(&set);
	if (sched_getaffinity(0, sizeof(set), &set) == 0)
	    n = CPU_COUNT(&set);
    }
#elif defined(__FreeBSD__) && defined(CPU_COUNT)
    {
	cpuset_t set;

	CPU_ZERO(&set);
	if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
						sizeof(set), &set) == 0)
	    n = CPU_COUNT(&set);
    }
#endif
#ifdef _SC_NPROCESSORS_ONLN
    if (n <= 0) {
	long c = sysconf(_SC_NPROCESSORS_ONLN);

	if (c > 0 && c <= READERFORKS_MAX)
	    n = (int)c;
	else if (c > READERFORKS_MAX)
	    n = READERFORKS_MAX;
    }
#endif
#ifdef __linux__
    {
	int q = CgroupCpuLimit();

	if (q > 0 && (n <= 0 || q < n))
	    n = q;
    }
#endif
    return((n > 0) ? n : 1);
}

#ifdef __linux__
/*
 * CgroupCpuLimit - the smallest cgroup v2 cpu.max quota on the path from
 * our cgroup to the root, rounded up to whole CPUs; 0 when none is set.
 */
static int
CgroupCpuLimit(void)
{
    static const char root[] = "/sys/fs/cgroup";
    char path[PATH_MAX];
    char line[PATH_MAX];
    FILE *fi;
    int best = 0;

    if ((fi = fopen("/proc/self/cgroup", "r")) == NULL)
	return(0);
    path[0] = 0;
    while (fgets(line, sizeof(line), fi) != NULL) {
	if (strncmp(line, "0::/", 4) == 0) {
	    line[strcspn(line, "\n")] = 0;
	    if (snprintf(path, sizeof(path), "%s%s", root, line + 3) >=
							(int)sizeof(path))
		path[0] = 0;
	    break;
	}
    }
    fclose(fi);

    while (path[0]) {
	char file[PATH_MAX + 16];
	long long quota;
	long long period;
	char *p;

	snprintf(file, sizeof(file), "%s/cpu.max", path);
	if ((fi = fopen(file, "r")) != NULL) {
	    /* "max 100000" (no limit) or "<quota> <period>" */
	    if (fscanf(fi, "%lld %lld", &quota, &period) == 2 &&
						quota > 0 && period > 0) {
		long long c = (quota + period - 1) / period;

		if (c > READERFORKS_MAX)
		    c = READERFORKS_MAX;
		if (best == 0 || c < best)
		    best = (int)c;
	    }
	    fclose(fi);
	}
	if ((p = strrchr(path, '/')) == NULL || p < path + sizeof(root) - 1)
	    break;
	*p = 0;
    }
    return(best);
}
#endif

typedef struct vscount {
	Vserver *vserver;
	int count;
	struct vscount *next;
} vscount;

typedef struct rdcount {
	ReaderDef *reader;
	int count;
	struct vscount *vslist;
	struct rdcount *next;
} rdcount;


MemPool *StatsPool;

void
addVSlist(vscount **vslistp, DnsRes *dr)
{
    vscount *vslist = *vslistp;
    vscount *vs;
    vscount *pvs = NULL;

    if (vslist == NULL) {
	vslist = zalloc(&StatsPool, sizeof(vscount));
	vslist->vserver = dr->dr_VServerDef;
	vslist->count = 1;
	vslist->next = NULL;
	*vslistp = vslist;
    } else {
	for (vs = vslist; vs != NULL; pvs = vs, vs = vs->next) {
	    if (vs->vserver == dr->dr_VServerDef) {
		vs->count++;
		break;
	    }
	}
	if (vs == NULL) {
	    vs = zalloc(&StatsPool, sizeof(vscount));
	    vs->vserver = dr->dr_VServerDef;
	    vs->count = 1;
	    vs->next = NULL;
	    pvs->next = vs;
	}
    }
}

void
getVStats(FILE *fo)
{
    int hmi;
    DnsRes *dr;
    int vcount = 0;
    int rcount = 0;
    rdcount *rdlist = NULL;
    vscount *vslist = NULL;
    vscount *vs;
    rdcount *rd;
    rdcount *prd = NULL;

    for (hmi=0; hmi < DRHSIZE; hmi++) 
	for (dr = DnsResHash[hmi]; dr; dr = dr->dr_HNext) {
	    SetAuthDetails(dr, dr->dr_ReaderName);
	    if (rdlist == NULL) {
		rdlist = zalloc(&StatsPool, sizeof(rdcount));
		rdlist->reader = dr->dr_ReaderDef;
		rdlist->count = 1;
		rdlist->next = NULL;
		rdlist->vslist = NULL;
		addVSlist(&rdlist->vslist, dr);
	    } else {
		for (rd = rdlist; rd != NULL; prd = rd, rd = rd->next) {
		    if (rd->reader == dr->dr_ReaderDef) {
			addVSlist(&rd->vslist, dr);
			rd->count++;
			break;
		    }
		}
		if (rd == NULL) {
		    rd = zalloc(&StatsPool, sizeof(rdcount));
		    rd->reader = dr->dr_ReaderDef;
		    rd->count = 1;
		    rd->next = NULL;
		    rd->vslist = NULL;
		    addVSlist(&rd->vslist, dr);
		    prd->next = rd;
		}
	    }
	    addVSlist(&vslist, dr);
	}
    rcount = 0;
    for (rd = rdlist; rd != NULL; prd = rd, rd = rd->next) {
	for (vs = rd->vslist; vs != NULL; vs = vs->next)
	    fprintf(fo, "reader %s=%d vs %s=%d\n",
			rd->reader->rd_Name, rd->count,
			vs->vserver->vs_Name, vs->count);
	rcount++;
    }
    fprintf(fo, "readergroups in use = %d\n", rcount);
    vcount = 0;
    for (vs = vslist; vs != NULL; vs = vs->next) {
	fprintf(fo, "vserver %s=%d\n", vs->vserver->vs_Name, vs->count);
	vcount++;
    }
    fprintf(fo, "vservers in use = %d\n", vcount);
    freePool(&StatsPool);
}

void
getStats(FILE *fo, int raw)
{
    fprintf(fo, "UPTIME %d\n", (int)(time(NULL) - TimeStart));
    fprintf(fo, "SERVER Connect=%d Failed=%d Dns=%d/%d Act=%d/%d\n", 
		ConnectCount, FailCount,
		NumPending, DOpts.ReaderDns, 
		NumActive, MaxConnects
    );
    if (raw) {
	fprintf(fo, "CLIENT Groups=%.0f Articles=%.0f Bytes=%.0f\n", 
		TotalClientGroups, TotalClientArticles, TotalClientArtBytes
	);
	fprintf(fo, "POST Posts=%.0f PostBytes=%.0f PostsFailed=%.0f\n", 
		TotalClientPosts, TotalClientPostBytes, TotalClientPostFail
	);
	fprintf(fo, "SPOOL Articles=%.0f Bytes=%.0f\n", 
		TotalSpoolArticles, TotalSpoolBytes
	);
	fprintf(fo, "FEED Articles=%.0f Bytes=%.0f ArtFail=%.0f\n", 
		TotalFeedArticles, TotalFeedBytes,
		TotalFeedArtFail
	);
    } else {
	fprintf(fo, "CLIENT Groups=%.0f Articles=%.0f Bytes=%s\n", 
		TotalClientGroups, TotalClientArticles,
		ftos(TotalClientArtBytes)
	);
	fprintf(fo, "POST Posts=%.0f PostBytes=%s PostsFailed=%.0f\n", 
		TotalClientPosts, ftos(TotalClientPostBytes),
		TotalClientPostFail
	);
	fprintf(fo, "SPOOL Articles=%.0f Bytes=%s\n", 
		TotalSpoolArticles, ftos(TotalSpoolBytes)
	);
	fprintf(fo, "FEED Articles=%.0f Bytes=%s ArtFail=%.0f\n", 
		TotalFeedArticles, ftos(TotalFeedBytes),
		TotalFeedArtFail
	);
    }
}

#ifdef	READER_BAN_LISTS

void
SetBanList(char *ban)
{
    BanInfo *bi = NULL;

    if (ban == NULL)
	return;
    while (*ban) {
	switch (*ban) {
	    case ' ':
	    case '\t':
		ban++;
		break;
	    case 'U':
		bi = &UserBan;
		ban++;
		break;
	    case 'H':
		bi = &HostBan;
		ban++;
		break;
	    case 'G':
		bi = &GroupBan;
		ban++;
		break;
	    case 'L':
		bi = &GlobalBan;
		ban++;
		break;
	    case 's':
		bi->BanHashSize = atoi(++ban);
		while (isdigit((unsigned char)*ban))
		    ban++;
		break;
	    case 'l':
		bi->BanLinkSize = atoi(++ban);
		while (isdigit((unsigned char)*ban))
		    ban++;
		break;
	    case 'c':
		bi->BanCount = atoi(++ban);
		while (isdigit((unsigned char)*ban))
		    ban++;
		break;
	    case 't':
		bi->BanTime = atoi(++ban);
		while (isdigit((unsigned char)*ban))
		    ban++;
		break;
	    default:
		logit(LOG_ERR, "Unknown readerban option '%s'", ban);
		return;
	}
    }
}

int
Banned(char *ip, time_t t)
{
    if (UserBan.BanCount > 0 &&
		IPListCheck(UserBan.BanList, ip, 1, t + UserBan.BanTime, t, UserBan.BanHashSize) == 0)
	return(1);
    if (HostBan.BanCount > 0 &&
		IPListCheck(HostBan.BanList, ip, 1, t + HostBan.BanTime, t, HostBan.BanHashSize) == 0)
	return(1);
    if (GroupBan.BanCount > 0 &&
		IPListCheck(GroupBan.BanList, ip, 1, t + GroupBan.BanTime, t, GroupBan.BanHashSize) == 0)
	return(1);
    if (GlobalBan.BanCount > 0 &&
		IPListCheck(GlobalBan.BanList, ip, 1, t + GlobalBan.BanTime, t, GlobalBan.BanHashSize) == 0)
	return(1);
    return(0);
}

void
DumpBannedLists(FILE *fo)
{
    if (UserBan.BanCount > 0) {
	fprintf(fo, "User bans:\n");
	IPListDump(fo, UserBan.BanList, UserBan.BanHashSize);
    }
    if (HostBan.BanCount > 0) {
	fprintf(fo, "Host bans:\n");
	IPListDump(fo, HostBan.BanList, HostBan.BanHashSize);
    }
    if (GroupBan.BanCount > 0) {
	fprintf(fo, "Group bans:\n");
	IPListDump(fo, GroupBan.BanList, GroupBan.BanHashSize);
    }
    if (GlobalBan.BanCount > 0) {
	fprintf(fo, "Global bans:\n");
	IPListDump(fo, GlobalBan.BanList, GlobalBan.BanHashSize);
    }
}

void
DumpBannedConfigs(FILE *fo)
{
    if (UserBan.BanCount > 0)
	fprintf(fo, "user  : count=%10d  time=%ld\n", UserBan.BanCount,
							UserBan.BanTime);
    if (HostBan.BanCount > 0)
	fprintf(fo, "host  : count=%10d  time=%ld\n", HostBan.BanCount,
							HostBan.BanTime);
    if (GroupBan.BanCount > 0)
	fprintf(fo, "group : count=%10d  time=%ld\n", GroupBan.BanCount,
							GroupBan.BanTime);
    if (GlobalBan.BanCount > 0)
	fprintf(fo, "global: count=%10d  time=%ld\n", GlobalBan.BanCount,
							GlobalBan.BanTime);
}

#endif	/* READER_BAN_LISTS */
