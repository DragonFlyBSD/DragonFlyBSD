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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/kinfo.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#define STATE_UNKNOWN	0
#define STATE_LOW	1
#define STATE_HIGH	2

static void usage(void);
static double getcputime(void);
static void acpi_setcpufreq(int ostate, int nstate);

int DebugOpt;
int PowerState = STATE_UNKNOWN;
int PowerFd;
double Trigger = 0.25;

int
main(int ac, char **av)
{
	double qavg;
	double savg;
	int ch;
	int nstate;
	char buf[32];

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
	 * Prime delta cputime calculation, make sure at least dom0 exists,
	 * and make sure powerd is not already running.
	 */
	getcputime();
	savg = 0.0;

	if (sysctlbyname("hw.acpi.cpu.px_dom0.available", NULL, NULL,
			 NULL, 0) < 0) {
		fprintf(stderr, "hw.acpi.cpu.px_dom* sysctl not available\n");
		exit(1);
	}

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
	 * Monitoring loop
	 */
	for (;;) {
		qavg = getcputime();
		savg = (savg * 7.0 + qavg) / 8.0;

		if (DebugOpt) {
			printf("\rqavg=%5.2f savg=%5.2f\r", qavg, savg);
			fflush(stdout);
		}

		nstate = PowerState;
		if (nstate == STATE_UNKNOWN) {
			if (savg >= Trigger)
				nstate = STATE_HIGH;
			else
				nstate = STATE_LOW;
		} else if (nstate == STATE_LOW) {
			if (savg >= Trigger || qavg >= 0.9)
				nstate = STATE_HIGH;
		} else {
			if (savg < Trigger / 2.0 && qavg < Trigger / 2.0)
				nstate = STATE_LOW;
		}
		if (PowerState != nstate) {
			acpi_setcpufreq(PowerState, nstate);
			PowerState = nstate;
		}
		sleep(1);
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
			  ncpu_time[cpu].cp_intr) -
			 (ocpu_time[cpu].cp_user + ocpu_time[cpu].cp_sys +
			  ocpu_time[cpu].cp_intr);
	}
	return((double)delta / 1000000.0);
}


static
void
acpi_setcpufreq(int ostate, int nstate)
{
	int dom;
	int lowest;
	int highest;
	int desired;
	int v;
	char *sysid;
	char *ptr;
	char buf[256];
	size_t buflen;

	dom = 0;
	for (;;) {
		/*
		 * Retrieve availability list
		 */
		asprintf(&sysid, "hw.acpi.cpu.px_dom%d.available", dom);
		buflen = sizeof(buf) - 1;
		v = sysctlbyname(sysid, buf, &buflen, NULL, 0);
		free(sysid);
		if (v < 0)
			break;
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
		desired = (nstate == STATE_LOW) ? lowest : highest;

		asprintf(&sysid, "hw.acpi.cpu.px_dom%d.select", dom);
		buflen = sizeof(v);
		v = 0;
		sysctlbyname(sysid, &v, &buflen, NULL, 0);
		if (v != desired || ostate == STATE_UNKNOWN) {
			if (DebugOpt) {
				printf("dom%d set frequency %d\n",
				       dom, desired);
			} else {
				syslog(LOG_INFO, "dom%d set frequency %d\n",
				    dom, desired);
			}
			sysctlbyname(sysid, NULL, NULL,
				     &desired, sizeof(desired));
		}
		free(sysid);
		++dom;
	}
}

static
void
usage(void)
{
	fprintf(stderr, "usage: powerd [-d]\n");
	exit(1);
}
