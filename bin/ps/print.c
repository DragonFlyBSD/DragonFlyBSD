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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/bin/ps/print.c,v 1.19 2005/02/01 22:33:43 dillon Exp $
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <sys/ucred.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/rtprio.h>
#include <vm/vm.h>

#include <err.h>
#include <langinfo.h>
#include <locale.h>
#include <math.h>
#include <nlist.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <vis.h>

#include "ps.h"

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
	const VAR *v;
	int left;
	char *cp, *vis_env, *vis_args;

	v = vent->var;

	if (cflag) {
		/* Don't pad the last field. */
		if (STAILQ_NEXT(vent, link) == NULL)
			printf("%s", KI_THREAD(k)->td_comm);
		else
			printf("%-*s", vent->width, KI_THREAD(k)->td_comm);
		return;
	}

	if ((vis_args = malloc(strlen(k->ki_args) * 4 + 1)) == NULL)
		err(1, NULL);
	strvis(vis_args, k->ki_args, VIS_TAB | VIS_NL | VIS_NOSLASH);
	if (k->ki_env) {
		if ((vis_env = malloc(strlen(k->ki_env) * 4 + 1)) == NULL)
			err(1, NULL);
		strvis(vis_env, k->ki_env, VIS_TAB | VIS_NL | VIS_NOSLASH);
	} else
		vis_env = NULL;

	if (STAILQ_NEXT(vent, link) == NULL) {
		/* last field */
		if (termwidth == UNLIMITED) {
			if (vis_env)
				printf("%s ", vis_env);
			printf("%s", vis_args);
		} else {
			left = termwidth - (totwidth - vent->width);
			if (left < 1) /* already wrapped, just use std width */
				left = vent->width;
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
	printf("%-*s", vent->width, KI_THREAD(k)->td_comm);
}

void
logname(const KINFO *k, const struct varent *vent)
{
	const char *s = KI_EPROC(k)->e_login;

	printf("%-*s", vent->width, *s != '\0' ? s : "-");
}

void
state(const KINFO *k, const struct varent *vent)
{
	struct proc *p;
	int flag;
	char *cp;
	char buf[16];

	p = KI_PROC(k);
	flag = p->p_flag;
	cp = buf;

	switch (p->p_stat) {

	case SSTOP:
		*cp = 'T';
		break;

	case SSLEEP:
		if (flag & P_SINTR)	/* interruptable (long) */
			*cp = p->p_slptime >= MAXSLP ? 'I' : 'S';
		else if (KI_THREAD(k)->td_flags & TDF_SINTR)
			*cp = 'S';
		else
			*cp = 'D';
		break;

	case SRUN:
	case SIDL:
		*cp = 'R';
		if (KI_THREAD(k)->td_flags & TDF_RUNNING) {
		    ++cp;
		    sprintf(cp, "%d", KI_EPROC(k)->e_cpuid);
		    while (cp[1])
			++cp;
		}
		break;

	case SZOMB:
		*cp = 'Z';
		break;

	default:
		*cp = '?';
	}
	cp++;
	if (!(flag & P_INMEM))
		*cp++ = 'W';
	if (p->p_nice < NZERO)
		*cp++ = '<';
	else if (p->p_nice > NZERO)
		*cp++ = 'N';
	if (flag & P_TRACED)
		*cp++ = 'X';
	if (flag & P_WEXIT && p->p_stat != SZOMB)
		*cp++ = 'E';
	if (flag & P_PPWAIT)
		*cp++ = 'V';
	if ((flag & P_SYSTEM) || p->p_lock > 0)
		*cp++ = 'L';
	if (numcpus > 1 && KI_THREAD(k)->td_mpcount_unused == 0)
		*cp++ = 'M';
	if (flag & P_JAILED)
		*cp++ = 'J';
	if (KI_EPROC(k)->e_flag & EPROC_SLEADER)
		*cp++ = 's';
	if ((flag & P_CONTROLT) && KI_EPROC(k)->e_pgid == KI_EPROC(k)->e_tpgid)
		*cp++ = '+';
	*cp = '\0';
	printf("%-*s", vent->width, buf);
}

/*
 * Normalized priority (lower is better).  For pure threads
 * output a negated LWKT priority (so lower still means better).
 */
void
pri(const KINFO *k, const struct varent *vent)
{
	if (KI_THREAD(k)->td_proc)
	    printf("%*d", vent->width, KI_PROC(k)->p_priority);
	else
	    printf("%*d", vent->width, -(KI_THREAD(k)->td_pri & TDPRI_MASK));
}

void
tdpri(const KINFO *k, const struct varent *vent)
{
	char buf[32];
	int val = KI_THREAD(k)->td_pri;

	snprintf(buf, sizeof(buf), "%02d/%d", val & TDPRI_MASK, val / TDPRI_CRIT);
	printf("%*s", vent->width, buf);
}

void
uname(const KINFO *k, const struct varent *vent)
{
	printf("%-*s", vent->width,
	       user_from_uid(KI_EPROC(k)->e_ucred.cr_uid, 0));
}

int
s_uname(const KINFO *k)
{
	return (strlen(user_from_uid(KI_EPROC(k)->e_ucred.cr_uid, 0)));
}

void
runame(const KINFO *k, const struct varent *vent)
{
	printf("%-*s", vent->width,
	       user_from_uid(KI_EPROC(k)->e_ucred.cr_ruid, 0));
}

int
s_runame(const KINFO *k)
{
	return (strlen(user_from_uid(KI_EPROC(k)->e_ucred.cr_ruid, 0)));
}

void
tdev(const KINFO *k, const struct varent *vent)
{
	dev_t dev;
	char buff[16];

	dev = KI_EPROC(k)->e_tdev;
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

	dev = KI_EPROC(k)->e_tdev;
	if (dev == NODEV || (ttname = devname(dev, S_IFCHR)) == NULL)
		printf("%*s ", vent->width-1, "??");
	else {
		if (strncmp(ttname, "tty", 3) == 0 ||
		    strncmp(ttname, "cua", 3) == 0)
			ttname += 3;
		printf("%*.*s%c", vent->width-1, vent->width-1, ttname,
			KI_EPROC(k)->e_flag & EPROC_CTTY ? ' ' : '-');
	}
}

void
longtname(const KINFO *k, const struct varent *vent)
{
	dev_t dev;
	const char *ttname;

	dev = KI_EPROC(k)->e_tdev;
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

	if (!k->ki_u.u_valid) {
		printf("%-*s", vent->width, "-");
		return;
	}

	if (use_ampm < 0)
		use_ampm = (*nl_langinfo(T_FMT_AMPM) != '\0');

	if (KI_THREAD(k)->td_proc != NULL) {
		then = k->ki_u.u_start.tv_sec;
	} else {
		then = KI_THREAD(k)->td_start.tv_sec;
		if (then < btime.tv_sec) {
			then = btime.tv_sec;
		}
	}

	tp = localtime(&then);
	if (!now)
		time(&now);
	if (now - k->ki_u.u_start.tv_sec < 24 * 3600) {
		strftime(buf, sizeof(buf) - 1,
		use_ampm ? "%l:%M%p" : "%k:%M  ", tp);
	} else if (now - k->ki_u.u_start.tv_sec < 7 * 86400) {
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

	if (!k->ki_u.u_valid) {
		printf("%-*s", vent->width, "-");
		return;
	}
	then = k->ki_u.u_start.tv_sec;
	strftime(buf, sizeof(buf) -1, "%c", localtime(&then));
	printf("%-*s", vent->width, buf);
}

void
wchan(const KINFO *k, const struct varent *vent)
{
	if (KI_THREAD(k)->td_wchan) {
		if (KI_THREAD(k)->td_wmesg)
			printf("%-*.*s", vent->width, vent->width,
			       KI_EPROC(k)->e_wmesg);
		else
			printf("%-*lx", vent->width,
			       (long)KI_THREAD(k)->td_wchan);
	} else
		printf("%-*s", vent->width, "-");
}

#ifndef pgtok
#define pgtok(a)        (((a)*getpagesize())/1024)
#endif

void
vsize(const KINFO *k, const struct varent *vent)
{
	printf("%*d", vent->width, (KI_EPROC(k)->e_vm.vm_map.size/1024));
}

void
rssize(const KINFO *k, const struct varent *vent)
{
	/* XXX don't have info about shared */
	printf("%*lu", vent->width, (u_long)pgtok(KI_EPROC(k)->e_vm.vm_rssize));
}

void
p_rssize(const KINFO *k, const struct varent *vent)	/* doesn't account for text */
{
	printf("%*ld", vent->width, (long)pgtok(KI_EPROC(k)->e_vm.vm_rssize));
}

void
cputime(const KINFO *k, const struct varent *vent)
{
	long secs;
	long psecs;	/* "parts" of a second. first micro, then centi */
	char obuff[128];
	static char decimal_point = '\0';

	if (decimal_point == '\0')
		decimal_point = localeconv()->decimal_point[0];

	if (KI_PROC(k)->p_stat == SZOMB || !k->ki_u.u_valid) {
		secs = 0;
		psecs = 0;
	} else {
		u_int64_t timeus;

		/*
		 * This counts time spent handling interrupts.  We could
		 * fix this, but it is not 100% trivial (and interrupt
		 * time fractions only work on the sparc anyway).	XXX
		 */
		timeus = KI_EPROC(k)->e_uticks + KI_EPROC(k)->e_sticks +
			KI_EPROC(k)->e_iticks;
		secs = timeus / 1000000;
		psecs = timeus % 1000000;
		if (sumrusage) {
			secs += k->ki_u.u_cru.ru_utime.tv_sec +
				k->ki_u.u_cru.ru_stime.tv_sec;
			psecs += k->ki_u.u_cru.ru_utime.tv_usec +
				k->ki_u.u_cru.ru_stime.tv_usec;
		}
		/*
		 * round and scale to 100's
		 */
		psecs = (psecs + 5000) / 10000;
		secs += psecs / 100;
		psecs = psecs % 100;
	}
	snprintf(obuff, sizeof(obuff),
	    "%3ld:%02ld%c%02ld", secs/60, secs%60, decimal_point, psecs);
	printf("%*s", vent->width, obuff);
}

double
getpcpu(const KINFO *k)
{
	const struct proc *p;
	static int failure;

	if (!nlistread)
		failure = donlist();
	if (failure)
		return (0.0);

	p = KI_PROC(k);
#define	fxtofl(fixpt)	((double)(fixpt) / fscale)

	/* XXX - I don't like this */
	if (p->p_swtime == 0 || (p->p_flag & P_INMEM) == 0)
		return (0.0);
	if (rawcpu)
		return (100.0 * fxtofl(p->p_pctcpu));
	return (100.0 * fxtofl(p->p_pctcpu) /
		(1.0 - exp(p->p_swtime * log(fxtofl(ccpu)))));
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

	switch (KI_PROC(k)->p_rtprio.type) {
	case RTP_PRIO_REALTIME:
		niceval = PRIO_MIN - 1 - RTP_PRIO_MAX + KI_PROC(k)->p_rtprio.prio;
		break;
	case RTP_PRIO_IDLE:
		niceval = PRIO_MAX + 1 + KI_PROC(k)->p_rtprio.prio;
		break;
	case RTP_PRIO_THREAD:
		niceval = PRIO_MIN - 1 - RTP_PRIO_MAX - KI_PROC(k)->p_rtprio.prio;
		break;
	default:
		niceval = KI_PROC(k)->p_nice - NZERO;
		break;
	}
	printf("%*d", vent->width, niceval);
}


double
getpmem(const KINFO *k)
{
	static int failure;
	struct proc *p;
	struct eproc *e;
	double fracmem;
	int szptudot;

	if (!nlistread)
		failure = donlist();
	if (failure)
		return (0.0);

	p = KI_PROC(k);
	e = KI_EPROC(k);
	if ((p->p_flag & P_INMEM) == 0)
		return (0.0);
	/* XXX want pmap ptpages, segtab, etc. (per architecture) */
	szptudot = UPAGES;
	/* XXX don't have info about shared */
	fracmem = ((float)e->e_vm.vm_rssize + szptudot)/mempages;
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
	printf("%*ld", vent->width,
	       k->ki_u.u_valid ? k->ki_u.u_ru.ru_majflt : 0);
}

/* ARGSUSED */
void
maxrss(const KINFO *k __unused, const struct varent *vent)
{
	/* XXX not yet */
	printf("%*s", vent->width, "-");
}

void
tsize(const KINFO *k, const struct varent *vent)
{
	printf("%*ld", vent->width, (long)pgtok(KI_EPROC(k)->e_vm.vm_tsize));
}

void
rtprior(const KINFO *k, const struct varent *vent)
{
	struct rtprio *prtp;
	char str[8];
	unsigned prio, type;
 
	prtp = (struct rtprio *) ((char *)KI_PROC(k) + vent->var->off);
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
	printval((char *)((char *)KI_PROC(k) + vent->var->off), vent);
}

void
tvar(const KINFO *k, const struct varent *vent)
{
	printval((char *)((char *)KI_THREAD(k) + vent->var->off), vent);
}

void
evar(const KINFO *k, const struct varent *vent)
{
	printval((char *)((char *)KI_EPROC(k) + vent->var->off), vent);
}

void
uvar(const KINFO *k, const struct varent *vent)
{
	if (k->ki_u.u_valid)
		printval(((const char *)&k->ki_u + vent->var->off), vent);
	else
		printf("%*s", vent->width, "-");
}

void
rvar(const KINFO *k, const struct varent *vent)
{
	if (k->ki_u.u_valid)
		printval(((const char *)&k->ki_u.u_ru + vent->var->off), vent);
	else
		printf("%*s", vent->width, "-");
}
