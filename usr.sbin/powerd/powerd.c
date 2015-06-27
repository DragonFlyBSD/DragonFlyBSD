/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The powerd daemon monitors the cpu load and adjusts cpu frequencies
 * via hw.acpi.cpu.px_dom*.
 */

#define _KERNEL_STRUCTURES
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/kinfo.h>
#include <sys/file.h>
#include <sys/queue.h>
#include <sys/soundcard.h>
#include <sys/time.h>
#include <machine/cpufunc.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "alert1.h"

#define MAXDOM		MAXCPU	/* worst case, 1 cpu per domain */

#define MAXFREQ		64

struct cpu_pwrdom {
	TAILQ_ENTRY(cpu_pwrdom)	dom_link;
	int			dom_id;
	int			dom_ncpus;
	cpumask_t		dom_cpumask;
};
TAILQ_HEAD(cpu_pwrdom_list, cpu_pwrdom);

static void usage(void);
static double getcputime(double);
static void acpi_setcpufreq(int nstate);
static int setupdominfo(void);
static int has_battery(void);
static int mon_battery(void);
static void getncpus(void);

static struct cpu_pwrdom_list CpuPwrDomain;
static struct cpu_pwrdom *CpuPwrDomLimit;
static struct cpu_pwrdom CpuPwrDomLast;
static int NCpuPwrDomUsed;

static int TotalCpus;
int DebugOpt;
int TurboOpt = 1;
int CpuLimit;		/* # of cpus at max frequency */
int PowerFd;
int NCpus;
int CpuCount[MAXDOM];	/* # of cpus in any given domain */
int Hysteresis = 10;	/* percentage */
double TriggerUp = 0.25;/* single-cpu load to force max freq */
double TriggerDown; /* load per cpu to force the min freq */
static int BatLifeMin = 2; /* shutdown the box, if low on battery life */
static struct timespec BatLifePrevT;
static int BatLifePollIntvl = 5; /* unit: sec */

static struct timespec BatShutdownStartT;
static int BatShutdownLinger = -1;
static int BatShutdownLingerSet = 60; /* unit: sec */
static int BatShutdownLingerCnt;
static int BatShutdownAudioAlert = 1;

static void sigintr(int signo);

int
main(int ac, char **av)
{
	double qavg;
	double uavg;	/* uavg - used for speeding up */
	double davg;	/* davg - used for slowing down */
	double srt;
	double pollrate;
	int ch;
	int ustate;
	int dstate;
	int nstate;
	char buf[64];
	int monbat;

	srt = 8.0;	/* time for samples - 8 seconds */
	pollrate = 1.0;	/* polling rate in seconds */

	while ((ch = getopt(ac, av, "dp:r:tu:B:L:P:QT:")) != -1) {
		switch(ch) {
		case 'd':
			DebugOpt = 1;
			break;
		case 'p':
			Hysteresis = (int)strtol(optarg, NULL, 10);
			break;
		case 'r':
			pollrate = strtod(optarg, NULL);
			break;
		case 't':
			TurboOpt = 0;
			break;
		case 'u':
			TriggerUp = (double)strtol(optarg, NULL, 10) / 100;
			break;
		case 'B':
			BatLifeMin = strtol(optarg, NULL, 10);
			break;
		case 'L':
			BatShutdownLingerSet = strtol(optarg, NULL, 10);
			if (BatShutdownLingerSet < 0)
				BatShutdownLingerSet = 0;
			break;
		case 'P':
			BatLifePollIntvl = strtol(optarg, NULL, 10);
			break;
		case 'Q':
			BatShutdownAudioAlert = 0;
			break;
		case 'T':
			srt = strtod(optarg, NULL);
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	ac -= optind;
	av += optind;

	/* Get the number of cpus */
	getncpus();

	if (0 > Hysteresis || Hysteresis > 99) {
		fprintf(stderr, "Invalid hysteresis value\n");
		exit(1);
	}

	if (0 > TriggerUp || TriggerUp > 1) {
		fprintf(stderr, "Invalid load limit value\n");
		exit(1);
	}

	TriggerDown = TriggerUp - (TriggerUp * (double) Hysteresis / 100);

	/*
	 * Make sure powerd is not already running.
	 */
	PowerFd = open("/var/run/powerd.pid", O_CREAT|O_RDWR, 0644);
	if (PowerFd < 0) {
		fprintf(stderr,
			"Cannot create /var/run/powerd.pid, "
			"continuing anyway\n");
	} else {
		if (flock(PowerFd, LOCK_EX|LOCK_NB) < 0) {
			fprintf(stderr, "powerd is already running\n");
			exit(1);
		}
	}

	/*
	 * Demonize and set pid
	 */
	if (DebugOpt == 0) {
		daemon(0, 0);
		openlog("powerd", LOG_CONS | LOG_PID, LOG_DAEMON);
	}

	if (PowerFd >= 0) {
		ftruncate(PowerFd, 0);
		snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
		write(PowerFd, buf, strlen(buf));
	}

	/* Do we need to monitor battery life? */
	if (BatLifePollIntvl <= 0)
		monbat = 0;
	else
		monbat = has_battery();

	/*
	 * Wait hw.acpi.cpu.px_dom* sysctl to be created by kernel
	 *
	 * Since hw.acpi.cpu.px_dom* creation is queued into ACPI
	 * taskqueue and ACPI taskqueue is shared across various
	 * ACPI modules, any delay in other modules may cause
	 * hw.acpi.cpu.px_dom* to be created at quite a later time
	 * (e.g. cmbat module's task could take quite a lot of time).
	 */
	for (;;) {
		/*
		 * Prime delta cputime calculation, make sure at least
		 * dom0 exists.
		 */
		getcputime(pollrate);
		if (setupdominfo())
			break;
		usleep((int)(pollrate * 1000000.0));
	}

	/*
	 * Assume everything are used and are maxed out, before we
	 * start.
	 */
	CpuPwrDomLimit = &CpuPwrDomLast;
	CpuLimit = NCpus;

	/*
	 * Set to maximum performance if killed.
	 */
	signal(SIGINT, sigintr);
	signal(SIGTERM, sigintr);
	uavg = 0.0;
	davg = 0.0;

	srt = srt / pollrate;	/* convert to sample count */

	if (DebugOpt)
		printf("samples for downgrading: %5.2f\n", srt);

	/*
	 * Monitoring loop
	 *
	 * Calculate nstate, the number of cpus we wish to run at max
	 * frequency.  All remaining cpus will be set to their lowest
	 * frequency and mapped out of the user process scheduler.
	 */
	for (;;) {
		qavg = getcputime(pollrate);
		uavg = (uavg * 2.0 + qavg) / 3.0;	/* speeding up */
		davg = (davg * srt + qavg) / (srt + 1);	/* slowing down */
		if (davg < uavg)
			davg = uavg;

		ustate = uavg / TriggerUp;
		if (ustate < CpuLimit)
			ustate = uavg / TriggerDown;
		dstate = davg / TriggerUp;
		if (dstate < CpuLimit)
			dstate = davg / TriggerDown;

		nstate = (ustate > dstate) ? ustate : dstate;
		if (nstate > NCpus)
			nstate = NCpus;

		if (DebugOpt) {
			printf("\rqavg=%5.2f uavg=%5.2f davg=%5.2f "
			       "%2d/%2d ncpus=%d\r",
				qavg, uavg, davg,
				CpuLimit, NCpuPwrDomUsed, nstate);
			fflush(stdout);
		}
		if (nstate != CpuLimit)
			acpi_setcpufreq(nstate);
		if (monbat)
			monbat = mon_battery();
		usleep((int)(pollrate * 1000000.0));
	}
}

static
void
sigintr(int signo __unused)
{
	syslog(LOG_INFO, "killed, setting max and exiting");
	acpi_setcpufreq(NCpus);
	exit(1);
}

/*
 * Figure out the domains and calculate the CpuCount[] array.
 */
static int
setupdominfo(void)
{
	struct cpu_pwrdom *dom;
	struct cpu_pwrdom_list tmp_list;
	char buf[64];
	char members[1024];
	char *str;
	size_t msize;
	int n, i;

	TAILQ_INIT(&CpuPwrDomain);
	NCpuPwrDomUsed = 0;
	NCpus = 0;

	TAILQ_INIT(&tmp_list);
	for (i = 0; i < MAXDOM; ++i) {
		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.available", i);
		if (sysctlbyname(buf, NULL, NULL, NULL, 0) < 0)
			continue;

		dom = calloc(1, sizeof(*dom));
		dom->dom_id = i;
		TAILQ_INSERT_TAIL(&tmp_list, dom, dom_link);
	}

	while ((dom = TAILQ_FIRST(&tmp_list)) != NULL) {
		int bsp_domain = 0;

		TAILQ_REMOVE(&tmp_list, dom, dom_link);
		CPUMASK_ASSZERO(dom->dom_cpumask);

		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.members", dom->dom_id);
		msize = sizeof(members);
		if (sysctlbyname(buf, members, &msize, NULL, 0) < 0) {
			free(dom);
			continue;
		}

		members[msize] = 0;
		for (str = strtok(members, " "); str; str = strtok(NULL, " ")) {
			n = -1;
			sscanf(str, "cpu%d", &n);
			if (n >= 0) {
				++NCpus;
				++dom->dom_ncpus;
				if (n == 0)
					bsp_domain = 1;
				CPUMASK_ORBIT(dom->dom_cpumask, n);
			}
		}
		if (dom->dom_ncpus == 0) {
			free(dom);
			continue;
		}
		if (DebugOpt) {
			printf("dom%d cpumask: ", dom->dom_id);
			for (i = 0; i < (int)NELEM(dom->dom_cpumask.ary); ++i) {
				printf("%jx ",
				    (uintmax_t)dom->dom_cpumask.ary[i]);
			}
			printf("\n");
			fflush(stdout);
		}

		if (bsp_domain) {
			/*
			 * Use the power domain containing the BSP as the first
			 * power domain.  So if all CPUs are idle, we could
			 * leave BSP to the usched without too much trouble.
			 */
			TAILQ_INSERT_HEAD(&CpuPwrDomain, dom, dom_link);
		} else {
			TAILQ_INSERT_TAIL(&CpuPwrDomain, dom, dom_link);
		}
		++NCpuPwrDomUsed;
	}

	if (NCpus != TotalCpus) {
		while ((dom = TAILQ_FIRST(&CpuPwrDomain)) != NULL) {
			TAILQ_REMOVE(&CpuPwrDomain, dom, dom_link);
			free(dom);
		}
		if (DebugOpt) {
			printf("Found %d cpus, expecting %d\n",
			    NCpus, TotalCpus);
			fflush(stdout);
		}
		return 0;
	}

	/* Install sentinel */
	CpuPwrDomLast.dom_id = -1;
	TAILQ_INSERT_TAIL(&CpuPwrDomain, &CpuPwrDomLast, dom_link);

	return 1;
}

/*
 * Return the one-second cpu load.  One cpu at 100% will return a value
 * of 1.0.  On a SMP system N cpus running at 100% will return a value of N.
 */
static
double
getcputime(double pollrate)
{
	static struct kinfo_cputime ocpu_time[MAXCPU];
	static struct kinfo_cputime ncpu_time[MAXCPU];
	size_t slen;
	int ncpu;
	int cpu;
	uint64_t delta;

	/* NOTE: Don't use NCpus here; it may not be initialized yet */
	bcopy(ncpu_time, ocpu_time, sizeof(struct kinfo_cputime) * TotalCpus);

	slen = sizeof(ncpu_time);
	if (sysctlbyname("kern.cputime", &ncpu_time, &slen, NULL, 0) < 0) {
		fprintf(stderr, "kern.cputime sysctl not available\n");
		exit(1);
	}
	ncpu = slen / sizeof(ncpu_time[0]);

	delta = 0;
	for (cpu = 0; cpu < ncpu; ++cpu) {
		delta += (ncpu_time[cpu].cp_user + ncpu_time[cpu].cp_sys +
			  ncpu_time[cpu].cp_nice + ncpu_time[cpu].cp_intr) -
			 (ocpu_time[cpu].cp_user + ocpu_time[cpu].cp_sys +
			  ocpu_time[cpu].cp_nice + ocpu_time[cpu].cp_intr);
	}
	return((double)delta / (pollrate * 1000000.0));
}

static void
acpi_getcpufreq_str(int dom_id, int *highest0, int *lowest0)
{
	char buf[256], sysid[64];
	size_t buflen;
	char *ptr;
	int v, highest, lowest;

	/*
	 * Retrieve availability list
	 */
	snprintf(sysid, sizeof(sysid), "hw.acpi.cpu.px_dom%d.available",
	    dom_id);
	buflen = sizeof(buf) - 1;
	if (sysctlbyname(sysid, buf, &buflen, NULL, 0) < 0)
		return;
	buf[buflen] = 0;

	/*
	 * Parse out the highest and lowest cpu frequencies
	 */
	ptr = buf;
	highest = lowest = 0;
	while (ptr && (v = strtol(ptr, &ptr, 10)) > 0) {
		if (lowest == 0 || lowest > v)
			lowest = v;
		if (highest == 0 || highest < v)
			highest = v;
		/* 
		 * Detect turbo mode
		 */
		if (!TurboOpt && highest - v == 1)
			highest = v;
	}

	*highest0 = highest;
	*lowest0 = lowest;
}

static int
acpi_getcpufreq_bin(int dom_id, int *highest0, int *lowest0)
{
	char sysid[64];
	int freq[MAXFREQ];
	size_t freqlen;
	int freqcnt;

	/*
	 * Retrieve availability list
	 */
	snprintf(sysid, sizeof(sysid), "hw.acpi.cpu.px_dom%d.available_bin",
	    dom_id);
	freqlen = sizeof(freq);
	if (sysctlbyname(sysid, freq, &freqlen, NULL, 0) < 0)
		return 0;

	freqcnt = freqlen / sizeof(freq[0]);
	if (freqcnt == 0)
		return 0;

	*lowest0 = freq[freqcnt - 1];

	*highest0 = freq[0];
	if (!TurboOpt && freqcnt > 1 && freq[0] - freq[1] == 1)
		*highest0 = freq[1];
	return 1;
}

static void
acpi_getcpufreq(int dom_id, int *highest, int *lowest)
{
	*highest = 0;
	*lowest = 0;

	if (acpi_getcpufreq_bin(dom_id, highest, lowest))
		return;
	acpi_getcpufreq_str(dom_id, highest, lowest);
}

/*
 * nstate is the requested number of cpus that we wish to run at full
 * frequency.  We calculate how many domains we have to adjust to reach
 * this goal.
 *
 * This function also sets the user scheduler global cpu mask.
 */
static void
acpi_setcpufreq(int nstate)
{
	int ncpus = 0;
	int increasing = (nstate > CpuLimit);
	struct cpu_pwrdom *dom, *domBeg, *domEnd;
	int lowest;
	int highest;
	int desired;
	char sysid[64];
	cpumask_t global_cpumask;

	/*
	 * Calculate the ending domain if the number of operating cpus
	 * has increased.
	 *
	 * Calculate the starting domain if the number of operating cpus
	 * has decreased.
	 *
	 * Calculate the mask of cpus the userland scheduler is allowed
	 * to use.
	 */
	NCpuPwrDomUsed = 0;
	CPUMASK_ASSZERO(global_cpumask);
	for (dom = TAILQ_FIRST(&CpuPwrDomain); dom != &CpuPwrDomLast;
	     dom = TAILQ_NEXT(dom, dom_link)) {
		cpumask_t mask;

		if (ncpus >= nstate)
			break;
		ncpus += dom->dom_ncpus;
		++NCpuPwrDomUsed;

		mask = dom->dom_cpumask;
		if (ncpus > nstate) {
			int i, diff;

			diff = ncpus - nstate;
			for (i = 0; i < diff; ++i) {
				int c;

				c = BSRCPUMASK(mask);
				CPUMASK_NANDBIT(mask, c);
			}
		}
		CPUMASK_ORMASK(global_cpumask, mask);
	}

	/*
	 * Make sure that userland scheduler has at least one cpu.
	 */
	if (CPUMASK_TESTZERO(global_cpumask))
		CPUMASK_ORBIT(global_cpumask, 0);
	if (DebugOpt) {
		int i;

		printf("\nusched cpumask: ");
		for (i = 0; i < (int)NELEM(global_cpumask.ary); ++i)
			printf("%jx ", (uintmax_t)global_cpumask.ary[i]);
		printf("\n");
		fflush(stdout);
	}

	syslog(LOG_INFO, "using %d cpus", nstate);

	/*
	 * Set the mask of cpus the userland scheduler is allowed to use.
	 */
	sysctlbyname("kern.usched_global_cpumask", NULL, 0,
		     &global_cpumask, sizeof(global_cpumask));

	if (increasing) {
		domBeg = CpuPwrDomLimit;
		domEnd = dom;
	} else {
		domBeg = dom;
		domEnd = CpuPwrDomLimit;
	}
	CpuPwrDomLimit = dom;
	CpuLimit = nstate;

	/*
	 * Adjust the cpu frequency
	 */
	for (dom = domBeg; dom != domEnd; dom = TAILQ_NEXT(dom, dom_link)) {
		acpi_getcpufreq(dom->dom_id, &highest, &lowest);
		if (highest == 0 || lowest == 0)
			continue;

		/*
		 * Calculate the desired cpu frequency, test, and set.
		 */
		desired = increasing ? highest : lowest;

		snprintf(sysid, sizeof(sysid), "hw.acpi.cpu.px_dom%d.select",
		    dom->dom_id);
		if (DebugOpt) {
			printf("dom%d set frequency %d\n",
			       dom->dom_id, desired);
		}
		sysctlbyname(sysid, NULL, NULL, &desired, sizeof(desired));
	}
}

static
void
usage(void)
{
	fprintf(stderr, "usage: powerd [-dt] [-p hysteresis] "
	    "[-u trigger_up] [-T sample_interval] [-r poll_interval] "
	    "[-B min_battery_life] [-L low_battery_linger] "
	    "[-P battery_poll_interval] [-Q]\n");
	exit(1);
}

#ifndef timespecsub
#define timespecsub(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)
#endif

#define BAT_SYSCTL_TIME_MAX	50000000 /* unit: nanosecond */

static int
has_battery(void)
{
	struct timespec s, e;
	size_t len;
	int val;

	clock_gettime(CLOCK_MONOTONIC_FAST, &s);
	BatLifePrevT = s;

	len = sizeof(val);
	if (sysctlbyname("hw.acpi.acline", &val, &len, NULL, 0) < 0) {
		/* No AC line information */
		return 0;
	}
	clock_gettime(CLOCK_MONOTONIC_FAST, &e);

	timespecsub(&e, &s);
	if (e.tv_sec > 0 || e.tv_nsec > BAT_SYSCTL_TIME_MAX) {
		/* hw.acpi.acline takes to long to be useful */
		syslog(LOG_NOTICE, "hw.acpi.acline takes too long");
		return 0;
	}

	clock_gettime(CLOCK_MONOTONIC_FAST, &s);
	len = sizeof(val);
	if (sysctlbyname("hw.acpi.battery.life", &val, &len, NULL, 0) < 0) {
		/* No battery life */
		return 0;
	}
	clock_gettime(CLOCK_MONOTONIC_FAST, &e);

	timespecsub(&e, &s);
	if (e.tv_sec > 0 || e.tv_nsec > BAT_SYSCTL_TIME_MAX) {
		/* hw.acpi.battery.life takes to long to be useful */
		syslog(LOG_NOTICE, "hw.acpi.battery.life takes too long");
		return 0;
	}
	return 1;
}

static void
low_battery_alert(int life)
{
	int fmt, stereo, freq;
	int fd;

	syslog(LOG_ALERT, "low battery life %d%%, please plugin AC line, #%d",
	    life, BatShutdownLingerCnt);
	++BatShutdownLingerCnt;

	if (!BatShutdownAudioAlert)
		return;

	fd = open("/dev/dsp", O_WRONLY);
	if (fd < 0)
		return;

	fmt = AFMT_S16_LE;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &fmt, sizeof(fmt)) < 0)
		goto done;

	stereo = 0;
	if (ioctl(fd, SNDCTL_DSP_STEREO, &stereo, sizeof(stereo)) < 0)
		goto done;

	freq = 44100;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &freq, sizeof(freq)) < 0)
		goto done;

	write(fd, alert1, sizeof(alert1));
	write(fd, alert1, sizeof(alert1));

done:
	close(fd);
}

static int
mon_battery(void)
{
	struct timespec cur, ts;
	int acline, life;
	size_t len;

	clock_gettime(CLOCK_MONOTONIC_FAST, &cur);
	ts = cur;
	timespecsub(&ts, &BatLifePrevT);
	if (ts.tv_sec < BatLifePollIntvl)
		return 1;
	BatLifePrevT = cur;

	len = sizeof(acline);
	if (sysctlbyname("hw.acpi.acline", &acline, &len, NULL, 0) < 0)
		return 1;
	if (acline) {
		BatShutdownLinger = -1;
		BatShutdownLingerCnt = 0;
		return 1;
	}

	len = sizeof(life);
	if (sysctlbyname("hw.acpi.battery.life", &life, &len, NULL, 0) < 0)
		return 1;

	if (BatShutdownLinger > 0) {
		ts = cur;
		timespecsub(&ts, &BatShutdownStartT);
		if (ts.tv_sec > BatShutdownLinger)
			BatShutdownLinger = 0;
	}

	if (life <= BatLifeMin) {
		if (BatShutdownLinger == 0 || BatShutdownLingerSet == 0) {
			syslog(LOG_ALERT, "low battery life %d%%, "
			    "shutting down", life);
			if (vfork() == 0)
				execlp("poweroff", "poweroff", NULL);
			return 0;
		} else if (BatShutdownLinger < 0) {
			BatShutdownLinger = BatShutdownLingerSet;
			BatShutdownStartT = cur;
		}
		low_battery_alert(life);
	}
	return 1;
}

static void
getncpus(void)
{
	size_t slen;

	slen = sizeof(TotalCpus);
	if (sysctlbyname("hw.ncpu", &TotalCpus, &slen, NULL, 0) < 0)
		err(1, "sysctlbyname hw.ncpu failed");
	if (DebugOpt)
		printf("hw.ncpu %d\n", TotalCpus);
}
