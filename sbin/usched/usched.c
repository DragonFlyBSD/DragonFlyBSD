/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and Thomas Nikolajsen
 * <thomas.nikolajsen@mail.dk>
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

#define _KERNEL_STRUCTURES
#include <sys/types.h>
#include <sys/usched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void usage(void);

int DebugOpt;

int
main(int ac, char **av)
{
	int ch;
	int res;
	char *sched = NULL;
	char *cpustr = NULL;
	char *sched_cpustr = NULL;
	cpumask_t cpumask;
	int cpuid;

	CPUMASK_ASSZERO(cpumask);

	while ((ch = getopt(ac, av, "d")) != -1) {
		switch (ch) {
		case 'd':
			DebugOpt = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	ac -= optind;
	av += optind;

	if (ac < 2) {
		usage();
		/* NOTREACHED */
	}
	sched_cpustr = strdup(av[0]);
	sched = strsep(&sched_cpustr, ":");
	if (strcmp(sched, "default") == 0)
		fprintf(stderr, "Ignoring scheduler == \"default\": not implemented\n");
	cpustr = strsep(&sched_cpustr, "");
	if (strlen(sched) == 0 && cpustr == NULL) {
		usage();
		/* NOTREACHED */
	}

	/*
	 * XXX needs expanded support for > 64 cpus
	 */
	if (cpustr != NULL) {
		unsigned long v;

		v = strtoul(cpustr, NULL, 0);
		for (cpuid = 0; cpuid < (int)sizeof(v) * 8; ++cpuid) {
			if (v & (1LU << cpuid))
				CPUMASK_ORBIT(cpumask, cpuid);
		}
	}

	if (strlen(sched) != 0) {
		if (DebugOpt)
			fprintf(stderr, "DEBUG: USCHED_SET_SCHEDULER: scheduler: %s\n", sched);
		res = usched_set(getpid(), USCHED_SET_SCHEDULER, sched, strlen(sched));
		if (res != 0) {
			perror("usched_set(,USCHED_SET_SCHEDULER,,)");
			exit(1);
		}
	}
	if (CPUMASK_TESTNZERO(cpumask)) {
		for (cpuid = 0; cpuid < (int)sizeof(cpumask) * 8; ++cpuid) {
			if (CPUMASK_TESTBIT(cpumask, cpuid))
				break;
		}
		if (DebugOpt) {
			fprintf(stderr, "DEBUG: USCHED_SET_CPU: cpuid: %d\n",
				cpuid);
		}
		res = usched_set(getpid(), USCHED_SET_CPU,
				 &cpuid, sizeof(int));
		if (res != 0) {
			perror("usched_set(,USCHED_SET_CPU,,)");
			exit(1);
		}
		CPUMASK_NANDBIT(cpumask, cpuid);
		while (CPUMASK_TESTNZERO(cpumask)) {
			++cpuid;
			if (CPUMASK_TESTBIT(cpumask, cpuid) == 0)
				continue;
			CPUMASK_NANDBIT(cpumask, cpuid);
			if (DebugOpt) {
				fprintf(stderr,
					"DEBUG: USCHED_ADD_CPU: cpuid: %d\n",
					cpuid);
			}
			res = usched_set(getpid(), USCHED_ADD_CPU,
					 &cpuid, sizeof(int));
			if (res != 0) {
				perror("usched_set(,USCHED_ADD_CPU,,)");
				exit(1);
			}
		}
	}
	execvp(av[1], av + 1);
	exit(1);
}

static
void
usage(void)
{
	fprintf(stderr,
		"usage: usched [-d] {scheduler[:cpumask] | :cpumask} "
		"program [argument ...]\n");
	exit(1);
}
