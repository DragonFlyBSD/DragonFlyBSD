/*-
 * Copyright (c) 1983, 1989, 1992, 1993
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
 * @(#)vmstat.c	8.2 (Berkeley) 1/12/94
 * $FreeBSD: src/usr.bin/systat/vmstat.c,v 1.38.2.4 2002/03/12 19:50:23 phantom Exp $
 * $DragonFly: src/usr.bin/systat/vmstat.c,v 1.12 2008/11/10 04:59:45 swildner Exp $
 */

/*
 * Cursed vmstat -- from Robert Elz.
 */

#include <sys/user.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm_param.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <kinfo.h>
#include <langinfo.h>
#include <nlist.h>
#include <paths.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "utmpentry.h"
#include <devstat.h>
#include "systat.h"
#include "extern.h"
#include "devs.h"

static struct Info {
	struct kinfo_cputime cp_time;
	struct	vmmeter Vmm;
	struct	vmtotal Total;
	struct  vmstats Vms;
	struct	nchstats nchstats;
	long	nchcount;
	long	nchpathcount;
	long	*intrcnt;
	int	bufspace;
	int	desiredvnodes;
	int	numvnodes;
	int	freevnodes;
	int	dirtybufspace;
} s, s1, s2, z;

struct kinfo_cputime cp_time, old_cp_time;
struct statinfo cur, last, run;

#define	vmm s.Vmm
#define	vms s.Vms
#define oldvmm s1.Vmm
#define oldvms s1.Vms
#define	total s.Total
#define	nchtotal s.nchstats
#define	oldnchtotal s1.nchstats

static	enum state { BOOT, TIME, RUN } state = TIME;

static void allocinfo(struct Info *);
static void copyinfo(struct Info *, struct Info *);
static void dinfo(int, int, struct statinfo *, struct statinfo *);
static void getinfo(struct Info *);
static void putlong(long, int, int, int, int);
static void putfloat(double, int, int, int, int, int);
static void putlongdouble(long double, int, int, int, int, int);
static void putlongdoublez(long double, int, int, int, int, int);
static int ucount(void);

static	int ncpu;
static	char buf[26];
static	time_t t;
static	double etime;
static	int nintr;
static	long *intrloc;
static	char **intrname;
static	int nextintsrow;
static  int extended_vm_stats;



WINDOW *
openkre(void)
{

	return (stdscr);
}

void
closekre(WINDOW *w)
{

	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
}


static struct nlist namelist[] = {
#define	X_BUFFERSPACE	0
	{ .n_name = "_bufspace" },
#define	X_NCHSTATS	1
	{ .n_name = "_nchstats" },
#define	X_DESIREDVNODES	2
	{ .n_name = "_desiredvnodes" },
#define	X_NUMVNODES	3
	{ .n_name = "_numvnodes" },
#define	X_FREEVNODES	4
	{ .n_name = "_freevnodes" },
#define X_NUMDIRTYBUFFERS 5
	{ .n_name = "_dirtybufspace" },
	{ .n_name = "" },
};

/*
 * These constants define where the major pieces are laid out
 */
#define STATROW		 0	/* uses 1 row and 68 cols */
#define STATCOL		 2
#define MEMROW		 2	/* uses 4 rows and 31 cols */
#define MEMCOL		 0
#define PAGEROW		 2	/* uses 4 rows and 26 cols */
#define PAGECOL		46
#define INTSROW		 6	/* uses all rows to bottom and 17 cols */
#define INTSCOL		61
#define PROCSROW	 7	/* uses 2 rows and 20 cols */
#define PROCSCOL	 0
#define GENSTATROW	 7	/* uses 2 rows and 30 cols */
#define GENSTATCOL	16
#define VMSTATROW	 6	/* uses 17 rows and 12 cols */
#define VMSTATCOL	48
#define GRAPHROW	10	/* uses 3 rows and 51 cols */
#define GRAPHCOL	 0
#define NAMEIROW	14	/* uses 3 rows and 38 cols */
#define NAMEICOL	 0
#define DISKROW		17	/* uses 6 rows and 50 cols (for 9 drives) */
#define DISKCOL		 0

#define	DRIVESPACE	 7	/* max # for space */

#define	MAXDRIVES	DRIVESPACE	 /* max # to display */

int
initkre(void)
{
	char *intrnamebuf;
	size_t bytes;
	size_t b;
	size_t i;

	if (namelist[0].n_type == 0) {
		if (kvm_nlist(kd, namelist)) {
			nlisterr(namelist);
			return(0);
		}
		if (namelist[0].n_type == 0) {
			error("No namelist");
			return(0);
		}
	}

	if ((num_devices = getnumdevs()) < 0) {
		warnx("%s", devstat_errbuf);
		return(0);
	}

	cur.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	last.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	run.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	bzero(cur.dinfo, sizeof(struct devinfo));
	bzero(last.dinfo, sizeof(struct devinfo));
	bzero(run.dinfo, sizeof(struct devinfo));

	if (dsinit(MAXDRIVES, &cur, &last, &run) != 1)
		return(0);

	if (nintr == 0) {
		if (sysctlbyname("hw.intrnames", NULL, &bytes, NULL, 0) == 0) {
			intrnamebuf = malloc(bytes);
			sysctlbyname("hw.intrnames", intrnamebuf, &bytes,
					NULL, 0);
			for (i = 0; i < bytes; ++i) {
				if (intrnamebuf[i] == 0)
					++nintr;
			}
			intrname = malloc(nintr * sizeof(char *));
			intrloc = malloc(nintr * sizeof(*intrloc));
			nintr = 0;
			for (b = i = 0; i < bytes; ++i) {
				if (intrnamebuf[i] == 0) {
					intrname[nintr] = intrnamebuf + b;
					intrloc[nintr] = 0;
					b = i + 1;
					++nintr;
				}
			}
		}
		nextintsrow = INTSROW + 2;
		allocinfo(&s);
		allocinfo(&s1);
		allocinfo(&s2);
		allocinfo(&z);
	}
	getinfo(&s2);
	copyinfo(&s2, &s1);
	return(1);
}

void
fetchkre(void)
{
	time_t now;
	struct tm *tp;
	static int d_first = -1;

	if (d_first < 0)
		d_first = (*nl_langinfo(D_MD_ORDER) == 'd');

	time(&now);
	tp = localtime(&now);
	(void) strftime(buf, sizeof(buf),
			d_first ? "%e %b %R" : "%b %e %R", tp);
	getinfo(&s);
}

void
labelkre(void)
{
	int i, j;

	clear();
	mvprintw(STATROW, STATCOL + 4, "users    Load");
	mvprintw(MEMROW, MEMCOL, "Mem:MB    REAL            VIRTUAL");
	mvprintw(MEMROW + 1, MEMCOL, "        Tot   Share      Tot    Share");
	mvprintw(MEMROW + 2, MEMCOL, "Act");
	mvprintw(MEMROW + 3, MEMCOL, "All");

	mvprintw(MEMROW + 1, MEMCOL + 41, "Free");

	mvprintw(PAGEROW, PAGECOL,     "        VN PAGER  SWAP PAGER ");
	mvprintw(PAGEROW + 1, PAGECOL, "        in  out     in  out ");
	mvprintw(PAGEROW + 2, PAGECOL, "count");
	mvprintw(PAGEROW + 3, PAGECOL, "pages");

	mvprintw(INTSROW, INTSCOL + 3, " Interrupts");
	mvprintw(INTSROW + 1, INTSCOL + 9, "total");

	mvprintw(VMSTATROW + 1, VMSTATCOL + 10, "cow");
	mvprintw(VMSTATROW + 2, VMSTATCOL + 10, "wire");
	mvprintw(VMSTATROW + 3, VMSTATCOL + 10, "act");
	mvprintw(VMSTATROW + 4, VMSTATCOL + 10, "inact");
	mvprintw(VMSTATROW + 5, VMSTATCOL + 10, "cache");
	mvprintw(VMSTATROW + 6, VMSTATCOL + 10, "free");
	mvprintw(VMSTATROW + 7, VMSTATCOL + 10, "daefr");
	mvprintw(VMSTATROW + 8, VMSTATCOL + 10, "prcfr");
	mvprintw(VMSTATROW + 9, VMSTATCOL + 10, "react");
	mvprintw(VMSTATROW + 10, VMSTATCOL + 10, "pdwake");
	mvprintw(VMSTATROW + 11, VMSTATCOL + 10, "pdpgs");
	mvprintw(VMSTATROW + 12, VMSTATCOL + 10, "intrn");
	mvprintw(VMSTATROW + 13, VMSTATCOL + 10, "buf");
	mvprintw(VMSTATROW + 14, VMSTATCOL + 10, "dirtybuf");

	mvprintw(VMSTATROW + 15, VMSTATCOL + 10, "desiredvnodes");
	mvprintw(VMSTATROW + 16, VMSTATCOL + 10, "numvnodes");
	mvprintw(VMSTATROW + 17, VMSTATCOL + 10, "freevnodes");

	mvprintw(GENSTATROW, GENSTATCOL, "  Csw   Trp   Sys   Int  Sof   Flt");

	mvprintw(GRAPHROW, GRAPHCOL,
		"  . %%Sys    . %%Intr   . %%User   . %%Nice   . %%Idle");
	mvprintw(PROCSROW, PROCSCOL, "  r  p  d  s  w");
	mvprintw(GRAPHROW + 1, GRAPHCOL,
		"|    |    |    |    |    |    |    |    |    |    |");

	mvprintw(NAMEIROW, NAMEICOL, "Path-lookups   hits   %%    Components");
	mvprintw(DISKROW, DISKCOL, "Disks");
	mvprintw(DISKROW + 1, DISKCOL, "KB/t");
	mvprintw(DISKROW + 2, DISKCOL, "tpr/s");
	mvprintw(DISKROW + 3, DISKCOL, "MBr/s");
	mvprintw(DISKROW + 4, DISKCOL, "tpw/s");
	mvprintw(DISKROW + 5, DISKCOL, "MBw/s");
	mvprintw(DISKROW + 6, DISKCOL, "%% busy");
	/*
	 * For now, we don't support a fourth disk statistic.  So there's
	 * no point in providing a label for it.  If someone can think of a
	 * fourth useful disk statistic, there is room to add it.
	 */
	j = 0;
	for (i = 0; i < num_devices && j < MAXDRIVES; i++)
		if (dev_select[i].selected) {
			char tmpstr[80];
			sprintf(tmpstr, "%s%d", dev_select[i].device_name,
				dev_select[i].unit_number);
			mvprintw(DISKROW, DISKCOL + 5 + 6 * j,
				" %5.5s", tmpstr);
			j++;
		}

	if (j <= 4) {
		/*
		 * room for extended VM stats
		 */
		mvprintw(VMSTATROW + 11, VMSTATCOL - 6, "zfod");
		mvprintw(VMSTATROW + 12, VMSTATCOL - 6, "ozfod");
		mvprintw(VMSTATROW + 13, VMSTATCOL - 6, "%%slo-z");
		mvprintw(VMSTATROW + 14, VMSTATCOL - 6, "tfree");
		extended_vm_stats = 1;
	} else {
		extended_vm_stats = 0;
		mvprintw(VMSTATROW + 0, VMSTATCOL + 10, "zfod");
	}

	for (i = 0; i < nintr; i++) {
		if (intrloc[i] == 0)
			continue;
		mvprintw(intrloc[i], INTSCOL + 9, "%-10.10s", intrname[i]);
	}
}

#define CP_UPDATE(fld)	do {	\
	uint64_t lt;		\
	lt=s.fld;		\
	s.fld-=s1.fld;		\
	if(state==TIME)		\
		s1.fld=lt;	\
	lt=fld;			\
	fld-=old_##fld;		\
	if(state==TIME)		\
		old_##fld=lt;	\
	etime += s.fld;		\
} while(0)
#define X(fld)	{t=s.fld[i]; s.fld[i]-=s1.fld[i]; if(state==TIME) s1.fld[i]=t;}
#define Y(fld)	{t = s.fld; s.fld -= s1.fld; if(state == TIME) s1.fld = t;}
#define Z(fld)	{t = s.nchstats.fld; s.nchstats.fld -= s1.nchstats.fld; \
	if(state == TIME) s1.nchstats.fld = t;}
#define PUTRATE(fld, l, c, w) \
	Y(fld); \
	putlong((long)((float)s.fld/etime + 0.5), l, c, w, 'D')
#define MAXFAIL 5

#define CPUSTATES 5
static	const char cpuchar[5] = { '=' , '+', '>', '-', ' ' };

static	const size_t cpuoffsets[] = {
	offsetof(struct kinfo_cputime, cp_sys),
	offsetof(struct kinfo_cputime, cp_intr),
	offsetof(struct kinfo_cputime, cp_user),
	offsetof(struct kinfo_cputime, cp_nice),
	offsetof(struct kinfo_cputime, cp_idle)
};

void
showkre(void)
{
	float f1, f2;
	int psiz;
	int i, lc;
	long inttotal;
	long l;
	static int failcnt = 0;
	double total_time;

	etime = 0;
	CP_UPDATE(cp_time.cp_user);
	CP_UPDATE(cp_time.cp_nice);
	CP_UPDATE(cp_time.cp_sys);
	CP_UPDATE(cp_time.cp_intr);
	CP_UPDATE(cp_time.cp_idle);

	total_time = etime;
	if (total_time == 0.0)
		total_time = 1.0;

	if (etime < 100000.0) {	/* < 100ms ignore this trash */
		if (failcnt++ >= MAXFAIL) {
			clear();
			mvprintw(2, 10, "The alternate system clock has died!");
			mvprintw(3, 10, "Reverting to ``pigs'' display.");
			move(CMDLINE, 0);
			refresh();
			failcnt = 0;
			sleep(5);
			command("pigs");
		}
		return;
	}
	failcnt = 0;
	etime /= 1000000.0;
	etime /= ncpu;
	if (etime == 0)
		etime = 1;
	inttotal = 0;
	for (i = 0; i < nintr; i++) {
		if (s.intrcnt[i] == 0)
			continue;
		if (intrloc[i] == 0) {
			if (nextintsrow == LINES)
				continue;
			intrloc[i] = nextintsrow++;
			mvprintw(intrloc[i], INTSCOL + 9, "%-10.10s",
				intrname[i]);
		}
		X(intrcnt);
		l = (long)((float)s.intrcnt[i]/etime + 0.5);
		inttotal += l;
		putlong(l, intrloc[i], INTSCOL + 2, 6, 'D');
	}
	putlong(inttotal, INTSROW + 1, INTSCOL + 2, 6, 'D');
	Z(ncs_goodhits); Z(ncs_badhits); Z(ncs_miss);
	Z(ncs_longhits); Z(ncs_longmiss); Z(ncs_neghits);
	s.nchcount = nchtotal.ncs_goodhits + nchtotal.ncs_badhits +
	    nchtotal.ncs_miss + nchtotal.ncs_neghits;
	s.nchpathcount = nchtotal.ncs_longhits + nchtotal.ncs_longmiss;
	if (state == TIME) {
		s1.nchcount = s.nchcount;
		s1.nchpathcount = s.nchpathcount;
	}

	psiz = 0;
	f2 = 0.0;
	for (lc = 0; lc < CPUSTATES; lc++) {
		uint64_t val = *(uint64_t *)(((uint8_t *)&s.cp_time) +
		    cpuoffsets[lc]);
		f1 = 100.0 * val / total_time;
		f2 += f1;
		l = (int) ((f2 + 1.0) / 2.0) - psiz;
		if (f1 > 99.9)
			f1 = 99.9;	/* no room to display 100.0 */
		putfloat(f1, GRAPHROW, GRAPHCOL + 10 * lc, 4, 1, 0);
		move(GRAPHROW + 2, psiz);
		psiz += l;
		while (l-- > 0)
			addch(cpuchar[lc]);
	}

	putlong(ucount(), STATROW, STATCOL, 3, 'D');
	putfloat(avenrun[0], STATROW, STATCOL + 18, 6, 2, 0);
	putfloat(avenrun[1], STATROW, STATCOL + 25, 6, 2, 0);
	putfloat(avenrun[2], STATROW, STATCOL + 32, 6, 2, 0);
	mvaddstr(STATROW, STATCOL + 53, buf);
#define pgtokb(pg)	(long)((intmax_t)(pg) * vms.v_page_size / 1024)
#define pgtomb(pg)	(long)((intmax_t)(pg) * vms.v_page_size / (1024 * 1024))
	putlong(pgtomb(total.t_arm), MEMROW + 2, MEMCOL + 3, 8, 'M');
	putlong(pgtomb(total.t_armshr), MEMROW + 2, MEMCOL + 11, 8, 'M');
	putlong(pgtomb(total.t_avm), MEMROW + 2, MEMCOL + 19, 9, 'M');
	putlong(pgtomb(total.t_avmshr), MEMROW + 2, MEMCOL + 28, 9, 'M');
	putlong(pgtomb(total.t_rm), MEMROW + 3, MEMCOL + 3, 8, 'M');
	putlong(pgtomb(total.t_rmshr), MEMROW + 3, MEMCOL + 11, 8, 'M');
	putlong(pgtomb(total.t_vm), MEMROW + 3, MEMCOL + 19, 9, 'M');
	putlong(pgtomb(total.t_vmshr), MEMROW + 3, MEMCOL + 28, 9, 'M');
	putlong(pgtomb(total.t_free), MEMROW + 2, MEMCOL + 37, 8, 'M');
	putlong(total.t_rq - 1, PROCSROW + 1, PROCSCOL + 0, 3, 'D');
	putlong(total.t_pw, PROCSROW + 1, PROCSCOL + 3, 3, 'D');
	putlong(total.t_dw, PROCSROW + 1, PROCSCOL + 6, 3, 'D');
	putlong(total.t_sl, PROCSROW + 1, PROCSCOL + 9, 3, 'D');
	putlong(total.t_sw, PROCSROW + 1, PROCSCOL + 12, 3, 'D');
	if (extended_vm_stats == 0) {
		PUTRATE(Vmm.v_zfod, VMSTATROW + 0, VMSTATCOL + 4, 5);
	}
	PUTRATE(Vmm.v_cow_faults, VMSTATROW + 1, VMSTATCOL + 3, 6);
	putlong(pgtokb(vms.v_wire_count), VMSTATROW + 2, VMSTATCOL, 9, 'K');
	putlong(pgtokb(vms.v_active_count), VMSTATROW + 3, VMSTATCOL, 9, 'K');
	putlong(pgtokb(vms.v_inactive_count), VMSTATROW + 4, VMSTATCOL, 9, 'K');
	putlong(pgtokb(vms.v_cache_count), VMSTATROW + 5, VMSTATCOL, 9, 'K');
	putlong(pgtokb(vms.v_free_count), VMSTATROW + 6, VMSTATCOL, 9, 'K');
	PUTRATE(Vmm.v_dfree, VMSTATROW + 7, VMSTATCOL, 9);
	PUTRATE(Vmm.v_pfree, VMSTATROW + 8, VMSTATCOL, 9);
	PUTRATE(Vmm.v_reactivated, VMSTATROW + 9, VMSTATCOL, 9);
	PUTRATE(Vmm.v_pdwakeups, VMSTATROW + 10, VMSTATCOL, 9);
	PUTRATE(Vmm.v_pdpages, VMSTATROW + 11, VMSTATCOL, 9);
	PUTRATE(Vmm.v_intrans, VMSTATROW + 12, VMSTATCOL, 9);

	if (extended_vm_stats) {
		PUTRATE(Vmm.v_zfod, VMSTATROW + 11, VMSTATCOL - 16, 9);
		PUTRATE(Vmm.v_ozfod, VMSTATROW + 12, VMSTATCOL - 16, 9);
#define nz(x)	((x) ? (x) : 1)
		putlong((s.Vmm.v_zfod - s.Vmm.v_ozfod) * 100 / nz(s.Vmm.v_zfod),
		    VMSTATROW + 13, VMSTATCOL - 16, 9, 'D');
#undef nz
		PUTRATE(Vmm.v_tfree, VMSTATROW + 14, VMSTATCOL - 16, 9);
	}

	putlong(s.bufspace/1024, VMSTATROW + 13, VMSTATCOL, 9, 'K');
	putlong(s.dirtybufspace/1024, VMSTATROW + 14, VMSTATCOL, 9, 'K');
	putlong(s.desiredvnodes, VMSTATROW + 15, VMSTATCOL, 9, 'D');
	putlong(s.numvnodes, VMSTATROW + 16, VMSTATCOL, 9, 'D');
	putlong(s.freevnodes, VMSTATROW + 17, VMSTATCOL, 9, 'D');
	PUTRATE(Vmm.v_vnodein, PAGEROW + 2, PAGECOL + 5, 5);
	PUTRATE(Vmm.v_vnodeout, PAGEROW + 2, PAGECOL + 10, 5);
	PUTRATE(Vmm.v_swapin, PAGEROW + 2, PAGECOL + 17, 5);
	PUTRATE(Vmm.v_swapout, PAGEROW + 2, PAGECOL + 22, 5);
	PUTRATE(Vmm.v_vnodepgsin, PAGEROW + 3, PAGECOL + 5, 5);
	PUTRATE(Vmm.v_vnodepgsout, PAGEROW + 3, PAGECOL + 10, 5);
	PUTRATE(Vmm.v_swappgsin, PAGEROW + 3, PAGECOL + 17, 5);
	PUTRATE(Vmm.v_swappgsout, PAGEROW + 3, PAGECOL + 22, 5);
	PUTRATE(Vmm.v_swtch, GENSTATROW + 1, GENSTATCOL, 5);
	PUTRATE(Vmm.v_trap, GENSTATROW + 1, GENSTATCOL + 6, 5);
	PUTRATE(Vmm.v_syscall, GENSTATROW + 1, GENSTATCOL + 12, 5);
	PUTRATE(Vmm.v_intr, GENSTATROW + 1, GENSTATCOL + 18, 5);
	PUTRATE(Vmm.v_soft, GENSTATROW + 1, GENSTATCOL + 23, 5);
	PUTRATE(Vmm.v_vm_faults, GENSTATROW + 1, GENSTATCOL + 29, 5);
	mvprintw(DISKROW, DISKCOL + 5, "                              ");
	for (i = 0, lc = 0; i < num_devices && lc < MAXDRIVES; i++)
		if (dev_select[i].selected) {
			char tmpstr[80];
			sprintf(tmpstr, "%s%d", dev_select[i].device_name,
				dev_select[i].unit_number);
			mvprintw(DISKROW, DISKCOL + 5 + 6 * lc,
				" %5.5s", tmpstr);
			switch(state) {
			case TIME:
				dinfo(i, ++lc, &cur, &last);
				break;
			case RUN:
				dinfo(i, ++lc, &cur, &run);
				break;
			case BOOT:
				dinfo(i, ++lc, &cur, NULL);
				break;
			}
		}
#define nz(x)	((x) ? (x) : 1)
	putlong(s.nchpathcount, NAMEIROW + 1, NAMEICOL + 3, 9, 'D');

	putlong(nchtotal.ncs_longhits, NAMEIROW + 1, NAMEICOL + 12, 7, 'D');
	putfloat(nchtotal.ncs_longhits * 100.0 / nz(s.nchpathcount),
	    NAMEIROW + 1, NAMEICOL + 19, 4, 0, 0);

	putfloat((double)s.nchcount / nz(s.nchpathcount),
	    NAMEIROW + 1, NAMEICOL + 27, 5, 2, 1);
#undef nz
}

int
cmdkre(const char *cmd, char *args)
{
	int retval;

	if (prefix(cmd, "run")) {
		retval = 1;
		copyinfo(&s2, &s1);
		switch (getdevs(&run)) {
		case -1:
			errx(1, "%s", devstat_errbuf);
			break;
		case 1:
			num_devices = run.dinfo->numdevs;
			generation = run.dinfo->generation;
			retval = dscmd("refresh", NULL, MAXDRIVES, &cur);
			if (retval == 2)
				labelkre();
			break;
		default:
			break;
		}
		state = RUN;
		return (retval);
	}
	if (prefix(cmd, "boot")) {
		state = BOOT;
		copyinfo(&z, &s1);
		return (1);
	}
	if (prefix(cmd, "time")) {
		state = TIME;
		return (1);
	}
	if (prefix(cmd, "zero")) {
		retval = 1;
		if (state == RUN) {
			getinfo(&s1);
			switch (getdevs(&run)) {
			case -1:
				errx(1, "%s", devstat_errbuf);
				break;
			case 1:
				num_devices = run.dinfo->numdevs;
				generation = run.dinfo->generation;
				retval = dscmd("refresh",NULL, MAXDRIVES, &cur);
				if (retval == 2)
					labelkre();
				break;
			default:
				break;
			}
		}
		return (retval);
	}
	retval = dscmd(cmd, args, MAXDRIVES, &cur);

	if (retval == 2)
		labelkre();

	return(retval);
}

/* calculate number of users on the system */
static int
ucount(void)
{
	struct utmpentry *ep;
	int nusers = 0;

	getutentries(NULL, &ep);
	for (; ep; ep = ep->next)
		nusers++;

	return (nusers);
}

static void
putlong(long n, int l, int lc, int w, int type)
{
	char b[128];
	int xtype;

	move(l, lc);
	if (n == 0) {
		while (w-- > 0)
			addch(' ');
		return;
	}
	snprintf(b, sizeof(b), "%*ld", w, n);
	if (strlen(b) > (size_t)w) {
		if (type == 'D') {
			n /= 1000;
			xtype = 'K';
		} else {
			n /= 1024;
			xtype = 'M';
		}
		snprintf(b, sizeof(b), "%*ld%c", w - 1, n, xtype);
		if (strlen(b) > (size_t)w) {
			if (type == 'D') {
				n /= 1000;
				xtype = 'M';
			} else {
				n /= 1024;
				xtype = 'G';
			}
			snprintf(b, sizeof(b), "%*ld%c", w - 1, n, xtype);
			if (strlen(b) > (size_t)w) {
				while (w-- > 0)
					addch('*');
				return;
			}
		}
	}
	addstr(b);
}

static void
putfloat(double f, int l, int lc, int w, int d, int nz)
{
	char b[128];

	move(l, lc);
	if (nz && f == 0.0) {
		while (--w >= 0)
			addch(' ');
		return;
	}
	snprintf(b, sizeof(b), "%*.*f", w, d, f);
	if (strlen(b) > (size_t)w)
		snprintf(b, sizeof(b), "%*.0f", w, f);
	if (strlen(b) > (size_t)w) {
		while (--w >= 0)
			addch('*');
		return;
	}
	addstr(b);
}

static void
putlongdouble(long double f, int l, int lc, int w, int d, int nz)
{
	char b[128];

	move(l, lc);
	if (nz && f == 0.0) {
		while (--w >= 0)
			addch(' ');
		return;
	}
	sprintf(b, "%*.*Lf", w, d, f);
	if (strlen(b) > (size_t)w)
		sprintf(b, "%*.0Lf", w, f);
	if (strlen(b) > (size_t)w) {
		while (--w >= 0)
			addch('*');
		return;
	}
	addstr(b);
}

static void
putlongdoublez(long double f, int l, int lc, int w, int d, int nz)
{
	char b[128];

	if (f == 0.0) {
		move(l, lc);
		sprintf(b, "%*.*s", w, w, "");
		addstr(b);
	} else {
		putlongdouble(f, l, lc, w, d, nz);
	}
}

static void
getinfo(struct Info *ls)
{
	struct devinfo *tmp_dinfo;
	struct nchstats *nch_tmp;
	size_t size;
	size_t vms_size = sizeof(ls->Vms);
	size_t vmm_size = sizeof(ls->Vmm);
	size_t nch_size = sizeof(ls->nchstats) * SMP_MAXCPU;

        if (sysctlbyname("vm.vmstats", &ls->Vms, &vms_size, NULL, 0)) {
                perror("sysctlbyname: vm.vmstats");
                exit(1);
        }
        if (sysctlbyname("vm.vmmeter", &ls->Vmm, &vmm_size, NULL, 0)) {
                perror("sysctlbyname: vm.vmstats");
                exit(1);
        }

	if (kinfo_get_sched_cputime(&ls->cp_time))
		err(1, "kinfo_get_sched_cputime");
	if (kinfo_get_sched_cputime(&cp_time))
		err(1, "kinfo_get_sched_cputime");
	NREAD(X_BUFFERSPACE, &ls->bufspace, sizeof(ls->bufspace));
	NREAD(X_DESIREDVNODES, &ls->desiredvnodes, sizeof(ls->desiredvnodes));
	NREAD(X_NUMVNODES, &ls->numvnodes, sizeof(ls->numvnodes));
	NREAD(X_FREEVNODES, &ls->freevnodes, sizeof(ls->freevnodes));
	NREAD(X_NUMDIRTYBUFFERS, &ls->dirtybufspace, sizeof(ls->dirtybufspace));

	if (nintr) {
		size = nintr * sizeof(ls->intrcnt[0]);
		sysctlbyname("hw.intrcnt", ls->intrcnt, &size, NULL, 0);
	}
	size = sizeof(ls->Total);
	if (sysctlbyname("vm.vmtotal", &ls->Total, &size, NULL, 0) < 0) {
		error("Can't get kernel info: %s\n", strerror(errno));
		bzero(&ls->Total, sizeof(ls->Total));
	}

	if ((nch_tmp = malloc(nch_size)) == NULL) {
		perror("malloc");
		exit(1);
	} else {
		if (sysctlbyname("vfs.cache.nchstats", nch_tmp, &nch_size, NULL, 0)) {
			perror("sysctlbyname vfs.cache.nchstats");
			free(nch_tmp);
			exit(1);
		} else {
			if ((nch_tmp = realloc(nch_tmp, nch_size)) == NULL) {
				perror("realloc");
				exit(1);
			}
		}
	}

	if (kinfo_get_cpus(&ncpu))
		err(1, "kinfo_get_cpus");
	kvm_nch_cpuagg(nch_tmp, &ls->nchstats, ncpu);
	free(nch_tmp);

	tmp_dinfo = last.dinfo;
	last.dinfo = cur.dinfo;
	cur.dinfo = tmp_dinfo;

	last.busy_time = cur.busy_time;
	switch (getdevs(&cur)) {
	case -1:
		errx(1, "%s", devstat_errbuf);
		break;
	case 1:
		num_devices = cur.dinfo->numdevs;
		generation = cur.dinfo->generation;
		cmdkre("refresh", NULL);
		break;
	default:
		break;
	}
}

static void
allocinfo(struct Info *ls)
{
	ls->intrcnt = (long *) calloc(nintr, sizeof(long));
	if (ls->intrcnt == NULL)
		errx(2, "out of memory");
}

static void
copyinfo(struct Info *from, struct Info *to)
{
	long *intrcnt;

	/*
	 * time, wds, seek, and xfer are malloc'd so we have to
	 * save the pointers before the structure copy and then
	 * copy by hand.
	 */
	intrcnt = to->intrcnt;
	*to = *from;

	bcopy(from->intrcnt, to->intrcnt = intrcnt, nintr * sizeof (int));
}

static void
dinfo(int dn, int lc, struct statinfo *now, struct statinfo *then)
{
	long double kb_per_transfer;
	long double transfers_per_secondr;
	long double transfers_per_secondw;
	long double mb_per_secondr;
	long double mb_per_secondw;
	long double elapsed_time, device_busy;
	int di;

	di = dev_select[dn].position;

	elapsed_time = compute_etime(now->busy_time, then ?
				     then->busy_time :
				     now->dinfo->devices[di].dev_creation_time);

	device_busy =  compute_etime(now->dinfo->devices[di].busy_time, then ?
				     then->dinfo->devices[di].busy_time :
				     now->dinfo->devices[di].dev_creation_time);

	if (compute_stats(
			  &now->dinfo->devices[di],
			  (then ? &then->dinfo->devices[di] : NULL),
			  elapsed_time,
			  NULL, NULL, NULL,
			  &kb_per_transfer,
			  NULL,
			  NULL,
			  NULL, NULL) != 0)
		errx(1, "%s", devstat_errbuf);

	if (compute_stats_read(
			  &now->dinfo->devices[di],
			  (then ? &then->dinfo->devices[di] : NULL),
			  elapsed_time,
			  NULL, NULL, NULL,
			  NULL,
			  &transfers_per_secondr,
			  &mb_per_secondr,
			  NULL, NULL) != 0)
		errx(1, "%s", devstat_errbuf);

	if (compute_stats_write(
			  &now->dinfo->devices[di],
			  (then ? &then->dinfo->devices[di] : NULL),
			  elapsed_time,
			  NULL, NULL, NULL,
			  NULL,
			  &transfers_per_secondw,
			  &mb_per_secondw,
			  NULL, NULL) != 0)
		errx(1, "%s", devstat_errbuf);

	if ((device_busy == 0) &&
	    (transfers_per_secondr > 5 || transfers_per_secondw > 5)) {
		/* the device has been 100% busy, fake it because
		 * as long as the device is 100% busy the busy_time
		 * field in the devstat struct is not updated */
		device_busy = elapsed_time;
	}
	if (device_busy > elapsed_time) {
		/* this normally happens after one or more periods
		 * where the device has been 100% busy, correct it */
		device_busy = elapsed_time;
	}

	lc = DISKCOL + lc * 6;
	putlongdoublez(kb_per_transfer, DISKROW + 1, lc, 5, 2, 0);
	putlongdoublez(transfers_per_secondr, DISKROW + 2, lc, 5, 0, 0);
	putlongdoublez(mb_per_secondr, DISKROW + 3, lc, 5, 2, 0);
	putlongdoublez(transfers_per_secondw, DISKROW + 4, lc, 5, 0, 0);
	putlongdoublez(mb_per_secondw, DISKROW + 5, lc, 5, 2, 0);
	putlongdouble(device_busy * 100 / elapsed_time,
				      DISKROW + 6, lc, 5, 0, 0);
}
