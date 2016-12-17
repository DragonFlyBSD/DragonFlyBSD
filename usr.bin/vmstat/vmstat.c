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
 * @(#) Copyright (c) 1980, 1986, 1991, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)vmstat.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/vmstat/vmstat.c,v 1.38.2.4 2001/07/31 19:52:41 tmm Exp $
 */

#include <sys/user.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/signal.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/interrupt.h>

#include <vm/vm_param.h>
#include <vm/vm_zone.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <kinfo.h>
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
#define	X_BOOTTIME	0
	{ "_boottime",	0, 0, 0, 0 },
#define X_NCHSTATS	1
	{ "_nchstats",	0, 0, 0, 0 },
#define	X_KMEMSTATISTICS	2
	{ "_kmemstatistics",	0, 0, 0, 0 },
#define	X_ZLIST		3
	{ "_zlist",	0, 0, 0, 0 },
#ifdef notyet
#define	X_DEFICIT	4
	{ "_deficit",	0, 0, 0, 0 },
#define	X_FORKSTAT	5
	{ "_forkstat",	0, 0, 0, 0 },
#define X_REC		6
	{ "_rectime",	0, 0, 0, 0 },
#define X_PGIN		7
	{ "_pgintime",	0, 0, 0, 0 },
#define	X_XSTATS	8
	{ "_xstats",	0, 0, 0, 0 },
#define X_END		9
#else
#define X_END		4
#endif
	{ "", 0, 0, 0, 0 },
};

LIST_HEAD(zlist, vm_zone);

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
int	verbose = 0;

kvm_t *kd;

struct kinfo_cputime cp_time, old_cp_time, diff_cp_time;

#define	FORKSTAT	0x01
#define	INTRSTAT	0x02
#define	MEMSTAT		0x04
#define	SUMSTAT		0x08
#define	TIMESTAT	0x10
#define	VMSTAT		0x20
#define ZMEMSTAT	0x40

static void cpustats(void);
static void dointr(void);
static void domem(void);
static void dosum(void);
static void dozmem(u_int interval, int reps);
static void dovmstat(u_int, int);
static void kread(int, void *, size_t);
static void usage(void);
static char **getdrivedata(char **);
static long getuptime(void);
static void needhdr(int);
static long pct(long, long);

#ifdef notyet
static void dotimes(void); /* Not implemented */
static void doforkst(void);
#endif
static void printhdr(void);
static const char *formatnum(intmax_t value, int width);
static void devstats(int dooutput);

int
main(int argc, char **argv)
{
	int c, todo;
	u_int interval;		/* milliseconds */
	int reps;
	char *memf, *nlistf;
	char errbuf[_POSIX2_LINE_MAX];

	memf = nlistf = NULL;
	interval = reps = todo = 0;
	maxshowdevs = 2;
	while ((c = getopt(argc, argv, "c:fiM:mN:n:p:stvw:z")) != -1) {
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
		case 'v':
			++verbose;
			break;
		case 'w':
			interval = (u_int)(strtod(optarg, NULL) * 1000.0);
			break;
		case 'z':
			todo |= ZMEMSTAT;
			break;
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
	if (kd == NULL)
		errx(1, "kvm_openfiles: %s", errbuf);

	if ((c = kvm_nlist(kd, namelist)) != 0) {
		if (c > 0) {
			warnx("undefined symbols:");
			for (c = 0; c < (int)__arysize(namelist); c++)
				if (namelist[c].n_type == 0)
					fprintf(stderr, " %s",
					    namelist[c].n_name);
			fputc('\n', stderr);
		} else
			warnx("kvm_nlist: %s", kvm_geterr(kd));
		exit(1);
	}

	if (todo & VMSTAT) {
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
		ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *)&winsize);
		if (winsize.ws_row > 0)
			winlines = winsize.ws_row;

	}

#define	BACKWARD_COMPATIBILITY
#ifdef	BACKWARD_COMPATIBILITY
	if (*argv) {
		interval = (u_int)(strtod(*argv, NULL) * 1000.0);
		if (*++argv)
			reps = atoi(*argv);
	}
#endif

	if (interval) {
		if (!reps)
			reps = -1;
	} else if (reps) {
		interval = 1000;
	}

#ifdef notyet
	if (todo & FORKSTAT)
		doforkst();
#endif
	if (todo & MEMSTAT)
		domem();
	if (todo & ZMEMSTAT)
		dozmem(interval, reps);
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

static char **
getdrivedata(char **argv)
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

static long
getuptime(void)
{
	static time_t now, boottime;
	time_t uptime;

	if (boottime == 0)
		kread(X_BOOTTIME, &boottime, sizeof(boottime));
	time(&now);
	uptime = now - boottime;
	if (uptime <= 0 || uptime > 60*60*24*365*10)
		errx(1, "time makes no sense; namelist must be wrong");
	return(uptime);
}

int	hdrcnt;

static void
dovmstat(u_int interval, int reps)
{
	struct vmtotal total;
	struct devinfo *tmp_dinfo;
	size_t vmm_size = sizeof(vmm);
	size_t vms_size = sizeof(vms);
	size_t vmt_size = sizeof(total);
	int initial = 1;
	int dooutput = 1;

	signal(SIGCONT, needhdr);
	if (reps != 0)
		dooutput = 0;

	for (hdrcnt = 1;;) {
		if (!--hdrcnt)
			printhdr();
		if (kinfo_get_sched_cputime(&cp_time))
			err(1, "kinfo_get_sched_cputime");

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
		if (dooutput) {
			printf("%3ld %2ld %2ld",
			       total.t_rq - 1,
			       total.t_dw + total.t_pw,
			       total.t_sw);
		}

#define rate(x)		\
	(intmax_t)(initial ? (x) : ((intmax_t)(x) * 1000 + interval / 2) \
				   / interval)

		if (dooutput) {
			printf(" %s ",
			       formatnum((int64_t)total.t_free *
					 vms.v_page_size, 4));
			printf("%s ",
			       formatnum(rate(vmm.v_vm_faults -
					      ovmm.v_vm_faults), 5));
			printf("%s ",
			       formatnum(rate(vmm.v_reactivated -
					      ovmm.v_reactivated), 4));
			printf("%s ",
			       formatnum(rate(vmm.v_swapin + vmm.v_vnodein -
				      (ovmm.v_swapin + ovmm.v_vnodein)), 4));
			printf("%s ",
			       formatnum(rate(vmm.v_swapout + vmm.v_vnodeout -
				    (ovmm.v_swapout + ovmm.v_vnodeout)), 4));
			printf("%s ",
			       formatnum(rate(vmm.v_tfree -
					      ovmm.v_tfree), 4));
		}
		devstats(dooutput);
		if (dooutput) {
			printf("%s ",
			       formatnum(rate(vmm.v_intr -
					      ovmm.v_intr), 5));
			printf("%s ",
			       formatnum(rate(vmm.v_syscall -
					      ovmm.v_syscall), 5));
			printf("%s ",
			       formatnum(rate(vmm.v_swtch -
					      ovmm.v_swtch), 5));
			cpustats();
			printf("\n");
			fflush(stdout);
		}
		if (reps >= 0 && --reps <= 0)
			break;
		ovmm = vmm;
		usleep(interval * 1000);
		initial = 0;
		dooutput = 1;
	}
}

static const char *
formatnum(intmax_t value, int width)
{
	static char buf[64];
	const char *fmt;
	double d;

	d = (double)value;
	fmt = "n/a";

	switch(width) {
	case 4:
		if (value < 1024) {
			fmt = "%4.0f";
		} else if (value < 10*1024) {
			fmt = "%3.1fK";
			d = d / 1024;
		} else if (value < 1000*1024) {
			fmt = "%3.0fK";
			d = d / 1024;
		} else if (value < 10*1024*1024) {
			fmt = "%3.1fM";
			d = d / (1024 * 1024);
		} else if (value < 1000*1024*1024) {
			fmt = "%3.0fM";
			d = d / (1024 * 1024);
		} else {
			fmt = "%3.1fG";
			d = d / (1024.0 * 1024.0 * 1024.0);
		}
		break;
	case 5:
		if (value < 1024) {
			fmt = "%5.0f";
		} else if (value < 10*1024) {
			fmt = "%4.2fK";
			d = d / 1024;
		} else if (value < 1000*1024) {
			fmt = "%4.0fK";
			d = d / 1024;
		} else if (value < 10*1024*1024) {
			fmt = "%4.2fM";
			d = d / (1024 * 1024);
		} else if (value < 1000*1024*1024) {
			fmt = "%4.0fM";
			d = d / (1024 * 1024);
		} else {
			fmt = "%4.2fG";
			d = d / (1024.0 * 1024.0 * 1024.0);
		}
		break;
	default:
		fprintf(stderr, "formatnum: unsupported width %d\n", width);
		exit(1);
		break;
	}
	snprintf(buf, sizeof(buf), fmt, d);
	return buf;
}

static void
printhdr(void)
{
	int i, num_shown;

	num_shown = (num_selected < maxshowdevs) ? num_selected : maxshowdevs;
	printf("--procs-- ---memory-- -------paging------ ");
	if (num_shown > 1)
		printf("--disks%.*s",
		       num_shown * 4 - 6,
		       "---------------------------------");
	else if (num_shown == 1)
		printf("disk");
	printf(" -----faults------ ---cpu---\n");
	printf("  r  b  w   fre   flt   re   pi   po   fr ");
	for (i = 0; i < num_devices; i++)
		if ((dev_select[i].selected)
		 && (dev_select[i].selected <= maxshowdevs))
			printf(" %c%c%d ", dev_select[i].device_name[0],
				     dev_select[i].device_name[1],
				     dev_select[i].unit_number);
	printf("  int   sys   ctx us sy id\n");
	hdrcnt = winlines - 2;
}

/*
 * Force a header to be prepended to the next output.
 */
static void
needhdr(__unused int signo)
{

	hdrcnt = 1;
}

static long
pct(long top, long bot)
{
	long ans;

	if (bot == 0)
		return(0);
	ans = (quad_t)top * 100 / bot;
	return (ans);
}

#define	PCT(top, bot) pct((long)(top), (long)(bot))

static void
dosum(void)
{
	struct nchstats *nch_tmp, nchstats;
	size_t vms_size = sizeof(vms);
	size_t vmm_size = sizeof(vmm);
	int cpucnt;
	u_long nchtotal;
	u_long nchpathtotal;
	size_t nch_size = sizeof(struct nchstats) * SMP_MAXCPU;

	if (sysctlbyname("vm.vmstats", &vms, &vms_size, NULL, 0)) {
		perror("sysctlbyname: vm.vmstats");
		exit(1);
	}
	if (sysctlbyname("vm.vmmeter", &vmm, &vmm_size, NULL, 0)) {
		perror("sysctlbyname: vm.vmstats");
		exit(1);
	}
	printf("%9u cpu context switches\n", vmm.v_swtch);
	printf("%9u device interrupts\n", vmm.v_intr);
	printf("%9u software interrupts\n", vmm.v_soft);
	printf("%9u traps\n", vmm.v_trap);
	printf("%9u system calls\n", vmm.v_syscall);
	printf("%9u kernel threads created\n", vmm.v_kthreads);
	printf("%9u  fork() calls\n", vmm.v_forks);
	printf("%9u vfork() calls\n", vmm.v_vforks);
	printf("%9u rfork() calls\n", vmm.v_rforks);
	printf("%9u exec() calls\n", vmm.v_exec);
	printf("%9u swap pager pageins\n", vmm.v_swapin);
	printf("%9u swap pager pages paged in\n", vmm.v_swappgsin);
	printf("%9u swap pager pageouts\n", vmm.v_swapout);
	printf("%9u swap pager pages paged out\n", vmm.v_swappgsout);
	printf("%9u vnode pager pageins\n", vmm.v_vnodein);
	printf("%9u vnode pager pages paged in\n", vmm.v_vnodepgsin);
	printf("%9u vnode pager pageouts\n", vmm.v_vnodeout);
	printf("%9u vnode pager pages paged out\n", vmm.v_vnodepgsout);
	printf("%9u page daemon wakeups\n", vmm.v_pdwakeups);
	printf("%9u pages examined by the page daemon\n", vmm.v_pdpages);
	printf("%9u pages reactivated\n", vmm.v_reactivated);
	printf("%9u copy-on-write faults\n", vmm.v_cow_faults);
	printf("%9u copy-on-write optimized faults\n", vmm.v_cow_optim);
	printf("%9u zero fill pages zeroed\n", vmm.v_zfod);
	printf("%9u zero fill pages prezeroed\n", vmm.v_ozfod);
	printf("%9u intransit blocking page faults\n", vmm.v_intrans);
	printf("%9u total VM faults taken\n", vmm.v_vm_faults);
	printf("%9u pages affected by kernel thread creation\n", vmm.v_kthreadpages);
	printf("%9u pages affected by  fork()\n", vmm.v_forkpages);
	printf("%9u pages affected by vfork()\n", vmm.v_vforkpages);
	printf("%9u pages affected by rfork()\n", vmm.v_rforkpages);
	printf("%9u pages freed\n", vmm.v_tfree);
	printf("%9u pages freed by daemon\n", vmm.v_dfree);
	printf("%9u pages freed by exiting processes\n", vmm.v_pfree);
	printf("%9u pages active\n", vms.v_active_count);
	printf("%9u pages inactive\n", vms.v_inactive_count);
	printf("%9u pages in VM cache\n", vms.v_cache_count);
	printf("%9u pages wired down\n", vms.v_wire_count);
	printf("%9u pages free\n", vms.v_free_count);
	printf("%9u bytes per page\n", vms.v_page_size);
	printf("%9u global smp invltlbs\n", vmm.v_smpinvltlb);
	
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
	
	cpucnt = nch_size / sizeof(struct nchstats);
	kvm_nch_cpuagg(nch_tmp, &nchstats, cpucnt);

	nchtotal = nchstats.ncs_goodhits + nchstats.ncs_neghits +
	    nchstats.ncs_badhits + nchstats.ncs_falsehits +
	    nchstats.ncs_miss;
	nchpathtotal = nchstats.ncs_longhits + nchstats.ncs_longmiss;
	printf("%9ld total path lookups\n", nchpathtotal);
	printf("%9ld total component lookups\n", nchtotal);
	printf(
	    "%9s cache hits (%ld%% pos + %ld%% neg)\n",
	    "", PCT(nchstats.ncs_goodhits, nchtotal),
	    PCT(nchstats.ncs_neghits, nchtotal));
	printf("%9s deletions %ld%%, falsehits %ld%%\n", "",
	    PCT(nchstats.ncs_badhits, nchtotal),
	    PCT(nchstats.ncs_falsehits, nchtotal));
	free(nch_tmp);
}

#ifdef notyet
void
doforkst(void)
{
	struct forkstat fks;

	kread(X_FORKSTAT, &fks, sizeof(struct forkstat));
	printf("%d forks, %d pages, average %.2f\n",
	    fks.cntfork, fks.sizfork, (double)fks.sizfork / fks.cntfork);
	printf("%d vforks, %d pages, average %.2f\n",
	    fks.cntvfork, fks.sizvfork, (double)fks.sizvfork / fks.cntvfork);
}
#endif

static void
devstats(int dooutput)
{
	int dn;
	long double transfers_per_second;
	long double busy_seconds;

	diff_cp_time.cp_user = cp_time.cp_user - old_cp_time.cp_user;
	diff_cp_time.cp_nice = cp_time.cp_nice - old_cp_time.cp_nice;
	diff_cp_time.cp_sys = cp_time.cp_sys - old_cp_time.cp_sys;
	diff_cp_time.cp_intr = cp_time.cp_intr - old_cp_time.cp_intr;
	diff_cp_time.cp_idle = cp_time.cp_idle - old_cp_time.cp_idle;
	old_cp_time = cp_time;

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

		if (dooutput)
			printf("%s ", formatnum(transfers_per_second, 4));
	}
}

static void
cpustats(void)
{
	uint64_t total;
	double totusage;

	total = diff_cp_time.cp_user + diff_cp_time.cp_nice +
	    diff_cp_time.cp_sys + diff_cp_time.cp_intr + diff_cp_time.cp_idle;

	if (total)
		totusage = 100.0 / total;
	else
		totusage = 0;
	printf("%2.0f ",
	       (diff_cp_time.cp_user + diff_cp_time.cp_nice) * totusage);
	printf("%2.0f ",
	       (diff_cp_time.cp_sys + diff_cp_time.cp_intr) * totusage);
	printf("%2.0f",
	       diff_cp_time.cp_idle * totusage);
}

static void
dointr(void)
{
	u_long *intrcnt, uptime;
	u_int64_t inttotal;
	size_t nintr, inamlen, i, size;
	int nwidth;
	char *intrstr;
	char **intrname;

	uptime = getuptime();
	if (sysctlbyname("hw.intrnames", NULL, &inamlen, NULL, 0) != 0)
		errx(1, "sysctlbyname");
	intrstr = malloc(inamlen);
	if (intrstr == NULL)
		err(1, "malloc");
	sysctlbyname("hw.intrnames", intrstr, &inamlen, NULL, 0);
	for (nintr = 0, i = 0; i < inamlen; ++i) {
		if (intrstr[i] == 0)
			nintr++;
	}
	intrname = malloc(nintr * sizeof(char *));
	for (i = 0; i < nintr; ++i) {
		intrname[i] = intrstr;
		intrstr += strlen(intrstr) + 1;
	}

	size = nintr * sizeof(*intrcnt);
	intrcnt = calloc(nintr, sizeof(*intrcnt));
	if (intrcnt == NULL)
		err(1, "malloc");
	sysctlbyname("hw.intrcnt", intrcnt, &size, NULL, 0);

	nwidth = 21;
	for (i = 0; i < nintr; ++i) {
		if (nwidth < (int)strlen(intrname[i]))
			nwidth = (int)strlen(intrname[i]);
	}
	if (verbose) nwidth += 12;

	printf("%-*.*s %11s %10s\n",
		nwidth, nwidth, "interrupt", "total", "rate");
	inttotal = 0;
	for (i = 0; i < nintr; ++i) {
		int named;
		char *infop, irqinfo[72];

		if ((named = strncmp(intrname[i], "irq", 3)) != 0 ||
		    intrcnt[i] > 0) {
			infop = intrname[i];
			if (verbose) {
				ssize_t irq, cpu;

				irq = i % MAX_INTS;
				cpu = i / MAX_INTS;
				if (named) {
					snprintf(irqinfo, sizeof(irqinfo),
						 "irq%-3zd %3zd: %s",
						 irq, cpu, intrname[i]);
				} else {
					snprintf(irqinfo, sizeof(irqinfo),
						 "irq%-3zd %3zd: ", irq, cpu);
				}
				infop = irqinfo;
			}
			printf("%-*.*s %11lu %10lu\n", 
				nwidth, nwidth, infop,
				intrcnt[i], intrcnt[i] / uptime);
		}
		inttotal += intrcnt[i];
	}
	printf("%-*.*s %11llu %10llu\n", 
		nwidth, nwidth, "Total",
		(long long)inttotal, (long long)(inttotal / uptime));
}

#define	MAX_KMSTATS	1024

enum ksuse { KSINUSE, KSMEMUSE };

static long
cpuagg(struct malloc_type *ks, enum ksuse use)
{
    int i;
    long ttl;

    ttl = 0;

    switch(use) {
    case KSINUSE:
	for (i = 0; i < SMP_MAXCPU; ++i)
	    ttl += ks->ks_use[i].inuse;
	break;
    case KSMEMUSE:
	for (i = 0; i < SMP_MAXCPU; ++i)
	    ttl += ks->ks_use[i].memuse;
	break;
    }
    return(ttl);
}

static void
domem(void)
{
	struct malloc_type *ks;
	int i, j;
	int first, nkms;
	long totuse = 0, totfree = 0, totreq = 0;
	struct malloc_type kmemstats[MAX_KMSTATS], *kmsp;
	char buf[1024];

	kread(X_KMEMSTATISTICS, &kmsp, sizeof(kmsp));
	for (nkms = 0; nkms < MAX_KMSTATS && kmsp != NULL; nkms++) {
		if (sizeof(kmemstats[0]) != kvm_read(kd, (u_long)kmsp,
		    &kmemstats[nkms], sizeof(kmemstats[0])))
			err(1, "kvm_read(%p)", (void *)kmsp);
		if (sizeof(buf) !=  kvm_read(kd, 
	            (u_long)kmemstats[nkms].ks_shortdesc, buf, sizeof(buf)))
			err(1, "kvm_read(%p)", 
			    kmemstats[nkms].ks_shortdesc);
		buf[sizeof(buf) - 1] = '\0';
		kmemstats[nkms].ks_shortdesc = strdup(buf);
		kmsp = kmemstats[nkms].ks_next;
	}
	if (kmsp != NULL)
		warnx("truncated to the first %d memory types", nkms);

	printf(
	    "\nMemory statistics by type                          Type  Kern\n");
	printf(
"              Type   InUse  MemUse HighUse       Limit  Requests  Limit Limit\n");
	for (i = 0, ks = &kmemstats[0]; i < nkms; i++, ks++) {
		if (ks->ks_calls == 0)
			continue;
		printf("%19s%7ld%7ldK%7ldK%11zuK%10jd%5u%6u",
		    ks->ks_shortdesc,
		    cpuagg(ks, KSINUSE), (cpuagg(ks, KSMEMUSE) + 1023) / 1024,
		    (ks->ks_maxused + 1023) / 1024,
		    (ks->ks_limit + 1023) / 1024, (intmax_t)ks->ks_calls,
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
		totuse += cpuagg(ks, KSMEMUSE);
		totreq += ks->ks_calls;
	}
	printf("\nMemory Totals:  In Use    Free    Requests\n");
	printf("              %7ldK %6ldK    %8ld\n",
	     (totuse + 1023) / 1024, (totfree + 1023) / 1024, totreq);
}

#define MAXSAVE	16

static void
dozmem(u_int interval, int reps)
{
	struct zlist	zlist;
	struct vm_zone	*kz;
	struct vm_zone	zone;
	struct vm_zone	copy;
	struct vm_zone	save[MAXSAVE];
	char name[64];
	size_t namesz;
	int i;
	int first = 1;

	bzero(save, sizeof(save));

again:
	kread(X_ZLIST, &zlist, sizeof(zlist));
	kz = LIST_FIRST(&zlist);
	i = 0;

	while (kz) {
		if (kvm_read(kd, (intptr_t)kz, &zone, sizeof(zone)) !=
		    (ssize_t)sizeof(zone)) {
			perror("kvm_read");
			break;
		}
		copy = zone;
		zone.znalloc -= save[i].znalloc;
		save[i] = copy;
		namesz = sizeof(name);
		if (kvm_readstr(kd, (intptr_t)zone.zname, name, &namesz) == NULL) {
			perror("kvm_read");
			break;
		}
		if (first && interval) {
			/* do nothing */
		} else if (zone.zmax) {
			printf("%-10s %9ld/%9ld %5ldM used"
			       " use=%-9lu %6.2f%%\n",
				name,
				(long)(zone.ztotal - zone.zfreecnt),
				(long)zone.zmax,
				(long)(zone.ztotal - zone.zfreecnt) *
					zone.zsize / (1024 * 1024),
				(unsigned long)zone.znalloc,
				(double)(zone.ztotal - zone.zfreecnt) *
					100.0 / (double)zone.zmax);
		} else {
			printf("%-10s %9ld           %5ldM used"
			       " use=%-9lu\n",
				name,
				(long)(zone.ztotal - zone.zfreecnt),
				(long)(zone.ztotal - zone.zfreecnt) *
					zone.zsize / (1024 * 1024),
				(unsigned long)zone.znalloc);
		}
		kz = LIST_NEXT(&zone, zlink);
		++i;
	}
	if (reps) {
		first = 0;
		fflush(stdout);
		usleep(interval * 1000);
		--reps;
		printf("\n");
		goto again;
	}
}

/*
 * kread reads something from the kernel, given its nlist index.
 */
static void
kread(int nlx, void *addr, size_t size)
{
	const char *sym;

	if (namelist[nlx].n_type == 0 || namelist[nlx].n_value == 0) {
		sym = namelist[nlx].n_name;
		if (*sym == '_')
			++sym;
		errx(1, "symbol %s not defined", sym);
	}
	if (kvm_read(kd, namelist[nlx].n_value, addr, size) != (ssize_t)size) {
		sym = namelist[nlx].n_name;
		if (*sym == '_')
			++sym;
		errx(1, "%s: %s", sym, kvm_geterr(kd));
	}
}

static void
usage(void)
{
	fprintf(stderr, "%s%s",
		"usage: vmstat [-imsvz] [-c count] [-M core] [-N system] [-w wait]\n",
		"              [-n devs] [disks]\n");
	exit(1);
}
