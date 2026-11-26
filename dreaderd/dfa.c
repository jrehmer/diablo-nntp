
/*
 * DFA.C      - Direct File Access to articles
 *
 * (c)Copyright 2001, Francois Petillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

#if USE_AIO
    #if USE_LINUX_KAIO
      #include <linux/aio.h>
    #else
      #include <aio.h>
    #endif
#include <signal.h>
#endif

Prototype DirectFileAccess* NewDFA(Connection *conn, int lf, int offset, int size);
Prototype void DelDFA(Connection *conn);
Prototype void NNSendLocalArticle(Connection *conn);

int NNCopyLocalArticle(Connection *conn, int rs);
void DFADataWrite(ServReq *sreq, const void *data, int len);
void DFAPathDecide(ServReq *sreq, DirectFileAccess *dfa, const char *vs);
void NNSLA_Loop(Connection *conn);

#if USE_AIO
Prototype void AIOBlockSignal(void);
Prototype void AIOUnblockSignal(void);

int AIOBlockedSignal = 0;

void AIORDataRead(Connection *conn);
void Sig_AIO(int signo, siginfo_t *info, void *context);

struct sigaction* sig_aio = NULL;

#define AIOSIG        SIGUSR1

#endif

/* We are using a fifo to store unused DFA structure */
DirectFileAccess* dfa_Trash = NULL;

/* Alloc dfa struct */
DirectFileAccess*
NewDFA(Connection *conn, int lf, int offset, int size)
{
    ServReq *sreq = conn->co_SReq;
    DirectFileAccess* el=NULL;
    struct stat st;

#if USE_AIO
    if (!sig_aio) {
      sig_aio = (struct sigaction*) malloc(sizeof(struct sigaction)) ;
      sigemptyset(&sig_aio->sa_mask);
      sigaddset(&sig_aio->sa_mask, AIOSIG);
      sig_aio->sa_flags = SA_SIGINFO;
      sig_aio->sa_sigaction = Sig_AIO;
      if (sigaction(AIOSIG, sig_aio, NULL)) {
          logit(LOG_ERR, "NewDFA : cannot sigaction");
          exit(1);
      }
      AIOBlockSignal();
    }
#endif

    if (lf==-1) {
      logit(LOG_ERR, "NewDFA : fd is -1");
      return NULL;
    }

    if (dfa_Trash) {
      el = dfa_Trash ;
      dfa_Trash = el->dfa_Next ;
    } else {
      el = (DirectFileAccess*) malloc(sizeof(DirectFileAccess));
#if USE_AIO
      el->dfa_AIOcb = (struct aiocb*) malloc(sizeof(struct aiocb)) ;
#endif
    }

    if (!el) return NULL ;
    /*
     * Check that the whole article is in the file.  This used to lseek() to
     * its last byte, but lseek() past the end of a file succeeds, so a spool
     * file that ends early (data lost in a crash: the spool is not fsync'ed)
     * got through, and the client was left waiting for the rest of a body
     * that had already started.  Once the response has started there is no
     * clean way to fail it, so refuse the file here and let the caller fall
     * back to fetching the article from the spool server.
     */
    if (offset < 0 || size < 0 || fstat(lf, &st) != 0 ||
        st.st_size < (off_t)offset + (off_t)size) {
      logit(LOG_ERR, "NewDFA : spool file ends before the end of article %s",
          sreq->sr_MsgId);
      el->dfa_Next = dfa_Trash;
      dfa_Trash = el;
      return NULL ;
    }

#if USE_AIO
    el->dfa_AIOcb->aio_fildes = lf ;
    el->dfa_AIOcb->aio_offset = offset ;
    el->dfa_AIOcb->aio_nbytes = 0 ;
    el->dfa_AIOcb->aio_reqprio = 0 ;
    el->dfa_AIOcb->aio_buf = el->dfa_Buffer ;
    el->dfa_AIOcb->aio_sigevent.sigev_notify = SIGEV_SIGNAL ;
    el->dfa_AIOcb->aio_sigevent.sigev_signo = AIOSIG;
    el->dfa_AIOcb->aio_sigevent.sigev_value.sival_ptr = conn ;
#else
    if (lseek(lf, offset, SEEK_SET)!=offset) { /* bring it back home */
      logit(LOG_ERR, "NewDFA : cannot seek to the beginning of article");
      el->dfa_Next = dfa_Trash;
      dfa_Trash = el;
      return NULL ;
    }
    el->dfa_Fd     = lf ;
#endif

    conn->co_DirectFA = el ;

    el->dfa_Size   = size ;
    el->dfa_LF     = 0 ;
    el->dfa_Flag   = 0 ;
    el->dfa_PLen   = 0 ;
    el->dfa_PLead  = 0 ;
    el->dfa_PInLead = 0 ;
    el->dfa_POut   = 0 ;
    memset(el->dfa_Field, 0, sizeof(el->dfa_Field)) ;
    el->dfa_Next   = NULL ;

    switch(conn->co_SReq->sr_CConn->co_ArtMode) {
      case COM_BODY :
      case COM_BODYWVF :
      case COM_BODYNOSTAT :
          el->dfa_InHdr = JMPBDY ;
          if (sreq->sr_Cache) {
              AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0) ;
              fclose(sreq->sr_Cache) ;
              sreq->sr_Cache = NULL ;
          }
          break ;
      case COM_HEAD :
          if (sreq->sr_Cache) {
              AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0) ;
              fclose(sreq->sr_Cache) ;
              sreq->sr_Cache = NULL ;
          }
          /* fall through */
      default :
          el->dfa_InHdr = 0 ;
    }

    return el;
}

void
DelDFA(Connection *conn)
{
    DirectFileAccess* el=conn->co_DirectFA ;
    conn->co_DirectFA = NULL ;
#if USE_AIO
    if (el->dfa_AIOcb->aio_fildes!=-1) {
      close(el->dfa_AIOcb->aio_fildes) ;
      el->dfa_AIOcb->aio_fildes = -1 ;
    }
#else
    if (el->dfa_Fd!=-1) {
      close(el->dfa_Fd) ;
      el->dfa_Fd = -1 ;
    }
#endif
    el->dfa_Next = dfa_Trash;
    dfa_Trash = el;
}

#if USE_AIO
/* Get a signal, read data */
void Sig_AIO(int signo, siginfo_t *info, void *context)
{
    Connection *conn = (Connection*) info->si_value.sival_ptr ;
    DirectFileAccess *dfa = conn->co_DirectFA;
    struct aiocb *aio=dfa->dfa_AIOcb;
    const struct aiocb *aiolist[1];
    int ret;

    /* check IO */
    aiolist[0] = aio;
    if ((ret = aio_error(aio))) {
      if (ret==EINPROGRESS) {
          logit(LOG_ERR, "SigAIOPoll : EINPROGRESS", errno);
          if (aio_suspend(aiolist,1,NULL)) {
              logit(LOG_ERR, "SigAIOPoll : aio_suspend failed (errno=%i)", errno);
              DelDFA(conn);
              NNServerTerminate(conn);
              return;
          } else {
              logit(LOG_NOTICE, "SigAIOPoll : aio_suspend on %i", aio->aio_fildes) ;
          }
      } else {
          logit(LOG_ERR, "SigAIOPoll : IO error %i", ret);
          DelDFA(conn);
          NNServerTerminate(conn);
      }
    }
    /* we can read the buffer */
    NNSLA_Loop(conn);
}

void
AIODataRead(Connection *conn)
{
    DirectFileAccess *dfa = conn->co_DirectFA;

    dfa->dfa_AIOcb->aio_nbytes = (dfa->dfa_Size>sizeof(dfa->dfa_Buffer)) ? sizeof(dfa->dfa_Buffer) : dfa->dfa_Size ;
    if (DebugOpt)
      printf("AIODataRead : fd %i sz %i of %i", dfa->dfa_AIOcb->aio_fildes, dfa->dfa_AIOcb->aio_nbytes, (int)dfa->dfa_AIOcb->aio_offset);

    if (aio_read(dfa->dfa_AIOcb)) {
      logit(LOG_ERR, "AIODataRead : error on reading (errno=%i)", errno);
      DelDFA(conn);
      NNServerTerminate(conn);
    }
}

void AIOBlockSignal(void)
{
    if (sig_aio)
      sigprocmask(SIG_BLOCK, &sig_aio->sa_mask, NULL) ;
}

void AIOUnblockSignal(void)
{
    if (sig_aio)
      sigprocmask(SIG_UNBLOCK, &sig_aio->sa_mask, NULL) ;
}

#endif

void
DFADataWrite(ServReq *sreq, const void *data, int len)
{
    if (sreq->sr_CConn)
      MBWrite(&sreq->sr_CConn->co_ArtBuf, data, len) ;
    if (sreq->sr_Cache)
      fwrite(data, 1, len, sreq->sr_Cache);
}

/*
 * What follows "Path:" in the line is in dfa_PBuf: the white space right after
 * the colon (up to DFA_PATHWS characters, dfa_PLead of them) and then the
 * first characters of the value, or all of the line when it ends sooner.  Put
 * this site's name in front of the value unless it is there already, as
 * NNSpoolResponse2 does for an article fetched over NNTP: "Path: <name>!<value>",
 * the white space dropped; an empty value, or one that starts with the name
 * and a "!", leaves the line as it was.  White space beyond DFA_PATHWS is
 * written out as it comes (dfa_POut), and the name then goes after it.
 */
void
DFAPathDecide(ServReq *sreq, DirectFileAccess *dfa, const char *vs)
{
    int lead = dfa->dfa_PInLead ? dfa->dfa_PLen : dfa->dfa_PLead ;
    const char *p = dfa->dfa_PBuf + lead ;
    int n = dfa->dfa_PLen - lead ;
    int vsl = strlen(vs) ;

    if (n > 0 && !(n > vsl && strncmp(p, vs, vsl) == 0 && p[vsl] == '!')) {
      if (!dfa->dfa_POut)
          DFADataWrite(sreq, " ", 1) ;
      DFADataWrite(sreq, vs, vsl) ;
      DFADataWrite(sreq, "!", 1) ;
      DFADataWrite(sreq, p, n) ;
    } else {
      DFADataWrite(sreq, dfa->dfa_PBuf, dfa->dfa_PLen) ;
    }
    dfa->dfa_PLen = 0 ;
}

/* Reading buffer */
void
NNSLA_Loop(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    DirectFileAccess *dfa = conn->co_DirectFA;
    int rs ;

    conn->co_Func = NNSendLocalArticle;
    conn->co_State = "getlclart";

#if USE_AIO
      rs = aio_return(dfa->dfa_AIOcb);        
#else
    do {
      int sizofbuf = sizeof(dfa->dfa_Buffer) ;
      rs = read(dfa->dfa_Fd, dfa->dfa_Buffer, (dfa->dfa_Size>sizofbuf) ? sizofbuf : dfa->dfa_Size) ;

      if (rs<0) {
          if (errno == EAGAIN) { /* retry later */
              if (DebugOpt)
                  printf("NNGetLocal2: retry later...") ;
              return;
          } else {
              /* ooops, real error */
              DelDFA(conn) ;
              NNServerTerminate(conn) ;
              return;
          }
      }
#endif

      switch (NNCopyLocalArticle(conn, rs)) {
          case BLOCK_OK :
              dfa->dfa_Size -= rs ;
              if (!dfa->dfa_Size) {
                  /* an article that ends inside a Path header: what was held back goes out */
                  if (dfa->dfa_InHdr == PATHCHK && sreq->sr_CConn)
                          DFAPathDecide(sreq, dfa, sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName) ;
                  if (sreq->sr_CConn)
                          MBCopy(&sreq->sr_CConn->co_ArtBuf, &sreq->sr_CConn->co_TMBuf);
                  if (sreq->sr_Cache) {
                      if (fflush(sreq->sr_Cache) || ferror(sreq->sr_Cache))
                          AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
                      else
                          CommitCache(conn, 0);
                      fclose(sreq->sr_Cache) ;
                      sreq->sr_Cache = NULL ;
                  }
                  DelDFA(conn) ;
                  NNFinishSReq(conn, ".\r\n", 0) ;
                  return;
              }
              if (FastCopyOpt && sreq->sr_CConn) {
                  MBCopy(&sreq->sr_CConn->co_ArtBuf, &sreq->sr_CConn->co_TMBuf);
              }
              break ;
          case BLOCK_END :
              if (sreq->sr_CConn)
                      MBCopy(&sreq->sr_CConn->co_ArtBuf, &sreq->sr_CConn->co_TMBuf);
              if (sreq->sr_Cache) {
                  AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
                  fclose(sreq->sr_Cache) ;
                  sreq->sr_Cache = NULL ;
              }
              DelDFA(conn) ;
              NNFinishSReq(conn, ".\r\n", 0) ;
              return;
          case BLOCK_ERROR :
          default :
              DelDFA(conn);
              if (sreq->sr_Cache) {
                  AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
                  fclose(sreq->sr_Cache) ;
                  sreq->sr_Cache = NULL ;
              }
              NNServerTerminate(conn);
              return;
      }
#if USE_AIO
    dfa->dfa_AIOcb->aio_offset += rs;
    AIODataRead(conn);
#else
    } while (rs) ;
#endif
}

/* Reading buffer */
void
NNSendLocalArticle(Connection *conn)
{
#if USE_AIO
    /* let's start the pump */
    AIODataRead(conn);
#else
    /* blocking loop */
    NNSLA_Loop(conn);
#endif
}

/* Copying article... Return 1 when an error occurred */
int
NNCopyLocalArticle(Connection *conn, int rs)
{
    ServReq *sreq = conn->co_SReq;
    DirectFileAccess *dfa = conn->co_DirectFA;
    int sizofbuf = sizeof(dfa->dfa_Buffer) ;
    int i=0, ptr=0 ;
    char *vs;

    while (i < rs) {
      switch (dfa->dfa_InHdr) {
          case INBODY : /* body, just regular copy */
              /* Fast read, halt on end of buffer/line */
              while ( (dfa->dfa_Buffer[i] != '\n') && (dfa->dfa_Buffer[i] != '\r') && !dfa->dfa_LF && (i+1<rs) ) {
                  i++ ;
              }
              switch (dfa->dfa_Buffer[i]) {
                  case '\n' :
                      if ( dfa->dfa_LF != CR) { /* if I get a CRLF, I can delay the writing */
                          dfa->dfa_Buffer[i] = '\r' ;
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          dfa->dfa_Buffer[i] = '\n' ;
                          ptr = i ;
                      }
                      dfa->dfa_LF = CRLF ;
                      break ;
                  case '\r' :
                      dfa->dfa_LF = CR ;
                      break ;
                  case '.' :
                      if (dfa->dfa_LF == CRLF) { /* we have to dupplicate this '.' */
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          ptr = i ;
                      }
                      /* fall through */
                  default :
                      dfa->dfa_LF = 0 ;
              }
              break ;
          case JMPBDY : /* COM_BODY, del headers */
              /* fast read, halt on end of buffer/line */
              if (dfa->dfa_LF == LF) {
                  switch (dfa->dfa_Buffer[i]) {
                      case '\n' :
                          dfa->dfa_InHdr = INBODY ;
                          /* fall through */
                      case '\t' :
                          ptr = i + 1 ;
                          break ;
                  } 
              }
              while ( (dfa->dfa_Buffer[i]!='\n') && (i+1<rs)) i++ ;
              if (dfa->dfa_Buffer[i]=='\n') 
                  /* the body starts after the blank line: its first line is at the start
                   * of a line like any other, so a "." there is doubled */
                  dfa->dfa_LF = (dfa->dfa_InHdr == INBODY) ? CRLF : LF ;
              else
                  dfa->dfa_LF = 0 ;
              ptr = i + 1 ;
              break ;
          case WRDDEL : /* header, drop current word & copy til the end of line */
              /* Fast read, halt on end of buffer/word */
              while ( (i<rs) && (dfa->dfa_Buffer[i]!=' ') && (dfa->dfa_Buffer[i]!='\t')
                      && (dfa->dfa_Buffer[i]!='\r') && (dfa->dfa_Buffer[i]!='\n') )
                  i++ ;
              ptr = i ;
              if (i==rs) { /* end of buffer: the word may go on in the next one */
                  i-- ; /* the i++ below must leave i at rs, not past it */
                  break ;
              }
              dfa->dfa_LF = 0 ;
              /* fall through */
          case MAX_HDR_CC : /* stop checking, just copy line */
              dfa->dfa_InHdr = HDRCPY ;
              /* fall through */
          case HDRCPY : /* header, no more check on this line */
              /* Fast read, halt on end of buffer/line */
              while ( (dfa->dfa_Buffer[i] != '\n') && (dfa->dfa_Buffer[i] != '\r') && !dfa->dfa_LF && (i+1<rs))
                  i++ ;
              switch (dfa->dfa_Buffer[i]) {
                  case '\n' :
                      if (dfa->dfa_LF != CR) { /* no need to copy when having a CRLF */
                          /* copying & adding a CRLF */
                          dfa->dfa_Buffer[i] = '\r' ;
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          dfa->dfa_Buffer[i] = '\n' ;
                          ptr = i ;
                      }
                      dfa->dfa_InHdr = 0 ; 
                      dfa->dfa_LF = HDRCPY ;
                      break ;
                  case '\r' :
                      dfa->dfa_LF = CR ;
                      break ;
                  default :
                      dfa->dfa_LF = 0 ;
                      break ;
              }
              break ;
          case PATHCHK : /* Path: header, the start of what follows the colon */
              switch (dfa->dfa_Buffer[i]) {
                  case '\n' :
                  case '\r' :
                      /* the line ends: decide, then HDRCPY ends it (same character) */
                      DFAPathDecide(sreq, dfa, sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName) ;
                      dfa->dfa_InHdr = HDRCPY ;
                      continue ;
                  default : {
                      /*
                       * Held back until it is known whether the name is there already.
                       * The white space in front of the value does not count towards
                       * the characters that decide that; of that, DFA_PATHWS are held
                       * and the rest goes out at once.
                       */
                      const char *vs = sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName ;
                      int white = isspace((unsigned char)dfa->dfa_Buffer[i]) ;

                      if (dfa->dfa_PInLead && white && dfa->dfa_PLen >= DFA_PATHWS) {
                          DFADataWrite(sreq, dfa->dfa_PBuf, dfa->dfa_PLen) ;
                          dfa->dfa_PLen = 0 ;
                          dfa->dfa_POut = 1 ;
                      }
                      if (dfa->dfa_PInLead && !white) {
                          dfa->dfa_PInLead = 0 ;
                          dfa->dfa_PLead = dfa->dfa_PLen ;
                      }
                      dfa->dfa_PBuf[dfa->dfa_PLen++] = dfa->dfa_Buffer[i] ;
                      ptr = i + 1 ;
                      /* decide at the latest when dfa_PBuf is full (a name of 63 characters or less never gets there) */
                      if (!dfa->dfa_PInLead && (dfa->dfa_PLen - dfa->dfa_PLead > (int)strlen(vs)
                                                 || dfa->dfa_PLen >= DFA_PATHBUF)) {
                          DFAPathDecide(sreq, dfa, vs) ;
                          dfa->dfa_InHdr = HDRCPY ;
                      }
                      break ;
                  }
              }
              break ;
          case HDRDEL : /* header, drop the rest of the current line */ /* unused */
              /* Fast read, halt on end of buffer/line */
              while ((dfa->dfa_Buffer[i] != '\n') && (i+1<rs)) i++ ;
              if (dfa->dfa_Buffer[i] == '\n') {
                  dfa->dfa_InHdr = 0 ;
                  dfa->dfa_LF = HDRDEL ;
              }
              ptr = i+1 ;
              break ;
          case 0 : /* 1st char of line */
              switch (dfa->dfa_Buffer[i]) {
                  case '\t' :
                  case ' ' : /* header continuation... */
                      if (dfa->dfa_LF < 0) {
                          dfa->dfa_InHdr = dfa->dfa_LF ;
                      } else { /* this should not happen : we should be in HDRCPY or HDRDEL */
                          logit(LOG_ERR, "NNGetLocal2: bad header continuation (%i)", dfa->dfa_LF) ;
                          return BLOCK_ERROR;
                      }
                      break ;
                  case '\r' :
                      /* Check missing headers, end of last buffer or HEAD mode */
                      if ( ((dfa->dfa_Flag&FLAG_NEEDED)==FLAG_NEEDED) || (i+1 == sizofbuf)
                           || (sreq->sr_CConn->co_ArtMode == COM_HEAD)) {
                          /* we "simply" forget this '\r' for laziness reasons */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          ptr = i+1 ;
                          dfa->dfa_LF = 0 ;
                      } else {
                          dfa->dfa_LF = CR ;
                      }
                      /* dfa->dfa_InHdr = 0 ; */
                      break ;
                  case '\n' : /* fin des entetes */
                      /*
                       * A missing Path or Xref header is not made up: the
                       * article is sent as the spool has it, as when it is
                       * fetched over NNTP.
                       */
                      if (sreq->sr_CConn->co_ArtMode == COM_HEAD) { /* end of header in HEAD mode */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          return BLOCK_END;
                      }
                      if ( dfa->dfa_LF != CR) { /* if I get a CRLF, I can delay the writings */
                          dfa->dfa_Buffer[i] = '\r' ;
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          dfa->dfa_Buffer[i] = '\n' ;
                          ptr = i ;
                      }
                      dfa->dfa_LF = CRLF ;
                      dfa->dfa_InHdr = INBODY ;
                      break ;
                  case '.' :
                      /* a header line that starts with a dot goes out with it doubled,
                       * as SendArticle() sends it: the dot is written now and the line
                       * (from the dot) after it */
                      DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                      ptr = i ;
                      /* fall through */
                  default :
                      dfa->dfa_LF = 0 ;
                      dfa->dfa_Field[(int)dfa->dfa_InHdr] = dfa->dfa_Buffer[i] ;
                      dfa->dfa_InHdr++ ;
                      break ;
              }
              break ;
          case 0x05 : /* 6th char of line */
              switch (dfa->dfa_Buffer[i]) {
                  case ' ' :
                      if (!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate
                           && *(vs=sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName)
                           && (strncasecmp(dfa->dfa_Field, "Xref:",5) == 0)
                         ) {
                          /* copying up to the space */
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          ptr = i + 1 ;
                          /* We just replace hostname in Xref, maybe we should
                           * replace the full line with overview content ? */
                          DFADataWrite(sreq, vs, strlen(vs)) ;
                          dfa->dfa_InHdr = WRDDEL ;
                          dfa->dfa_Flag = dfa->dfa_Flag | FLAG_XREF ;
                          break ;
                      }
                      /* fall through */
                  default : /* no more check... */
                      if (!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoReadPath
                           && *(vs=sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName)
                           && (strncasecmp(dfa->dfa_Field, "Path:",5) == 0)
                         ) {
                          /*
                           * This site's name goes in front of the path, unless it is the
                           * first element already (DFAPathDecide).  What follows the colon,
                           * this character too, is held back (PATHCHK); only "Path:" goes out.
                           */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          ptr = i ;
                          dfa->dfa_PLen = 0 ;
                          dfa->dfa_PLead = 0 ;
                          dfa->dfa_PInLead = 1 ;
                          dfa->dfa_POut = 0 ;
                          dfa->dfa_InHdr = PATHCHK ;
                          dfa->dfa_Flag = dfa->dfa_Flag | FLAG_PATH ;
                          continue ;
                      }
                      dfa->dfa_InHdr = HDRCPY ;
                      break ;
                  case '\n' :
                  case '\r' :
                      /* a line of exactly 5 characters ends here: the end of
                       * the line goes the way it does for any other length
                       * (the generic state, 6), not into HDRCPY, which would
                       * take the line after it for the rest of this one and
                       * lose the end of the headers when that is the blank line */
                      dfa->dfa_InHdr = 6 ;
                      continue ;
              }
              break ;
          default : /* Else, we are reading firsts chars on headers lines */
              if (dfa->dfa_InHdr<0) { /* ooops */
                  return BLOCK_ERROR;
              }
              switch (dfa->dfa_Buffer[i]) {
                  case ' ' : /* end of "keyword: ", thus go for a HDRCPY */
                      dfa->dfa_InHdr = HDRCPY ;
                      break ;
                  case '\n' :
                      if (dfa->dfa_LF != CR) { /* no need to copy when having a CRLF */
                          /* copying & adding a CRLF */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          DFADataWrite(sreq, "\r\n", 2) ;
                          ptr = i + 1 ;
                      }
                      dfa->dfa_LF = HDRCPY ; /* in case this header continues on the following line */
                      dfa->dfa_InHdr = 0 ;
                      break ;
                  case '\r' :
                      dfa->dfa_LF = CR ;
                      dfa->dfa_InHdr = HDRCPY ;
                      break ;
                  default :
                      dfa->dfa_LF = 0 ;
                      dfa->dfa_Field[(int)dfa->dfa_InHdr] = dfa->dfa_Buffer[i] ;
                      dfa->dfa_InHdr++ ;
                      break ;
              }
              break ;
      }

      i++ ;
    }

    if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;

    return BLOCK_OK;
}

