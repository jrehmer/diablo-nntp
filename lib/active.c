
/*
 * LIB/ACTIVE.C	- dactive.kp active file support
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype void InitDActive(const char *fileNamePat);
Prototype int InActive(const char *nglist);
Prototype int GenerateXRef(Buffer *b, const char *nglist, const char *npName, const char *ctl);
Prototype int UpdateActiveNX(char *xrefdata);
Prototype int CheckForModeratedGroups(const char *nglist);
Prototype int FirstModeratedGroup(const char *nglist, char *buf, int bufLen);

KPDB *DActiveDB;

void 
InitDActive(const char *fileNamePat)
{
    DActiveDB = KPDBOpen(PatDbExpand(fileNamePat), O_RDWR);
    if (DActiveDB == NULL) {
	logit(LOG_CRIT, "InitDActive, open failed: %s", PatDbExpand(fileNamePat));
	exit(1);
    }
}

/*
 * Check to see if any of a list of comma-separated newsgroups exist in
 * the active file
 *
 *	Returns: 0 = no groups in active
 *		 1 = one or more groups in active
 */
int 
InActive(const char *nglist)
{
    const char *p = nglist;
    char group[MAXGNAME];
    int found = 0;

    while (*p) {
	int i;
	for (i = 0; p[i] && p[i] != ',' && p[i] != ' ' && p[i] != '\t'; ++i)
	    ;
	if (i == 0) {
	    /* empty entry (',,' or a leading comma): skip it, don't stop */
	    ++p;
	    continue;
	}
	if (i < sizeof(group)) {
	    const char *rec;
	    int recLen;

	    bcopy(p, group, i);
	    group[i] = 0;
	    rec = KPDBReadRecord(DActiveDB, group, 0, &recLen);
	    if (rec != NULL) {
		found = 1;
		break;
	    }
	}
	p += i;
	if (*p != ',')
	    break;
	++p;
	while (*p == ' ' || *p == '\t')
	    ++p;
    }
    return(found);
}

/*
 * A group listed twice in Newsgroups: gets one Xref entry and one article
 * number, not one for each repeat.  The first entries of a list are
 * compared with the ones before them; from XREF_SEEN_LINEAR entries on,
 * the names seen so far are kept in a hash table, so a list of thousands
 * of names costs one lookup per name instead of a scan of the whole list.
 * The table holds offsets into nglist (+1, 0 is an empty slot); a list is
 * at most MAXLINE long, so at most MAXLINE/2 names go into it.
 */

#define XREF_SEEN_LINEAR	32
#define XREF_SEEN_SLOTS		16384		/* power of 2, >= MAXLINE */

static unsigned short XrefSeenTab[XREF_SEEN_SLOTS];

static int
xrefNameLen(const char *q, const char *end)
{
    int i;

    for (i = 0; (end == NULL || q + i < end) && q[i] && q[i] != ',' && q[i] != ' ' && q[i] != '\t'; ++i)
	;
    return(i);
}

/*
 * xrefGroupSeen() - return 1 if the group name of length len at p is
 *		     also an earlier entry of nglist.
 */

static int
xrefGroupSeen(const char *nglist, const char *p, int len)
{
    const char *q = nglist;

    while (q < p) {
	int i;

	while (q < p && (*q == ' ' || *q == ','))
	    ++q;
	i = xrefNameLen(q, p);
	if (i == len && strncmp(q, p, len) == 0)
	    return(1);
	if (i == 0 && q < p)
	    ++q;		/* a TAB: not an entry, step over it */
	q += i;
    }
    return(0);
}

/*
 * xrefHashSeen() - return 1 if the name of length len at p is in the
 *		    table already, otherwise enter it and return 0.
 */

static int
xrefHashSeen(const char *nglist, const char *p, int len)
{
    unsigned int h = 2166136261U;
    int i;

    for (i = 0; i < len; ++i)
	h = (h ^ (unsigned char)p[i]) * 16777619U;
    for (h &= XREF_SEEN_SLOTS - 1; XrefSeenTab[h]; h = (h + 1) & (XREF_SEEN_SLOTS - 1)) {
	const char *q = nglist + XrefSeenTab[h] - 1;

	if (xrefNameLen(q, NULL) == len && strncmp(q, p, len) == 0)
	    return(1);
    }
    XrefSeenTab[h] = (unsigned short)(p - nglist + 1);
    return(0);
}

/*
 * xrefHashStart() - enter the names of nglist before p into the table.
 */

static void
xrefHashStart(const char *nglist, const char *p)
{
    const char *q = nglist;

    memset(XrefSeenTab, 0, sizeof(XrefSeenTab));
    while (q < p) {
	int i;

	while (q < p && (*q == ' ' || *q == ','))
	    ++q;
	if ((i = xrefNameLen(q, p)) > 0)
	    (void)xrefHashSeen(nglist, q, i);
	else if (q < p)
	    ++q;
	q += i;
    }
}

/*
 * GenerateXRef() - generate XRef: header for feeder.  Return the number
 *		    of groups that were included in the header.
 *
 *		    Note: if this is a control message, no XRef is generated
 *		    but we still return the group-match count.
 */

int 
GenerateXRef(Buffer *b, const char *nglist, const char *npName, const char *ctl)
{
    const char *p = nglist;
    char group[MAXGNAME];
    int count = 0;
    int doneHeader = 0;
    int ntok = 0;
    int useHash = 0;

    while (*p) {
	int i;
	int seen;
	while (*p == ' ')
	    ++p;
	for (i = 0; p[i] && p[i] != ',' && p[i] != ' ' && p[i] != '\t'; ++i)
	    ;
	if (i == 0) {
	    /* empty entry (',,' or a leading comma): skip it, don't stop */
	    ++p;
	    continue;
	}
	/*
	 * Seen before in this list?  The table is only used for a list
	 * that fits its 16-bit offsets, which every list from LoadArticle()
	 * does.
	 */
	if (ntok == XREF_SEEN_LINEAR && strlen(nglist) < XREF_SEEN_SLOTS) {
	    xrefHashStart(nglist, p);
	    useHash = 1;
	}
	++ntok;
	if (useHash)
	    seen = xrefHashSeen(nglist, p, i);
	else
	    seen = xrefGroupSeen(nglist, p, i);
	if (i < sizeof(group) && seen == 0) {
	    const char *rec;
	    int recLen;

	    bcopy(p, group, i);
	    group[i] = 0;
	    rec = KPDBReadRecord(DActiveDB, group, KP_LOCK, &recLen);

	    /*
	     * Create ALL seen groups, if this define is enabled.  You will
	     * have to manually add a GD, M, and fix S as appropriate
	     * through some external process, if you use this.
	     */

	    if (! rec && DOpts.FeederAutoAddToActive) {
		if (ValidGroupName(group) < 0) {
		    /* logit(LOG_ERR, "group %s illegal", group); */
		} else {
		    char tsBuf[64];

		    /*
		     * Create if absent (D-254): the group may have been made,
		     * and numbered, since the read above.  Then it is left as
		     * it is.
		     */
		    if (KPDBCreate(DActiveDB, group, "NB", "1", KP_LOCK) == 0) {
			KPDBWrite(DActiveDB, group, "NE", "0", KP_LOCK_CONTINUE);
			sprintf(tsBuf, "%08x", (int)time(NULL));
			KPDBWrite(DActiveDB, group, "CTS", tsBuf, KP_LOCK_CONTINUE);
			KPDBWrite(DActiveDB, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
			KPDBWrite(DActiveDB, group, "S", "y", KP_UNLOCK);
		    } else {
			KPDBWrite(DActiveDB, group, NULL, NULL, KP_UNLOCK);
		    }
	    	    rec = KPDBReadRecord(DActiveDB, group, KP_LOCK, &recLen);
		}
	    }

	    /*
	     * Control messages are special, Xref header is still required,
	     * but must only contain control.* groups. Therefore, for non
	     * control messages, process as normal. But, only the control
	     * group is put in the Xref for control messages.
	     */

	    if ((rec != NULL && !ctl[0]) || (rec != NULL && ctl[0] &&
					(strncmp("control",group,7) == 0)))
		{
		int f1len;
		int f2len;
		const char *f1 = KPDBGetField(rec, recLen, "NE", &f1len, NULL);
		const char *f2 = KPDBGetField(rec, recLen, "NX", &f2len, NULL);

		if (f1 || f2) {
		    artno_t n;
		    char buf[20];

		    if (f2)
			n = (strtoll(f2, NULL, 10) + 1) & 0x7FFFFFFFFFFFFFFFLL;
		    else
			n = (strtoll(f1, NULL, 10) + 1) & 0x7FFFFFFFFFFFFFFFLL;

		    sprintf(buf, "%010lld", n);
		    KPDBWrite(DActiveDB, group, "NX", buf, KP_UNLOCK);
		    if (!doneHeader) {
			bwrite(b, "Xref: ", 6);
			bwrite(b, npName, strlen(npName));
			doneHeader = 1;
		    }
		    bwrite(b, " ", 1);
		    bwrite(b, group, i);
		    bwrite(b, ":", 1);
		    sprintf(buf, "%lld", n);
		    bwrite(b, buf, strlen(buf));
		    ++count;
		} else {
		    KPDBUnlock(DActiveDB, rec);
		}

	    /*
	     * Now catch the non control groups that were listed for a
	     * control message and unlock the active file.
	     */
	    } else if (rec != NULL && ctl[0]) {
                        ++count;
                        KPDBUnlock(DActiveDB, rec);
                    }
	}
	p += i;
	while (*p == ' ')
	    ++p;
	if (*p != ',')
	    break;
	++p;
    }
    if (doneHeader) {
	/* the article is stored as it came in when wireformat is on */
	if (DOpts.WireFormat)
	    bwrite(b, "\r\n", 2);
	else
	    bwrite(b, "\n", 1);
    }
    return(count);
}

/*
 * CheckForModeratedGroups() - return 1 if any of the newsgroups listed are
 *			       marked as moderated in our active file.
 */
int
CheckForModeratedGroups(const char *nglist)
{
    return(FirstModeratedGroup(nglist, NULL, 0));
}

/*
 * FirstModeratedGroup() - like CheckForModeratedGroups(), and copy the
 *			   first moderated group's name into buf (if given).
 */
int
FirstModeratedGroup(const char *nglist, char *buf, int bufLen)
{
    const char *p = nglist;
    char group[MAXGNAME];
    int foundModerated = 0;

    while (*p) {
	int i;
	for (i = 0; p[i] && p[i] != ',' && p[i] != ' ' && p[i] != '\t'; ++i)
	    ;
	if (i == 0) {
	    /* empty entry (',,' or a leading comma): skip it, don't stop */
	    ++p;
	    continue;
	}

	if (i < sizeof(group)) {
	    const char *rec;
	    int recLen;

	    bcopy(p, group, i);
	    group[i] = 0;
	    rec = KPDBReadRecord(DActiveDB, group, 0, &recLen);

	    if (rec != NULL) {
		int sLen;
		const char *s = KPDBGetField(rec, recLen, "S", &sLen, NULL);

		if (s && ((s[0] == 'm') || (s[0] == 'M'))) {
		    foundModerated = 1;
		    if (buf != NULL && bufLen > 0)
			snprintf(buf, bufLen, "%s", group);
		    break;
		}
	    }
	}

	p += i;
	if (*p != ',')
	    break;
	++p;
	while (*p == ' ' || *p == '\t')
	    ++p;
    }

    return foundModerated;
}

/*
 * UpdateActiveNX - update the active file NX field based on Xref: data
 *  This is useful for slaving Xref: data, but also maintaining
 *  the NX field in case this machine needs to become the Xref: master.
 *
 *  A group that is not in the active file is skipped; the groups after
 *  it are still updated.  An entry whose number is empty, not all digits
 *  or too large leaves that group's NX as it is (the group still counts
 *  as found).
 *
 *  Returns -1 if there is no Xref: data, 0 if the Xref: line names
 *  groups but none of them is in the active file, and 1 otherwise.
 */
int
UpdateActiveNX(char *xrefdata)
{
    char *p, *artnum, group[MAXGNAME];
    int named = 0;
    int found = 0;

    /* Throw away the first part of the line */
    p=strtok(xrefdata, " \t\n");
    if (p == NULL)
	return(-1);

    /* Now get the group:artnum pairs one at a time */
    p=strtok(NULL, " \t\n");
    while (p != NULL) {
	strncpy(group, p, sizeof(group) - 1);
	group[sizeof(group) - 1] = '\0';
	artnum=strchr(group, ':');
	if (artnum != NULL) {
	    const char *rec;
	    int recLen;

	    group[artnum-group]='\0';
	    artnum++;	
	    ++named;
		
	    if (DebugOpt)
		ddprintf("XREF UPDATE %s %s\n",group, artnum);

	    rec=KPDBReadRecord(DActiveDB, group, KP_LOCK, &recLen);
	    if (rec != NULL) {
		artno_t n;
		char buf[20];
		char *end;

		++found;
		errno = 0;
		n = strtoll(artnum, &end, 10);
		if (*artnum < '0' || *artnum > '9' || *end != '\0' ||
								errno == ERANGE) {
		    KPDBUnlock(DActiveDB, rec);
		} else {
		    sprintf(buf, "%010lld", n);
		    KPDBWrite(DActiveDB, group, "NX", buf, KP_UNLOCK);
		}
	    }
	}
	p=strtok(NULL, " \t\n");
    }
    return((named > 0 && found == 0) ? 0 : 1);
}

