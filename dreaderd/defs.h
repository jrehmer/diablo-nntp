
#include <lib/defs.h>
#include <sys/times.h>
#include <sys/uio.h>
#ifdef	USE_PRCTL
#include <sys/resource.h>
#include <sys/prctl.h>
#endif

#define DRHSIZE		256
#define DRHMASK		(DRHSIZE-1)

/*
 * MSG_ flags that may not be implemented on a particular OS
 */

#ifndef MSG_WAITALL
#define MSG_WAITALL	0
#endif
#ifndef MSG_EOR
#define MSG_EOR		0
#endif

struct ServReq;
struct Timer;
struct DnsRes;

#define THREAD_LISTEN	1	/* listening thread		*/
#define THREAD_DNS	2	/* dns resolver thread		*/
#define THREAD_READER	3	/* reader thread		*/
#define THREAD_SDNS	4	/* server dns resolver thread	*/
#define THREAD_DRCMD	5	/* drcmd listener thread	*/

#define THREAD_NNTP	6	/* nntp connection		*/
#define THREAD_SPOOL	7	/* spool connection		*/
#define THREAD_POST	8	/* outgoing post		*/
#define THREAD_FEEDER	9	/* feeder thread		*/

#define OVERVIEW_FMT	"Subject:\r\nFrom:\r\nDate:\r\nMessage-ID:\r\nReferences:\r\nBytes:\r\nLines:\r\nXref:full\r\n"
#define DEFMAXARTSINGROUP		1024
#define DEFARTSINGROUP			512
#define OV_CACHE_MAX			8	/* readerovcache default, and its floor */
#define MAX_OVERVIEW_CACHE_REGIONS      2       /* 256x2 = 512 articles   */
#define MAX_CROSS			64

/*
 * THREAD_QSIZE is the number of spool requests that can be queued to
 * a spool thread before we back off to a higher d_Pri.
 *
 * THREAD_PSIZE is the number of post requests that can be queued to
 * a spool thread before we back off to a higher d_Pri.
 */

#define THREAD_QSIZE	16	/* for spool access		*/
#define THREAD_PSIZE	2	/* for posting			*/
#define THREAD_LIMSIZE	(((THREAD_QSIZE > THREAD_PSIZE) ? THREAD_QSIZE : THREAD_PSIZE) * 2)

#ifndef INET6
typedef struct sockaddr sockaddr_storage;
#endif

/*
 * ForkDesc - structure used to manage descriptors for select() threads
 */

typedef struct ForkDesc {
    struct Timer *d_Timer;	/* active timer		*/
    struct ForkDesc *d_Next;	/* linked list @ pri	*/ 
    struct ForkDesc *d_Prev;	/* linked list @ pri	*/ 
    char	*d_Id;		/* host name 		*/
    pid_t	d_Pid;
    int		d_Slot;		/* status reporting slot*/
    int		d_Fd;
    int		d_Type;
    int		d_FdPend;	/* save pending fd	*/
    int		d_Count;	/* active subthreads	*/
    int		d_Pri;
    void	*d_Data;
    struct DnsRes *d_DnsRes;	/* partial DNS reply (THREAD_DNS only) */
    size_t	d_DnsRead;	/* number of reply bytes accumulated */
    int		d_Dead;		/* worker failed; never pick it, wait3() reaps */
    struct ReaderOut *d_OutQ;	/* main: clients still to pass to this reader */
    struct ReaderOut **d_OutTail;
    double	d_OutRetryMs;	/* main: after ENOBUFS, retry from then on */
    double	d_SendMs;	/* when the last DNS request went out (ms) */
    int		d_SendKnown;	/* ... and needed no lookups */
    struct sockaddr_storage d_SaveRSin;
    char	*d_LocalSpool;	/* direct spool access	*/
    int		d_Cache;	/* is spooler to be locally cached */
    int		d_CacheMin;	/* min article size to be cached */
    int		d_CacheMax;	/* max article size to be cached */
    int		d_CacheableTime;	/* oldest article to be cached */
    int		d_Timeout;	/* server nonresponsiveness timeout */
    int		d_Timewarn;	/* server slow responsiveness timer */
    double	d_ReadNewRatio;	/* read/new articles ratio to reach to cache a group */
    double	d_CacheReadRatio;	/* cachehits/read articles ratio to reach to cache a group */
#ifdef	DREADER_CLIENT_TIMING
    clock_t	d_utime;
    clock_t	d_stime;
#endif
} ForkDesc;

/*
 * Authentication structures
 */

/*
 * The current output mode of the server to the client
 */
#define	DRBC_NONE	0
#define	DRBC_ARTICLE	1
#define	DRBC_HEAD	2
#define	DRBC_BODY	3
#define	DRBC_LIST	4
#define	DRBC_XOVER	5
#define	DRBC_XHDR	6
#define	DRBC_XZVER	7
#define DRBC_XXXX	8	/* The last of the DRBC_ defines */

#define	OPT_NONE	0
#define	OPT_VSERVER	1
#define	OPT_GROUPS	2
#define	OPT_AUTH	3
#define	OPT_READERGRP	4

#define DEFNAMELEN	128

#define MAXVSINTERFACE	4

typedef struct Vserver {		/* Define a virtual server	*/
    char vs_Name[DEFNAMELEN];
    char vs_ClusterName[64];
    char vs_HostName[64];
    char vs_PostPath[64];
    char vs_NewsAdm[64];
    char vs_Org[128];
    char vs_AbuseTo[64];
    char vs_CryptPw[64];
    char vs_Interface[MAXVSINTERFACE][48];
    char vs_AccessFile[128];
    int vs_NoXrefHostUpdate;
    int vs_NoReadPath;
    char vs_Welcome[256];
    char vs_Comments[256];
    char vs_PostTrailer[256];	/* unused; keep the access-cache layout */
    char vs_CanLockAdmin[CANLOCK_MAXSECRETS][CANLOCK_SECRETLEN];	/* every one makes locks */
    char vs_CanLockUser[CANLOCK_MAXSECRETS][CANLOCK_SECRETLEN];
    int vs_NoCanLockSha1;		/* canlocksha1 no: sha256 only */
    int vs_Rfc5537;		/* rfc5537 yes: post as RFC 5536/5537 want (F-07); 0: the old way */
    int vs_ShowPoster;		/* showposter yes: Injection-Info names the login */
    int vs_HidePostHost;	/* showposthost no: Injection-Info has no posting-host */
} Vserver;

typedef struct GroupDef {		/* Define a group access list	*/
    char *gr_Name;
    int  gr_Count;
    struct GroupList *gr_Groups;
    struct GroupDef *gr_Next;
} GroupDef;

typedef struct AuthDef {		/* Define an authentication type */
    char au_Name[DEFNAMELEN];
    char au_File[255];
    char au_Cdb[255];
    char au_Db[255];
    char au_External[255];
    char au_Radius[255];
    char au_User[255];
    char au_Pass[255];
    char au_Realm[255];
    char au_AddRealm[255];
    char au_NetRemote[255];
    char au_LDAP[255];		/* LDAP is gone: keeps the access-cache layout; set only in an older cache, then logins are refused */
    char au_Perl[255];
    char au_PAM[255];
    int au_Ident;
} AuthDef;

#define MAXPROXYFROM	8

typedef struct ProxyFrom {		/* trusted proxy address or network	*/
    int pf_Family;			/* AF_INET or AF_INET6			*/
    unsigned char pf_Addr[16];
    int pf_Bits;			/* prefix length			*/
} ProxyFrom;

typedef struct ReaderDef {		/* Define a reader group	*/
    char rd_Name[DEFNAMELEN];
    char rd_Auth[DEFNAMELEN];
    char rd_Groups[DEFNAMELEN];
    char rd_ListGroups[DEFNAMELEN];
    char rd_PostGroups[DEFNAMELEN];
    char rd_Vserver[DEFNAMELEN];
    int rd_Read;
    int rd_Post;
    int rd_Feed;
    int rd_Status;
    int rd_UseProxied;
    int rd_HaProxy;
    int rd_Quiet;
    int rd_ControlPost;
    int rd_MaxConnTotal;
    int rd_MaxConnPerHost;
    int rd_MaxConnPerUser;
    int rd_MaxConnPerGroup;
    int rd_MaxConnUniqueHostLimit;
    int rd_MaxConnPerVs;
    int rd_RateLimit[DRBC_XXXX];
    int rd_RateLimitTax;
    int rd_RateLimitRangeLow;
    int rd_RateLimitRangeHigh;
    int rd_ByteLimit;
    int rd_PathComponents;
    int rd_GroupLog;
    int rd_UseVerifiedDns;
    int rd_DenyMismatchedDns;
    int rd_DenyNoDns;
    int rd_AllowNewnews;
    int rd_SpoolHeaders;
    int rd_IgnoreAuthInfo;
    int rd_CheckPostGroups;
    int rd_AllowApprove;	/* may post with Approved: (approve moderated) */
    int rd_LogCmd;
#ifdef	IP_TOS
    int rd_SetTOS;
#endif
    time_t rd_IdleTimeout;
    time_t rd_SessionTimeout;
    int rd_RxBufSize;
    int rd_TxBufSize;
    int rd_MaxAgePolicy;
    int rd_TurnOffNNPH;
    int rd_XzverLevel;
    int rd_RequireGroupCmd;
    int rd_AddCanLockUser;		/* CANLOCK_USER_*: user in the Cancel-Lock key */
    int rd_NProxyFrom;			/* proxyfrom entries; 0 = any peer */
    ProxyFrom rd_ProxyFrom[MAXPROXYFROM];
} ReaderDef;

#define CANLOCK_USER_NAME	0	/* the AUTHINFO user (default) */
#define CANLOCK_USER_IP		1	/* the client's IP address */
#define CANLOCK_USER_NONE	2	/* no user lock, only the admin lock */

typedef struct AccessDef {		/* Define the access list	*/
    char ad_Pattern[100];
    char ad_IdentUser[16];
    char ad_Reader[DEFNAMELEN];
    int ad_MatchExit;
} AccessDef;

typedef struct AccessMap {		/* Define an access mapping	*/
    Vserver *VServerList;
    int VServerCount;
    GroupDef *GroupsList;
    int GroupCount;
    void *GroupMap;
    long GroupMapSize;
    AuthDef *AuthList;
    int AuthCount;
    ReaderDef *ReaderList;
    int ReaderCount;
    AccessDef *AccessList;
    int AccessCount;
} AccessMap;

/*
 * DnsReq/DnsRes - structures used to manage DNS lookups
 *
 * At this point, I'm having doubts as to the wisdom of using
 * two structures, and wonder if it wouldn't be more prudent to
 * use DnsRes to talk to DnsTest.  Unless dreaderd is rewritten
 * with real threads, using DnsTest to handle arbitrary time-
 * intensive tasks such as dealing with auth is very attractive,
 * and will eventually end up requiring many DnsRes vars to be
 * stuffed into DnsReq.  Yech.  JG200106061214
 */

/*
 * Name lookup result of a client address (dr_Lookup).  Main passes a known
 * result (DNS cache, or the connect-time result on AUTHINFO) in DnsReq and
 * the worker then skips the reverse and forward lookups.
 */
#define DNSHOSTLEN	256		/* longest name we pass (DNS: 253) */
#define DL_NONE		0		/* unknown: worker looks it up	*/
#define DL_HOST		1		/* name, forward lookup confirms */
#define DL_MISMATCH	2		/* name, forward does not confirm */
#define DL_NOHOST	3		/* no reverse name		*/
#define DL_MASK		0x0f
#define DL_FRESH	0x10		/* worker did the lookups now	*/
#define DL_TEMP		0x20		/* temporary failure: worker only */

typedef struct DnsReq {
    struct sockaddr_storage dr_LSin;	/* local interface		*/
    struct sockaddr_storage dr_RSin;	/* remote ip:port		*/
    char dr_AuthUser[64];		/* authinfo user		*/
    char dr_AuthPass[64];		/* authinfo pass		*/
    int dr_Lookup;			/* DL_*: known lookup result	*/
    char dr_RevHost[DNSHOSTLEN];	/* its name (DL_HOST/MISMATCH)	*/
    unsigned int dr_CacheGen;		/* DNS cache flush generation	*/
    int dr_Flags;			/* flags to pass res->req->res	*/
    int dr_ResultFlags;			/* see #defines below		*/
    double dr_ByteCount;		/* how many bytes sent - stats	*/
    int dr_GrpCount;			/* number of groups entered	*/
    int dr_ArtCount;			/* number of articles fetched	*/
    double dr_PostBytes;		/* how many bytes posted 	*/
    int dr_PostCount;			/* number of articles posted	*/
    int dr_PostFailCount;		/* number of failed postings	*/
    double dr_ByteCountArticle;		/* Bytes sent for art fetches	*/
    double dr_ByteCountHead;		/* Bytes sent for head fetches	*/
    double dr_ByteCountBody;		/* Bytes sent for body fetches	*/
    double dr_ByteCountList;		/* Bytes sent for list fetches	*/
    double dr_ByteCountXover;		/* Bytes sent for xover fetches	*/
    double dr_ByteCountXhdr;		/* Bytes sent for xhdr fetches	*/
    double dr_ByteCountOther;		/* Bytes sent for other fetches	*/
    time_t dr_SessionLength;		/* duration of session in secs	*/
    char dr_ReaderName[DEFNAMELEN];	/* session end: its readerdef	*/
} DnsReq;

/*
 * Define some values used in dr_ResultFlags
 */
#define	RECV_FD_LOST	(-2)		/* RecvMsg: message whole, fd dropped */
#define	DR_REQUIRE_DNS	0x0001		/* authinfo check required	*/
#define	DR_SERVER_STATS	0x0002		/* struct has info server stats	*/
#define	DR_SESSEXIT_RPT	0x0004		/* reporting session term stats	*/

typedef struct DnsRes {
    struct DnsRes *dr_HNext;		/* main process hash link	*/
    int dr_Code;			/* the auth result code		*/
    int dr_Flags;			/* what options are enabled	*/
    int dr_ArtCount;			/* how many articles fetched	*/
    int dr_PostCount;			/* how many articles posted	*/
    double dr_PostBytes;		/* how many bytes posted	*/
    int dr_PostFailCount;		/* how many posts failed	*/
    double dr_ByteCount;		/* how many bytes sent		*/
    int dr_GrpCount;			/* how many groups accessed	*/
    double dr_ByteCountArticle;		/* Bytes sent for art fetches	*/
    double dr_ByteCountHead;		/* Bytes sent for head fetches	*/
    double dr_ByteCountBody;		/* Bytes sent for body fetches	*/
    double dr_ByteCountList;		/* Bytes sent for list fetches	*/
    double dr_ByteCountXover;		/* Bytes sent for xover fetches	*/
    double dr_ByteCountXhdr;		/* Bytes sent for xhdr fetches	*/
    double dr_ByteCountOther;		/* Bytes sent for other fetches	*/
    time_t dr_TimeStart;		/* Time connection was made	*/
    int dr_ReaderPid;			/* process ID of reader process	*/
    struct sockaddr_storage dr_Addr;	/* remote IP address and port	*/
    char dr_Host[64];			/* the remote hostname		*/
    char dr_ReaderName[DEFNAMELEN];	/* the reader group name	*/
    char dr_VServer[DEFNAMELEN];	/* the virtual server		*/
    char dr_IdentUser[16];		/* the ident username		*/
    char dr_AuthUser[64];		/* the authinfo user		*/
    char dr_AuthPass[64];		/* the authinfo pass		*/
    int dr_Lookup;			/* DL_*: name lookup result	*/
    char dr_RevHost[DNSHOSTLEN];	/* reverse name (DL_HOST/MISMATCH) */
    struct sockaddr_storage dr_LookupAddr; /* address it is for	*/
    unsigned int dr_CacheGen;		/* from the DnsReq		*/
    int dr_DnsMismatch;			/* Fwd/Rev DNS don't match	*/
    int dr_ResultFlags;			/* see #defines above		*/
    int dr_StaticAuth;			/* don't change auth details	*/
    int dr_ConnCount;			/* for rate limit calculation	*/
    ReaderDef *dr_ReaderDef;		/* reader group	pointer		*/
    Vserver *dr_VServerDef;		/* vserver pointer		*/
    GroupDef *dr_GroupDef;		/* group list pointer		*/
    GroupDef *dr_ListGroupDef;		/* list group list pointer	*/
    GroupDef *dr_PostGroupDef;		/* post group list pointer	*/
    AuthDef *dr_AuthDef;		/* auth def pointer		*/
    int dr_AuthTimeout;			/* backend that ran out of time	*/
} DnsRes;

/*
 * After its alarm cut an auth backend off, a DNS worker sends its reply
 * with DR_AUTHTO_EXIT in dr_AuthTimeout and exits with this status.
 */
#define DNS_EXIT_AUTHTIMEOUT	3
#define DR_AUTHTO_EXIT		0x100

#define DF_FEED		0x00000001	/* can feed			*/
#define DF_READ		0x00000002	/* can read			*/
#define DF_POST		0x00000004	/* can post			*/
#define DF_STATUS	0x00000008	/* only want status report	*/
#define DF_QUIET	0x00000010	/* don't log			*/
#define DF_CONTROLPOST	0x00000020	/* allowed to post Control:	*/
#define DF_AUTHREQUIRED	0x00000040	/* authentication required	*/
#define DF_FEEDONLY	0x00000080 	/* feed-only thread		*/
#define	DF_GROUPLOG	0x00000100	/* log groups accessed by user  */
#define	DF_AUTH		0x00000200	/* user valid with AUTHINFO	*/
#define	DF_USEPROXIED	0x00000400	/* Use proxy-supplied IP	*/
#define	DF_HAPROXY	0x00000800	/* Ditto, HAPROXY protocol	*/
#define	DF_ISPROXIED	0x00001000	/* This is a proxied session	*/
#define DF_IGNORED_AUTH_LONG 0x00002000 /* unprefixed, full-length ignored AUTHINFO user */

/* Flags to pass from DnsRes -> DnsReq -> DnsRes */
#define DF_RESREQRES	DF_ISPROXIED

/*
 * MBuf - structure used to manage read and write buffers
 */

typedef struct MBuf {
    struct MBuf *mb_Next;
    char	*mb_Buf;
    int		mb_Index;	/* finished index		*/
    int		mb_NLScan;	/* newline scan index 		*/
    int		mb_Size;
    int		mb_Max;
} MBuf;

typedef struct MBufHead {
    MBuf	*mh_MBuf;
    MemPool	**mh_MemPool;
    MemPool	**mh_BufPool;
    int		mh_Bytes;
    int		mh_MaxLine;	/* longest line MBReadLine() takes, 0 = any */
    int		mh_Wait;
    int		mh_Fd;
    double	mh_TotalBytes;
    char	mh_REof;
    char	mh_WEof;
    char	mh_RError;
    char	mh_WError;
    char	mh_SkipLong;	/* caller wants a long line skipped, not EOF */
    char	mh_Skip;	/* dropping the rest of a long line */
    char	mh_LongLine;	/* MBReadLine() dropped a long line */
    char	mh_LongCR;	/* the dropped long line ended with CR LF */
    char	mh_NoNL;	/* its last line ended at EOF, not with a newline */
    int		mh_RErrno;	/* errno of a read() error taken as EOF, 0 = none */
} MBufHead;

/*
 * Timer
 */

typedef struct Timer {
    struct Timer   *ti_Next;
    struct Timer   *ti_Prev;
    struct ForkDesc *ti_Desc;
    struct timeval ti_To;	/* requested timeout		*/
    struct timeval ti_Tv;	/* absolute time of timeout	*/
    int		ti_Flags;
} Timer;

#define TIF_READ	0x01
#define TIF_WRITE	0x02

typedef struct TimeRestrict {
    time_t	     tr_Time;
} TimeRestrict;

#define MAX_HDR_CC	0x08

#define LF		0x01
#define CR		0x02
#define CRLF		0x03
#define FLAG_PATH	0x01
#define FLAG_XREF	0x02
#define FLAG_NEEDED	0x03

#define INBODY		-1
#define JMPBDY		-2
#define HDRCPY		-3
#define HDRDEL		-4
#define WRDDEL		-5
#define PATHCHK		-6

#define BLOCK_OK	0
#define BLOCK_ERROR	1
#define BLOCK_END	2

#ifdef __linux__
#define USE_AIO		0
#define USE_LINUX_KAIO	0
#else
#define USE_AIO		0
#endif

/*
 * Spool read buffer for local article fetches.  NNSLA_Loop reads an article
 * through it in one blocking loop, so a larger buffer only means fewer read()
 * calls per article; each reader process holds about one DFA at a time.
 */
#define DFA_BUFSIZE	65536

/*
 * What follows "Path:" in a header line, held back until it is known whether
 * this site's name is first in the value (dfa.c): up to DFA_PATHWS characters
 * of white space, then up to 64 of the value (the cluster name is shorter).
 */
#define DFA_PATHWS	256
#define DFA_PATHBUF	(DFA_PATHWS + 66)

typedef struct DirectFileAccess {
#if USE_AIO
    struct aiocb *dfa_AIOcb;
    int		lock;
#else
    int		dfa_Fd;
#endif
    int		dfa_Size;
    char	dfa_Buffer[DFA_BUFSIZE];
    char	dfa_Field[MAX_HDR_CC];
    char	dfa_PBuf[DFA_PATHBUF];
    short	dfa_PLen;
    short	dfa_PLead;	/* white space held in front of the value */
    signed char	dfa_PInLead;	/* still in that white space */
    signed char	dfa_POut;	/* some of it was written already */
    /*
     * Parser states: both hold the negative codes above (INBODY ...
     * WRDDEL) and are compared with < 0, so they must be signed.  Plain
     * char is unsigned on arm64, ppc and s390x, where NNCopyLocalArticle
     * never matched those cases and rejected every local article.
     */
    signed char	dfa_InHdr;
    signed char	dfa_LF;
    char	dfa_Flag;
    struct DirectFileAccess *dfa_Next;
} DirectFileAccess;

/*
 * Connection - structure used to manage an NNTP connection
 *
 * note(1): Server requests are attached in two places:  The co_SReq in a 
 *	    THREAD_NNTP connection and the co_SReq in a THREAD_SPOOL or
 *	    THREAD_POST connection.
 */

#define CACHE_OFF		0x0000
#define CACHE_ON		0x0001
#define CACHE_LAZY		0x0002
#define CACHE_SCOREBOARD	0x0003

typedef struct Connection {
    ForkDesc	*co_Desc;
    void	(*co_Func)(struct Connection *conn);
    int		(*co_ArtFuncHead)(struct Connection *conn, char *buf, int len);
    int		(*co_ArtFuncBody)(struct Connection *conn, char *buf, int len);
    const char	*co_State;
    MemPool	*co_MemPool;
    MemPool	*co_BufPool;
    MBufHead	co_TMBuf;
    MBufHead	co_RMBuf;
#ifdef USE_ZLIB
    z_streamp	co_ZStream;
#else
    void	*co_ZStream;	/* unused without zlib (no XZVER) */
#endif
    MBufHead	*co_TMZBufP;
    time_t	co_SessionStartTime;
    time_t	co_LastActiveTime;
    int		co_FCounter;
    double	co_ByteCounter;
    int		co_BytesHeader;
    int		co_LinesHeader;	/* body lines from Bytes:, -1: none */
    int		co_BodyLines;	/* body lines received		*/
    int		co_PrevCrlf;	/* the previous article line ended with CRLF,
				 * -1: it was a dropped long line, see mh_LongCR */
    int		co_Flags;
    int		co_Numbering;
    TimeRestrict co_TimeRestrict;
    DnsRes	co_Auth;
    char	*co_GroupName;	/* current group or NULL	*/
    char	*co_SavedGroupName;	/* the client's group during NEWNEWS */
    char	*co_IHaveMsgId;
    char	*co_DiscardReply;	/* answer once a dropped article is read */
    Control	*co_Ctl;	/* control lookup cache		*/
    struct ServReq *co_SReq;	/* see note(1)			*/
    int		co_ListRec;
    int		co_ListRecLen;
    char	*co_ListPat;
    char	*co_ListHdrs;
    char	*co_ListMsgId;	/* the message-id of a message-id list */
    artno_t	co_ListBegNo;
    artno_t	co_ListEndNo;
    int		co_ListCacheMode;
    struct activeCacheEnt *co_ListCachePtr;
    struct GroupList *co_ListCacheGroups;
    int		co_ArtMode;	/* current article mode		*/
    int		co_ListStrict;	/* OVER/HDR (RFC 3977), not XOVER/XHDR */
    artno_t	co_ArtNo;	/* current article number	*/
    artno_t	co_ArtBeg;
    artno_t	co_ArtEnd;
    int		co_RequestFlags;	/* 0x01 = art by message-id */
					/* 0x02 = art by art number */
    struct DirectFileAccess *co_DirectFA;	/* Direct file access */

    int		co_Retention;
    struct GroupList *co_GroupDef;
    struct timeval co_RateTv;
    int		co_RateCounter;
    int		co_RateLimitRangeCurrentRandom;
    HashFeed_MatchList	*co_RequestHash;

    time_t		co_LastServerLog;
    unsigned long	co_ServerByteCount;
    unsigned long	co_ServerArticleCount;
    unsigned long	co_ServerArticleRequestedCount;
    unsigned long	co_ServerArticleNotFoundErrorCount;
    unsigned long	co_ServerArticleMiscErrorCount;
    double		co_ClientTotalByteCount;
    unsigned long	co_ClientTotalArticleCount;
    double		co_ClientGroupByteCount;
    unsigned long	co_ClientGroupArticleCount;
    unsigned long	co_ClientPostCount;
    unsigned long	co_ClientGroupCount;
    unsigned long	co_CommandCount;	/* command lines read from the client */
    int 		co_ByteCountType; /* Temp for by-type byte count */

    MBufHead	co_ArtBuf;	/* article buffer		*/
} Connection;

#define COF_SERVER	0x00000001
#define COF_HEADFEED	0x00000002
#define COF_STREAM	0x00000004
#define COF_IHAVE	0x00000008	/* temporary ihave->takethis */
#define COF_POST	0x00000010	/* post command, else feed   */
#define COF_INHEADER	0x00000020	/* post/feed, reading headers*/
#define COF_DORANGE	0x00000040	/* do article range, else msgid */
#define COF_WASCONTROL	0x00000080
#define COF_MODEREADER  0x00000100	/* spool fetch, do mode reader	*/
#define COF_CLOSESERVER 0x00010000	/* close server ASAP	     	*/
#define COF_PATISWILD	0x00020000	/* list pattern is wildcard	*/
#define COF_MAYCLOSESRV 0x00040000	/* maybe close server 		*/
#define COF_INPROGRESS	0x00080000	/* operation in progress	*/
#define COF_ININIT	0x00100000	/* see server.c			*/
#define COF_MAYNOTCLOSE 0x00200000	/* may not close the connection	*/
#define	COF_READONLY	0x00400000	/* send ``mode readonly''	*/
#define	COF_LOGIN	0x00800000	/* send ``authinfo'' to log in	*/
#define	COF_POSTTOOBIG	0x01000000	/* posted article is too big	*/
#define	COF_MAXAGE	0x02000000	/* maximum age allowed		*/
#define	COF_AUTHINFO	0x04000000	/* logged in with AUTHINFO (281)	*/
#define	COF_POSTXREF	0x08000000	/* the post had an Xref: header	*/
#define	COF_POSTINJDATE	0x10000000	/* we added the post's Injection-Date */
#define	COF_BROKEN	0x20000000	/* server reply out of step: close, take no new request */

#define	CON_RFC3977	0		/* RFC3977 numbering behaviour	*/
#define	CON_RFC977	1		/* RFC977 numbering behaviour	*/
#define	CON_WINDOW	2		/* Windowed numbering behaviour	*/

#ifndef	BIG_MBUF
#define MBUF_SIZE	4096
#define MBUF_HIWAT	(MBUF_SIZE*2-(MBUF_SIZE/4))
#else /*BIG_MBUF*/
#define MBUF_SIZE	8192
#define MBUF_HIWAT	(MBUF_SIZE*8-(MBUF_SIZE/4))
#endif /*BIG_MBUF*/

/*
 * longest line (newline included) accepted from a client.  Commands are
 * short and article lines are bounded in practice, so this only stops a
 * client that sends data without ever ending the line.
 */
#define CLIENT_MAXLINE	(1024 * 1024)

#define COM_ARTICLE	0
#define COM_BODY	1
#define COM_HEAD	2
#define COM_STAT	3
#define COM_BODYNOSTAT	4
#define COM_FUNC	5

#define COM_ACTIVE	6		/* list active			*/
#define COM_GROUPDESC	7		/* list newsgroups grouppat	*/

#define COM_XHDR	8
#define COM_XOVER	9
#define COM_XPAT	10

#define COM_NEWGROUPS	11		/* newgroups			*/
#define COM_ARTICLEWVF	12		/* verify body before output	*/
#define COM_BODYWVF	13		/* verify body before output	*/
#define COM_NEWNEWS	14

#define	COM_XZVER	15
#define	COM_ACTIVETIMES	16		/* list active.times		*/

#define	ARTFETCH_MSGID	0x01		/* Request was by Message-ID */
#define	ARTFETCH_ARTNO	0x02		/* Request was by article number */

typedef struct ServReq {
    struct ServReq *sr_Next;	/* linked list of requests	*/
    Connection *sr_CConn;	/* client making request	*/
    Connection *sr_SConn;	/* server handling request	*/
    char	*sr_Group;	/* request related to group	*/
    int		sr_GrpIter;	/* Group iteration, for cachehits hash */
    artno_t	sr_endNo;	/* last group article, for cachehits */
    char	*sr_MsgId;	/* request related to messageid	*/
    time_t	sr_Time;	/* time of request for timeout calc	*/
    FILE	*sr_Cache;	/* cache write (locked for duration)	*/
    int		sr_TimeRcvd;	/* article received time (NNRetrieveHead) */
    int		sr_Rolodex;	/* see server.c		*/
    int		sr_NoPass;	/* see server.c		*/
    int		sr_MaxAge;	/* Maximum age allowed	*/
    int		sr_HeadOnly;	/* HDR/XHDR fallback: HEAD, not ARTICLE */
    char	*sr_AccHdr;	/* D-195: its Xref and Newsgroups lines, for the groups check */
    int		sr_AccLen;	/* its length */
    int		sr_AccIn;	/* 1: in an Xref or Newsgroups line, for continuations */
    int		sr_AccCut;	/* 1: such a line was left out: the check fails closed */
    int		sr_Denied;	/* D-195: the reader may not read this article */
    int		sr_SpoolOutput;	/* network fetch moved data to the client's transmit buffer */
    int		sr_Post;	/* 1: a POST (SWrite, post servers), 0: a fetch */
} ServReq;

#define SREQ_RETRIEVE	1
#define SREQ_POST	2
#define SREQ_HDR		3


/*
 * Overview record.	over.groupname	(overview information - headers)
 *			numa.groupname	(article number assignment file)
 */

typedef union OverHead {
    struct {
	int	reserved[32];
    } u;
    struct {
	int	version;
	int	byteOrder;
	int	headSize;
	int	maxArts;
	char	gName[256];
	int	dataEntries;
    } v;
} OverHead;

#define oh_Version	v.version
#define oh_HeadSize	v.headSize
#define oh_MaxArts	v.maxArts	/* adjusted by dexpireover -s */
#define oh_ByteOrder	v.byteOrder
#define oh_Gname	v.gName
#define oh_DataEntries	v.dataEntries

#define OH_VERSION	4
#define OH_BYTEORDER	((int)0xF1E2D3C4)

/*
 * OA_ARTNOEQ(x, y)
 * Test whether or not a 64-bit artno_t (1st arg) matches an oa_ArtNo
 *
 * OA_ARTNOSET(x)
 * Return a 32-bit signed oa_ArtNo based on the 64-bit artno_t arg
 *
 * OA_ARTVALID(x)
 * The old test for validity was oa_ArtNo > 0.  This is kind of a pain
 * when you cycle back to zero, so a bunch of tests have been changed
 * to test for validity of an oa structure, basically by also looking at
 * oa_TimeRcvd in addition to a non-negative oa_ArtNo.  In theory, the
 * test could simply be oa_TimeRcvd.
 */

#define	OA_ARTNOEQ(x,y)		(((x)&OA_ARTNOMASK)==(y))
#define	OA_ARTNOSET(x)		((int)((x)&OA_ARTNOMASK))
#define	OA_ARTVALID(x)		(((x)->oa_ArtNo >= 0) && ((x)->oa_TimeRcvd))
#define	OA_ARTNOMASK	0x000000007FFFFFFFLL

typedef struct OverArt {
    int		oa_ArtNo; 	/* 1st 31 bits of article number        */
    int         oa_SeekPos;     /* seek in data.grouphash file          */
    int		oa_Bytes;	/* bytes of headers in data.grphash file*/
    hash_t	oa_MsgHash;	/* locate message-id (used by cancel)	*/
    int		oa_ArtSize;	/* used for xover Bytes: header		*/
    int		oa_TimeRcvd;	/* time received			*/
    int		oa_Lines;	/* body lines + 1 from the feed, 0: none	*/
} OverArt;

/* the :lines of a record (RFC 3977 8.4.2), -1 when the feed gave none */
#define	OA_LINES(x)		((x)->oa_Lines - 1)

typedef struct OverData {
    struct OverData *od_Next;
    int		od_HFd;
    int		od_ArtBase;
    int		od_HMapPos;
    int		od_HMapBytes;
    const char *od_HMapBase;
} OverData;

typedef struct OverInfo {
    struct OverInfo *ov_Next;
    struct OverInfo *ov_LruPrev;	/* more recently used		*/
    struct OverInfo *ov_LruNext;	/* less recently used		*/
    int		ov_Refs;
    char        *ov_Group;
    int         ov_MaxArts;     /* maximum number of articles in group  */
    OverData	*ov_HData;	/* hdata file reference linked list	*/
    OverData	*ov_HCache;	/* last accessed file reference		*/
    int         ov_OFd;
    OverHead	*ov_Head;
    off_t	ov_Size;
    int		ov_Iter;
    int		ov_endNo;
    int		ov_DataEntryMask;
    int		ov_LimitSecs;	/* don't show entries older than this	*/
} OverInfo;

typedef struct ArtNumAss {
    struct ArtNumAss *an_Next;
    const char	     *an_GroupName;	/* NOT TERMINATED	*/
    int	 	     an_GroupLen;
    artno_t	     an_ArtNo;
} ArtNumAss;

/*
 * Active file cache for LIST-type commands
 */

typedef struct activeCacheEnt {
    struct GroupList	*nglist;
    int			cts;
    struct activeCacheEnt *next, *prev;
    struct activeCacheEnt *left, *right;
    struct activeCacheEnt *parent;
} activeCacheEnt;

#define	ACMODE_NONE	0
#define	ACMODE_READ	1
#define	ACMODE_WRITE	2

/*
 * This structure is to share info between the master process and
 * all the child processes. The info is mainly stats gathering.
 */
struct SharedInfo {
    struct in_addr dr_Addr;		/* remote IP address	*/
    int		dr_Port;		/* remote port 		*/
    int		si_RateLimit;
    double	si_ServerByteCount;
    double	si_ServerArticleCount;
    double	si_ClientTotalByteCount;
    double	si_ClientTotalArticleCount;
    double	si_ClientGroupByteCount;
    double	si_ClientGroupArticleCount;
    double	si_ClientPostCount;
    double	si_ClientPostBytes;
    double	si_ClientPostFail;
    double	si_ClientGroupCount;
};

#include <obj/dreaderd-protos.h>

#include <lib/debug-mbuf.h>

