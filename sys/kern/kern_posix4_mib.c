/*-
 * Copyright (c) 1998
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
 * $FreeBSD: src/sys/posix4/posix4_mib.c,v 1.3.2.1 2000/08/03 01:09:59 peter Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/posix4.h>

static int facility[CTL_P1003_1B_MAXID - 1];

/* OID_AUTO isn't working with sysconf(3).  I guess I'd have to
 * modify it to do a lookup by name from the index.
 * For now I've left it a top-level sysctl.
 */

SYSCTL_DECL(_p1003_1b);

#define P1B_SYSCTL(num, name, desc)  \
SYSCTL_INT(_p1003_1b, num, \
	name, CTLFLAG_RD, facility + num - 1, 0, desc);

P1B_SYSCTL(CTL_P1003_1B_ASYNCHRONOUS_IO, asynchronous_io,
    "POSIX Asynchronous input/output support");
P1B_SYSCTL(CTL_P1003_1B_MAPPED_FILES, mapped_files,
    "POSIX Memory mapped file support");
P1B_SYSCTL(CTL_P1003_1B_MEMLOCK, memlock,
    "Support for POSIX process memory locking");
P1B_SYSCTL(CTL_P1003_1B_MEMLOCK_RANGE, memlock_range,
    "Support for POSIX range memory locking");
P1B_SYSCTL(CTL_P1003_1B_MEMORY_PROTECTION, memory_protection,
    "POSIX Memory protection support");
P1B_SYSCTL(CTL_P1003_1B_MESSAGE_PASSING, message_passing,
    "Support for POSIX message passing");
P1B_SYSCTL(CTL_P1003_1B_PRIORITIZED_IO, prioritized_io,
    "POSIX prioritized input/output support");
P1B_SYSCTL(CTL_P1003_1B_PRIORITY_SCHEDULING, priority_scheduling,
    "Support for POSIX process scheduling");
P1B_SYSCTL(CTL_P1003_1B_REALTIME_SIGNALS, realtime_signals,
    "POSIX real time signal support");
P1B_SYSCTL(CTL_P1003_1B_SEMAPHORES, semaphores,
    "POSIX semaphores support");
P1B_SYSCTL(CTL_P1003_1B_FSYNC, fsync,
    "Support of POSIX file synchronization");
P1B_SYSCTL(CTL_P1003_1B_SHARED_MEMORY_OBJECTS, shared_memory_objects,
    "Support of POSIX share memory objects");
P1B_SYSCTL(CTL_P1003_1B_SYNCHRONIZED_IO, synchronized_io,
    "POSIX synchronized input/output support");
P1B_SYSCTL(CTL_P1003_1B_TIMERS, timers,
    "POSIX timers support");
P1B_SYSCTL(CTL_P1003_1B_AIO_LISTIO_MAX, aio_listio_max,
    "Maximum number of operations in a single list I/O call");
P1B_SYSCTL(CTL_P1003_1B_AIO_MAX, aio_max,
    "Maximum outstanding AIO operations");
P1B_SYSCTL(CTL_P1003_1B_AIO_PRIO_DELTA_MAX, aio_prio_delta_max,
    "Maximum AIO priority deviation from the own scheduling priority");
P1B_SYSCTL(CTL_P1003_1B_DELAYTIMER_MAX, delaytimer_max,
    "Maximum timer expiration overruns");
P1B_SYSCTL(CTL_P1003_1B_PAGESIZE, pagesize,
    "Size of a page in bytes");
P1B_SYSCTL(CTL_P1003_1B_RTSIG_MAX, rtsig_max,
    "Maximum number of realtime signals reserved for application use");
P1B_SYSCTL(CTL_P1003_1B_SEM_NSEMS_MAX, sem_nsems_max,
    "Maximum number of semaphores per process");
P1B_SYSCTL(CTL_P1003_1B_SIGQUEUE_MAX, sigqueue_max,
    "Maximum number of queued signals per process");
P1B_SYSCTL(CTL_P1003_1B_TIMER_MAX, timer_max,
    "Maximum number of timers per process");

/* p31b_setcfg: Set the configuration
 */
void p31b_setcfg(int num, int value)
{
	if (num >= 1 && num < CTL_P1003_1B_MAXID)
		facility[num - 1] = value;
}
