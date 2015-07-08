/*-
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define	ELAPSED(elapsed, limit)		(abs((elapsed) - (limit)) < 100)

#define	TEST_PATH	"/posixsem_regression_test"

extern sem_t *alarm_id;
extern int alarm_errno;
extern int alarm_handler_installed;

/* Cut and pasted from kernel header, bah! */

/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timespecadd(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec += (uvp)->tv_sec;				\
		(vvp)->tv_nsec += (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec >= 1000000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_nsec -= 1000000000;			\
		}							\
	} while (0)
#define timespecsub(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)


/* Macros for passing child status to parent over a pipe. */
#define	CSTAT(class, error)		((class) << 16 | (error))
#define	CSTAT_CLASS(stat)		((stat) >> 16)
#define	CSTAT_ERROR(stat)		((stat) & 0xffff)

int checkvalue(sem_t *, int);
sem_t *construct_shared_unnamed_sem(unsigned int);
void destruct_shared_unnamed_sem(sem_t *);
int testwait(sem_t *, u_int *);
int timedwait(sem_t *, u_int, u_int *, int);
int schedule_post(sem_t *, u_int);
int check_alarm(int);
int child_worker(int (*)(void *), void *, int *);
int wait_twoproc_child(void *);
int sem_open_should_fail(const char *, int, mode_t, unsigned int, int);
int sem_init_should_fail(unsigned int, int);
int sem_unlink_should_fail(const char *, int);
int sem_destroy_should_fail(sem_t *, int);
int sem_close_should_fail(sem_t *, int);

#endif
