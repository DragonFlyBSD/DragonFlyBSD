/*	$NetBSD: linux_time.c,v 1.14 2006/05/14 03:40:54 christos Exp $ */

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/imgact_aout.h>
#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/nlookup.h>
#include <sys/blist.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/time.h>
#include <sys/timers.h>
#include <sys/unistd.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/wait.h>

#include <sys/signal2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <machine/frame.h>
#include <machine/limits.h>
#include <machine/psl.h>
#include <machine/sysarch.h>
#ifdef __i386__
#include <machine/segments.h>
#endif

#include <sys/sched.h>

#include <emulation/linux/linux_sysproto.h>
#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_mib.h"
#include "linux_util.h"

static void native_to_linux_timespec(struct l_timespec *,
				     struct timespec *);
static int linux_to_native_timespec(struct timespec *,
				     struct l_timespec *);
static int linux_to_native_clockid(clockid_t *, clockid_t);

static void
native_to_linux_timespec(struct l_timespec *ltp, struct timespec *ntp)
{
	ltp->tv_sec = ntp->tv_sec;
	ltp->tv_nsec = ntp->tv_nsec;
}

static int
linux_to_native_timespec(struct timespec *ntp, struct l_timespec *ltp)
{
	if (ltp->tv_sec < 0 || ltp->tv_nsec > (l_long)999999999L)
		return (EINVAL);
	ntp->tv_sec = ltp->tv_sec;
	ntp->tv_nsec = ltp->tv_nsec;

	return (0);
}

static int
linux_to_native_clockid(clockid_t *n, clockid_t l)
{
	switch (l) {
	case LINUX_CLOCK_REALTIME:
		*n = CLOCK_REALTIME;
		break;
	case LINUX_CLOCK_MONOTONIC:
		*n = CLOCK_MONOTONIC;
		break;
	case LINUX_CLOCK_PROCESS_CPUTIME_ID:
	case LINUX_CLOCK_THREAD_CPUTIME_ID:
	case LINUX_CLOCK_REALTIME_HR:
	case LINUX_CLOCK_MONOTONIC_HR:
	default:
		return (EINVAL);
		break;
	}

	return (0);
}

int
sys_linux_clock_gettime(struct linux_clock_gettime_args *args)
{
	struct l_timespec lts;
	int error;
	clockid_t nwhich = 0;	/* XXX: GCC */
	struct timespec tp;

	error = linux_to_native_clockid(&nwhich, args->which);
	if (error != 0)
		return (error);
	error = kern_clock_gettime(nwhich, &tp);
	if (error != 0)
		return (error);
	native_to_linux_timespec(&lts, &tp);

	return (copyout(&lts, args->tp, sizeof lts));
}

int
sys_linux_clock_settime(struct linux_clock_settime_args *args)
{
	struct timespec ts;
	struct l_timespec lts;
	int error;
	clockid_t nwhich = 0;	/* XXX: GCC */

	error = linux_to_native_clockid(&nwhich, args->which);
	if (error != 0)
		return (error);
	error = copyin(args->tp, &lts, sizeof lts);
	if (error != 0)
		return (error);
	error = linux_to_native_timespec(&ts, &lts);
	if (error != 0)
		return (error);

	return (kern_clock_settime(nwhich, &ts));
}

int
sys_linux_clock_getres(struct linux_clock_getres_args *args)
{
	struct timespec ts;
	struct l_timespec lts;
	int error;
	clockid_t nwhich = 0;	/* XXX: GCC */

	if (args->tp == NULL)
	  	return (0);

	error = linux_to_native_clockid(&nwhich, args->which);
	if (error != 0)
		return (error);
	error = kern_clock_getres(nwhich, &ts);
	if (error != 0)
		return (error);
	native_to_linux_timespec(&lts, &ts);

	return (copyout(&lts, args->tp, sizeof lts));
}

int
sys_linux_clock_nanosleep(struct linux_clock_nanosleep_args *args)
{
	struct timespec *rmtp;
	struct l_timespec lrqts, lrmts;
	struct timespec rqts, rmts;
	int error;

	if (args->flags != 0)
		return (EINVAL);	/* XXX deal with TIMER_ABSTIME */

	if (args->which != LINUX_CLOCK_REALTIME)
		return (EINVAL);

	error = copyin(args->rqtp, &lrqts, sizeof lrqts);
	if (error != 0)
		return (error);

	if (args->rmtp != NULL)
	   	rmtp = &rmts;
	else
	   	rmtp = NULL;

	error = linux_to_native_timespec(&rqts, &lrqts);
	if (error != 0)
		return (error);
	error = nanosleep1(&rqts, rmtp);
	if (error != 0)
		return (error);

	if (args->rmtp != NULL) {
	   	native_to_linux_timespec(&lrmts, rmtp);
	   	error = copyout(&lrmts, args->rmtp, sizeof lrmts );
		if (error != 0)
		   	return (error);
	}

	return (0);
}
