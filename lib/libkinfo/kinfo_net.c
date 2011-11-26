/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@dragonflybsd.org>.
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
 * $DragonFly: src/lib/libkinfo/kinfo_net.c,v 1.1 2005/05/01 04:05:35 hmp Exp $
 */

#include <sys/kinfo.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/route.h>

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <kinfo.h>

int
kinfo_get_net_rtstatistics(struct rtstatistics *rts)
{
	struct rtstatistics *percpu = NULL;
	size_t len = sizeof(struct rtstatistics) * SMP_MAXCPU;
	int cpucount, error = 0;

	_DIAGASSERT(rts != NULL);

	if ((percpu = malloc(len)) == NULL) {
		error = ENOMEM;
		goto done;
	}

	/* retrieve verbatim per-cpu statistics from kernel */
	if (sysctlbyname("net.route.stats", percpu, &len, NULL, 0) < 0) {
		error = errno;
		goto done;
	} else {
		percpu = reallocf(percpu, len);
		if (percpu == NULL) {
			error = ENOMEM;
			goto done;
		}
	}

	/* aggregate per-cpu statistics retrieved from kernel */
	cpucount = len / sizeof(struct rtstatistics);
	route_pcpu_statistics(percpu, rts, cpucount);

done:
	if (percpu != NULL)
		free(percpu);
	return (error);
}
