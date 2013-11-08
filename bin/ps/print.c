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
 * @(#)print.c	8.6 (Berkeley) 4/16/94
 * $FreeBSD: src/bin/ps/print.c,v 1.36.2.4 2002/11/30 13:00:14 tjr Exp $
 * $DragonFly: src/bin/ps/print.c,v 1.34 2008/11/10 14:56:33 swildner Exp $
 */

#include <sys/user.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <sys/ucred.h>
#include <sys/sysctl.h>
#include <sys/rtprio.h>
#include <vm/vm.h>

#include <err.h>
#include <langinfo.h>
#include <locale.h>
#include <math.h>
#include <nlist.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <vis.h>

#include "ps.h"

static const char *make_printable(const char *str);
static void put64(u_int64_t n, int w, int type);

void
printheader(void)
{
	const VAR *v;
	struct varent *vent;
	int allempty;

	allempty = 1;
	STAILQ_FOREACH(vent, &var_head, link) {
		if (*vent->header != '\0') {
			allempty = 0;
			break;
		}
	}
	if (allempty)
		return;
	STAILQ_FOREACH(vent, &var_head, link) {
		v = vent->var;
		if (v->flag & LJUST) {
			if (STAILQ_NEXT(vent, link) == NULL)	/* last one */
				printf("%s", vent->header);
			else
				printf("%-*s", vent->width, vent->header);
		} else
			printf("%*s", vent->width, vent->header);
		if (STAILQ_NEXT(vent, link) != NULL)
			putchar(' ');
	}
	putchar('\n');
}

void
command(const KINFO *k, const struct varent *vent)
{
	int left;
	int indent;
	char *cp, *vis_env, *vis_args;

	if (cflag) {
		/* Don't pad the last field. */
		if (STAILQ_NEXT(vent, link) == NULL)
			printf("%s", make_printable(KI_PROC(k, comm)));
		else
			printf("%-*s", vent->width, 
				make_printable(KI_PROC(k, comm)));
		return;
	}

	if ((vis_args = malloc(strlen(k->ki_args) * 4 + 1)) == NULL)
		err(1, NULL);
	strvis(vis_args, k->ki_args, VIS_TAB | VIS_NL | VIS_NOSLASH);
	if (k->ki_env) {
		if ((vis_env = malloc(strlen(k->ki_env) * 4 + 1)) == NULL)
			err(1, NULL);
		strvis(vis_env, k->ki_env, VIS_TAB | VIS_NL | VIS_NOSLASH);
	} else {
		vis_env = NULL;
	}

	indent = k->ki_indent;
	if (indent < 0)
		indent = 0;

	if (STAILQ_NEXT(vent, link) == NULL) {
		/* last field */
		if (termwidth == UNLIMITED) {
			if (vis_env)
				printf("%s ", vis_env);
			while (indent) {
				putchar(' ');
				--indent;
			}
			printf("%s", vis_args);
		} else {
			left = termwidth - (totwidth - vent->width);
			if (left < 1) /* already wrapped, just use std width */
				left = vent->width;
			while (indent && left > 1) {
				putchar(' ');
				--indent;
				--left;
			}
			if ((cp = vis_env) != NULL) {
				while (--left >= 0 && *cp)
					putchar(*cp++);
				if (--left >= 0)
					putchar(' ');
			}
			for (cp = vis_args; --left >= 0 && *cp != '\0';)
				putchar(*cp++);
		}
	} else
		/* XXX env? */
		printf("%-*.*s", vent->width, vent->width, vis_args);
	free(vis_args);
	if (vis_env != NULL)
		free(vis_env);
}

void
ucomm(const KINFO *k, const struct varent *vent)
{
	/* Do not pad the last field */
	if (STAILQ_NEXT(vent, link) == NULL)
		printf("%s", make_printable(KI_PROC(k, comm)));
	else
		printf("%-*s", vent->width, make_printable(KI_PROC(k, comm)));
}

void
logname(const KINFO *k, const struct varent *vent)
{
	const char *s = KI_PROC(k, login);

	printf("%-*s", vent->width, *s != '\0' ? s : "-");
}

void
state(const KINFO *k, const struct varent *vent)
{
	int flag;
	char *cp;
	char buf[16];

	flag = KI_PROC(k, flags);
	cp = buf;

	switch (KI_PROC(k, stat)) {

	case SSTOP:
		*cp = 'T';
		break;

	case SACTIVE:
		switch (KI_LWP(k, stat)) {
		case LSSLEEP:
			if (KI_LWP(k, flags) & LWP_SINTR) {
				/* interruptable wait short/long */
				*cp = KI_LWP(k, slptime) >= MAXSLP ? 'I' : 'S';
			}
			else if (KI_LWP(k, tdflags) & TDF_SINTR)
				*cp = 'S';	/* interruptable lwkt wait */
			else if (KI_PROC(k, paddr))
				*cp = 'D';	/* uninterruptable wait */
			else
				*cp = 'B';	/* uninterruptable lwkt wait */
			/*break;*/

		case LSRUN:
			if (KI_LWP(k, stat) == LSRUN) {
				*cp = 'R';
				if (!(KI_LWP(k, tdflags) &
				    (TDF_RUNNING | TDF_RUNQ)))
					*++cp = 'Q';
			}
			/*if (KI_LWP(k, tdflags) & (TDF_RUNNING | TDF_RUNQ))*/ {
			    ++cp;
			    sprintf(cp, "%d", KI_LWP(k, cpuid));
			    while (cp[1])
				++cp;
			}
			break;

		case LSSTOP:
			/* shouldn't happen anyways */
			*cp = 'T';
			break;
		}
		break;

	case SZOMB:
		*cp = 'Z';
		break;

	default:
		*cp = '?';
	}

	cp++;
	if (flag & P_SWAPPEDOUT)
		*cp++ = 'W';
	if (KI_PROC(k, nice) < NZERO)
		*cp++ = '<';
	else if (KI_PROC(k, nice) > NZERO)
		*cp++ = 'N';
	if (flag & P_TRACED)
		*cp++ = 'X';
	if (flag & P_WEXIT && KI_PROC(k, stat) != SZOMB)
		*cp++ = 'E';
	if (flag & P_PPWAIT)
		*cp++ = 'V';
	if ((flag & P_SYSTEM) || KI_PROC(k, lock) > 0)
		*cp++ = 'L';
	if (flag & P_JAILED)
		*cp++ = 'J';
	if (KI_PROC(k, auxflags) & KI_SLEADER)
		*cp++ = 's';
	if ((flag & P_CONTROLT) && KI_PROC(k, pgid) == KI_PROC(k, tpgid))
		*cp++ = '+';
	*cp = '\0';
	printf("%-*s", vent->width, buf);
}

/*
 * Normalized priority (lower is better).  For pure threads
 * output a negated LWKT priority (so lower still means better).
 *
 * XXX bsd4 scheduler specific.
 */
void
pri(const KINFO *k, const struct varent *vent)
{
	if (KI_LWP(k, pid) != -1)
	    printf("%*d", vent->width, KI_LWP(k, prio));
	else
	    printf("%*d", vent->width, -(KI_LWP(k, tdprio)));
}

void
tdpri(const KINFO *k, const struct varent *vent)
{
	char buf[32];
	int val = KI_LWP(k, tdprio);

	snprintf(buf, sizeof(buf), "%2d", val);
	printf("%*s", vent->width, buf);
}

void
uname(const KINFO *k, const struct varent *vent)
{
	printf("%-*s", vent->width,
	       user_from_uid(KI_PROC(k, uid), 0));
}

int
s_uname(const KINFO *k)
{
	return (strlen(user_from_uid(KI_PROC(k, uid), 0)));
}

void
runame(const KINFO *k, const struct varent *vent)
{
	printf("%-*s", vent->width,
	       user_from_uid(KI_PROC(k, ruid), 0));
}

int
s_runame(const KINFO *k)
{
	return (strlen(user_from_uid(KI_PROC(k, ruid), 0)));
}

void
tdev(const KINFO *k, const struct varent *vent)
{
	dev_t dev;
	char buff[16];

	dev = KI_PROC(k, tdev);
	if (dev == NODEV)
		printf("%*s", vent->width, "??");
	else {
		snprintf(buff, sizeof(buff), "%d/%d", major(dev), minor(dev));
		printf("%*s", vent->width, buff);
	}
}

void
tname(const KINFO *k, const struct varent *vent)
{
	dev_t dev;
	const char *ttname;

	dev = KI_PROC(k, tdev);
	if (dev == NODEV || (ttname = devname(dev, S_IFCHR)) == NULL)
		printf("%*s ", vent->width-1, "??");
	else {
		if (strncmp(ttname, "tty", 3) == 0 ||
		    strncmp(ttname, "cua", 3) == 0)
			ttname += 3;
		if (strncmp(ttname, "pts/", 4) == 0)
			ttname += 4;
		printf("%*.*s%c", vent->width-1, vent->width-1, ttname,
			KI_PROC(k, auxflags) & KI_CTTY ? ' ' : '-');
	}
}

void
longtname(const KINFO *k, const struct varent *vent)
{
	dev_t dev;
	const char *ttname;

	dev = KI_PROC(k, tdev);
	if (dev == NODEV || (ttname = devname(dev, S_IFCHR)) == NULL)
		printf("%-*s", vent->width, "??");
	else
		printf("%-*s", vent->width, ttname);
}

void
started(const KINFO *k, const struct varent *vent)
{
	static time_t now;
	time_t then;
	struct tm *tp;
	char buf[100];
	static int  use_ampm = -1;

	if (use_ampm < 0)
		use_ampm = (*nl_langinfo(T_FMT_AMPM) != '\0');

	then = KI_PROC(k, start).tv_sec;
	if (then < btime.tv_sec) {
		then = btime.tv_sec;
	}

	tp = localtime(&then);
	if (!now)
		time(&now);
	if (now - then < 24 * 3600) {
		strftime(buf, sizeof(buf) - 1,
		use_ampm ? "%l:%M%p" : "%k:%M  ", tp);
	} else if (now - then < 7 * 86400) {
		strftime(buf, sizeof(buf) - 1,
		use_ampm ? "%a%I%p" : "%a%H  ", tp);
	} else
		strftime(buf, sizeof(buf) - 1, "%e%b%y", tp);
	printf("%-*s", vent->width, buf);
}

void
lstarted(const KINFO *k, const struct varent *vent)
{
	time_t then;
	char buf[100];

	then = KI_PROC(k, start).tv_sec;
	strftime(buf, sizeof(buf) -1, "%c", localtime(&then));
	printf("%-*s", vent->width, buf);
}

void
wchan(const KINFO *k, const struct varent *vent)
{
	if (*KI_LWP(k, wmesg)) {
		printf("%-*.*s", vent->width, vent->width,
		       KI_LWP(k, wmesg));
	} else {
		printf("%-*s", vent->width, "-");
	}
}

static u_int64_t
pgtob(u_int64_t pg)
{
	static size_t pgsize;

	if (pgsize == 0)
		pgsize = getpagesize();
	return(pg * pgsize);
}

void
vsize(const KINFO *k, const struct varent *vent)
{
	put64((uintmax_t)KI_PROC(k, vm_map_size)/1024, vent->width, 'k');
}

void
rssize(const KINFO *k, const struct varent *vent)
{
	/* XXX don't have info about shared */
	put64(pgtob(KI_PROC(k, vm_rssize))/1024, vent->width, 'k');
}

/* doesn't account for text */
void
p_rssize(const KINFO *k, const struct varent *vent)
{
	put64(pgtob(KI_PROC(k, vm_rssize))/1024, vent->width, 'k');
}

void
cputime(const KINFO *k, const struct varent *vent)
{
	long secs;
	long psecs;	/* "parts" of a second. first micro, then centi */
	u_int64_t timeus;
	char obuff[128];
	static char decimal_point = '\0';

	if (decimal_point == '\0')
		decimal_point = localeconv()->decimal_point[0];

	/*
	 * This counts time spent handling interrupts.  We could
	 * fix this, but it is not 100% trivial (and interrupt
	 * time fractions only work on the sparc anyway).	XXX
	 */
	timeus = KI_LWP(k, uticks) + KI_LWP(k, sticks) +
		KI_LWP(k, iticks);
	secs = timeus / 1000000;
	psecs = timeus % 1000000;
	if (sumrusage) {
		secs += KI_PROC(k, cru).ru_utime.tv_sec +
			KI_PROC(k, cru).ru_stime.tv_sec;
		psecs += KI_PROC(k, cru).ru_utime.tv_usec +
			KI_PROC(k, cru).ru_stime.tv_usec;
	}
	/*
	 * round and scale to 100's
	 */
	psecs = (psecs + 5000) / 10000;
	secs += psecs / 100;
	psecs = psecs % 100;
#if 1
	if (secs >= 86400) {
		snprintf(obuff, sizeof(obuff), "%3ldd%02ld:%02ld",
			secs / 86400, secs / (60 * 60) % 24, secs / 60 % 60);
	} else if (secs >= 100 * 60) {
		snprintf(obuff, sizeof(obuff), "%2ld:%02ld:%02ld",
			secs / 60 / 60, secs / 60 % 60, secs % 60);
	} else
#endif
	{
		snprintf(obuff, sizeof(obuff), "%3ld:%02ld%c%02ld",
			 secs / 60, secs % 60,
			 decimal_point, psecs);
	}
	printf("%*s", vent->width, obuff);
}

double
getpcpu(const KINFO *k)
{
	static int failure;

	if (!nlistread)
		failure = donlist();
	if (failure)
		return (0.0);

#define	fxtofl(fixpt)	((double)(fixpt) / fscale)

	/* XXX - I don't like this */
	if (KI_PROC(k, swtime) == 0 || (KI_PROC(k, flags) & P_SWAPPEDOUT))
		return (0.0);
	return (100.0 * fxtofl(KI_LWP(k, pctcpu)));
}

void
pcpu(const KINFO *k, const struct varent *vent)
{
	printf("%*.1f", vent->width, getpcpu(k));
}

void
pnice(const KINFO *k, const struct varent *vent)
{
	int niceval;

	switch (KI_LWP(k, rtprio).type) {
	case RTP_PRIO_REALTIME:
		niceval = PRIO_MIN - 1 - RTP_PRIO_MAX + KI_LWP(k, rtprio).prio;
		break;
	case RTP_PRIO_IDLE:
		niceval = PRIO_MAX + 1 + KI_LWP(k, rtprio).prio;
		break;
	case RTP_PRIO_THREAD:
		niceval = PRIO_MIN - 1 - RTP_PRIO_MAX - KI_LWP(k, rtprio).prio;
		break;
	default:
		niceval = KI_PROC(k, nice) - NZERO;
		break;
	}
	printf("%*d", vent->width, niceval);
}


double
getpmem(const KINFO *k)
{
	static int failure;
	double fracmem;
	int szptudot;

	if (!nlistread)
		failure = donlist();
	if (failure)
		return (0.0);

	if (KI_PROC(k, flags) & P_SWAPPEDOUT)
		return (0.0);
	/* XXX want pmap ptpages, segtab, etc. (per architecture) */
	szptudot = UPAGES;
	/* XXX don't have info about shared */
	fracmem = ((float)KI_PROC(k, vm_rssize) + szptudot)/mempages;
	return (100.0 * fracmem);
}

void
pmem(const KINFO *k, const struct varent *vent)
{
	printf("%*.1f", vent->width, getpmem(k));
}

void
pagein(const KINFO *k, const struct varent *vent)
{
	printf("%*ld", vent->width, KI_LWP(k, ru).ru_majflt);
}

/* ARGSUSED */
void
maxrss(const KINFO *k __unused, const struct varent *vent)
{
	printf("%*ld", vent->width, KI_PROC(k, ru).ru_maxrss);
}

void
tsize(const KINFO *k, const struct varent *vent)
{
	put64(pgtob(KI_PROC(k, vm_tsize))/1024, vent->width, 'k');
}

void
rtprior(const KINFO *k, const struct varent *vent)
{
	struct rtprio *prtp;
	char str[8];
	unsigned prio, type;
 
	prtp = &KI_LWP(k, rtprio);
	prio = prtp->prio;
	type = prtp->type;
	switch (type) {
	case RTP_PRIO_REALTIME:
		snprintf(str, sizeof(str), "real:%u", prio);
		break;
	case RTP_PRIO_NORMAL:
		strncpy(str, "normal", sizeof(str));
		break;
	case RTP_PRIO_IDLE:
		snprintf(str, sizeof(str), "idle:%u", prio);
		break;
	default:
		snprintf(str, sizeof(str), "%u:%u", type, prio);
		break;
	}
	str[sizeof(str) - 1] = '\0';
	printf("%*s", vent->width, str);
}

/*
 * Generic output routines.  Print fields from various prototype
 * structures.
 */
static void
printval(const char *bp, const struct varent *vent)
{
	static char ofmt[32] = "%";
	const char *fcp;
	char *cp;

	cp = ofmt + 1;
	fcp = vent->var->fmt;
	if (vent->var->flag & LJUST)
		*cp++ = '-';
	*cp++ = '*';
	while ((*cp++ = *fcp++));

	switch (vent->var->type) {
	case CHAR:
		printf(ofmt, vent->width, *(const char *)bp);
		break;
	case UCHAR:
		printf(ofmt, vent->width, *(const u_char *)bp);
		break;
	case SHORT:
		printf(ofmt, vent->width, *(const short *)bp);
		break;
	case USHORT:
		printf(ofmt, vent->width, *(const u_short *)bp);
		break;
	case INT:
		printf(ofmt, vent->width, *(const int *)bp);
		break;
	case UINT:
		printf(ofmt, vent->width, *(const u_int *)bp);
		break;
	case LONG:
		printf(ofmt, vent->width, *(const long *)bp);
		break;
	case ULONG:
		printf(ofmt, vent->width, *(const u_long *)bp);
		break;
	case KPTR:
		printf(ofmt, vent->width, *(const u_long *)bp);
		break;
	default:
		errx(1, "unknown type %d", vent->var->type);
	}
}

void
pvar(const KINFO *k, const struct varent *vent)
{
	printval((char *)((char *)k->ki_proc + vent->var->off), vent);
}

void
lpest(const KINFO *k, const struct varent *vent)
{
	int val;

	val = *(int *)((char *)&k->ki_proc->kp_lwp + vent->var->off);
	val = val / 128;
	printval((char *)&val, vent);
}


void
lpvar(const KINFO *k, const struct varent *vent)
{
	printval((char *)((char *)&k->ki_proc->kp_lwp + vent->var->off), vent);
}

void
rvar(const KINFO *k, const struct varent *vent)
{
	printval(((const char *)&KI_LWP(k, ru) + vent->var->off), vent);
}

static const char *
make_printable(const char *str)
{
    static char *cpy;
    int len;

    if (cpy)
	free(cpy);
    len = strlen(str);
    if ((cpy = malloc(len * 4 + 1)) == NULL)
	err(1, NULL);
    strvis(cpy, str, VIS_TAB | VIS_NL | VIS_NOSLASH);
    return(cpy);
}

/*
 * Output a number, divide down as needed to fit within the
 * field.  This function differs from the code used by systat
 * in that it tries to differentiate the display by always
 * using a decimal point for excessively large numbers so
 * the human eye naturally notices the difference.
 */
static void
put64(u_int64_t n, int w, int type)
{
	char b[128];
	u_int64_t d;
	u_int64_t u;
	size_t len;
	int ntype;

	snprintf(b, sizeof(b), "%*jd", w, (uintmax_t)n);
	len = strlen(b);
	if (len <= (size_t)w) {
		fwrite(b, len, 1, stdout);
		return;
	}

	if (type == 'D')
		u = 1000;
	else
		u = 1024;

	ntype = 0;
	for (d = 1; n / d >= 100000; d *= u) {
		switch(type) {
		case 'D':
		case 0:
			type = 'k';
			ntype = 'M';
			break;
		case 'k':
			type = 'M';
			ntype = 'G';
			break;
		case 'M':
			type = 'G';
			ntype = 'T';
			break;
		case 'G':
			type = 'T';
			ntype = 'X';
			break;
		case 'T':
			type = 'X';
			ntype = '?';
			break;
		default:
			type = '?';
			break;
		}
	}
	if (w > 4 && n / d >= u) {
		snprintf(b, sizeof(b), "%*ju.%02u%c",
			 w - 4,
			 (uintmax_t)(n / (d * u)),
			 (u_int)(n / (d * u / 100) % 100),
			 ntype);
	} else {
		snprintf(b, sizeof(b), "%*jd%c",
			w - 1, (uintmax_t)n / d, type);
	}
	len = strlen(b);
	fwrite(b, len, 1, stdout);
}
