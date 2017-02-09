/*-
 * Copyright (c) 1996, 1997
 *	HD Associates, Inc.  All rights reserved.
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
 *	This product includes software developed by HD Associates, Inc
 *	and Jukka Antero Ukkonen.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/posix4/sched.h,v 1.4 1999/12/29 04:55:02 peter Exp $
 */

/* sched.h: POSIX 1003.1b Process Scheduling header */

#ifndef _SCHED_H_
#define	_SCHED_H_

/* Scheduling policies
 */
#define	SCHED_FIFO	1
#define	SCHED_OTHER	2
#define	SCHED_RR	3

struct sched_param
{
	int sched_priority;
};

#ifndef _KERNEL
#include <sys/cdefs.h>
#include <sys/types.h>		/* For pid_t */

#include <time.h>		/* Per P1003.4 */

#if __BSD_VISIBLE
#include <machine/cpumask.h>

typedef	cpumask_t		cpu_set_t;
typedef	cpumask_t		cpuset_t;	/* FreeBSD compat */

#define	CPU_SETSIZE		(sizeof(cpumask_t) * 8)

#define	CPU_ZERO(set)		CPUMASK_ASSZERO(*set)
#define	CPU_SET(cpu, set)	CPUMASK_ORBIT(*set, cpu)
#define	CPU_CLR(cpu, set)	CPUMASK_NANDBIT(*set, cpu)
#define	CPU_ISSET(cpu, set)	CPUMASK_TESTBIT(*set, cpu)

#define	CPU_COUNT(set)				\
	(__builtin_popcountl((set)->ary[0]) +	\
	 __builtin_popcountl((set)->ary[1]) +	\
	 __builtin_popcountl((set)->ary[2]) +	\
	 __builtin_popcountl((set)->ary[3]))

#define	CPU_AND(dst, set1, set2)		\
do {						\
	if (dst == set1) {			\
		CPUMASK_ANDMASK(*dst, *set2);	\
	} else {				\
		*dst = *set2;			\
		CPUMASK_ANDMASK(*dst, *set1);	\
	}					\
} while (0)

#define	CPU_OR(dst, set1, set2)			\
do {						\
	if (dst == set1) {			\
		CPUMASK_ORMASK(*dst, *set2);	\
	} else {				\
		*dst = *set2;			\
		CPUMASK_ORMASK(*dst, *set1);	\
	}					\
} while (0)

#define	CPU_XOR(dst, set1, set2)		\
do {						\
	if (dst == set1) {			\
		CPUMASK_XORMASK(*dst, *set2);	\
	} else {				\
		*dst = *set2;			\
		CPUMASK_XORMASK(*dst, *set1);	\
	}					\
} while (0)

#define	CPU_EQUAL(set1, set2)	CPUMASK_CMPMASKEQ(*set1, *set2)
#endif /* __BSD_VISIBLE */

__BEGIN_DECLS
int sched_setparam(pid_t, const struct sched_param *);
int sched_getparam(pid_t, struct sched_param *);

int sched_setscheduler(pid_t, int, const struct sched_param *);
int sched_getscheduler(pid_t);

int sched_yield(void);
int sched_get_priority_max(int);
int sched_get_priority_min(int);
int sched_rr_get_interval(pid_t, struct timespec *);

#if __BSD_VISIBLE
int sched_setaffinity(pid_t, size_t, const cpu_set_t *);
int sched_getaffinity(pid_t, size_t, cpu_set_t *);

int sched_getcpu(void);
#endif
__END_DECLS

#endif

#endif /* _SCHED_H_ */
