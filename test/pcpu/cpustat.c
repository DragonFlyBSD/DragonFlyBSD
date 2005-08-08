/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@dragonflybsd.org> and Matthew Dillon
 * <dillon@backplane.com>
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
 * 
 * $DragonFly: src/test/pcpu/cpustat.c,v 1.1 2005/08/08 03:31:00 hmp Exp $
 */

/*
 * CPUSTAT - Utility for displaying per-cpu cpu load statistics.
 *
 * NB: this program should be either made part of top(1) or part
 * of a new accounting program that has the ability to display
 * per-cpu break-up for statistics that support them.
 */
#include <sys/param.h>
#include <sys/sysctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <kinfo.h>
#include <unistd.h>

static int numcpus = 0;

#define	INTERVAL	1

static int
cputime_get(struct kinfo_cputime **percpu)
{
	int error = 0;
	size_t len = sizeof(struct kinfo_cputime) * numcpus;

	if ((*percpu = malloc(len)) == NULL) {
		error = ENOMEM;
	}

	/* retrieve per-cpu statistics from kernel */
	if (error == 0) {
		bzero(*percpu, len);
		error = sysctlbyname("kern.cputime", *percpu, &len, NULL, 0);
		if (error < 0) {
			warn("sysctl: kern.cputime");
			error = EINVAL;
		}
	}

	/* cross-check size */
	if (error == 0 && (len / sizeof(struct kinfo_cputime)) != numcpus) {
		error = EINVAL;
	}

	if (error) {
		free(*percpu);
		*percpu = NULL;
	}
	return (error);
}

static void
cputime_get_diff(struct kinfo_cputime *old, struct kinfo_cputime *new,
	struct kinfo_cputime *delta)
{
	delta->cp_user = new->cp_user - old->cp_user;
	delta->cp_nice = new->cp_nice - old->cp_nice;
	delta->cp_sys = new->cp_sys - old->cp_sys;
	delta->cp_intr = new->cp_intr - old->cp_intr;
	delta->cp_idle = new->cp_idle - old->cp_idle;
}

static __inline uint64_t
cputime_get_total(struct kinfo_cputime *cpt)
{
	return(
	cpt->cp_user + cpt->cp_nice + cpt->cp_sys + cpt->cp_intr + cpt->cp_idle);
}

int
main(void)
{
	int i, error = 0;

	/* get number of cpus */
	if ( kinfo_get_cpus(&numcpus) )
		exit(-1);

	printf("%d cpus\n", numcpus);

	for (;;) {
		struct kinfo_cputime *old, *new, delta;

		error = cputime_get(&old);
		if (error)
			return -1;

		sleep(INTERVAL);

		error = cputime_get(&new);
		if (error)
			return -1;

		for (i = 0; i < numcpus; ++i) {
			uint64_t total = 0;
#define pct(t)	(total == 0 ? 0.0 : ((double)t * 100.0 / (double)total))
			cputime_get_diff(&old[i], &new[i], &delta);
			total = cputime_get_total(&delta);
			printf("CPU-%d state: ", i);
			printf("%6.2f%% user, %6.2f%% nice, %6.2f%% sys, "
				"%6.2f%% intr, %6.2f%% idle\n",
				pct(delta.cp_user), pct(delta.cp_nice),
				pct(delta.cp_sys),
				pct(delta.cp_intr), pct(delta.cp_idle));
		}
		free(old);
		free(new);
	}
}
