/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 The DragonFly Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#ifndef _SYS__CLOCK_ID_H_
#define	_SYS__CLOCK_ID_H_

#if !defined(CLOCK_REALTIME) && __POSIX_VISIBLE >= 199309
#define	CLOCK_REALTIME		0
#if __BSD_VISIBLE
#define	CLOCK_VIRTUAL		1
#define	CLOCK_PROF		2
#endif
#define	CLOCK_MONOTONIC		4
#define	CLOCK_UPTIME		5	/* FreeBSD-specific. */
#define	CLOCK_UPTIME_PRECISE	7	/* FreeBSD-specific. */
#define	CLOCK_UPTIME_FAST	8	/* FreeBSD-specific. */
#define	CLOCK_REALTIME_PRECISE	9	/* FreeBSD-specific. */
#define	CLOCK_REALTIME_FAST	10	/* FreeBSD-specific. */
#define	CLOCK_MONOTONIC_PRECISE	11	/* FreeBSD-specific. */
#define	CLOCK_MONOTONIC_FAST	12	/* FreeBSD-specific. */
#define	CLOCK_SECOND		13	/* FreeBSD-specific. */
#define	CLOCK_THREAD_CPUTIME_ID	14
#define	CLOCK_PROCESS_CPUTIME_ID	15
#endif /* !defined(CLOCK_REALTIME) && __POSIX_VISIBLE >= 199309 */

#if !defined(TIMER_ABSTIME) && __POSIX_VISIBLE >= 199309
#if __BSD_VISIBLE
#define	TIMER_RELTIME	0x0	/* relative timer */
#endif
#define	TIMER_ABSTIME	0x1	/* absolute timer */
#endif /* !defined(TIMER_ABSTIME) && __POSIX_VISIBLE >= 199309 */

#endif
