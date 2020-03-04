/*
 * Copyright (c) 2012-2020 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>,
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

#ifndef _SYS_USCHED_DFLY_H_
#define _SYS_USCHED_DFLY_H_

/*
 * Priorities.  Note that with 32 run queues per scheduler each queue
 * represents four priority levels.
 */

#define MAXPRI			128
#define PRIMASK			(MAXPRI - 1)
#define PRIBASE_REALTIME	0
#define PRIBASE_NORMAL		MAXPRI
#define PRIBASE_IDLE		(MAXPRI * 2)
#define PRIBASE_THREAD		(MAXPRI * 3)
#define PRIBASE_NULL		(MAXPRI * 4)

#define NQS	32			/* 32 run queues. */
#define PPQ	(MAXPRI / NQS)		/* priorities per queue */
#define PPQMASK	(PPQ - 1)

/*
 * NICE_QS	- maximum queues nice can shift the process
 * EST_QS	- maximum queues estcpu can shift the process
 *
 * ESTCPUPPQ	- number of estcpu units per priority queue
 * ESTCPUMAX	- number of estcpu units
 *
 * Remember that NICE runs over the whole -20 to +20 range.
 */
#define NICE_QS		24	/* -20 to +20 shift in whole queues */
#define EST_QS		20	/* 0-MAX shift in whole queues */
#define ESTCPUPPQ	512
#define ESTCPUMAX	(ESTCPUPPQ * EST_QS)
#define PRIO_RANGE	(PRIO_MAX - PRIO_MIN + 1)

#define ESTCPULIM(v)	min((v), ESTCPUMAX)

TAILQ_HEAD(rq, lwp);

#define lwp_priority	lwp_usdata.dfly.priority
#define lwp_forked	lwp_usdata.dfly.forked
#define lwp_rqindex	lwp_usdata.dfly.rqindex
#define lwp_estcpu	lwp_usdata.dfly.estcpu
#define lwp_estfast	lwp_usdata.dfly.estfast
#define lwp_uload	lwp_usdata.dfly.uload
#define lwp_rqtype	lwp_usdata.dfly.rqtype
#define lwp_qcpu	lwp_usdata.dfly.qcpu
#define lwp_rrcount	lwp_usdata.dfly.rrcount

static __inline int
lptouload(struct lwp *lp)
{
	int uload;

	uload = lp->lwp_estcpu / NQS;
	uload -= uload * lp->lwp_proc->p_nice / (PRIO_MAX + 1);

	return uload;
}

/*
 * DFly scheduler pcpu structure.  Note that the pcpu uload field must
 * be 64-bits to avoid overflowing in the situation where more than 32768
 * processes are on a single cpu's queue.  Since high-end systems can
 * easily run 900,000+ processes, we have to deal with it.
 */
struct usched_dfly_pcpu {
	struct spinlock spin;
	struct thread	*helper_thread;
	struct globaldata *gd;
	u_short		scancpu;
	short		upri;
	long		uload;		/* 64-bits to avoid overflow (1) */
	int		ucount;
	int		flags;
	struct lwp	*uschedcp;
	struct rq	queues[NQS];
	struct rq	rtqueues[NQS];
	struct rq	idqueues[NQS];
	u_int32_t	queuebits;
	u_int32_t	rtqueuebits;
	u_int32_t	idqueuebits;
	int		runqcount;
	int		cpuid;
	cpumask_t	cpumask;
	cpu_node_t	*cpunode;
} __cachealign;

/*
 * Reflecting bits in the global atomic masks allows us to avoid
 * a certain degree of global ping-ponging.
 */
#define DFLY_PCPU_RDYMASK	0x0001	/* reflect rdyprocmask */
#define DFLY_PCPU_CURMASK	0x0002	/* reflect curprocmask */

typedef struct usched_dfly_pcpu	*dfly_pcpu_t;

#endif
