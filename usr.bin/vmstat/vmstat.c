/*
 * Copyright (c) 1980, 1986, 1991, 1993
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
 * @(#) Copyright (c) 1980, 1986, 1991, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)vmstat.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/vmstat/vmstat.c,v 1.38.2.4 2001/07/31 19:52:41 tmm Exp $
 * $DragonFly: src/usr.bin/vmstat/vmstat.c,v 1.6 2003/08/29 17:09:33 hmp Exp $
 */

#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/dkstat.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/signal.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm_param.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>
#include <devstat.h>

static struct nlist namelist[] = {
#define	X_CPTIME	0
	{ "_cp_time" },
#define	X_BOOTTIME	1
	{ "_boottime" },
#define X_HZ		2
	{ "_hz" },
#define X_STATHZ	3
	{ "_stathz" },
#define X_NCHSTATS	4
	{ "_nchstats" },
#define	X_INTRNAMES	5
	{ "_intrnames" },
#define	X_EINTRNAMES	6
	{ "_eintrnames" },
#define	X_INTRCNT	7
	{ "_intrcnt" },
#define	X_EINTRCNT	8
	{ "_eintrcnt" },
#define	X_KMEMSTATISTICS	9
	{ "_kmemstatistics" },
#if 0
#define	X_KMEMBUCKETS	10
	{ "_bucket" },
#else
	{ "_kmemstatistics" },
#endif
#define	X_ZLIST		11
	{ "_zlist" },
#ifdef notyet
#define	X_DEFICIT	12
	{ "_deficit" },
#define	X_FORKSTAT	13
	{ "_forkstat" },
#define X_REC		14
	{ "_rectime" },
#define X_PGIN		15
	{ "_pgintime" },
#define	X_XSTATS	16
	{ "_xstats" },
#define X_END		17
#else
#define X_END		12
#endif
	{ "" },
};

struct statinfo cur, last;
int num_devices, maxshowdevs;
long generation;
struct device_selection *dev_select;
int num_selected;
struct devstat_match *matches;
int num_matches = 0;
int num_devices_specified, num_selections;
long select_generation;
char **specified_devices;
devstat_select_mode select_mode;

struct	vmmeter vmm, ovmm;
struct	vmstats vms, ovms;

int	winlines = 20;
int	nflag = 0;

kvm_t *kd;

#define	FORKSTAT	0x01
#define	INTRSTAT	0x02
#define	MEMSTAT		0x04
#define	SUMSTAT		0x08
#define	TIMESTAT	0x10
#define	VMSTAT		0x20
#define ZMEMSTAT	0x40

void	cpustats(), dointr(), domem(), dosum(), dozmem();
void	dovmstat(), kread(), usage();
#ifdef notyet
void	dotimes(), doforkst();
#endif
void printhdr __P((void));
static void devstats();

int
main(argc, argv)
	register int argc;
	register char **argv;
{
	register int c, todo;
	u_int interval;
	int reps;
	char *memf, *nlistf;
	char errbuf[_POSIX2_LINE_MAX];

	memf = nlistf = NULL;
	interval = reps = todo = 0;
	maxshowdevs = 2;
	while ((c = getopt(argc, argv, "c:fiM:mN:n:p:stw:z")) != -1) {
		switch (c) {
		case 'c':
			reps = atoi(optarg);
			break;
		case 'f':
#ifdef notyet
			todo |= FORKSTAT;
#else
			errx(EX_USAGE, "sorry, -f is not (re)implemented yet");
#endif
			break;
		case 'i':
			todo |= INTRSTAT;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'm':
			todo |= MEMSTAT;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			nflag = 1;
			maxshowdevs = atoi(optarg);
			if (maxshowdevs < 0)
				errx(1, "number of devices %d is < 0",
				     maxshowdevs);
			break;
		case 'p':
			if (buildmatch(optarg, &matches, &num_matches) != 0)
				errx(1, "%s", devstat_errbuf);
			break;
		case 's':
			todo |= SUMSTAT;
			break;
		case 't':
#ifdef notyet
			todo |= TIMESTAT;
#else
			errx(EX_USAGE, "sorry, -t is not (re)implemented yet");
#endif
			break;
		case 'w':
			interval = atoi(optarg);
			break;
		case 'z':
			todo |= ZMEMSTAT;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (todo == 0)
		todo = VMSTAT;

	/*
	 * Discard setgid privileges if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	if (nlistf != NULL || memf != NULL)
		setgid(getgid());

	kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, errbuf);
	if (kd == 0) 
		errx(1, "kvm_openfiles: %s", errbuf);

	if ((c = kvm_nlist(kd, namelist)) != 0) {
		if (c > 0) {
			warnx("undefined symbols:");
			for (c = 0;
			    c < sizeof(namelist)/sizeof(namelist[0]); c++)
				if (namelist[c].n_type == 0)
					fprintf(stderr, " %s",
					    namelist[c].n_name);
			(void)fputc('\n', stderr);
		} else
			warnx("kvm_nlist: %s", kvm_geterr(kd));
		exit(1);
	}

	if (todo & VMSTAT) {
		char **getdrivedata();
		struct winsize winsize;

		/*
		 * Make sure that the userland devstat version matches the
		 * kernel devstat version.  If not, exit and print a
		 * message informing the user of his mistake.
		 */
		if (checkversion() < 0)
			errx(1, "%s", devstat_errbuf);


		argv = getdrivedata(argv);
		winsize.ws_row = 0;
		(void) ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *)&winsize);
		if (winsize.ws_row > 0)
			winlines = winsize.ws_row;

	}

#define	BACKWARD_COMPATIBILITY
#ifdef	BACKWARD_COMPATIBILITY
	if (*argv) {
		interval = atoi(*argv);
		if (*++argv)
			reps = atoi(*argv);
	}
#endif

	if (interval) {
		if (!reps)
			reps = -1;
	} else if (reps)
		interval = 1;

#ifdef notyet
	if (todo & FORKSTAT)
		doforkst();
#endif
	if (todo & MEMSTAT)
		domem();
	if (todo & ZMEMSTAT)
		dozmem();
	if (todo & SUMSTAT)
		dosum();
#ifdef notyet
	if (todo & TIMESTAT)
		dotimes();
#endif
	if (todo & INTRSTAT)
		dointr();
	if (todo & VMSTAT)
		dovmstat(interval, reps);
	exit(0);
}

char **
getdrivedata(argv)
	char **argv;
{
	if ((num_devices = getnumdevs()) < 0)
		errx(1, "%s", devstat_errbuf);

	cur.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	last.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	bzero(cur.dinfo, sizeof(struct devinfo));
	bzero(last.dinfo, sizeof(struct devinfo));

	if (getdevs(&cur) == -1)
		errx(1, "%s", devstat_errbuf);

	num_devices = cur.dinfo->numdevs;
	generation = cur.dinfo->generation;

	specified_devices = (char **)malloc(sizeof(char *));
	for (num_devices_specified = 0; *argv; ++argv) {
		if (isdigit(**argv))
			break;
		num_devices_specified++;
		specified_devices = (char **)realloc(specified_devices,
						     sizeof(char *) *
						     num_devices_specified);
		specified_devices[num_devices_specified - 1] = *argv;
	}
	dev_select = NULL;

	if (nflag == 0 && maxshowdevs < num_devices_specified)
			maxshowdevs = num_devices_specified;

	/*
	 * People are generally only interested in disk statistics when
	 * they're running vmstat.  So, that's what we're going to give
	 * them if they don't specify anything by default.  We'll also give
	 * them any other random devices in the system so that we get to
	 * maxshowdevs devices, if that many devices exist.  If the user
	 * specifies devices on the command line, either through a pattern
	 * match or by naming them explicitly, we will give the user only
	 * those devices.
	 */
	if ((num_devices_specified == 0) && (num_matches == 0)) {
		if (buildmatch("da", &matches, &num_matches) != 0)
			errx(1, "%s", devstat_errbuf);

		select_mode = DS_SELECT_ADD;
	} else
		select_mode = DS_SELECT_ONLY;

	/*
	 * At this point, selectdevs will almost surely indicate that the
	 * device list has changed, so we don't look for return values of 0
	 * or 1.  If we get back -1, though, there is an error.
	 */
	if (selectdevs(&dev_select, &num_selected, &num_selections,
		       &select_generation, generation, cur.dinfo->devices,
		       num_devices, matches, num_matches, specified_devices,
		       num_devices_specified, select_mode,
		       maxshowdevs, 0) == -1)
		errx(1, "%s", devstat_errbuf);

	return(argv);
}

long
getuptime()
{
	static time_t now, boottime;
	time_t uptime;

	if (boottime == 0)
		kread(X_BOOTTIME, &boottime, sizeof(boottime));
	(void)time(&now);
	uptime = now - boottime;
	if (uptime <= 0 || uptime > 60*60*24*365*10)
		errx(1, "time makes no sense; namelist must be wrong");
	return(uptime);
}

int	hz, hdrcnt;

void
dovmstat(interval, reps)
	u_int interval;
	int reps;
{
	struct vmtotal total;
	time_t uptime, halfuptime;
	struct devinfo *tmp_dinfo;
	void needhdr();
	int mib[2];
	size_t size;
	int vmm_size = sizeof(vmm);
	int vms_size = sizeof(vms);
	int vmt_size = sizeof(total);

	uptime = getuptime();
	halfuptime = uptime / 2;
	(void)signal(SIGCONT, needhdr);

	if (namelist[X_STATHZ].n_type != 0 && namelist[X_STATHZ].n_value != 0)
		kread(X_STATHZ, &hz, sizeof(hz));
	if (!hz)
		kread(X_HZ, &hz, sizeof(hz));

	for (hdrcnt = 1;;) {
		if (!--hdrcnt)
			printhdr();
		kread(X_CPTIME, cur.cp_time, sizeof(cur.cp_time));

		tmp_dinfo = last.dinfo;
		last.dinfo = cur.dinfo;
		cur.dinfo = tmp_dinfo;
		last.busy_time = cur.busy_time;

		/*
		 * Here what we want to do is refresh our device stats.
		 * getdevs() returns 1 when the device list has changed.
		 * If the device list has changed, we want to go through
		 * the selection process again, in case a device that we
		 * were previously displaying has gone away.
		 */
		switch (getdevs(&cur)) {
		case -1:
			errx(1, "%s", devstat_errbuf);
			break;
		case 1: {
			int retval;

			num_devices = cur.dinfo->numdevs;
			generation = cur.dinfo->generation;

			retval = selectdevs(&dev_select, &num_selected,
					    &num_selections, &select_generation,
					    generation, cur.dinfo->devices,
					    num_devices, matches, num_matches,
					    specified_devices,
					    num_devices_specified, select_mode,
					    maxshowdevs, 0);
			switch (retval) {
			case -1:
				errx(1, "%s", devstat_errbuf);
				break;
			case 1:
				printhdr();
				break;
			default:
				break;
			}
		}
		default:
			break;
		}

		if (sysctlbyname("vm.vmstats", &vms, &vms_size, NULL, 0)) {
			perror("sysctlbyname: vm.vmstats");
			exit(1);
		}
		if (sysctlbyname("vm.vmmeter", &vmm, &vmm_size, NULL, 0)) {
			perror("sysctlbyname: vm.vmmeter");
			exit(1);
		} 
		if (sysctlbyname("vm.vmtotal", &total, &vmt_size, NULL, 0)) {
			perror("sysctlbyname: vm.vmtotal");
			exit(1);
		} 
		(void)printf("%2d %1d %1d",
		    total.t_rq - 1, total.t_dw + total.t_pw, total.t_sw);
#define vmstat_pgtok(a) ((a) * vms.v_page_size >> 10)
#define	rate(x)	(((x) + halfuptime) / uptime)	/* round */
		(void)printf(" %7ld %6ld ",
		    (long)vmstat_pgtok(total.t_avm), (long)vmstat_pgtok(total.t_free));
		(void)printf("%4lu ",
		    (u_long)rate(vmm.v_vm_faults - ovmm.v_vm_faults));
		(void)printf("%3lu ",
		    (u_long)rate(vmm.v_reactivated - ovmm.v_reactivated));
		(void)printf("%3lu ",
		    (u_long)rate(vmm.v_swapin + vmm.v_vnodein -
		    (ovmm.v_swapin + ovmm.v_vnodein)));
		(void)printf("%3lu ",
		    (u_long)rate(vmm.v_swapout + vmm.v_vnodeout -
		    (ovmm.v_swapout + ovmm.v_vnodeout)));
		(void)printf("%3lu ",
		    (u_long)rate(vmm.v_tfree - ovmm.v_tfree));
		(void)printf("%3lu ",
		    (u_long)rate(vmm.v_pdpages - ovmm.v_pdpages));
		devstats();
		(void)printf("%4lu %4lu %3lu ",
		    (u_long)rate(vmm.v_intr - ovmm.v_intr),
		    (u_long)rate(vmm.v_syscall - ovmm.v_syscall),
		    (u_long)rate(vmm.v_swtch - ovmm.v_swtch));
		cpustats();
		(void)printf("\n");
		(void)fflush(stdout);
		if (reps >= 0 && --reps <= 0)
			break;
		ovmm = vmm;
		uptime = interval;
		/*
		 * We round upward to avoid losing low-frequency events
		 * (i.e., >= 1 per interval but < 1 per second).
		 */
		if (interval != 1)
			halfuptime = (uptime + 1) / 2;
		else
			halfuptime = 0;
		(void)sleep(interval);
	}
}

void
printhdr()
{
	int i, num_shown;

	num_shown = (num_selected < maxshowdevs) ? num_selected : maxshowdevs;
	(void)printf(" procs      memory      page%*s", 19, "");
	if (num_shown > 1)
		(void)printf(" disks %*s", num_shown * 4 - 7, "");
	else if (num_shown == 1)
		(void)printf("disk");
	(void)printf("   faults      cpu\n");
	(void)printf(" r b w     avm    fre  flt  re  pi  po  fr  sr ");
	for (i = 0; i < num_devices; i++)
		if ((dev_select[i].selected)
		 && (dev_select[i].selected <= maxshowdevs))
			(void)printf("%c%c%d ", dev_select[i].device_name[0],
				     dev_select[i].device_name[1],
				     dev_select[i].unit_number);
	(void)printf("  in   sy  cs us sy id\n");
	hdrcnt = winlines - 2;
}

/*
 * Force a header to be prepended to the next output.
 */
void
needhdr()
{

	hdrcnt = 1;
}

long
pct(top, bot)
	long top, bot;
{
	long ans;

	if (bot == 0)
		return(0);
	ans = (quad_t)top * 100 / bot;
	return (ans);
}

#define	PCT(top, bot) pct((long)(top), (long)(bot))

void
dosum()
{
	struct nchstats nchstats;
	long nchtotal;
	int vms_size = sizeof(vms);
	int vmm_size = sizeof(vmm);

	if (sysctlbyname("vm.vmstats", &vms, &vms_size, NULL, 0)) {
		perror("sysctlbyname: vm.vmstats");
		exit(1);
	}
	if (sysctlbyname("vm.vmmeter", &vmm, &vmm_size, NULL, 0)) {
		perror("sysctlbyname: vm.vmstats");
		exit(1);
	} 
	(void)printf("%9u cpu context switches\n", vmm.v_swtch);
	(void)printf("%9u device interrupts\n", vmm.v_intr);
	(void)printf("%9u software interrupts\n", vmm.v_soft);
	(void)printf("%9u traps\n", vmm.v_trap);
	(void)printf("%9u system calls\n", vmm.v_syscall);
	(void)printf("%9u kernel threads created\n", vmm.v_kthreads);
	(void)printf("%9u  fork() calls\n", vmm.v_forks);
	(void)printf("%9u vfork() calls\n", vmm.v_vforks);
	(void)printf("%9u rfork() calls\n", vmm.v_rforks);
	(void)printf("%9u swap pager pageins\n", vmm.v_swapin);
	(void)printf("%9u swap pager pages paged in\n", vmm.v_swappgsin);
	(void)printf("%9u swap pager pageouts\n", vmm.v_swapout);
	(void)printf("%9u swap pager pages paged out\n", vmm.v_swappgsout);
	(void)printf("%9u vnode pager pageins\n", vmm.v_vnodein);
	(void)printf("%9u vnode pager pages paged in\n", vmm.v_vnodepgsin);
	(void)printf("%9u vnode pager pageouts\n", vmm.v_vnodeout);
	(void)printf("%9u vnode pager pages paged out\n", vmm.v_vnodepgsout);
	(void)printf("%9u page daemon wakeups\n", vmm.v_pdwakeups);
	(void)printf("%9u pages examined by the page daemon\n", vmm.v_pdpages);
	(void)printf("%9u pages reactivated\n", vmm.v_reactivated);
	(void)printf("%9u copy-on-write faults\n", vmm.v_cow_faults);
	(void)printf("%9u copy-on-write optimized faults\n", vmm.v_cow_optim);
	(void)printf("%9u zero fill pages zeroed\n", vmm.v_zfod);
	(void)printf("%9u zero fill pages prezeroed\n", vmm.v_ozfod);
	(void)printf("%9u intransit blocking page faults\n", vmm.v_intrans);
	(void)printf("%9u total VM faults taken\n", vmm.v_vm_faults);
	(void)printf("%9u pages affected by kernel thread creation\n", vmm.v_kthreadpages);
	(void)printf("%9u pages affected by  fork()\n", vmm.v_forkpages);
	(void)printf("%9u pages affected by vfork()\n", vmm.v_vforkpages);
	(void)printf("%9u pages affected by rfork()\n", vmm.v_rforkpages);
	(void)printf("%9u pages freed\n", vmm.v_tfree);
	(void)printf("%9u pages freed by daemon\n", vmm.v_dfree);
	(void)printf("%9u pages freed by exiting processes\n", vmm.v_pfree);
	(void)printf("%9u pages active\n", vms.v_active_count);
	(void)printf("%9u pages inactive\n", vms.v_inactive_count);
	(void)printf("%9u pages in VM cache\n", vms.v_cache_count);
	(void)printf("%9u pages wired down\n", vms.v_wire_count);
	(void)printf("%9u pages free\n", vms.v_free_count);
	(void)printf("%9u bytes per page\n", vms.v_page_size);
	kread(X_NCHSTATS, &nchstats, sizeof(nchstats));
	nchtotal = nchstats.ncs_goodhits + nchstats.ncs_neghits +
	    nchstats.ncs_badhits + nchstats.ncs_falsehits +
	    nchstats.ncs_miss + nchstats.ncs_long;
	(void)printf("%9ld total name lookups\n", nchtotal);
	(void)printf(
	    "%9s cache hits (%ld%% pos + %ld%% neg) system %ld%% per-directory\n",
	    "", PCT(nchstats.ncs_goodhits, nchtotal),
	    PCT(nchstats.ncs_neghits, nchtotal),
	    PCT(nchstats.ncs_pass2, nchtotal));
	(void)printf("%9s deletions %ld%%, falsehits %ld%%, toolong %ld%%\n", "",
	    PCT(nchstats.ncs_badhits, nchtotal),
	    PCT(nchstats.ncs_falsehits, nchtotal),
	    PCT(nchstats.ncs_long, nchtotal));
}

#ifdef notyet
void
doforkst()
{
	struct forkstat fks;

	kread(X_FORKSTAT, &fks, sizeof(struct forkstat));
	(void)printf("%d forks, %d pages, average %.2f\n",
	    fks.cntfork, fks.sizfork, (double)fks.sizfork / fks.cntfork);
	(void)printf("%d vforks, %d pages, average %.2f\n",
	    fks.cntvfork, fks.sizvfork, (double)fks.sizvfork / fks.cntvfork);
}
#endif

static void
devstats()
{
	register int dn, state;
	long double transfers_per_second;
	long double busy_seconds;
	long tmp;
	
	for (state = 0; state < CPUSTATES; ++state) {
		tmp = cur.cp_time[state];
		cur.cp_time[state] -= last.cp_time[state];
		last.cp_time[state] = tmp;
	}

	busy_seconds = compute_etime(cur.busy_time, last.busy_time);

	for (dn = 0; dn < num_devices; dn++) {
		int di;

		if ((dev_select[dn].selected == 0)
		 || (dev_select[dn].selected > maxshowdevs))
			continue;

		di = dev_select[dn].position;

		if (compute_stats(&cur.dinfo->devices[di],
				  &last.dinfo->devices[di], busy_seconds,
				  NULL, NULL, NULL,
				  NULL, &transfers_per_second, NULL,
				  NULL, NULL) != 0)
			errx(1, "%s", devstat_errbuf);

		printf("%3.0Lf ", transfers_per_second);
	}
}

void
cpustats()
{
	register int state;
	double pct, total;

	total = 0;
	for (state = 0; state < CPUSTATES; ++state)
		total += cur.cp_time[state];
	if (total)
		pct = 100 / total;
	else
		pct = 0;
	(void)printf("%2.0f ", (cur.cp_time[CP_USER] +
				cur.cp_time[CP_NICE]) * pct);
	(void)printf("%2.0f ", (cur.cp_time[CP_SYS] +
				cur.cp_time[CP_INTR]) * pct);
	(void)printf("%2.0f", cur.cp_time[CP_IDLE] * pct);
}

void
dointr()
{
	register u_long *intrcnt, uptime;
	register u_int64_t inttotal;
	register int nintr, inamlen;
	register char *intrname;

	uptime = getuptime();
	nintr = namelist[X_EINTRCNT].n_value - namelist[X_INTRCNT].n_value;
	inamlen =
	    namelist[X_EINTRNAMES].n_value - namelist[X_INTRNAMES].n_value;
	intrcnt = malloc((size_t)nintr);
	intrname = malloc((size_t)inamlen);
	if (intrcnt == NULL || intrname == NULL)
		errx(1, "malloc");
	kread(X_INTRCNT, intrcnt, (size_t)nintr);
	kread(X_INTRNAMES, intrname, (size_t)inamlen);
	(void)printf("interrupt                   total       rate\n");
	inttotal = 0;
	nintr /= sizeof(long);
	while (--nintr >= 0) {
		if (*intrcnt)
			(void)printf("%-12s %20lu %10lu\n", intrname,
			    *intrcnt, *intrcnt / uptime);
		intrname += strlen(intrname) + 1;
		inttotal += *intrcnt++;
	}
	(void)printf("Total        %20llu %10llu\n", inttotal,
			inttotal / (u_int64_t) uptime);
}

#define	MAX_KMSTATS	200

void
domem()
{
	register struct kmembuckets *kp;
	register struct malloc_type *ks;
	register int i, j;
	int len, size, first, nkms;
	long totuse = 0, totfree = 0, totreq = 0;
	const char *name;
	struct malloc_type kmemstats[MAX_KMSTATS], *kmsp;
	char buf[1024];
	struct kmembuckets buckets[MINBUCKET + 16];

#ifdef X_KMEMBUCKETS
	kread(X_KMEMBUCKETS, buckets, sizeof(buckets));
#else
	bzero(buckets, sizeof(buckets));
#endif
	kread(X_KMEMSTATISTICS, &kmsp, sizeof(kmsp));
	for (nkms = 0; nkms < MAX_KMSTATS && kmsp != NULL; nkms++) {
		if (sizeof(kmemstats[0]) != kvm_read(kd, (u_long)kmsp,
		    &kmemstats[nkms], sizeof(kmemstats[0])))
			err(1, "kvm_read(%p)", (void *)kmsp);
		if (sizeof(buf) !=  kvm_read(kd, 
	            (u_long)kmemstats[nkms].ks_shortdesc, buf, sizeof(buf)))
			err(1, "kvm_read(%p)", 
			    (void *)kmemstats[nkms].ks_shortdesc);
		buf[sizeof(buf) - 1] = '\0';
		kmemstats[nkms].ks_shortdesc = strdup(buf);
		kmsp = kmemstats[nkms].ks_next;
	}
	if (kmsp != NULL)
		warnx("truncated to the first %d memory types", nkms);
	(void)printf("Memory statistics by bucket size\n");
	(void)printf(
	    "Size   In Use   Free   Requests  HighWater  Couldfree\n");
	for (i = MINBUCKET, kp = &buckets[i]; i < MINBUCKET + 16; i++, kp++) {
		if (kp->kb_calls == 0)
			continue;
		size = 1 << i;
		if(size < 1024)
			(void)printf("%4d",size);
		else
			(void)printf("%3dK",size>>10);
		(void)printf(" %8ld %6ld %10lld %7ld %10ld\n",
			kp->kb_total - kp->kb_totalfree,
			kp->kb_totalfree, kp->kb_calls,
			kp->kb_highwat, kp->kb_couldfree);
		totfree += size * kp->kb_totalfree;
	}

	(void)printf("\nMemory usage type by bucket size\n");
	(void)printf("Size  Type(s)\n");
	kp = &buckets[MINBUCKET];
	for (j =  1 << MINBUCKET; j < 1 << (MINBUCKET + 16); j <<= 1, kp++) {
		if (kp->kb_calls == 0)
			continue;
		first = 1;
		len = 8;
		for (i = 0, ks = &kmemstats[0]; i < nkms; i++, ks++) {
			if (ks->ks_calls == 0)
				continue;
			if ((ks->ks_size & j) == 0)
				continue;
			name = ks->ks_shortdesc;
			len += 2 + strlen(name);
			if (first && j < 1024)
				printf("%4d  %s", j, name);
			else if (first)
				printf("%3dK  %s", j>>10, name);
			else
				printf(",");
			if (len >= 79) {
				printf("\n\t ");
				len = 10 + strlen(name);
			}
			if (!first)
				printf(" %s", name);
			first = 0;
		}
		printf("\n");
	}

	(void)printf(
	    "\nMemory statistics by type                          Type  Kern\n");
	(void)printf(
"        Type  InUse MemUse HighUse  Limit Requests Limit Limit Size(s)\n");
	for (i = 0, ks = &kmemstats[0]; i < nkms; i++, ks++) {
		if (ks->ks_calls == 0)
			continue;
		(void)printf("%13s%6ld%6ldK%7ldK%6ldK%9lld%5u%6u",
		    ks->ks_shortdesc,
		    ks->ks_inuse, (ks->ks_memuse + 1023) / 1024,
		    (ks->ks_maxused + 1023) / 1024,
		    (ks->ks_limit + 1023) / 1024, ks->ks_calls,
		    ks->ks_limblocks, ks->ks_mapblocks);
		first = 1;
		for (j =  1 << MINBUCKET; j < 1 << (MINBUCKET + 16); j <<= 1) {
			if ((ks->ks_size & j) == 0)
				continue;
			if (first)
				printf("  ");
			else
				printf(",");
			if(j<1024)
				printf("%d",j);
			else
				printf("%dK",j>>10);
			first = 0;
		}
		printf("\n");
		totuse += ks->ks_memuse;
		totreq += ks->ks_calls;
	}
	(void)printf("\nMemory Totals:  In Use    Free    Requests\n");
	(void)printf("              %7ldK %6ldK    %8ld\n",
	     (totuse + 1023) / 1024, (totfree + 1023) / 1024, totreq);
}

void
dozmem()
{
	char *buf;
	size_t bufsize;

	buf = NULL;
	bufsize = 1024;
	for (;;) {
		if ((buf = realloc(buf, bufsize)) == NULL)
			err(1, "realloc()");
		if (sysctlbyname("vm.zone", buf, &bufsize, 0, NULL) == 0)
			break;
		if (errno != ENOMEM)
			err(1, "sysctl()");
		bufsize *= 2;
	}
	buf[bufsize] = '\0'; /* play it safe */
	(void)printf("%s\n\n", buf);
	free(buf);
}

/*
 * kread reads something from the kernel, given its nlist index.
 */
void
kread(nlx, addr, size)
	int nlx;
	void *addr;
	size_t size;
{
	char *sym;

	if (namelist[nlx].n_type == 0 || namelist[nlx].n_value == 0) {
		sym = namelist[nlx].n_name;
		if (*sym == '_')
			++sym;
		errx(1, "symbol %s not defined", sym);
	}
	if (kvm_read(kd, namelist[nlx].n_value, addr, size) != size) {
		sym = namelist[nlx].n_name;
		if (*sym == '_')
			++sym;
		errx(1, "%s: %s", sym, kvm_geterr(kd));
	}
}

void
usage()
{
	(void)fprintf(stderr, "%s%s",
		"usage: vmstat [-imsz] [-c count] [-M core] [-N system] [-w wait]\n",
		"              [-n devs] [disks]\n");
	exit(1);
}
