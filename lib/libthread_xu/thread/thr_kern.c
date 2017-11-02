/*
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/signalvar.h>
#include <sys/rtprio.h>
#include <sys/lwp.h>
#include <pthread.h>

#include "thr_private.h"

/*#define DEBUG_THREAD_KERN */
#ifdef DEBUG_THREAD_KERN
#define DBG_MSG		stdout_debug
#else
#define DBG_MSG(x...)
#endif

/*
 * This is called when the first thread (other than the initial
 * thread) is created.
 *
 * NOTE: we no longer call _thrd_rtld_fini here.
 */
int
_thr_setthreaded(int threaded)
{
	if (((threaded == 0) ^ (__isthreaded == 0)) == 0)
		return (0);
	__isthreaded = threaded;
#if 0
	/* save for later. */
	if (threaded != 0)
		/* blah */ ;
	else
		/* blah */ ;
#endif
	return (0);
}

void
_thr_signal_block(struct pthread *curthread)
{
	sigset_t set;

	if (curthread->sigblock > 0) {
		curthread->sigblock++;
		return;
	}
	SIGFILLSET(set);
	SIGDELSET(set, SIGBUS);
	SIGDELSET(set, SIGILL);
	SIGDELSET(set, SIGFPE);
	SIGDELSET(set, SIGSEGV);
	SIGDELSET(set, SIGTRAP);
	__sys_sigprocmask(SIG_BLOCK, &set, &curthread->sigmask);
	curthread->sigblock++;
}

void
_thr_signal_unblock(struct pthread *curthread)
{
	if (--curthread->sigblock == 0)
		__sys_sigprocmask(SIG_SETMASK, &curthread->sigmask, NULL);
}

void
_thr_assert_lock_level(void)
{
	PANIC("locklevel <= 0");
}

int
_thr_send_sig(struct pthread *thread, int sig)
{
	return (lwp_kill(-1, thread->tid, sig));
}

int
_thr_get_tid(void)
{
	return (lwp_gettid());
}

/*
 * We don't use the priority for SCHED_OTHER, but
 * some programs may depend on getting an error when
 * setting a priority that is out of the range returned
 * by sched_get_priority_{min,max}. Not sure if this
 * falls into implementation defined behavior or not.
 */
int
_thr_set_sched_other_prio(struct pthread *pth __unused, int prio)
{
	static int max, min, init_status;

	/*
	 * switch (init_status) {
	 * case 0: need initialization
	 * case 1: initialization successful
	 * case 2: initialization failed. can't happen, but if
	 *	   it does, accept all and hope for the best.
	 *	   It's not like we use it anyway.
	 */
	if (!init_status) {
		int tmp = errno;

		errno = 0;
		init_status = 1;
		if (((min = sched_get_priority_min(SCHED_OTHER)) == -1) &&
								(errno != 0))
			init_status = 2;
		if (((max = sched_get_priority_max(SCHED_OTHER)) == -1) &&
								(errno != 0))
			init_status = 2;
		errno = tmp;
	}
	if ((init_status == 2) || ((prio >= min) && (prio <= max))) {
		return 0;
	}
	errno = ENOTSUP;
	return -1;
}

int
_rtp_to_schedparam(const struct rtprio *rtp, int *policy,
	struct sched_param *param)
{
	switch(rtp->type) {
	case RTP_PRIO_REALTIME:
		*policy = SCHED_RR;
		param->sched_priority = RTP_PRIO_MAX - rtp->prio;
		break;
	case RTP_PRIO_FIFO:
		*policy = SCHED_FIFO;
		param->sched_priority = RTP_PRIO_MAX - rtp->prio;
		break;
	default:
		*policy = SCHED_OTHER;
		param->sched_priority = 0;
		break;
	}
	return (0);
}

int
_schedparam_to_rtp(int policy, const struct sched_param *param,
	struct rtprio *rtp)
{
	switch(policy) {
	case SCHED_RR:
		rtp->type = RTP_PRIO_REALTIME;
		rtp->prio = RTP_PRIO_MAX - param->sched_priority;
		break;
	case SCHED_FIFO:
		rtp->type = RTP_PRIO_FIFO;
		rtp->prio = RTP_PRIO_MAX - param->sched_priority;
		break;
	case SCHED_OTHER:
	default:
		rtp->type = RTP_PRIO_NORMAL;
		rtp->prio = 0;
		break;
	}
	return (0);
}

int
_thr_getscheduler(lwpid_t lwpid, int *policy, struct sched_param *param)
{
	struct pthread *curthread = tls_get_curthread();
	struct rtprio rtp;
	int ret;

	if (lwpid == curthread->tid)
		lwpid = -1;
	ret = lwp_rtprio(RTP_LOOKUP, 0, lwpid, &rtp);
	if (ret == -1)
		return (ret);
	_rtp_to_schedparam(&rtp, policy, param);
	return (0);
}

int
_thr_setscheduler(lwpid_t lwpid, int policy, const struct sched_param *param)
{
	struct pthread *curthread = tls_get_curthread();
	struct rtprio rtp;

	if (lwpid == curthread->tid)
		lwpid = -1;
	_schedparam_to_rtp(policy, param, &rtp);
	return (lwp_rtprio(RTP_SET, 0, lwpid, &rtp));
}
