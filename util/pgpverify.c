/*
 * UTIL/PGPVERIFY.C
 *
 * pgpverify [-d] [-k keyring] [program [args...]] < control-news-article
 *
 * Extract the X-PGP-Sig header from a news article, rebuild the signed
 * text and check it.  On success print the first word of the signer's
 * user id (e.g. "news.announce.newgroups") and exit 0.
 *
 * By default GnuPG's gpgv (GnuPG 1.x or 2.x) is used: the signed text and
 * the signature are written to temporary files and verified as a detached
 * signature with "gpgv --status-fd", whose GOODSIG line names the signer.
 * When the program given is PGP 2.x ("pgp"), the old clear-signed message
 * is piped into "pgp -f" instead.
 *
 * Exit status: 0 good signature, 1 no X-PGP-Sig header, 2 malformed
 * header or verifier not runnable, 3 bad or unverifiable signature.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

typedef struct Header {
    struct Header *he_Next;	/* next header				*/
    char	  *he_Name;	/* name of header & body, incl of newlines */
    int		  he_Flagged;
} Header;

char *FindHeader(const char *name, char *def);
static int CountHeader(const char *name);
static const char *FindGpgv(void);
static int VerifyGpgv(char **argv, const char *keyring, const char *version,
		      char *sheaders, const char *signature);
static int VerifyPgp2(char **argv, const char *version, char *sheaders,
		      const char *signature);
static void PutStripped(FILE *fo, const char *text);

Header *HBase = NULL;
Header **PHe = &HBase;
char *argv[12];

/* The article (read into memory first), and its signed body. */
static FILE *In = NULL;
/* Signed body of the article, read after the headers. */
static char *Body = NULL;
static size_t BodyLen = 0;

void
Usage(void)
{
    printf("Usage: pgpverify [-d] [-k keyring] [program [args]]\n");
    exit(1);
}

int
main(int ac, char **av)
{
    char buf[8192];
    Header *he = NULL;
    int nl = 1;
    int r;
    char *sig;
    char *version = NULL;
    char *sheaders = NULL;
    char *signature = NULL;
    const char *keyring = NULL;
    const char *prog;
    int i;
    int argcount = 0;
    int argend = 0;

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-' || argend) {
	    /*
	     * The verifier and its arguments: argv[0] is the path to exec,
	     * argv[1] the name it is run as, then its arguments.
	     */
	    argend = 1;
	    if (argcount > 9)
		break;
	    if (argcount == 0) {
		argv[argcount++] = ptr;
		argv[argcount++] = strrchr(ptr, '/') ? strrchr(ptr, '/') + 1 : ptr;
	    } else {
		argv[argcount++] = ptr;
	    }
	    argv[argcount] = NULL;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case '-':
	    argend = 1;
	    break;
	case 'd':
	    if (*ptr)
		DebugOpt = strtol(ptr, NULL, 0);
	    else
		DebugOpt = 1;
	    break;
	case 'k':
	    keyring = (*ptr) ? ptr : (++i < ac ? av[i] : NULL);
	    if (keyring == NULL)
		Usage();
	    break;
	default:
	    /* Older setups may pass options this version doesn't know
	     * through pgpverifyargs: carry on rather than fail every check.
	     */
	    fprintf(stderr, "pgpverify: ignoring unknown option %s\n", ptr - 2);
	    break;
	}
    }

    /*
     * Read the whole article first.  A NUL byte or a bare CR can make the
     * text we verify differ from what dreaderd acts on (C strings stop at a
     * NUL, the control handlers go by length), so such an article can't be
     * verified at all.  lib/pgp.c has already turned CRLF into LF.
     */

    {
	char *art = NULL;
	size_t artLen = 0;
	size_t n;
	size_t k;

	while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
	    if ((art = realloc(art, artLen + n + 1)) == NULL)
		exit(2);
	    memcpy(art + artLen, buf, n);
	    artLen += n;
	}
	if (art == NULL || artLen == 0)
	    exit(1);
	for (k = 0; k < artLen; ++k) {
	    if (art[k] == 0) {
		fprintf(stderr, "pgpverify: NUL byte in the article, can't verify\n");
		exit(3);
	    }
	    if (art[k] == '\r' && (k + 1 == artLen || art[k + 1] != '\n')) {
		fprintf(stderr, "pgpverify: bare CR in the article, can't verify\n");
		exit(3);
	    }
	}
	if ((In = fmemopen(art, artLen, "r")) == NULL)
	    exit(2);
    }

    /*
     * Scan headers in message
     */

    while (fgets(buf, sizeof(buf), In) != NULL) {
	int l;

	if ((l = strlen(buf)) == 1)	/* blank line, end of headers */
	    break;

	/*
	 * check for end of previous header
	 */
	if (he && nl && buf[0] != ' ' && buf[0] != '\t') {
	    he = NULL;
	}

	/*
	 * create new header if necessary
	 */
	if (he == NULL) {		/* new header		      */
	    if ((he = malloc(sizeof(Header))) == NULL)
		exit(2);
	    nl = 1;
	    *PHe = he;
	    PHe = &he->he_Next;
	    he->he_Next = NULL;
	    if ((he->he_Name = malloc(1)) == NULL)
		exit(2);
	    he->he_Name[0] = 0;
	}

	/*
	 * append data buffer to header
	 */
	{
	    size_t r = strlen(he->he_Name);
	    char *name = realloc(he->he_Name, r + l + 1);

	    if (name == NULL)
		exit(2);
	    memcpy(name + r, buf, l + 1);
	    he->he_Name = name;
	}

	/*
	 * handle headers longer then our buffer size
	 */

	if (buf[l-1] != '\n')
	    nl = 0;
	else
	    nl = 1;
    }

    /*
     * Read the body: both verifiers need it after the headers.
     */

    {
	size_t n;

	while ((n = fread(buf, 1, sizeof(buf), In)) > 0) {
	    Body = realloc(Body, BodyLen + n + 1);
	    memcpy(Body + BodyLen, buf, n);
	    BodyLen += n;
	}
	if (Body == NULL)
	    Body = strdup("");
	Body[BodyLen] = 0;
    }

    /*
     * Extract X-PGP-Sig: header
     */

    if ((sig = FindHeader("X-PGP-Sig", NULL)) == NULL)
	exit(1);

    /*
     * Extract version and list of signed headers
     */

    version = strtok(sig, " \n");
    sheaders = strtok(NULL, " \n");
    signature = strtok(NULL, "");

    if (version == NULL || sheaders == NULL || signature == NULL)
	exit(2);

    /*
     * Each signed header, and X-PGP-Sig itself, must occur once.  We verify
     * the first occurrence, but dreaderd acts on the last Control:, From:
     * and Newsgroups:, so a duplicate could carry an unsigned value through
     * a good signature.  As INN's pgpverify, but case-insensitively, since
     * header names are (and dreaderd matches them that way).
     */

    if (CountHeader("X-PGP-Sig") > 1) {
	fprintf(stderr, "pgpverify: duplicate X-PGP-Sig header, can't verify\n");
	exit(3);
    }
    {
	char *copy = strdup(sheaders);
	char *name;

	if (copy == NULL)
	    exit(2);
	for (name = strtok(copy, ","); name; name = strtok(NULL, ",")) {
	    if (CountHeader(name) > 1) {
		fprintf(stderr, "pgpverify: duplicate signed header %s, can't verify\n", name);
		exit(3);
	    }
	}
	free(copy);
    }

    /*
     * Choose the verifier: the one given, else gpgv found on the system.
     */

    if (argcount == 0) {
	if ((prog = FindGpgv()) == NULL) {
	    fprintf(stderr, "pgpverify: no gpgv, gpgv2 or gpgv1 found\n");
	    exit(2);
	}
	argv[argcount++] = (char *)prog;
	argv[argcount++] = (char *)strrchr(prog, '/') + 1;
	argv[argcount] = NULL;
    }

    if (strcmp(argv[1], "pgp") == 0)
	r = VerifyPgp2(argv, version, sheaders, signature);
    else
	r = VerifyGpgv(argv, keyring, version, sheaders, signature);
    exit(r);
}

/*
 * FindGpgv() - locate gpgv (GnuPG 2.x usually, 1.x on older systems),
 *		gpgv2 or gpgv1 in the usual binary directories.
 */

static const char *
FindGpgv(void)
{
    static const char *dirs[] = { "/usr/bin", "/usr/local/bin", "/opt/local/bin",
				  "/usr/pkg/bin", "/opt/ooce/bin", NULL };
    static const char *names[] = { "gpgv", "gpgv2", "gpgv1", NULL };
    static char path[PATH_MAX];
    int n;
    int d;

    for (n = 0; names[n]; ++n) {
	for (d = 0; dirs[d]; ++d) {
	    snprintf(path, sizeof(path), "%s/%s", dirs[d], names[n]);
	    if (access(path, X_OK) == 0)
		return(path);
	}
    }
    return(NULL);
}

/*
 * PutStripped() - write text with trailing blanks removed from each line,
 *		   as signcontrol and INN's pgpverify do (PGP's clear
 *		   signatures ignore trailing whitespace).
 */

static void
PutStripped(FILE *fo, const char *text)
{
    while (*text) {
	const char *eol = strchr(text, '\n');
	const char *end = eol ? eol : text + strlen(text);
	const char *e = end;

	while (e > text && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
	    --e;
	fwrite(text, 1, e - text, fo);
	if (eol == NULL)
	    break;
	fputc('\n', fo);
	text = eol + 1;
    }
}

/*
 * VerifyGpgv() - verify with gpgv as a detached signature.
 *
 * The signed text is X-Signed-Headers, the signed headers, a blank line
 * and the body.  It goes into one temporary file, the ASCII-armored
 * signature into another, and gpgv reports the result on its status fd.
 * Only options gpgv 1.x and 2.x both understand are used.
 */

static int
VerifyGpgv(char **argv, const char *keyring, const char *version,
	   char *sheaders, const char *signature)
{
    const char *tmpdir = getenv("TMPDIR");
    char msgfile[PATH_MAX];
    char sigfile[PATH_MAX];
    char homeenv[PATH_MAX];
    char defring[PATH_MAX];
    char *gargv[20];	/* up to 10 from argv, 6 of ours, NULL */
    char *env[2];
    char buf[1024];
    int mfd;
    int sfd;
    int n;
    int fds[3] = { -1, -1, -1 };
    int r = 3;
    pid_t pid;
    FILE *fo;
    FILE *fi;
    const char *name;

    if (tmpdir == NULL || *tmpdir == 0)
	tmpdir = "/tmp";
    snprintf(msgfile, sizeof(msgfile), "%s/pgpverify.msg.XXXXXX", tmpdir);
    snprintf(sigfile, sizeof(sigfile), "%s/pgpverify.sig.XXXXXX", tmpdir);
    if ((mfd = mkstemp(msgfile)) < 0)
	return(2);
    if ((sfd = mkstemp(sigfile)) < 0) {
	close(mfd);
	unlink(msgfile);
	return(2);
    }

    /*
     * signed text
     */

    fo = fdopen(mfd, "w");
    fprintf(fo, "X-Signed-Headers: %s\n", sheaders);
    for (name = strtok(sheaders, ","); name; name = strtok(NULL, ",")) {
	fprintf(fo, "%s:", name);
	PutStripped(fo, FindHeader(name, "\n"));
    }
    fprintf(fo, "\n");
    PutStripped(fo, Body);	/* including its final newline, as signed */
    fclose(fo);

    /*
     * ASCII-armored signature
     */

    fo = fdopen(sfd, "w");
    fprintf(fo, "-----BEGIN PGP SIGNATURE-----\n");
    fprintf(fo, "Version: %s\n", version);
    fprintf(fo, "\n");
    for (name = signature; *name; ++name) {
	if (*name == ' ' || *name == '\t' || *name == '\r')
	    continue;
	fputc(*name, fo);
    }
    if (name > signature && name[-1] != '\n')
	fputc('\n', fo);
    fprintf(fo, "-----END PGP SIGNATURE-----\n");
    fclose(fo);

    /*
     * gpgv --status-fd 1 [--keyring K] sigfile msgfile
     *
     * Without -k, use <home>/pgp/trustedkeys.gpg when it exists, else
     * gpgv's default keyring under HOME (~/.gnupg/trustedkeys.*).
     */

    if (keyring == NULL) {
	snprintf(defring, sizeof(defring), "%s/pgp/trustedkeys.gpg", NewsHome);
	if (access(defring, R_OK) == 0)
	    keyring = defring;
    } else if (keyring[0] != '/') {
	/* gpgv looks for a relative --keyring in its GnuPG home, not here */
	char cwd[PATH_MAX];

	if (getcwd(cwd, sizeof(cwd)) != NULL) {
	    int n = snprintf(defring, sizeof(defring), "%s/%s", cwd, keyring);

	    if (n > 0 && (size_t)n < sizeof(defring))
		keyring = defring;
	}
    }
    n = 0;
    while (n < 10 && argv[n] != NULL) {
	gargv[n] = argv[n];
	++n;
    }
    gargv[n++] = "--status-fd";
    gargv[n++] = "1";
    if (keyring) {
	gargv[n++] = "--keyring";
	gargv[n++] = (char *)keyring;
    }
    gargv[n++] = sigfile;
    gargv[n++] = msgfile;
    gargv[n] = NULL;

    snprintf(homeenv, sizeof(homeenv), "HOME=%s", NewsHome);
    env[0] = homeenv;
    env[1] = NULL;

    if (DebugOpt) {
	for (n = 0; gargv[n]; ++n)
	    fprintf(stderr, "gpgv argv[%d]=%s\n", n, gargv[n]);
    }

    pid = RunProgramPipe(fds, RPF_STDIN, gargv, env);
    if (pid <= 0) {
	unlink(msgfile);
	unlink(sigfile);
	return(2);
    }

    /*
     * [GNUPG:] GOODSIG <keyid> <user id>
     */

    fi = fdopen(fds[0], "r");
    while (fgets(buf, sizeof(buf), fi) != NULL) {
	if (DebugOpt)
	    fprintf(stderr, ">> %s", buf);
	if (r == 3 && strncmp(buf, "[GNUPG:] GOODSIG ", 17) == 0) {
	    char *uid = strchr(buf + 17, ' ');
	    char *tok = uid ? strtok(uid, " \t\r\n") : NULL;

	    if (tok && tok[0]) {
		printf("%s\n", tok);
		r = 0;
	    }
	}
    }
    fclose(fi);
    {
	int status = 0;

	waitpid(pid, &status, 0);
	if (r == 0 && !(WIFEXITED(status) && WEXITSTATUS(status) == 0))
	    r = 3;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 9)
	    r = 2;		/* RunProgramPipe could not exec it */
    }
    if (DebugOpt < 2) {
	unlink(msgfile);
	unlink(sigfile);
    }
    return(r);
}

/*
 * VerifyPgp2() - PGP 2.x: pipe the clear-signed message into "pgp -f"
 *		  and read the signer from its "Good signature from" line.
 */

static int
VerifyPgp2(char **argv, const char *version, char *sheaders,
	   const char *signature)
{
    char buf[8192];
    char nhtmp[PATH_MAX];
    char *env[2];
    int fds[3] = { -1, -1, -1 };
    int r = 2;
    pid_t pid;
    const char *name;

    env[0] = nhtmp;
    env[1] = NULL;
    snprintf(nhtmp, sizeof(nhtmp), "HOME=%s", NewsHome);

    pid = RunProgramPipe(fds, RPF_STDOUT|RPF_STDERR, argv, env);
    if (pid <= 0)
	return(2);

    {
	FILE *fo = fdopen(fds[1], "w");
	const char *p;

	fprintf(fo, "-----BEGIN PGP SIGNED MESSAGE-----\n");
	fprintf(fo, "\n");
	fprintf(fo, "X-Signed-Headers: %s\n", sheaders);

	for (name = strtok(sheaders, ","); name; name = strtok(NULL, ",")) {
	    fprintf(fo, "%s:%s", name, FindHeader(name, "\n"));
	}

	fprintf(fo, "\n");

	for (p = Body; *p; ) {
	    const char *eol = strchr(p, '\n');
	    size_t l = eol ? (size_t)(eol - p + 1) : strlen(p);

	    if (*p == '-')
		fputs("- ", fo);
	    fwrite(p, 1, l, fo);
	    p += l;
	}

	fprintf(fo, "\n");
	fprintf(fo, "-----BEGIN PGP SIGNATURE-----\n");
	fprintf(fo, "Version: %s\n", version);
	fprintf(fo, "\n");

	for (name = signature; *name; ++name) {
	    if (*name == ' ' || *name == '\t')
		continue;
	    fputc(*name, fo);
	}

	fprintf(fo, "-----END PGP SIGNATURE-----\n");
	fclose(fo);
    }

    {
	FILE *fi = fdopen(fds[2], "r");
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    char *good;

	    if (DebugOpt)
		printf(">> %s", buf);
	    if (strstr(buf, "Bad signature")) {
		r = 3;
		break;
	    }
	    if ((good = strstr(buf, "Good signature from"))) {
		char *tok;
		good = strchr(good + 19, '"');
		if (good == NULL)
		    break;
		tok = strtok(good + 1, " \"\n");
		if (tok && tok[0]) {
		    r = 0;
		    printf("%s\n", tok);
		}
		break;
	    }
	}
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    if (DebugOpt)
		printf(">> %s", buf);
	}
	fclose(fi);
    }
    waitpid(pid, NULL, 0);
    return(r);
}

/*
 * FindHeader() - header body (after the colon), matched case-insensitively
 *		  as header names are.
 */

/*
 * CountHeader() - how many headers called name (case-insensitive) the
 *		   article has.
 */
static int
CountHeader(const char *name)
{
    Header *he;
    int l = strlen(name);
    int n = 0;

    for (he = HBase; he; he = he->he_Next) {
	if (strncasecmp(he->he_Name, name, l) == 0 && he->he_Name[l] == ':')
	    ++n;
    }
    return(n);
}

char *
FindHeader(const char *name, char *def)
{
    Header *he;
    int l = strlen(name);

    for (he = HBase; he; he = he->he_Next) {
	if (strncasecmp(he->he_Name, name, l) == 0 && he->he_Name[l] == ':')
	    break;
    }
    if (he)
	return(he->he_Name + l + 1);
    return(def);
}
