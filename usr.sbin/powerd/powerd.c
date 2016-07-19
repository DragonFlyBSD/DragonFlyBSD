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
 * The powerd daemon :
 * - Monitor the cpu load and adjusts cpu and cpu power domain
 *   performance accordingly.
 * - Monitor battery life.  Alarm alerts and shutdown the machine
 *   if battery life goes low.
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
#include <machine/cpumask.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "alert1.h"

#define MAXDOM		MAXCPU	/* worst case, 1 cpu per domain */

#define MAXFREQ		64
#define CST_STRLEN	16

struct cpu_pwrdom {
	TAILQ_ENTRY(cpu_pwrdom)	dom_link;
	int			dom_id;
	int			dom_ncpus;
	cpumask_t		dom_cpumask;
};

struct cpu_state {
	double			cpu_qavg;
	double			cpu_uavg;	/* used for speeding up */
	double			cpu_davg;	/* used for slowing down */
	int			cpu_limit;
	int			cpu_count;
	char			cpu_name[8];
};

static void usage(void);
static void get_ncpus(void);

/* usched cpumask */
static void get_uschedcpus(void);
static void set_uschedcpus(void);

/* perfbias(4) */
static int has_perfbias(void);
static void set_perfbias(int, int);

/* acpi(4) P-state */
static void acpi_getcpufreq_str(int, int *, int *);
static int acpi_getcpufreq_bin(int, int *, int *);
static void acpi_get_cpufreq(int, int *, int *);
static void acpi_set_cpufreq(int, int);
static int acpi_get_cpupwrdom(void);

/* mwait C-state hint */
static int probe_cstate(void);
static void set_cstate(int, int);

/* Performance monitoring */
static void init_perf(void);
static void mon_perf(double);
static void adj_perf(cpumask_t, cpumask_t);
static void adj_cpu_pwrdom(int, int);
static void adj_cpu_perf(int, int);
static void get_cputime(double);
static int get_nstate(struct cpu_state *, double);
static void add_spare_cpus(const cpumask_t, int);
static void restore_perf(void);

/* Battery monitoring */
static int has_battery(void);
static int mon_battery(void);
static void low_battery_alert(int);

/* Backlight */
static void restore_backlight(void);

/* Runtime states for performance monitoring */
static int global_pcpu_limit;
static struct cpu_state pcpu_state[MAXCPU];
static struct cpu_state global_cpu_state;
static cpumask_t cpu_used;		/* cpus w/ high perf */
static cpumask_t cpu_pwrdom_used;	/* cpu power domains w/ high perf */
static cpumask_t usched_cpu_used;	/* cpus for usched */

/* Constants */
static cpumask_t cpu_pwrdom_mask;	/* usable cpu power domains */
static int cpu2pwrdom[MAXCPU];		/* cpu to cpu power domain map */
static struct cpu_pwrdom *cpu_pwrdomain[MAXDOM];
static int NCpus;			/* # of cpus */
static char orig_global_cx[CST_STRLEN];
static char cpu_perf_cx[CST_STRLEN];
static int cpu_perf_cxlen;
static char cpu_idle_cx[CST_STRLEN];
static int cpu_idle_cxlen;

static int DebugOpt;
static int TurboOpt = 1;
static int PowerFd;
static int Hysteresis = 10;	/* percentage */
static double TriggerUp = 0.25;	/* single-cpu load to force max freq */
static double TriggerDown;	/* load per cpu to force the min freq */
static int HasPerfbias = 0;
static int AdjustCpuFreq = 1;
static int AdjustCstate = 0;
static int HighestCpuFreq;
static int LowestCpuFreq;

static volatile int stopped;

/* Battery life monitoring */
static int BatLifeMin = 2;	/* shutdown the box, if low on battery life */
static struct timespec BatLifePrevT;
static int BatLifePollIntvl = 5; /* unit: sec */
static struct timespec BatShutdownStartT;
static int BatShutdownLinger = -1;
static int BatShutdownLingerSet = 60; /* unit: sec */
static int BatShutdownLingerCnt;
static int BatShutdownAudioAlert = 1;
static int BackLightPct = 100;
static int OldBackLightLevel;
static int BackLightDown;

static void sigintr(int signo);

int
main(int ac, char **av)
{
	double srt;
	double pollrate;
	int ch;
	char buf[64];
	int monbat;

	srt = 8.0;	/* time for samples - 8 seconds */
	pollrate = 1.0;	/* polling rate in seconds */

	while ((ch = getopt(ac, av, "b:cdefh:l:p:r:tu:B:L:P:QT:")) != -1) {
		switch(ch) {
		case 'b':
			BackLightPct = strtol(optarg, NULL, 10);
			break;
		case 'c':
			AdjustCstate = 1;
			break;
		case 'd':
			DebugOpt = 1;
			break;
		case 'e':
			HasPerfbias = 1;
			break;
		case 'f':
			AdjustCpuFreq = 0;
			break;
		case 'h':
			HighestCpuFreq = strtol(optarg, NULL, 10);
			break;
		case 'l':
			LowestCpuFreq = strtol(optarg, NULL, 10);
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

	setlinebuf(stdout);

	/* Get number of cpus */
	get_ncpus();

	if (0 > Hysteresis || Hysteresis > 99) {
		fprintf(stderr, "Invalid hysteresis value\n");
		exit(1);
	}

	if (0 > TriggerUp || TriggerUp > 1) {
		fprintf(stderr, "Invalid load limit value\n");
		exit(1);
	}

	if (BackLightPct > 100 || BackLightPct <= 0) {
		fprintf(stderr, "Invalid backlight setting, ignore\n");
		BackLightPct = 100;
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

	/* Do we have perfbias(4)? */
	if (HasPerfbias)
		HasPerfbias = has_perfbias();

	/* Could we adjust C-state? */
	if (AdjustCstate)
		AdjustCstate = probe_cstate();

	/*
	 * Wait hw.acpi.cpu.px_dom* sysctl to be created by kernel.
	 *
	 * Since hw.acpi.cpu.px_dom* creation is queued into ACPI
	 * taskqueue and ACPI taskqueue is shared across various
	 * ACPI modules, any delay in other modules may cause
	 * hw.acpi.cpu.px_dom* to be created at quite a later time
	 * (e.g. cmbat module's task could take quite a lot of time).
	 */
	for (;;) {
		/* Prime delta cputime calculation. */
		get_cputime(pollrate);

		/* Wait for all cpus to appear */
		if (acpi_get_cpupwrdom())
			break;
		usleep((int)(pollrate * 1000000.0));
	}

	/*
	 * Catch some signals so that max performance could be restored.
	 */
	signal(SIGINT, sigintr);
	signal(SIGTERM, sigintr);

	/* Initialize performance states */
	init_perf();

	srt = srt / pollrate;	/* convert to sample count */
	if (DebugOpt)
		printf("samples for downgrading: %5.2f\n", srt);

	/*
	 * Monitoring loop
	 */
	while (!stopped) {
		/*
		 * Monitor performance
		 */
		get_cputime(pollrate);
		mon_perf(srt);

		/*
		 * Monitor battery
		 */
		if (monbat)
			monbat = mon_battery();

		usleep((int)(pollrate * 1000000.0));
	}

	/*
	 * Set to maximum performance if killed.
	 */
	syslog(LOG_INFO, "killed, setting max and exiting");
	restore_perf();
	restore_backlight();

	exit(0);
}

static void
sigintr(int signo __unused)
{
	stopped = 1;
}

/*
 * Figure out the cpu power domains.
 */
static int
acpi_get_cpupwrdom(void)
{
	struct cpu_pwrdom *dom;
	cpumask_t pwrdom_mask;
	char buf[64];
	char members[1024];
	char *str;
	size_t msize;
	int n, i, ncpu = 0, dom_id;

	memset(cpu2pwrdom, 0, sizeof(cpu2pwrdom));
	memset(cpu_pwrdomain, 0, sizeof(cpu_pwrdomain));
	CPUMASK_ASSZERO(cpu_pwrdom_mask);

	for (i = 0; i < MAXDOM; ++i) {
		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.available", i);
		if (sysctlbyname(buf, NULL, NULL, NULL, 0) < 0)
			continue;

		dom = calloc(1, sizeof(*dom));
		dom->dom_id = i;

		if (cpu_pwrdomain[i] != NULL) {
			fprintf(stderr, "cpu power domain %d exists\n", i);
			exit(1);
		}
		cpu_pwrdomain[i] = dom;
		CPUMASK_ORBIT(cpu_pwrdom_mask, i);
	}
	pwrdom_mask = cpu_pwrdom_mask;

	while (CPUMASK_TESTNZERO(pwrdom_mask)) {
		dom_id = BSFCPUMASK(pwrdom_mask);
		CPUMASK_NANDBIT(pwrdom_mask, dom_id);
		dom = cpu_pwrdomain[dom_id];

		CPUMASK_ASSZERO(dom->dom_cpumask);

		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.members", dom->dom_id);
		msize = sizeof(members);
		if (sysctlbyname(buf, members, &msize, NULL, 0) < 0) {
			cpu_pwrdomain[dom_id] = NULL;
			free(dom);
			continue;
		}

		members[msize] = 0;
		for (str = strtok(members, " "); str; str = strtok(NULL, " ")) {
			n = -1;
			sscanf(str, "cpu%d", &n);
			if (n >= 0) {
				++ncpu;
				++dom->dom_ncpus;
				CPUMASK_ORBIT(dom->dom_cpumask, n);
				cpu2pwrdom[n] = dom->dom_id;
			}
		}
		if (dom->dom_ncpus == 0) {
			cpu_pwrdomain[dom_id] = NULL;
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
		}
	}

	if (ncpu != NCpus) {
		if (DebugOpt)
			printf("Found %d cpus, expecting %d\n", ncpu, NCpus);

		pwrdom_mask = cpu_pwrdom_mask;
		while (CPUMASK_TESTNZERO(pwrdom_mask)) {
			dom_id = BSFCPUMASK(pwrdom_mask);
			CPUMASK_NANDBIT(pwrdom_mask, dom_id);
			dom = cpu_pwrdomain[dom_id];
			if (dom != NULL)
				free(dom);
		}
		return 0;
	}
	return 1;
}

/*
 * Save per-cpu load and sum of per-cpu load.
 */
static void
get_cputime(double pollrate)
{
	static struct kinfo_cputime ocpu_time[MAXCPU];
	static struct kinfo_cputime ncpu_time[MAXCPU];
	size_t slen;
	int ncpu;
	int cpu;
	uint64_t delta;

	bcopy(ncpu_time, ocpu_time, sizeof(struct kinfo_cputime) * NCpus);

	slen = sizeof(ncpu_time);
	if (sysctlbyname("kern.cputime", &ncpu_time, &slen, NULL, 0) < 0) {
		fprintf(stderr, "kern.cputime sysctl not available\n");
		exit(1);
	}
	ncpu = slen / sizeof(ncpu_time[0]);

	delta = 0;
	for (cpu = 0; cpu < ncpu; ++cpu) {
		uint64_t d;

		d = (ncpu_time[cpu].cp_user + ncpu_time[cpu].cp_sys +
		     ncpu_time[cpu].cp_nice + ncpu_time[cpu].cp_intr) -
		    (ocpu_time[cpu].cp_user + ocpu_time[cpu].cp_sys +
		     ocpu_time[cpu].cp_nice + ocpu_time[cpu].cp_intr);
		pcpu_state[cpu].cpu_qavg = (double)d / (pollrate * 1000000.0);

		delta += d;
	}
	global_cpu_state.cpu_qavg = (double)delta / (pollrate * 1000000.0);
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
		if ((lowest == 0 || lowest > v) &&
		    (LowestCpuFreq <= 0 || v >= LowestCpuFreq))
			lowest = v;
		if ((highest == 0 || highest < v) &&
		    (HighestCpuFreq <= 0 || v <= HighestCpuFreq))
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
	int freqcnt, i;

	/*
	 * Retrieve availability list
	 */
	snprintf(sysid, sizeof(sysid), "hw.acpi.cpu.px_dom%d.avail", dom_id);
	freqlen = sizeof(freq);
	if (sysctlbyname(sysid, freq, &freqlen, NULL, 0) < 0)
		return 0;

	freqcnt = freqlen / sizeof(freq[0]);
	if (freqcnt == 0)
		return 0;

	for (i = freqcnt - 1; i >= 0; --i) {
		*lowest0 = freq[i];
		if (LowestCpuFreq <= 0 || *lowest0 >= LowestCpuFreq)
			break;
	}

	i = 0;
	*highest0 = freq[0];
	if (!TurboOpt && freqcnt > 1 && freq[0] - freq[1] == 1) {
		i = 1;
		*highest0 = freq[1];
	}
	for (; i < freqcnt; ++i) {
		if (HighestCpuFreq <= 0 || *highest0 <= HighestCpuFreq)
			break;
		*highest0 = freq[i];
	}
	return 1;
}

static void
acpi_get_cpufreq(int dom_id, int *highest, int *lowest)
{
	*highest = 0;
	*lowest = 0;

	if (acpi_getcpufreq_bin(dom_id, highest, lowest))
		return;
	acpi_getcpufreq_str(dom_id, highest, lowest);
}

static
void
usage(void)
{
	fprintf(stderr, "usage: powerd [-cdeftQ] [-p hysteresis] "
	    "[-h highest_freq] [-l lowest_freq] "
	    "[-r poll_interval] [-u trigger_up] "
	    "[-B min_battery_life] [-L low_battery_linger] "
	    "[-P battery_poll_interval] [-T sample_interval] "
	    "[-b backlight]\n");
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
		restore_backlight();
		return 1;
	}

	if (!BackLightDown && BackLightPct != 100) {
		int backlight_max, backlight;

		len = sizeof(backlight_max);
		if (sysctlbyname("hw.backlight_max", &backlight_max, &len,
		    NULL, 0) < 0) {
			/* No more backlight adjustment */
			BackLightPct = 100;
			goto after_backlight;
		}

		len = sizeof(OldBackLightLevel);
		if (sysctlbyname("hw.backlight_level", &OldBackLightLevel, &len,
		    NULL, 0) < 0) {
			/* No more backlight adjustment */
			BackLightPct = 100;
			goto after_backlight;
		}

		backlight = (backlight_max * BackLightPct) / 100;
		if (backlight >= OldBackLightLevel) {
			/* No more backlight adjustment */
			BackLightPct = 100;
			goto after_backlight;
		}

		if (sysctlbyname("hw.backlight_level", NULL, NULL,
		    &backlight, sizeof(backlight)) < 0) {
			/* No more backlight adjustment */
			BackLightPct = 100;
			goto after_backlight;
		}
		BackLightDown = 1;
	}
after_backlight:

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
get_ncpus(void)
{
	size_t slen;

	slen = sizeof(NCpus);
	if (sysctlbyname("hw.ncpu", &NCpus, &slen, NULL, 0) < 0)
		err(1, "sysctlbyname hw.ncpu failed");
	if (DebugOpt)
		printf("hw.ncpu %d\n", NCpus);
}

static void
get_uschedcpus(void)
{
	size_t slen;

	slen = sizeof(usched_cpu_used);
	if (sysctlbyname("kern.usched_global_cpumask", &usched_cpu_used, &slen,
	    NULL, 0) < 0)
		err(1, "sysctlbyname kern.usched_global_cpumask failed");
	if (DebugOpt) {
		int i;

		printf("usched cpumask was: ");
		for (i = 0; i < (int)NELEM(usched_cpu_used.ary); ++i)
			printf("%jx ", (uintmax_t)usched_cpu_used.ary[i]);
		printf("\n");
	}
}

static void
set_uschedcpus(void)
{
	if (DebugOpt) {
		int i;

		printf("usched cpumask: ");
		for (i = 0; i < (int)NELEM(usched_cpu_used.ary); ++i) {
			printf("%jx ",
			    (uintmax_t)usched_cpu_used.ary[i]);
		}
		printf("\n");
	}
	sysctlbyname("kern.usched_global_cpumask", NULL, 0,
	    &usched_cpu_used, sizeof(usched_cpu_used));
}

static int
has_perfbias(void)
{
	size_t len;
	int hint;

	len = sizeof(hint);
	if (sysctlbyname("machdep.perfbias0.hint", &hint, &len, NULL, 0) < 0)
		return 0;
	return 1;
}

static void
set_perfbias(int cpu, int inc)
{
	int hint = inc ? 0 : 15;
	char sysid[64];

	if (DebugOpt)
		printf("cpu%d set perfbias hint %d\n", cpu, hint);
	snprintf(sysid, sizeof(sysid), "machdep.perfbias%d.hint", cpu);
	sysctlbyname(sysid, NULL, NULL, &hint, sizeof(hint));
}

static void
init_perf(void)
{
	struct cpu_state *state;
	int cpu;

	/* Get usched cpumask */
	get_uschedcpus();

	/*
	 * Assume everything are used and are maxed out, before we
	 * start.
	 */

	CPUMASK_ASSBMASK(cpu_used, NCpus);
	cpu_pwrdom_used = cpu_pwrdom_mask;
	global_pcpu_limit = NCpus;

	for (cpu = 0; cpu < NCpus; ++cpu) {
		state = &pcpu_state[cpu];

		state->cpu_uavg = 0.0;
		state->cpu_davg = 0.0;
		state->cpu_limit = 1;
		state->cpu_count = 1;
		snprintf(state->cpu_name, sizeof(state->cpu_name), "cpu%d",
		    cpu);
	}

	state = &global_cpu_state;
	state->cpu_uavg = 0.0;
	state->cpu_davg = 0.0;
	state->cpu_limit = NCpus;
	state->cpu_count = NCpus;
	strlcpy(state->cpu_name, "global", sizeof(state->cpu_name));
}

static int
get_nstate(struct cpu_state *state, double srt)
{
	int ustate, dstate, nstate;

	/* speeding up */
	state->cpu_uavg = (state->cpu_uavg * 2.0 + state->cpu_qavg) / 3.0;
	/* slowing down */
	state->cpu_davg = (state->cpu_davg * srt + state->cpu_qavg) / (srt + 1);
	if (state->cpu_davg < state->cpu_uavg)
		state->cpu_davg = state->cpu_uavg;

	ustate = state->cpu_uavg / TriggerUp;
	if (ustate < state->cpu_limit)
		ustate = state->cpu_uavg / TriggerDown;
	dstate = state->cpu_davg / TriggerUp;
	if (dstate < state->cpu_limit)
		dstate = state->cpu_davg / TriggerDown;

	nstate = (ustate > dstate) ? ustate : dstate;
	if (nstate > state->cpu_count)
		nstate = state->cpu_count;

	if (DebugOpt) {
		printf("%s qavg=%5.2f uavg=%5.2f davg=%5.2f "
		    "%2d ncpus=%d\n", state->cpu_name,
		    state->cpu_qavg, state->cpu_uavg, state->cpu_davg,
		    state->cpu_limit, nstate);
	}
	return nstate;
}

static void
mon_perf(double srt)
{
	cpumask_t ocpu_used, ocpu_pwrdom_used;
	int pnstate = 0, nstate;
	int cpu;

	/*
	 * Find cpus requiring performance and their cooresponding power
	 * domains.  Save the number of cpus requiring performance in
	 * pnstate.
	 */
	ocpu_used = cpu_used;
	ocpu_pwrdom_used = cpu_pwrdom_used;

	CPUMASK_ASSZERO(cpu_used);
	CPUMASK_ASSZERO(cpu_pwrdom_used);

	for (cpu = 0; cpu < NCpus; ++cpu) {
		struct cpu_state *state = &pcpu_state[cpu];
		int s;

		s = get_nstate(state, srt);
		if (s) {
			CPUMASK_ORBIT(cpu_used, cpu);
			CPUMASK_ORBIT(cpu_pwrdom_used, cpu2pwrdom[cpu]);
		}
		pnstate += s;

		state->cpu_limit = s;
	}

	/*
	 * Calculate nstate, the number of cpus we wish to run at max
	 * performance.
	 */
	nstate = get_nstate(&global_cpu_state, srt);

	if (nstate == global_cpu_state.cpu_limit &&
	    (pnstate == global_pcpu_limit || nstate > pnstate)) {
		/* Nothing changed; keep the sets */
		cpu_used = ocpu_used;
		cpu_pwrdom_used = ocpu_pwrdom_used;

		global_pcpu_limit = pnstate;
		return;
	}
	global_pcpu_limit = pnstate;

	if (nstate > pnstate) {
		/*
		 * Add spare cpus to meet global performance requirement.
		 */
		add_spare_cpus(ocpu_used, nstate - pnstate);
	}

	global_cpu_state.cpu_limit = nstate;

	/*
	 * Adjust cpu and cpu power domain performance
	 */
	adj_perf(ocpu_used, ocpu_pwrdom_used);
}

static void
add_spare_cpus(const cpumask_t ocpu_used, int ncpu)
{
	cpumask_t saved_pwrdom, xcpu_used;
	int done = 0, cpu;

	/*
	 * Find more cpus in the previous cpu set.
	 */
	xcpu_used = cpu_used;
	CPUMASK_XORMASK(xcpu_used, ocpu_used);
	while (CPUMASK_TESTNZERO(xcpu_used)) {
		cpu = BSFCPUMASK(xcpu_used);
		CPUMASK_NANDBIT(xcpu_used, cpu);

		if (CPUMASK_TESTBIT(ocpu_used, cpu)) {
			CPUMASK_ORBIT(cpu_pwrdom_used, cpu2pwrdom[cpu]);
			CPUMASK_ORBIT(cpu_used, cpu);
			--ncpu;
			if (ncpu == 0)
				return;
		}
	}

	/*
	 * Find more cpus in the used cpu power domains.
	 */
	saved_pwrdom = cpu_pwrdom_used;
again:
	while (CPUMASK_TESTNZERO(saved_pwrdom)) {
		cpumask_t unused_cpumask;
		int dom;

		dom = BSFCPUMASK(saved_pwrdom);
		CPUMASK_NANDBIT(saved_pwrdom, dom);

		unused_cpumask = cpu_pwrdomain[dom]->dom_cpumask;
		CPUMASK_NANDMASK(unused_cpumask, cpu_used);

		while (CPUMASK_TESTNZERO(unused_cpumask)) {
			cpu = BSFCPUMASK(unused_cpumask);
			CPUMASK_NANDBIT(unused_cpumask, cpu);

			CPUMASK_ORBIT(cpu_pwrdom_used, dom);
			CPUMASK_ORBIT(cpu_used, cpu);
			--ncpu;
			if (ncpu == 0)
				return;
		}
	}
	if (!done) {
		done = 1;
		/*
		 * Find more cpus in unused cpu power domains
		 */
		saved_pwrdom = cpu_pwrdom_mask;
		CPUMASK_NANDMASK(saved_pwrdom, cpu_pwrdom_used);
		goto again;
	}
	if (DebugOpt)
		printf("%d cpus not found\n", ncpu);
}

static void
acpi_set_cpufreq(int dom, int inc)
{
	int lowest, highest, desired;
	char sysid[64];

	acpi_get_cpufreq(dom, &highest, &lowest);
	if (highest == 0 || lowest == 0)
		return;
	desired = inc ? highest : lowest;

	if (DebugOpt)
		printf("dom%d set frequency %d\n", dom, desired);
	snprintf(sysid, sizeof(sysid), "hw.acpi.cpu.px_dom%d.select", dom);
	sysctlbyname(sysid, NULL, NULL, &desired, sizeof(desired));
}

static void
adj_cpu_pwrdom(int dom, int inc)
{
	if (AdjustCpuFreq)
		acpi_set_cpufreq(dom, inc);
}

static void
adj_cpu_perf(int cpu, int inc)
{
	if (DebugOpt) {
		if (inc)
			printf("cpu%d increase perf\n", cpu);
		else
			printf("cpu%d decrease perf\n", cpu);
	}

	if (HasPerfbias)
		set_perfbias(cpu, inc);
	if (AdjustCstate)
		set_cstate(cpu, inc);
}

static void
adj_perf(cpumask_t xcpu_used, cpumask_t xcpu_pwrdom_used)
{
	cpumask_t old_usched_used;
	int cpu, inc;

	/*
	 * Set cpus requiring performance to the userland process
	 * scheduler.  Leave the rest of cpus unmapped.
	 */
	old_usched_used = usched_cpu_used;
	usched_cpu_used = cpu_used;
	if (CPUMASK_TESTZERO(usched_cpu_used))
		CPUMASK_ORBIT(usched_cpu_used, 0);
	if (CPUMASK_CMPMASKNEQ(usched_cpu_used, old_usched_used))
		set_uschedcpus();

	/*
	 * Adjust per-cpu performance.
	 */
	CPUMASK_XORMASK(xcpu_used, cpu_used);
	while (CPUMASK_TESTNZERO(xcpu_used)) {
		cpu = BSFCPUMASK(xcpu_used);
		CPUMASK_NANDBIT(xcpu_used, cpu);

		if (CPUMASK_TESTBIT(cpu_used, cpu)) {
			/* Increase cpu performance */
			inc = 1;
		} else {
			/* Decrease cpu performance */
			inc = 0;
		}
		adj_cpu_perf(cpu, inc);
	}

	/*
	 * Adjust cpu power domain performance.  This could affect
	 * a set of cpus.
	 */
	CPUMASK_XORMASK(xcpu_pwrdom_used, cpu_pwrdom_used);
	while (CPUMASK_TESTNZERO(xcpu_pwrdom_used)) {
		int dom;

		dom = BSFCPUMASK(xcpu_pwrdom_used);
		CPUMASK_NANDBIT(xcpu_pwrdom_used, dom);

		if (CPUMASK_TESTBIT(cpu_pwrdom_used, dom)) {
			/* Increase cpu power domain performance */
			inc = 1;
		} else {
			/* Decrease cpu power domain performance */
			inc = 0;
		}
		adj_cpu_pwrdom(dom, inc);
	}
}

static void
restore_perf(void)
{
	cpumask_t ocpu_used, ocpu_pwrdom_used;

	/* Remove highest cpu frequency limitation */
	HighestCpuFreq = 0;

	ocpu_used = cpu_used;
	ocpu_pwrdom_used = cpu_pwrdom_used;

	/* Max out all cpus and cpu power domains performance */
	CPUMASK_ASSBMASK(cpu_used, NCpus);
	cpu_pwrdom_used = cpu_pwrdom_mask;

	adj_perf(ocpu_used, ocpu_pwrdom_used);

	if (AdjustCstate) {
		/*
		 * Restore the original mwait C-state
		 */
		if (DebugOpt)
			printf("global set cstate %s\n", orig_global_cx);
		sysctlbyname("machdep.mwait.CX.idle", NULL, NULL,
		    orig_global_cx, strlen(orig_global_cx) + 1);
	}
}

static int
probe_cstate(void)
{
	char cx_supported[1024];
	const char *target;
	char *ptr;
	int idle_hlt, deep = 1;
	size_t len;

	len = sizeof(idle_hlt);
	if (sysctlbyname("machdep.cpu_idle_hlt", &idle_hlt, &len, NULL, 0) < 0)
		return 0;
	if (idle_hlt != 1)
		return 0;

	len = sizeof(cx_supported);
	if (sysctlbyname("machdep.mwait.CX.supported", cx_supported, &len,
	    NULL, 0) < 0)
		return 0;

	len = sizeof(orig_global_cx);
	if (sysctlbyname("machdep.mwait.CX.idle", orig_global_cx, &len,
	    NULL, 0) < 0)
		return 0;

	strlcpy(cpu_perf_cx, "AUTODEEP", sizeof(cpu_perf_cx));
	cpu_perf_cxlen = strlen(cpu_perf_cx) + 1;
	if (sysctlbyname("machdep.mwait.CX.idle", NULL, NULL,
	    cpu_perf_cx, cpu_perf_cxlen) < 0) {
		/* AUTODEEP is not supported; try AUTO */
		deep = 0;
		strlcpy(cpu_perf_cx, "AUTO", sizeof(cpu_perf_cx));
		cpu_perf_cxlen = strlen(cpu_perf_cx) + 1;
		if (sysctlbyname("machdep.mwait.CX.idle", NULL, NULL,
		    cpu_perf_cx, cpu_perf_cxlen) < 0)
			return 0;
	}

	if (!deep)
		target = "C2/0";
	else
		target = NULL;
	for (ptr = strtok(cx_supported, " "); ptr != NULL;
	     ptr = strtok(NULL, " ")) {
		if (target == NULL ||
		    (target != NULL && strcmp(ptr, target) == 0)) {
			strlcpy(cpu_idle_cx, ptr, sizeof(cpu_idle_cx));
			cpu_idle_cxlen = strlen(cpu_idle_cx) + 1;
			if (target != NULL)
				break;
		}
	}
	if (cpu_idle_cxlen == 0)
		return 0;

	if (DebugOpt) {
		printf("cstate orig %s, perf %s, idle %s\n",
		    orig_global_cx, cpu_perf_cx, cpu_idle_cx);
	}
	return 1;
}

static void
set_cstate(int cpu, int inc)
{
	const char *cst;
	char sysid[64];
	size_t len;

	if (inc) {
		cst = cpu_perf_cx;
		len = cpu_perf_cxlen;
	} else {
		cst = cpu_idle_cx;
		len = cpu_idle_cxlen;
	}

	if (DebugOpt)
		printf("cpu%d set cstate %s\n", cpu, cst);
	snprintf(sysid, sizeof(sysid), "machdep.mwait.CX.idle%d", cpu);
	sysctlbyname(sysid, NULL, NULL, cst, len);
}

static void
restore_backlight(void)
{
	if (BackLightDown) {
		BackLightDown = 0;
		sysctlbyname("hw.backlight_level", NULL, NULL,
		    &OldBackLightLevel, sizeof(OldBackLightLevel));
	}
}
