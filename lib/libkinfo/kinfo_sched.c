/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 * $DragonFly: src/lib/libkinfo/kinfo_sched.c,v 1.1 2004/12/22 11:01:49 joerg Exp $
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <kinfo.h>

int
kinfo_get_cpus(int *ncpus)
{
	size_t len = sizeof(*ncpus);

	return(sysctlbyname("hw.ncpu", ncpus, &len, NULL, 0));
}

int
kinfo_get_sched_ccpu(int *ccpu)
{
	size_t len = sizeof(*ccpu);

	return(sysctlbyname("kern.ccpu", ccpu, &len, NULL, 0));
}

int
kinfo_get_sched_cputime(struct kinfo_cputime *cputime)
{
	size_t len = sizeof(*cputime);

	return(sysctlbyname("kern.cp_time", cputime, &len, NULL, 0));
}

int
kinfo_get_sched_hz(int *hz)
{
	struct kinfo_clockinfo clockinfo;
	size_t len = sizeof(clockinfo);
	int retval;

	retval = sysctlbyname("kern.clockrate", &clockinfo, &len, NULL, 0);
	if (retval)
		return(retval);

	*hz = clockinfo.ci_hz;
	return(0);
}

int
kinfo_get_sched_stathz(int *stathz)
{
	struct kinfo_clockinfo clockinfo;
	size_t len = sizeof(clockinfo);
	int retval;

	retval = sysctlbyname("kern.clockrate", &clockinfo, &len, NULL, 0);
	if (retval)
		return(retval);

	*stathz = clockinfo.ci_stathz;
	return(0);
}

int
kinfo_get_sched_profhz(int *profhz)
{
	struct kinfo_clockinfo clockinfo;
	size_t len = sizeof(clockinfo);
	int retval;

	retval = sysctlbyname("kern.clockrate", &clockinfo, &len, NULL, 0);
	if (retval)
		return(retval);

	*profhz = clockinfo.ci_profhz;
	return(0);
}
