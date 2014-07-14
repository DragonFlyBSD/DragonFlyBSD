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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

static void usage(void);
static double getcputime(void);
static void acpi_setcpufreq(int nstate);
static void setupdominfo(void);

int DebugOpt;
int CpuLimit;		/* # of cpus at max frequency */
int DomLimit;		/* # of domains at max frequency */
int PowerFd;
int DomBeg;
int DomEnd;
int NCpus;
int CpuCount[256];	/* # of cpus in any given domain */
int CpuToDom[256];	/* domain a particular cpu belongs to */
double Trigger = 0.25;	/* load per cpu to force max freq */

static void sigintr(int signo);

int
main(int ac, char **av)
{
	double qavg;
	double savg;
	int ch;
	int nstate;
	char buf[64];

	while ((ch = getopt(ac, av, "d")) != -1) {
		switch(ch) {
		case 'd':
			DebugOpt = 1;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	ac -= optind;
	av += optind;

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
		getcputime();
		savg = 0.0;

		setupdominfo();
		if (DomBeg >= DomEnd) {
			sleep(1);
			continue;
		}

		DomLimit = DomEnd;
		CpuLimit = NCpus;
		break;
	}

	/*
	 * Set to maximum performance if killed.
	 */
	signal(SIGINT, sigintr);
	signal(SIGTERM, sigintr);

	/*
	 * Monitoring loop
	 *
	 * Calculate nstate, the number of cpus we wish to run at max
	 * frequency.  All remaining cpus will be set to their lowest
	 * frequency and mapped out of the user process scheduler.
	 */
	for (;;) {
		qavg = getcputime();
		savg = (savg * 7.0 + qavg) / 8.0;

		nstate = savg / Trigger;
		if (nstate > NCpus)
			nstate = NCpus;
		if (DebugOpt) {
			printf("\rqavg=%5.2f savg=%5.2f %2d/%2d ncpus=%d\r",
				qavg, savg, CpuLimit, DomLimit, nstate);
			fflush(stdout);
		}
		if (nstate != CpuLimit)
			acpi_setcpufreq(nstate);
		sleep(1);
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
 * Figure out the domains and calculate the CpuCount[] and CpuToDom[]
 * arrays.
 */
static
void
setupdominfo(void)
{
	char buf[64];
	char members[1024];
	char *str;
	size_t msize;
	int i;
	int n;

	for (i = 0; i < 256; ++i) {
		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.available", i);
		if (sysctlbyname(buf, NULL, NULL, NULL, 0) >= 0)
			break;
	}
	DomBeg = i;

	for (i = 255; i >= DomBeg; --i) {
		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.available", i);
		if (sysctlbyname(buf, NULL, NULL, NULL, 0) >= 0) {
			++i;
			break;
		}
	}
	DomEnd = i;

	for (i = DomBeg; i < DomEnd; ++i) {
		snprintf(buf, sizeof(buf),
			 "hw.acpi.cpu.px_dom%d.members", i);
		msize = sizeof(members);
		if (sysctlbyname(buf, members, &msize, NULL, 0) == 0) {
			members[msize] = 0;
			for (str = strtok(members, " "); str;
			     str = strtok(NULL, " ")) {
				n = -1;
				sscanf(str, "cpu%d", &n);
				if (n >= 0) {
					++NCpus;
					++CpuCount[i];
					CpuToDom[n]= i;
				}
			}
		}
	}
}

/*
 * Return the one-second cpu load.  One cpu at 100% will return a value
 * of 1.0.  On a SMP system N cpus running at 100% will return a value of N.
 */
static
double
getcputime(void)
{
	static struct kinfo_cputime ocpu_time[64];
	static struct kinfo_cputime ncpu_time[64];
	size_t slen;
	int ncpu;
	int cpu;
	uint64_t delta;

	bcopy(ncpu_time, ocpu_time, sizeof(ncpu_time));
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
	return((double)delta / 1000000.0);
}

/*
 * nstate is the requested number of cpus that we wish to run at full
 * frequency.  We calculate how many domains we have to adjust to reach
 * this goal.
 *
 * This function also sets the user scheduler global cpu mask.
 */
static
void
acpi_setcpufreq(int nstate)
{
	int ncpus = 0;
	int increasing = (nstate > CpuLimit);
	int dom;
	int domBeg;
	int domEnd;
	int lowest;
	int highest;
	int desired;
	int v;
	char *sysid;
	char *ptr;
	char buf[256];
	size_t buflen;
	cpumask_t global_cpumask;

	/*
	 * Calculate the ending domain if the number of operating cpus
	 * has increased.
	 *
	 * Calculate the starting domain if the number of operating cpus
	 * has decreased.
	 */
	for (dom = DomBeg; dom < DomEnd; ++dom) {
		if (ncpus >= nstate)
			break;
		ncpus += CpuCount[dom];
	}

	syslog(LOG_INFO, "using %d cpus", nstate);

	/*
	 * Set the mask of cpus the userland scheduler is allowed to use.
	 */
	CPUMASK_ASSBMASK(global_cpumask, nstate);
	sysctlbyname("kern.usched_global_cpumask", NULL, 0,
		     &global_cpumask, sizeof(global_cpumask));

	if (increasing) {
		domBeg = DomLimit;
		domEnd = dom;
	} else {
		domBeg = dom;
		domEnd = DomLimit;
	}
	DomLimit = dom;
	CpuLimit = nstate;

	/*
	 * Adjust the cpu frequency
	 */
	if (DebugOpt)
		printf("\n");
	for (dom = domBeg; dom < domEnd; ++dom) {
		/*
		 * Retrieve availability list
		 */
		asprintf(&sysid, "hw.acpi.cpu.px_dom%d.available", dom);
		buflen = sizeof(buf) - 1;
		v = sysctlbyname(sysid, buf, &buflen, NULL, 0);
		free(sysid);
		if (v < 0)
			continue;
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
		}

		/*
		 * Calculate the desired cpu frequency, test, and set.
		 */
		desired = increasing ? highest : lowest;

		asprintf(&sysid, "hw.acpi.cpu.px_dom%d.select", dom);
		buflen = sizeof(v);
		v = 0;
		sysctlbyname(sysid, &v, &buflen, NULL, 0);
		{
			if (DebugOpt) {
				printf("dom%d set frequency %d\n",
				       dom, desired);
			}
			sysctlbyname(sysid, NULL, NULL,
				     &desired, sizeof(desired));
		}
		free(sysid);
	}
}

static
void
usage(void)
{
	fprintf(stderr, "usage: powerd [-d]\n");
	exit(1);
}
