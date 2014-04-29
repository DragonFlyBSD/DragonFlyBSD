/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  For DragonFly 2.x and later
 *
 * DESCRIPTION:
 * Originally written for BSD4.4 system by Christos Zoulas.
 * Ported to FreeBSD 2.x by Steven Wallace && Wolfram Schneider
 * Order support hacked in from top-3.5beta6/machine/m_aix41.c
 *   by Monte Mitzelfelt (for latest top see http://www.groupsys.com/topinfo/)
 *
 * This is the machine-dependent module for DragonFly 2.5.1
 * Should work for:
 *	DragonFly 2.x and above
 *
 * LIBS: -lkvm
 *
 * AUTHOR: Jan Lentfer <Jan.Lentfer@web.de>
 * This module has been put together from different sources and is based on the
 * work of many other people, e.g. Matthew Dillon, Simon Schubert, Jordan Gordeev.
 *
 * $FreeBSD: src/usr.bin/top/machine.c,v 1.29.2.2 2001/07/31 20:27:05 tmm Exp $
 */

#include <sys/user.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/param.h>

#include "os.h"
#include <err.h>
#include <kvm.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <pwd.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/vmmeter.h>
#include <sys/resource.h>
#include <sys/rtprio.h>

/* Swap */
#include <stdlib.h>
#include <sys/conf.h>

#include <osreldate.h>		/* for changes in kernel structures */

#include <sys/kinfo.h>
#include <kinfo.h>
#include "top.h"
#include "display.h"
#include "machine.h"
#include "screen.h"
#include "utils.h"

int swapmode(int *retavail, int *retfree);
static int namelength;
static int cmdlength;
static int show_fullcmd;

int n_cpus = 0;

/* get_process_info passes back a handle.  This is what it looks like: */

struct handle {
	struct kinfo_proc **next_proc;	/* points to next valid proc pointer */
	int remaining;		/* number of pointers remaining */
};

/* declarations for load_avg */
#include "loadavg.h"

#define PP(pp, field) ((pp)->kp_ ## field)
#define LP(pp, field) ((pp)->kp_lwp.kl_ ## field)
#define VP(pp, field) ((pp)->kp_vm_ ## field)

/* what we consider to be process size: */
#define PROCSIZE(pp) (VP((pp), map_size) / 1024)

/*
 * These definitions control the format of the per-process area
 */

static char smp_header[] =
"   PID %-*.*s NICE  SIZE    RES    STATE CPU  TIME   CTIME    CPU COMMAND";

#define smp_Proc_format \
	"%6d %-*.*s %3d%7s %6s %8.8s %2d %6s %7s %5.2f%% %.*s"

/* process state names for the "STATE" column of the display */
/*
 * the extra nulls in the string "run" are for adding a slash and the
 * processor number when needed
 */

const char *state_abbrev[] = {
	"", "RUN\0\0\0", "STOP", "SLEEP",
};


static kvm_t *kd;

/* values that we stash away in _init and use in later routines */

static long lastpid;

/* these are for calculating cpu state percentages */

static struct kinfo_cputime *cp_time, *cp_old;

/* these are for detailing the process states */

#define MAXPSTATES	6

int process_states[MAXPSTATES];

char *procstatenames[] = {
	" running, ", " idle, ", " active, ", " stopped, ", " zombie, ",
	NULL
};

/* these are for detailing the cpu states */
#define CPU_STATES 5
int *cpu_states;
int* cpu_averages;
char *cpustatenames[CPU_STATES + 1] = {
	"user", "nice", "system", "interrupt", "idle", NULL
};

/* these are for detailing the memory statistics */

long memory_stats[7];
char *memorynames[] = {
	"K Active, ", "K Inact, ", "K Wired, ", "K Cache, ", "K Buf, ", "K Free",
	NULL
};

long swap_stats[7];
char *swapnames[] = {
	/* 0           1            2           3            4       5 */
	"K Total, ", "K Used, ", "K Free, ", "% Inuse, ", "K In, ", "K Out",
	NULL
};


/* these are for keeping track of the proc array */

static int nproc;
static int onproc = -1;
static int pref_len;
static struct kinfo_proc *pbase;
static struct kinfo_proc **pref;

/* these are for getting the memory statistics */

static int pageshift;		/* log base 2 of the pagesize */

/* define pagetok in terms of pageshift */

#define pagetok(size) ((size) << pageshift)

/* sorting orders. first is default */
char *ordernames[] = {
  "cpu", "size", "res", "time", "pri", "thr", "pid", "ctime",  "pres", NULL
};

/* compare routines */
int proc_compare (struct kinfo_proc **, struct kinfo_proc **);
int compare_size (struct kinfo_proc **, struct kinfo_proc **);
int compare_res (struct kinfo_proc **, struct kinfo_proc **);
int compare_time (struct kinfo_proc **, struct kinfo_proc **);
int compare_ctime (struct kinfo_proc **, struct kinfo_proc **);
int compare_prio(struct kinfo_proc **, struct kinfo_proc **);
int compare_thr (struct kinfo_proc **, struct kinfo_proc **);
int compare_pid (struct kinfo_proc **, struct kinfo_proc **);
int compare_pres(struct kinfo_proc **, struct kinfo_proc **);

int (*proc_compares[]) (struct kinfo_proc **,struct kinfo_proc **) = {
	proc_compare,
	compare_size,
	compare_res,
	compare_time,
	compare_prio,
	compare_thr,
	compare_pid,
	compare_ctime,
	compare_pres,
	NULL
};

static void
cputime_percentages(int out[CPU_STATES], struct kinfo_cputime *new,
    struct kinfo_cputime *old)
{
	struct kinfo_cputime diffs;
	uint64_t total_change, half_total;

	/* initialization */
	total_change = 0;

	diffs.cp_user = new->cp_user - old->cp_user;
	diffs.cp_nice = new->cp_nice - old->cp_nice;
	diffs.cp_sys = new->cp_sys - old->cp_sys;
	diffs.cp_intr = new->cp_intr - old->cp_intr;
	diffs.cp_idle = new->cp_idle - old->cp_idle;
	total_change = diffs.cp_user + diffs.cp_nice + diffs.cp_sys +
	    diffs.cp_intr + diffs.cp_idle;
	old->cp_user = new->cp_user;
	old->cp_nice = new->cp_nice;
	old->cp_sys = new->cp_sys;
	old->cp_intr = new->cp_intr;
	old->cp_idle = new->cp_idle;

	/* avoid divide by zero potential */
	if (total_change == 0)
		total_change = 1;

	/* calculate percentages based on overall change, rounding up */
	half_total = total_change >> 1;

	out[0] = ((diffs.cp_user * 1000LL + half_total) / total_change);
	out[1] = ((diffs.cp_nice * 1000LL + half_total) / total_change);
	out[2] = ((diffs.cp_sys * 1000LL + half_total) / total_change);
	out[3] = ((diffs.cp_intr * 1000LL + half_total) / total_change);
	out[4] = ((diffs.cp_idle * 1000LL + half_total) / total_change);
}

int
machine_init(struct statics *statics)
{
	int pagesize;
	size_t modelen;
	struct passwd *pw;
	struct timeval boottime;

	if (n_cpus < 1) {
		if (kinfo_get_cpus(&n_cpus))
			err(1, "kinfo_get_cpus failed");
	}
	/* get boot time */
	modelen = sizeof(boottime);
	if (sysctlbyname("kern.boottime", &boottime, &modelen, NULL, 0) == -1) {
		/* we have no boottime to report */
		boottime.tv_sec = -1;
	}

	while ((pw = getpwent()) != NULL) {
		if ((int)strlen(pw->pw_name) > namelength)
			namelength = strlen(pw->pw_name);
	}
	if (namelength < 8)
		namelength = 8;
	if (namelength > 13)
		namelength = 13;

	if ((kd = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL)) == NULL)
		return -1;

	pbase = NULL;
	pref = NULL;
	nproc = 0;
	onproc = -1;
	/*
	 * get the page size with "getpagesize" and calculate pageshift from
	 * it
	 */
	pagesize = getpagesize();
	pageshift = 0;
	while (pagesize > 1) {
		pageshift++;
		pagesize >>= 1;
	}

	/* we only need the amount of log(2)1024 for our conversion */
	pageshift -= LOG1024;

	/* fill in the statics information */
	statics->procstate_names = procstatenames;
	statics->cpustate_names = cpustatenames;
	statics->memory_names = memorynames;
	statics->boottime = boottime.tv_sec;
	statics->swap_names = swapnames;
	statics->order_names = ordernames;
	/* we need kvm descriptor in order to show full commands */
	statics->flags.fullcmds = kd != NULL;

	/* all done! */
	return (0);
}

char *
format_header(char *uname_field)
{
	static char Header[128];

	snprintf(Header, sizeof(Header), smp_header,
	    namelength, namelength, uname_field);

	if (screen_width <= 79)
		cmdlength = 80;
	else
		cmdlength = screen_width;

	cmdlength = cmdlength - strlen(Header) + 6;

	return Header;
}

static int swappgsin = -1;
static int swappgsout = -1;
extern struct timeval timeout;

void
get_system_info(struct system_info *si)
{
	size_t len;
	int cpu;

	if (cpu_states == NULL) {
		cpu_states = malloc(sizeof(*cpu_states) * CPU_STATES * n_cpus);
		if (cpu_states == NULL)
			err(1, "malloc");
		bzero(cpu_states, sizeof(*cpu_states) * CPU_STATES * n_cpus);
	}
	if (cp_time == NULL) {
		cp_time = malloc(2 * n_cpus * sizeof(cp_time[0]));
		if (cp_time == NULL)
			err(1, "cp_time");
		cp_old = cp_time + n_cpus;
		len = n_cpus * sizeof(cp_old[0]);
		bzero(cp_time, len);
		if (sysctlbyname("kern.cputime", cp_old, &len, NULL, 0))
			err(1, "kern.cputime");
	}
	len = n_cpus * sizeof(cp_time[0]);
	bzero(cp_time, len);
	if (sysctlbyname("kern.cputime", cp_time, &len, NULL, 0))
		err(1, "kern.cputime");

	getloadavg(si->load_avg, 3);

	lastpid = 0;

	/* convert cp_time counts to percentages */
	int combine_cpus = (enable_ncpus == 0 && n_cpus > 1);
	for (cpu = 0; cpu < n_cpus; ++cpu) {
		cputime_percentages(cpu_states + cpu * CPU_STATES,
		    &cp_time[cpu], &cp_old[cpu]);
	}
	if (combine_cpus) {
		if (cpu_averages == NULL) {
			cpu_averages = malloc(sizeof(*cpu_averages) * CPU_STATES);
			if (cpu_averages == NULL)
				err(1, "cpu_averages");
		}
		bzero(cpu_averages, sizeof(*cpu_averages) * CPU_STATES);
		for (cpu = 0; cpu < n_cpus; ++cpu) {
			int j = 0;
			cpu_averages[0] += *(cpu_states + ((cpu * CPU_STATES) + j++) );
			cpu_averages[1] += *(cpu_states + ((cpu * CPU_STATES) + j++) );
			cpu_averages[2] += *(cpu_states + ((cpu * CPU_STATES) + j++) );
			cpu_averages[3] += *(cpu_states + ((cpu * CPU_STATES) + j++) );
			cpu_averages[4] += *(cpu_states + ((cpu * CPU_STATES) + j++) );
		}
		for (int i = 0; i < CPU_STATES; ++i)
			cpu_averages[i] /= n_cpus;
	}

	/* sum memory & swap statistics */
	{
		struct vmmeter vmm;
		struct vmstats vms;
		size_t vms_size = sizeof(vms);
		size_t vmm_size = sizeof(vmm);
		static unsigned int swap_delay = 0;
		static int swapavail = 0;
		static int swapfree = 0;
		static long bufspace = 0;

		if (sysctlbyname("vm.vmstats", &vms, &vms_size, NULL, 0))
			err(1, "sysctlbyname: vm.vmstats");

		if (sysctlbyname("vm.vmmeter", &vmm, &vmm_size, NULL, 0))
			err(1, "sysctlbyname: vm.vmmeter");

		if (kinfo_get_vfs_bufspace(&bufspace))
			err(1, "kinfo_get_vfs_bufspace");

		/* convert memory stats to Kbytes */
		memory_stats[0] = pagetok(vms.v_active_count);
		memory_stats[1] = pagetok(vms.v_inactive_count);
		memory_stats[2] = pagetok(vms.v_wire_count);
		memory_stats[3] = pagetok(vms.v_cache_count);
		memory_stats[4] = bufspace / 1024;
		memory_stats[5] = pagetok(vms.v_free_count);
		memory_stats[6] = -1;

		/* first interval */
		if (swappgsin < 0) {
			swap_stats[4] = 0;
			swap_stats[5] = 0;
		}
		/* compute differences between old and new swap statistic */
		else {
			swap_stats[4] = pagetok(((vmm.v_swappgsin - swappgsin)));
			swap_stats[5] = pagetok(((vmm.v_swappgsout - swappgsout)));
		}

		swappgsin = vmm.v_swappgsin;
		swappgsout = vmm.v_swappgsout;

		/* call CPU heavy swapmode() only for changes */
		if (swap_stats[4] > 0 || swap_stats[5] > 0 || swap_delay == 0) {
			swap_stats[3] = swapmode(&swapavail, &swapfree);
			swap_stats[0] = swapavail;
			swap_stats[1] = swapavail - swapfree;
			swap_stats[2] = swapfree;
		}
		swap_delay = 1;
		swap_stats[6] = -1;
	}

	/* set arrays and strings */
	si->cpustates = combine_cpus == 1 ?
	    cpu_averages : cpu_states;
	si->memory = memory_stats;
	si->swap = swap_stats;


	if (lastpid > 0) {
		si->last_pid = lastpid;
	} else {
		si->last_pid = -1;
	}
}


static struct handle handle;

caddr_t 
get_process_info(struct system_info *si, struct process_select *sel,
    int compare_index)
{
	int i;
	int total_procs;
	int active_procs;
	struct kinfo_proc **prefp;
	struct kinfo_proc *pp;

	/* these are copied out of sel for speed */
	int show_idle;
	int show_system;
	int show_uid;
	int show_threads;

	show_threads = sel->threads;


	pbase = kvm_getprocs(kd,
	    KERN_PROC_ALL | (show_threads ? KERN_PROC_FLAG_LWP : 0), 0, &nproc);
	if (nproc > onproc)
		pref = (struct kinfo_proc **)realloc(pref, sizeof(struct kinfo_proc *)
		    * (onproc = nproc));
	if (pref == NULL || pbase == NULL) {
		(void)fprintf(stderr, "top: Out of memory.\n");
		quit(23);
	}
	/* get a pointer to the states summary array */
	si->procstates = process_states;

	/* set up flags which define what we are going to select */
	show_idle = sel->idle;
	show_system = sel->system;
	show_uid = sel->uid != -1;
	show_fullcmd = sel->fullcmd;

	/* count up process states and get pointers to interesting procs */
	total_procs = 0;
	active_procs = 0;
	memset((char *)process_states, 0, sizeof(process_states));
	prefp = pref;
	for (pp = pbase, i = 0; i < nproc; pp++, i++) {
		/*
		 * Place pointers to each valid proc structure in pref[].
		 * Process slots that are actually in use have a non-zero
		 * status field.  Processes with P_SYSTEM set are system
		 * processes---these get ignored unless show_sysprocs is set.
		 */
		if ((show_system && (LP(pp, pid) == -1)) ||
		    (show_system || ((PP(pp, flags) & P_SYSTEM) == 0))) {
			int pstate = LP(pp, stat);

			total_procs++;
			if (pstate == LSRUN)
				process_states[0]++;
			if (pstate >= 0 && pstate < MAXPSTATES)
				process_states[pstate]++;
			if ((show_system && (LP(pp, pid) == -1)) ||
			    (show_idle || (LP(pp, pctcpu) != 0) ||
			    (pstate == LSRUN)) &&
			    (!show_uid || PP(pp, ruid) == (uid_t) sel->uid)) {
				*prefp++ = pp;
				active_procs++;
			}
		}
	}

	qsort((char *)pref, active_procs, sizeof(struct kinfo_proc *),
	    (int (*)(const void *, const void *))proc_compares[compare_index]);

	/* remember active and total counts */
	si->p_total = total_procs;
	si->p_active = pref_len = active_procs;

	/* pass back a handle */
	handle.next_proc = pref;
	handle.remaining = active_procs;
	return ((caddr_t) & handle);
}

char fmt[MAX_COLS];		/* static area where result is built */

char *
format_next_process(caddr_t xhandle, char *(*get_userid) (int))
{
	struct kinfo_proc *pp;
	long cputime;
	long ccputime;
	double pct;
	struct handle *hp;
	char status[16];
	int state;
	int xnice;
	char **comm_full;
	char *comm;
	char cputime_fmt[10], ccputime_fmt[10];

	/* find and remember the next proc structure */
	hp = (struct handle *)xhandle;
	pp = *(hp->next_proc++);
	hp->remaining--;

	/* get the process's command name */
	if (show_fullcmd) {
		if ((comm_full = kvm_getargv(kd, pp, 0)) == NULL) {
			return (fmt);
		}
	}
	else {
		comm = PP(pp, comm);
	}
	
	/*
	 * Convert the process's runtime from microseconds to seconds.  This
	 * time includes the interrupt time to be in compliance with ps output.
	*/
	cputime = (LP(pp, uticks) + LP(pp, sticks) + LP(pp, iticks)) / 1000000;
	ccputime = cputime + PP(pp, cru).ru_stime.tv_sec + PP(pp, cru).ru_utime.tv_sec;
	format_time(cputime, cputime_fmt, sizeof(cputime_fmt));
	format_time(ccputime, ccputime_fmt, sizeof(ccputime_fmt));

	/* calculate the base for cpu percentages */
	pct = pctdouble(LP(pp, pctcpu));

	/* generate "STATE" field */
	switch (state = LP(pp, stat)) {
	case LSRUN:
		if (LP(pp, tdflags) & TDF_RUNNING)
			sprintf(status, "CPU%d", LP(pp, cpuid));
		else
			strcpy(status, "RUN");
		break;
	case LSSLEEP:
		if (LP(pp, wmesg) != NULL) {
			sprintf(status, "%.8s", LP(pp, wmesg)); /* WMESGLEN */
			break;
		}
		/* fall through */
	default:

		if (state >= 0 &&
		    (unsigned)state < sizeof(state_abbrev) / sizeof(*state_abbrev))
			sprintf(status, "%.6s", state_abbrev[(unsigned char)state]);
		else
			sprintf(status, "?%5d", state);
		break;
	}

	if (PP(pp, stat) == SZOMB)
		strcpy(status, "ZOMB");

	/*
	 * idle time 0 - 31 -> nice value +21 - +52 normal time      -> nice
	 * value -20 - +20 real time 0 - 31 -> nice value -52 - -21 thread
	 * 0 - 31 -> nice value -53 -
	 */
	switch (LP(pp, rtprio.type)) {
	case RTP_PRIO_REALTIME:
		xnice = PRIO_MIN - 1 - RTP_PRIO_MAX + LP(pp, rtprio.prio);
		break;
	case RTP_PRIO_IDLE:
		xnice = PRIO_MAX + 1 + LP(pp, rtprio.prio);
		break;
	case RTP_PRIO_THREAD:
		xnice = PRIO_MIN - 1 - RTP_PRIO_MAX - LP(pp, rtprio.prio);
		break;
	default:
		xnice = PP(pp, nice);
		break;
	}

	/* format this entry */
	snprintf(fmt, sizeof(fmt),
	    smp_Proc_format,
	    (int)PP(pp, pid),
	    namelength, namelength,
	    get_userid(PP(pp, ruid)),
	    (int)xnice,
	    format_k(PROCSIZE(pp)),
	    format_k(pagetok(VP(pp, rssize))),
	    status,
	    LP(pp, cpuid),
	    cputime_fmt,
	    ccputime_fmt,
	    100.0 * pct,
	    cmdlength,
	    show_fullcmd ? *comm_full : comm);

	/* return the result */
	return (fmt);
}

/* comparison routines for qsort */

/*
 *  proc_compare - comparison function for "qsort"
 *	Compares the resource consumption of two processes using five
 *  	distinct keys.  The keys (in descending order of importance) are:
 *  	percent cpu, cpu ticks, state, resident set size, total virtual
 *  	memory usage.  The process states are ordered as follows (from least
 *  	to most important):  WAIT, zombie, sleep, stop, start, run.  The
 *  	array declaration below maps a process state index into a number
 *  	that reflects this ordering.
 */

static unsigned char sorted_state[] =
{
	0,			/* not used		 */
	3,			/* sleep		 */
	1,			/* ABANDONED (WAIT)	 */
	6,			/* run			 */
	5,			/* start		 */
	2,			/* zombie		 */
	4			/* stop			 */
};


#define ORDERKEY_PCTCPU \
  if (lresult = (long) LP(p2, pctcpu) - (long) LP(p1, pctcpu), \
     (result = lresult > 0 ? 1 : lresult < 0 ? -1 : 0) == 0)

#define CPTICKS(p)	(LP(p, uticks) + LP(p, sticks) + LP(p, iticks))

#define ORDERKEY_CPTICKS \
  if ((result = CPTICKS(p2) > CPTICKS(p1) ? 1 : \
		CPTICKS(p2) < CPTICKS(p1) ? -1 : 0) == 0)

#define CTIME(p)	(((LP(p, uticks) + LP(p, sticks) + LP(p, iticks))/1000000) + \
  PP(p, cru).ru_stime.tv_sec + PP(p, cru).ru_utime.tv_sec)

#define ORDERKEY_CTIME \
   if ((result = CTIME(p2) > CTIME(p1) ? 1 : \
		CTIME(p2) < CTIME(p1) ? -1 : 0) == 0)

#define ORDERKEY_STATE \
  if ((result = sorted_state[(unsigned char) PP(p2, stat)] - \
                sorted_state[(unsigned char) PP(p1, stat)]) == 0)

#define ORDERKEY_PRIO \
  if ((result = LP(p2, prio) - LP(p1, prio)) == 0)

#define ORDERKEY_KTHREADS \
  if ((result = (LP(p1, pid) == 0) - (LP(p2, pid) == 0)) == 0)

#define ORDERKEY_KTHREADS_PRIO \
  if ((result = LP(p2, tdprio) - LP(p1, tdprio)) == 0)

#define ORDERKEY_RSSIZE \
  if ((result = VP(p2, rssize) - VP(p1, rssize)) == 0)

#define ORDERKEY_MEM \
  if ( (result = PROCSIZE(p2) - PROCSIZE(p1)) == 0 )

#define ORDERKEY_PID \
  if ( (result = PP(p1, pid) - PP(p2, pid)) == 0)

#define ORDERKEY_PRSSIZE \
  if((result = VP(p2, prssize) - VP(p1, prssize)) == 0)

/* compare_cpu - the comparison function for sorting by cpu percentage */

int
proc_compare(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_PCTCPU
	ORDERKEY_CPTICKS
	ORDERKEY_STATE
	ORDERKEY_PRIO
	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	{} 
	
	return (result);
}

/* compare_size - the comparison function for sorting by total memory usage */

int
compare_size(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_MEM
	ORDERKEY_RSSIZE
	ORDERKEY_PCTCPU
	ORDERKEY_CPTICKS
	ORDERKEY_STATE
	ORDERKEY_PRIO
	{}

	return (result);
}

/* compare_res - the comparison function for sorting by resident set size */

int
compare_res(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	ORDERKEY_PCTCPU
	ORDERKEY_CPTICKS
	ORDERKEY_STATE
	ORDERKEY_PRIO
	{}

	return (result);
}

/* compare_pres - the comparison function for sorting by proportional resident set size */

int
compare_pres(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_PRSSIZE
	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	ORDERKEY_PCTCPU
	ORDERKEY_CPTICKS
	ORDERKEY_STATE
	ORDERKEY_PRIO
	{}

	return (result);
}

/* compare_time - the comparison function for sorting by total cpu time */

int
compare_time(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_CPTICKS
	ORDERKEY_PCTCPU
	ORDERKEY_KTHREADS
	ORDERKEY_KTHREADS_PRIO
	ORDERKEY_STATE
	ORDERKEY_PRIO
	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	{}

	return (result);
}

int
compare_ctime(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;
	
	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;
	
	ORDERKEY_CTIME
	ORDERKEY_PCTCPU
	ORDERKEY_KTHREADS
	ORDERKEY_KTHREADS_PRIO
	ORDERKEY_STATE
	ORDERKEY_PRIO
	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	{}
	
	return (result);
}

/* compare_prio - the comparison function for sorting by cpu percentage */

int
compare_prio(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_KTHREADS
	ORDERKEY_KTHREADS_PRIO
	ORDERKEY_PRIO
	ORDERKEY_CPTICKS
	ORDERKEY_PCTCPU
	ORDERKEY_STATE
	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	{}

	return (result);
}

int
compare_thr(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;
	pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **)pp1;
	p2 = *(struct kinfo_proc **)pp2;

	ORDERKEY_KTHREADS
	ORDERKEY_KTHREADS_PRIO
	ORDERKEY_CPTICKS
	ORDERKEY_PCTCPU
	ORDERKEY_STATE
	ORDERKEY_RSSIZE
	ORDERKEY_MEM
	{}

	return (result);
}

/* compare_pid - the comparison function for sorting by process id */

int
compare_pid(struct kinfo_proc **pp1, struct kinfo_proc **pp2)
{
	struct kinfo_proc *p1;
	struct kinfo_proc *p2;
	int result;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;
	
	ORDERKEY_PID
	;
	
	return(result);
}

/*
 * proc_owner(pid) - returns the uid that owns process "pid", or -1 if
 *		the process does not exist.
 *		It is EXTREMLY IMPORTANT that this function work correctly.
 *		If top runs setuid root (as in SVR4), then this function
 *		is the only thing that stands in the way of a serious
 *		security problem.  It validates requests for the "kill"
 *		and "renice" commands.
 */

int
proc_owner(int pid)
{
	int xcnt;
	struct kinfo_proc **prefp;
	struct kinfo_proc *pp;

	prefp = pref;
	xcnt = pref_len;
	while (--xcnt >= 0) {
		pp = *prefp++;
		if (PP(pp, pid) == (pid_t) pid) {
			return ((int)PP(pp, ruid));
		}
	}
	return (-1);
}


/*
 * swapmode is based on a program called swapinfo written
 * by Kevin Lahey <kml@rokkaku.atl.ga.us>.
 */
int
swapmode(int *retavail, int *retfree)
{
	int n;
	int pagesize = getpagesize();
	struct kvm_swap swapary[1];

	*retavail = 0;
	*retfree = 0;

#define CONVERT(v)	((quad_t)(v) * pagesize / 1024)

	n = kvm_getswapinfo(kd, swapary, 1, 0);
	if (n < 0 || swapary[0].ksw_total == 0)
		return (0);

	*retavail = CONVERT(swapary[0].ksw_total);
	*retfree = CONVERT(swapary[0].ksw_total - swapary[0].ksw_used);

	n = (int)((double)swapary[0].ksw_used * 100.0 /
	    (double)swapary[0].ksw_total);
	return (n);
}
