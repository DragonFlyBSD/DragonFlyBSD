/*
 *
 * Portions of this code was derived from the file kern_fork.c and as such
 * is subject to the copyrights below.
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * Copyright (c) 1996 Douglas Santry
 *
 * This code is subject to the beer copyright.  If I chance to meet you in a
 * bar and this code helped you in some way, you owe me a beer.  Only
 * in Germany will I accept domestic beer.  This code may or may not work
 * and I certainly make no claims as to its fitness for *any* purpose.
 * 
 * $FreeBSD: src/sys/kern/kern_threads.c,v 1.15 1999/08/28 00:46:15 peter Exp $
 * $DragonFly: src/sys/kern/kern_threads.c,v 1.12 2007/02/18 16:12:43 corecode Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sysproto.h>

#if 0

/*
 * XXX lwp
 *
 * I am unhappy code, please remove me
 */


/*
 * Low level support for sleep/wakeup paradigm
 * If a timeout is specified:
 *	returns 0 if wakeup
 *	returns EAGAIN if timed out
 *	returns EINVAL if error
 *
 * If a timeout is not specified:
 *
 *	returns time waiting in ticks.
 */
int
sys_thr_sleep(struct thr_sleep_args *uap) 
{
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	int sleepstart;
	struct timespec ts;
	struct timeval atv;
	int error, timo;

	timo = 0;
	if (uap->timeout != 0) {
		/*
		 * Get timespec struct
		 */
		if ((error = copyin(uap->timeout, &ts, sizeof(ts))) != 0) {
			p->p_wakeup = 0;
			return error;
		}
		if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) {
			p->p_wakeup = 0;
			return (EINVAL);
		}
		TIMESPEC_TO_TIMEVAL(&atv, &ts);
		if (itimerfix(&atv)) {
			p->p_wakeup = 0;
			return (EINVAL);
		}
		timo = tvtohz_high(&atv);
	}

	uap->sysmsg_result = 0;
	if (p->p_wakeup == 0) {
		sleepstart = ticks;
		lp->lwp_flags |= LWP_SINTR;
		error = tsleep(p, 0, "thrslp", timo);
		lp->lwp_flags &= ~LWP_SINTR;
		if (error == EWOULDBLOCK) {
			p->p_wakeup = 0;
			uap->sysmsg_result = EAGAIN;
			return 0;
		}
		if (uap->timeout == 0)
			uap->sysmsg_result = ticks - sleepstart;
	}
	p->p_wakeup = 0;
	return (0);
}

int
sys_thr_wakeup(struct thr_wakeup_args *uap) 
{
	struct proc *p = curproc;
	struct proc *pSlave = p->p_leader;
	struct lwp *lpSlave;

	while(pSlave && (pSlave->p_pid != uap->pid))
		pSlave = pSlave->p_peers;

	if(pSlave == 0) {
		uap->sysmsg_result = ESRCH;
		return(0);
	}

	lpSlave = FIRST_LWP_IN_PROC(pSlave);
	pSlave->p_wakeup++;
	if((lpSlave->lwp_stat == LSSLEEP) && lpSlave->lwp_wchan == pSlave) {
		wakeup(pSlave);
		return(0);
	}

	uap->sysmsg_result = EAGAIN;
	return 0;
}

#endif

/*
 * General purpose yield system call
 *
 * MPSAFE
 */
int
sys_yield(struct yield_args *uap) 
{
	uap->sysmsg_result = 0;
	lwkt_user_yield();
	return(0);
}
