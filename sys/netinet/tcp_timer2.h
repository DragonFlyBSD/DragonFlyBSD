/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_timer.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netinet/tcp_timer.h,v 1.18.2.1 2002/08/16 22:16:39 dillon Exp $
 * $DragonFly: src/sys/netinet/tcp_timer.h,v 1.3 2003/08/23 11:18:00 rob Exp $
 */

#ifndef _NETINET_TCP_TIMER2_H_
#define _NETINET_TCP_TIMER2_H_

#ifdef _KERNEL

#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif

#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif

#ifndef _SYS_CALLOUT_H_
#include <sys/callout.h>
#endif

#ifndef _NETINET_TCP_VAR_H_
#include <netinet/tcp_var.h>
#endif

#ifndef _NETINET_TCP_TIMER_H_
#include <netinet/tcp_timer.h>
#endif

/*
 * The TCP_FASTKEEP option uses keepintvl for the initial keepidle timeout
 * instead of keepidle.
 */
static __inline int
tcp_getkeepidle(struct tcpcb *_tp)
{
	if (_tp->t_flags & TF_FASTKEEP)
		return (tcp_keepintvl);
	else
		return (tcp_keepidle);
}

static __inline void
tcp_callout_stop(struct tcpcb *_tp, struct tcp_callout *_tc)
{
	KKASSERT(_tp->tt_msg->tt_cpuid == mycpuid);

	crit_enter();
	callout_stop(&_tc->tc_callout);
	_tp->tt_msg->tt_tasks &= ~_tc->tc_task;
	_tp->tt_msg->tt_running_tasks &= ~_tc->tc_task;
	crit_exit();
}

static __inline void
tcp_callout_reset(struct tcpcb *_tp, struct tcp_callout *_tc, int _to_ticks,
		  void (*_func)(void *))
{
	KKASSERT(_tp->tt_msg->tt_cpuid == mycpuid);

	crit_enter();
	callout_reset(&_tc->tc_callout, _to_ticks, _func, _tp);
	_tp->tt_msg->tt_tasks &= ~_tc->tc_task;
	_tp->tt_msg->tt_running_tasks &= ~_tc->tc_task;
	crit_exit();
}

static __inline int
tcp_callout_active(struct tcpcb *_tp, struct tcp_callout *_tc)
{
	int _act;

	KKASSERT(_tp->tt_msg->tt_cpuid == mycpuid);

	crit_enter();
	_act = callout_active(&_tc->tc_callout);
	if (!_act) {
		_act = (_tp->tt_msg->tt_tasks |
			_tp->tt_msg->tt_running_tasks) & _tc->tc_task;
	}
	crit_exit();
	return _act;
}

static __inline int
tcp_callout_pending(struct tcpcb *_tp, struct tcp_callout *_tc)
{
	KKASSERT(_tp->tt_msg->tt_cpuid == mycpuid);

	return callout_pending(&_tc->tc_callout);
}

#endif	/* !_KERNEL */

#endif	/* _NETINET_TCP_TIMER2_H_ */
