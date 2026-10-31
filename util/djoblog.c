
/*
 * UTIL/DJOBLOG.C - run a job, and write its output to a log with a time stamp on every line
 *
 * djoblog [-C diablo.config] [-s] [-f file] [-V] job command [arg ...]
 *
 *	Runs command with its standard output and error read through a pipe.  Every
 *	line is written as
 *
 *		2026-10-31 02:40:01.123 job[pid]: line
 *
 *	to <path_log>/<job>.log, or to the file of -f.  -s sends the lines to syslog
 *	(facility news, tag job) instead.  The job's start and end are logged too
 *	(BEGIN, END with the exit status).  The exit status is the command's.
 *
 *	The file is opened for each batch of lines, so it can be rotated while the job
 *	runs, even by the job itself.
 */

#include "defs.h"
#include <sys/wait.h>
#include <signal.h>

#define LINEMAX		4096	/* longer lines are split */
#define EXIT_NOEXEC	127

static volatile pid_t Child = -1;
static const char *Job;
static const char *LogFile = NULL;
static int UseSyslog = 0;
static int WriteFailed = 0;
static char Out[16384];
static int OutLen = 0;

static void
Usage(void)
{
    fprintf(stderr, "Usage: djoblog [-C diablo.config] [-s] [-f file] [-V] job command [arg ...]\n");
    fprintf(stderr, "  -C file   diablo.config to read\n");
    fprintf(stderr, "  -f file   write the log to this file, not <path_log>/<job>.log\n");
    fprintf(stderr, "  -s        log to syslog (facility news, tag job), not to a file\n");
    fprintf(stderr, "  -V        print the version and exit\n");
    exit(2);
}

static int
JobNameOk(const char *s)
{
    if (*s == 0 || *s == '.' || *s == '-')
	return(0);
    for (; *s; ++s) {
	if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-')
	    return(0);
    }
    return(1);
}

static void
Forward(int sig)
{
    if (Child > 0)
	kill(Child, sig);
}

/*
 * Write the lines collected in Out to the log file (one open and write for the batch).
 */
static void
FlushFile(void)
{
    int fd;
    int off = 0;

    if (OutLen == 0)
	return;
    if ((fd = open(LogFile, O_WRONLY|O_APPEND|O_CREAT, 0644)) < 0) {
	if (!WriteFailed)
	    fprintf(stderr, "djoblog: cannot open %s: %s\n", LogFile, strerror(errno));
	WriteFailed = 1;
	OutLen = 0;
	return;
    }
    while (off < OutLen) {
	int n = write(fd, Out + off, OutLen - off);

	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    if (!WriteFailed)
		fprintf(stderr, "djoblog: cannot write %s: %s\n", LogFile, strerror(errno));
	    WriteFailed = 1;
	    break;
	}
	off += n;
    }
    close(fd);
    OutLen = 0;
}

/*
 * One line of output (without its newline).  Control characters other than TAB become '?',
 * so a job cannot break the one-line-per-entry format.
 */
static void
Emit(int prio, const char *line, int len)
{
    char text[LINEMAX + 1];
    int i;

    if (len > LINEMAX)
	len = LINEMAX;
    for (i = 0; i < len; ++i) {
	unsigned char c = (unsigned char)line[i];

	text[i] = (c < ' ' && c != '\t') || c == 0x7f ? '?' : line[i];
    }
    text[len] = 0;

    if (UseSyslog) {
	syslog(prio, "%s", text);
	return;
    }
    {
	char head[256];
	int hl = snprintf(head, sizeof(head), "%s %s[%d]: ", LogTime(), Job, (int)getpid());

	if (hl < 0 || hl >= (int)sizeof(head))
	    hl = 0;
	if (OutLen + hl + len + 1 > (int)sizeof(Out))
	    FlushFile();
	memcpy(Out + OutLen, head, hl);
	OutLen += hl;
	memcpy(Out + OutLen, text, len);
	OutLen += len;
	Out[OutLen++] = '\n';
    }
}

static void
EmitStr(int prio, const char *fmt, ...)
{
    char buf[LINEMAX];
    va_list va;

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    Emit(prio, buf, strlen(buf));
    if (!UseSyslog)
	FlushFile();
}

int
main(int ac, char **av)
{
    int i;
    int fds[2];
    int status;
    int rc;
    int jobpos;
    char cmdline[1024];
    char buf[8192];
    char line[LINEMAX];
    int linelen = 0;
    pid_t pid;

    /*
     * Options up to the job name; the command's own options are not ours.
     */
    for (i = 1; i < ac && av[i][0] == '-' && av[i][1]; ++i) {
	char *ptr = av[i] + 2;

	switch (av[i][1]) {
	case 'C':
	    if (*ptr == 0 && ++i >= ac)
		Usage();
	    break;
	case 'f':
	    if (*ptr == 0 && ++i >= ac)
		Usage();
	    LogFile = *ptr ? ptr : av[i];
	    break;
	case 's':
	    UseSyslog = 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    Usage();
	}
    }
    jobpos = i;
    if (jobpos + 1 >= ac)
	Usage();
    Job = av[jobpos];
    if (!JobNameOk(Job)) {
	fprintf(stderr, "djoblog: bad job name \"%s\" (letters, digits, '.', '_' and '-')\n", Job);
	exit(2);
    }

    LoadDiabloConfig(jobpos, av);	/* only our options, the command may have -C */

    if (!UseSyslog) {
	static char path[PATH_MAX];

	if (LogFile == NULL) {
	    snprintf(path, sizeof(path), "%s/%s.log", PatExpand(LogHomePat), Job);
	    LogFile = path;
	}
    } else {
	openlog(Job, LOG_PID, LOG_NEWS);
    }

    cmdline[0] = 0;
    for (i = jobpos + 1; i < ac; ++i) {
	int l = strlen(cmdline);

	if (l + 2 >= (int)sizeof(cmdline))
	    break;
	snprintf(cmdline + l, sizeof(cmdline) - l, "%s%s", l ? " " : "", av[i]);
    }

    if (pipe(fds) < 0) {
	fprintf(stderr, "djoblog: pipe: %s\n", strerror(errno));
	exit(1);
    }
    EmitStr(LOG_NOTICE, "BEGIN %s", cmdline);

    signal(SIGPIPE, SIG_IGN);
    if ((pid = fork()) < 0) {
	EmitStr(LOG_ERR, "END (cannot fork: %s)", strerror(errno));
	exit(1);
    }
    if (pid == 0) {
	close(fds[0]);
	dup2(fds[1], 1);
	dup2(fds[1], 2);
	if (fds[1] > 2)
	    close(fds[1]);
	signal(SIGPIPE, SIG_DFL);
	execvp(av[jobpos + 1], av + jobpos + 1);
	fprintf(stderr, "djoblog: cannot execute %s: %s\n", av[jobpos + 1], strerror(errno));
	_exit(EXIT_NOEXEC);
    }
    Child = pid;
    close(fds[1]);
    signal(SIGINT, Forward);
    signal(SIGTERM, Forward);
    signal(SIGHUP, Forward);

    /*
     * Read the job's output; a line is complete at the newline, or when it gets too long.
     */
    for (;;) {
	int n = read(fds[0], buf, sizeof(buf));
	int j;

	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	if (n == 0)
	    break;
	for (j = 0; j < n; ++j) {
	    if (buf[j] == '\n') {
		if (linelen > 0 && line[linelen - 1] == '\r')
		    --linelen;
		Emit(LOG_NOTICE, line, linelen);
		linelen = 0;
	    } else {
		if (linelen == LINEMAX) {
		    Emit(LOG_NOTICE, line, linelen);
		    linelen = 0;
		}
		line[linelen++] = buf[j];
	    }
	}
	if (!UseSyslog)
	    FlushFile();
    }
    if (linelen > 0)
	Emit(LOG_NOTICE, line, linelen);
    close(fds[0]);

    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
	;
    Child = -1;
    if (WIFEXITED(status)) {
	rc = WEXITSTATUS(status);
	EmitStr(rc ? LOG_ERR : LOG_NOTICE, "END (exit status %d)", rc);
    } else if (WIFSIGNALED(status)) {
	rc = 128 + WTERMSIG(status);
	EmitStr(LOG_ERR, "END (killed by signal %d)", WTERMSIG(status));
    } else {
	rc = 1;
	EmitStr(LOG_ERR, "END (unknown wait status %d)", status);
    }
    return(rc);
}
