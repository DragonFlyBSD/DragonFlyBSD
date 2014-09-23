/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/kern/kern_iosched.c,v 1.1 2008/06/28 17:59:49 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <machine/cpu.h>
#include <sys/spinlock.h>
#include <sys/iosched.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/limits.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

SYSCTL_NODE(, OID_AUTO, iosched, CTLFLAG_RW, 0, "I/O Scheduler");

static int iosched_debug = 0;
SYSCTL_INT(_iosched, OID_AUTO, debug, CTLFLAG_RW, &iosched_debug, 0, "");

static struct iosched_data	ioscpu[SMP_MAXCPU];

/*
 * MPSAFE
 */
static int
badjiosched(thread_t td, size_t bytes)
{
	globaldata_t gd = mycpu;
	size_t iostotal;
	int factor;
	int i;
	int delta;

	iostotal = 0;
	for (i = 0; i < ncpus; ++i)
		iostotal += ioscpu[i].iowbytes;
	if (SIZE_T_MAX / SMP_MAXCPU - td->td_iosdata.iowbytes < bytes)
		bytes = SIZE_T_MAX / SMP_MAXCPU - td->td_iosdata.iowbytes;
	td->td_iosdata.iowbytes += bytes;
	ioscpu[gd->gd_cpuid].iowbytes += bytes;
	iostotal += bytes;
	delta = ticks - td->td_iosdata.lastticks;
	if (delta) {
		td->td_iosdata.lastticks = ticks;
		if (delta < 0 || delta > hz * 10)
			delta = hz * 10;
		/* be careful of interger overflows */
		bytes = (int64_t)td->td_iosdata.iowbytes * delta / (hz * 10);
		td->td_iosdata.iowbytes -= bytes;
		ioscpu[gd->gd_cpuid].iowbytes -= bytes;
		iostotal -= bytes;
	}

	/* be careful of interger overflows */
	if (iostotal > 0)
		factor = (int64_t)td->td_iosdata.iowbytes * 100 / iostotal;
	else
		factor = 50;

	if (delta && (iosched_debug & 1)) {
		kprintf("proc %12s (%-5d) factor %3d (%zd/%zd)\n",
			td->td_comm,
			(td->td_lwp ? (int)td->td_lwp->lwp_proc->p_pid : -1),
			factor, td->td_iosdata.iowbytes, iostotal);
	}
	return (factor);
}

void
biosched_done(thread_t td)
{
	globaldata_t gd = mycpu;
	size_t bytes;

	if ((bytes = td->td_iosdata.iowbytes) != 0) {
		td->td_iosdata.iowbytes = 0;
		ioscpu[gd->gd_cpuid].iowbytes -= bytes;
	}
}

/*
 * Caller intends to write (bytes)
 *
 * MPSAFE
 */
void
bwillwrite(int bytes)
{
	long count;
	long factor;

	count = bd_heatup();
	if (count > 0) {
		/* be careful of interger overflows */
		factor = badjiosched(curthread, (size_t)bytes);
		count = hidirtybufspace / 100 * factor;
		bd_wait(count);
	}
}

/*
 * Caller intends to read (bytes)
 *
 * MPSAFE
 */
void
bwillread(int bytes)
{
}

/*
 * Call intends to do an inode-modifying operation of some sort.
 *
 * MPSAFE
 */
void
bwillinode(int n)
{
	long count;
	long factor;

	count = bd_heatup();
	if (count > 0) {
		factor = badjiosched(curthread, PAGE_SIZE);
		count = count * factor / 100;
		bd_wait(count);
	}
}
