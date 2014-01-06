/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)keyword.c	8.5 (Berkeley) 4/2/94
 * $FreeBSD: src/bin/ps/keyword.c,v 1.24.2.3 2002/10/10 20:05:32 jmallett Exp $
 */

#include <sys/user.h>
#include <sys/kinfo.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>

#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps.h"

static struct varent	*makevarent(const char *);
static int		 vcmp(const void *, const void *);

struct varent_head var_head = STAILQ_HEAD_INITIALIZER(var_head);

#ifdef NOTINUSE
int	utime(), stime(), ixrss(), idrss(), isrss();
	{{"utime"}, "UTIME", USER, utime, NULL, 4},
	{{"stime"}, "STIME", USER, stime, NULL, 4},
	{{"ixrss"}, "IXRSS", USER, ixrss, NULL, 4},
	{{"idrss"}, "IDRSS", USER, idrss, NULL, 4},
	{{"isrss"}, "ISRSS", USER, isrss, NULL, 4},
#endif

/* Compute offset in common structures. */
#define	POFF(x)	offsetof(struct kinfo_proc, kp_##x)
#define	LPOFF(x) offsetof(struct kinfo_lwp, kl_##x)
#define	ROFF(x)	offsetof(struct rusage, x)

#define	UIDFMT	"u"
#define	UIDLEN	5
#define	PIDFMT	"d"
#define	PIDLEN	5
#define USERLEN (MAXLOGNAME-1)

static const VAR var[] = {
	{"%cpu", "%CPU", NULL, 0, pcpu, NULL, 4, 0, 0, NULL, NULL},
	{"%mem", "%MEM", NULL, 0, pmem, NULL, 4, 0, 0, NULL, NULL},
	{"acflag", "ACFLG", NULL, 0, pvar, NULL, 3, POFF(acflag), USHORT, "x",
		NULL},
	{"acflg", "", "acflag", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"batch", "BAT", NULL, 0, lpest, NULL, 3, LPOFF(origcpu), UINT, "d", NULL},
	{"blocked", "", "sigmask", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"caught", "", "sigcatch", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"comm", "", "ucomm", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"command", "COMMAND", NULL, COMM|LJUST|USER, command, NULL, 16, 0, 0, NULL,
		NULL},
	{"cpu", "CPU", NULL, 0, lpest, NULL, 3, LPOFF(estcpu), UINT, "d", NULL},
	{"cputime", "", "time", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"f", "F", NULL, 0, pvar, NULL, 7, POFF(flags), INT, "x", NULL},
	{"flags", "", "f", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
#if 0
	{"iac", "IAC",  NULL, 0, pvar, NULL, 4, POFF(p_usdata.bsd4.interactive), CHAR, PIDFMT,
		NULL},
#endif
	{"ignored", "", "sigignore", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"inblk", "INBLK", NULL, USER, rvar, NULL, 4, ROFF(ru_inblock), LONG, "ld",
		NULL},
	{"inblock", "", "inblk", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"jail", "JAIL", NULL, 0, pvar, NULL, 3, POFF(jailid), INT, "d", NULL},
	{"jobc", "JOBC", NULL, 0, pvar, NULL, 4, POFF(jobc), SHORT, "d", NULL},
	{"ktrace", "KTRACE", NULL, 0, pvar, NULL, 8, POFF(traceflag), INT, "x",
		NULL},
#if 0
	{"ktracep", "KTRACEP", NULL, 0, pvar, NULL, 8, POFF(p_tracenode), LONG, "lx",
		NULL},
#endif
	{"lastcpu", "C", NULL, 0, lpvar, NULL, 3, LPOFF(cpuid), UINT, "d", NULL},
	{"lim", "LIM", NULL, 0, maxrss, NULL, 5, 0, 0, NULL, NULL},
	{"login", "LOGIN", NULL, LJUST, logname, NULL, MAXLOGNAME-1, 0, 0, NULL,
		NULL},
	{"logname", "", "login", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"lstart", "STARTED", NULL, LJUST|USER, lstarted, NULL, 28, 0, 0, NULL, NULL},
	{"majflt", "MAJFLT", NULL, USER, rvar, NULL, 4, ROFF(ru_majflt), LONG, "ld",
		NULL},
	{"minflt", "MINFLT", NULL, USER, rvar, NULL, 4, ROFF(ru_minflt), LONG, "ld",
		NULL},
	{"msgrcv", "MSGRCV", NULL, USER, rvar, NULL, 4, ROFF(ru_msgrcv), LONG, "ld",
		NULL},
	{"msgsnd", "MSGSND", NULL, USER, rvar, NULL, 4, ROFF(ru_msgsnd), LONG, "ld",
		NULL},
	{"ni", "", "nice", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"nice", "NI", NULL, 0, pnice, NULL, 3, 0, 0, NULL, NULL},
	{"nivcsw", "NIVCSW", NULL, USER, rvar, NULL, 5, ROFF(ru_nivcsw), LONG, "ld",
		NULL},
	{"nlwp", "NLWP", NULL, 0, pvar, NULL, 4, POFF(nthreads), INT, "d",
		NULL},
	{"nsignals", "", "nsigs", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"nsigs", "NSIGS", NULL, USER, rvar, NULL, 4, ROFF(ru_nsignals), LONG, "ld",
		NULL},
	{"nswap", "NSWAP", NULL, USER, rvar, NULL, 4, ROFF(ru_nswap), LONG, "ld",
		NULL},
	{"nvcsw", "NVCSW", NULL, USER, rvar, NULL, 5, ROFF(ru_nvcsw), LONG, "ld",
		NULL},
	{"nwchan", "WCHAN", NULL, 0, lpvar, NULL, 8, LPOFF(wchan), KPTR, "lx", NULL},
	{"oublk", "OUBLK", NULL, USER, rvar, NULL, 4, ROFF(ru_oublock), LONG, "ld",
		NULL},
	{"oublock", "", "oublk", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"p_ru", "P_RU", NULL, 0, pvar, NULL, 6, POFF(ru), KPTR, "lx", NULL},
	{"paddr", "PADDR", NULL, 0, pvar, NULL, 8, POFF(paddr), KPTR, "lx", NULL},
	{"pagein", "PAGEIN", NULL, USER, pagein, NULL, 6, 0, 0, NULL, NULL},
	{"pcpu", "", "%cpu", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"pending", "", "sig", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"pgid", "PGID", NULL, 0, pvar, NULL, PIDLEN, POFF(pgid), UINT, PIDFMT,
		NULL},
	{"pid", "PID", NULL, 0, pvar, NULL, PIDLEN, POFF(pid), UINT, PIDFMT,
		NULL},
	{"pmem", "", "%mem", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"ppid", "PPID", NULL, 0, pvar, NULL, PIDLEN, POFF(ppid), UINT, PIDFMT,
		NULL},
	{"pri", "PRI", NULL, 0, pri, NULL, 3, 0, 0, NULL, NULL},
	{"re", "RE", NULL, 0, lpvar, NULL, 3, POFF(swtime), UINT, "d", NULL},
	{"rgid", "RGID", NULL, 0, pvar, NULL, UIDLEN, POFF(rgid),
		UINT, UIDFMT, NULL},
#if 0
	{"rlink", "RLINK", NULL, 0, pvar, NULL, 8, POFF(p_lwp.lwp_procq.tqe_prev), KPTR, "lx",
		NULL},
#endif
	{"rss", "RSS", NULL, 0, p_rssize, NULL, 6, 0, 0, NULL, NULL},
	{"rssize", "", "rsz", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"rsz", "RSZ", NULL, 0, rssize, NULL, 6, 0, 0, NULL, NULL},
	{"rtprio", "RTPRIO", NULL, 0, rtprior, NULL, 7, 0, 0, NULL, NULL},
	{"ruid", "RUID", NULL, 0, pvar, NULL, UIDLEN, POFF(ruid),
		UINT, UIDFMT, NULL},
	{"ruser", "RUSER", NULL, LJUST|DSIZ, runame, s_runame, USERLEN, 0, 0, NULL,
		NULL},
	{"sess", "SESS", NULL, 0, pvar, NULL, 6, POFF(sid), UINT, PIDFMT, NULL},
	{"sig", "PENDING", NULL, 0, pvar, NULL, 8, POFF(siglist), INT, "x", NULL},
	{"sigcatch", "CAUGHT", NULL, 0, pvar, NULL, 8, POFF(sigcatch),
		UINT, "x", NULL},
	{"sigignore", "IGNORED", NULL, 0, pvar, NULL, 8, POFF(sigignore),
		UINT, "x", NULL},
	{"sigmask", "BLOCKED", NULL, 0, lpvar, NULL, 8, LPOFF(sigmask), UINT, "x",
		NULL},
	{"sl", "SL", NULL, 0, lpvar, NULL, 3, LPOFF(slptime), UINT, "d", NULL},
	{"start", "STARTED", NULL, LJUST|USER, started, NULL, 7, 0, 0, NULL, NULL},
	{"stat", "", "state", 0, NULL, NULL, 0, 0, 0, NULL, NULL},
	{"state", "STAT  ", NULL, 0, state, NULL, 6, 0, 0, NULL, NULL},
	{"svgid", "SVGID", NULL, 0, pvar, NULL, UIDLEN, POFF(svgid), UINT,
		UIDFMT, NULL},
	{"svuid", "SVUID", NULL, 0, pvar, NULL, UIDLEN, POFF(svuid), UINT,
		UIDFMT, NULL},
	{"tdev", "TDEV", NULL, 0, tdev, NULL, 4, 0, 0, NULL, NULL},
	{"tdpri", "TDPRI", NULL, 0, tdpri, NULL, 5, 0, 0, NULL, NULL},
	{"tid", "TID", NULL, 0, lpvar, NULL, PIDLEN, LPOFF(tid), UINT, PIDFMT,
		NULL},
	{"time", "TIME", NULL, USER, cputime, NULL, 9, 0, 0, NULL, NULL},
	{"tpgid", "TPGID", NULL, 0, pvar, NULL, 4, POFF(tpgid), UINT, PIDFMT, NULL},
	{"tsess", "TSESS", NULL, 0, pvar, NULL, 6, POFF(tsid), UINT, PIDFMT, NULL},
	{"tsig", "PENDING", NULL, 0, lpvar, NULL, 8, LPOFF(siglist), INT, "x", NULL},
	{"tsiz", "TSIZ", NULL, 0, tsize, NULL, 4, 0, 0, NULL, NULL},
	{"tt", "TT ", NULL, 0, tname, NULL, 2, 0, 0, NULL, NULL},
	{"tty", "TTY", NULL, LJUST, longtname, NULL, 8, 0, 0, NULL, NULL},
	{"ucomm", "UCOMM", NULL, LJUST, ucomm, NULL, MAXCOMLEN, 0, 0, NULL, NULL},
	{"uid", "UID", NULL, 0, pvar, NULL, UIDLEN, POFF(uid),
		UINT, UIDFMT, NULL},
	{"user", "USER", NULL, LJUST|DSIZ, uname, s_uname, USERLEN, 0, 0, NULL, NULL},
	{"vsize", "", "vsz", 0, 0, NULL, 0, 0, 0, NULL, NULL},
	{"vsz", "VSZ", NULL, 0, vsize, NULL, 6, 0, 0, NULL, NULL},
	{"wchan", "WCHAN", NULL, LJUST, wchan, NULL, WMESGLEN, 0, 0, NULL, NULL},
	{"xstat", "XSTAT", NULL, 0, pvar, NULL, 7, POFF(exitstat), USHORT, "x", NULL},
	{"", NULL, NULL, 0, NULL, NULL, 0, 0, 0, NULL, NULL},
};

void
showkey(void)
{
	const VAR *v;
	int i;
	const char *p, *sep;

	i = 0;
	sep = "";
	for (v = var; *(p = v->name); ++v) {
		int len = strlen(p);
		if (termwidth && (i += len + 1) > termwidth) {
			i = len;
			sep = "\n";
		}
		printf("%s%s", sep, p);
		sep = " ";
	}
	printf("\n");
}

void
parsefmt(const char *fmt)
{
	struct varent *vent;
	char *p, *op;
	const char *cp;

	op = p = strdup(fmt);

	if (op == NULL)
		errx(1, "Not enough memory");

#define	FMTSEP	" \t,\n"
	while (p != NULL && *p != '\0') {
		while ((cp = strsep(&p, FMTSEP)) != NULL && *cp == '\0')
			/* void */;
		if (cp == NULL)
			continue;
		if ((vent = makevarent(cp)) == NULL)
			continue;
		STAILQ_INSERT_TAIL(&var_head, vent, link);
	}
	if (STAILQ_EMPTY(&var_head)) {
		warnx("no valid keywords; valid keywords:");
		showkey();
		exit(1);
	}

	free(op);
}

/*
 * Insert TID column after PID one in selected output format.
 */
void
insert_tid_in_fmt(void)
{
	struct varent *tidvent;
	struct varent *vent;

	if ((tidvent = makevarent("tid")) == NULL)
		errx(1, "Not enough memory");

	STAILQ_FOREACH(vent, &var_head, link) {
		if (strcmp(vent->var->name, "pid") == 0) {
			STAILQ_INSERT_AFTER(&var_head, vent, tidvent, link);
			break;
		}
	}
}

static struct varent *
makevarent(const char *p)
{
	struct varent *vent = NULL;
	const VAR *v;
	VAR key;
	char *hp;

	hp = strchr(p, '=');
	if (hp)
		*hp++ = '\0';

	key.name = p;
	v = bsearch(&key, var, sizeof(var)/sizeof(VAR) - 1, sizeof(VAR), vcmp);

	if (v && v->alias) {
		if (hp) {
			warnx("%s: illegal keyword specification", p);
			eval = 1;
		}
		parsefmt(v->alias);
		return(NULL);
	}
	if (!v) {
		warnx("%s: keyword not found", p);
		eval = 1;
		return(NULL);
	}
	if ((vent = malloc(sizeof(struct varent))) == NULL)
		err(1, NULL);
	vent->var = v;
	vent->width = v->width;
	if (hp) {
		vent->header = strdup(hp);
		if (vent->header == NULL)
			err(1, "Not enough memory");
	}
	else
		vent->header = v->header;
	return (vent);
}

static int
vcmp(const void *a, const void *b)
{
        return (strcmp(((const VAR *)a)->name, ((const VAR *)b)->name));
}
