/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

/*
 * Collects general statistics on a 10-second interval.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/kinfo.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/spinlock.h>
#include <sys/kcollect.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <machine/stdarg.h>
#include <machine/smp.h>
#include <machine/clock.h>

static uint32_t kcollect_samples = -1;	/* 0 to disable */
TUNABLE_INT("kern.collect_samples", &kcollect_samples);
SYSCTL_UINT(_kern, OID_AUTO, collect_samples, CTLFLAG_RD,
	&kcollect_samples, 0, "number of 10-second samples");

static uint64_t kcollect_index;
static const char *kcollect_slots[KCOLLECT_ENTRIES];
static kcallback_t kcollect_callback[KCOLLECT_ENTRIES];
static kcollect_t kcollect_scale;
static kcollect_t *kcollect_ary;
static struct lock kcollect_lock = LOCK_INITIALIZER("kcolk", 0, 0);

MALLOC_DEFINE(M_KCOLLECT, "kcollect", "kcollect array");

int
kcollect_register(int which, const char *id, kcallback_t func, uint64_t scale)
{
	int n;

	lockmgr(&kcollect_lock, LK_EXCLUSIVE);
	if (which >= 0) {
		n = which;
	} else {
		for (n = KCOLLECT_DYNAMIC_START; n < KCOLLECT_ENTRIES; ++n) {
			if (kcollect_slots[n] == NULL)
				break;
		}
	}
	if (n < 0 || n >= KCOLLECT_ENTRIES) {
		n = -1;
	} else {
		kcollect_slots[n] = id;
		kcollect_callback[n] = func;
		kcollect_scale.data[n] = scale;
	}
	lockmgr(&kcollect_lock, LK_RELEASE);

	return n;
}

void
kcollect_unregister(int n)
{
	lockmgr(&kcollect_lock, LK_EXCLUSIVE);
	if (n >= 0 && n < KCOLLECT_ENTRIES) {
		kcollect_slots[n] = NULL;
		kcollect_callback[n] = NULL;
	}
	lockmgr(&kcollect_lock, LK_RELEASE);
}

/*
 * Typically called by a rollup function in the callback from the
 * collection thread.  Not usually called ad-hoc.  This allows a
 * subsystem to register several collection ids but only one callback
 * which populates all of them.
 */
void
kcollect_setvalue(int n, uint64_t value)
{
	uint32_t i;

	if (n >= 0 && n < KCOLLECT_ENTRIES) {
		i = kcollect_index % kcollect_samples;
		kcollect_ary[i].data[n] = value;
	}
}

/*
 * Callback to change scale adjustment, if necessary.  Certain statistics
 * have scale info available (such as KCOLLECT_SWAPANO and SWAPCAC).
 */
void
kcollect_setscale(int n, uint64_t value)
{
	if (n >= 0 && n < KCOLLECT_ENTRIES) {
		kcollect_scale.data[n] = value;
	}
}

static
void
kcollect_thread(void *dummy)
{
	uint32_t i;
	int n;

	for (;;) {
		lockmgr(&kcollect_lock, LK_EXCLUSIVE);
		i = kcollect_index % kcollect_samples;
		bzero(&kcollect_ary[i], sizeof(kcollect_ary[i]));
		crit_enter();
		kcollect_ary[i].ticks = ticks;
		getmicrotime(&kcollect_ary[i].realtime);
		crit_exit();
		for (n = 0; n < KCOLLECT_ENTRIES; ++n) {
			if (kcollect_callback[n]) {
				kcollect_ary[i].data[n] =
					kcollect_callback[n](n);
			}
		}
		cpu_sfence();
		++kcollect_index;
		lockmgr(&kcollect_lock, LK_RELEASE);
		tsleep(&dummy, 0, "sleep", hz * KCOLLECT_INTERVAL);
	}
}

/*
 * No requirements.
 */
static int
sysctl_kcollect_data(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t i;
	uint32_t start;
	uint64_t count;
	kcollect_t scale;
	kcollect_t id;

	if (kcollect_samples == (uint32_t)-1 ||
	    kcollect_samples == 0) {
		return EINVAL;
	}

	error = 0;
	count = kcollect_index;
	start = count % kcollect_samples;
	if (count >= kcollect_samples)
		count = kcollect_samples - 1;

	/*
	 * Sizing request
	 */
	if (req->oldptr == NULL) {
		error = SYSCTL_OUT(req, 0, sizeof(kcollect_t) * (count + 2));
		return error;
	}

	/*
	 * Output request.  We output a scale record, a string record, and
	 * N collection records.  The strings in the string record can be
	 * up to 8 characters long, and if a string is 8 characters long it
	 * will not be zero-terminated.
	 *
	 * The low byte of the scale record specifies the format.  To get
	 * the scale value shift right by 8.
	 */
	if (kcollect_ary == NULL)
		return ENOTSUP;

	lockmgr(&kcollect_lock, LK_EXCLUSIVE);
	scale = kcollect_scale;
	scale.ticks = ticks;
	scale.hz = hz;

	bzero(&id, sizeof(id));
	for (i = 0; i < KCOLLECT_ENTRIES; ++i) {
		if (kcollect_slots[i]) {
			char *ptr = (char *)&id.data[i];
			size_t len = strlen(kcollect_slots[i]);
			if (len > sizeof(id.data[0]))
				len = sizeof(id.data[0]);
			bcopy(kcollect_slots[i], ptr, len);
		}
	}
	lockmgr(&kcollect_lock, LK_RELEASE);

	error = SYSCTL_OUT(req, &scale, sizeof(scale));
	if (error == 0)
		error = SYSCTL_OUT(req, &id, sizeof(id));

	/*
	 * Start at the current entry (not yet populated) and work
	 * backwards.  This allows callers of the sysctl to acquire
	 * a lesser amount of data aligned to the most recent side of
	 * the array.
	 */
	i = start;
	while (count) {
		if (req->oldlen - req->oldidx < sizeof(kcollect_t))
			break;
		if (i == 0)
			i = kcollect_samples - 1;
		else
			--i;
		error = SYSCTL_OUT(req, &kcollect_ary[i], sizeof(kcollect_t));
		if (error)
			break;
		--count;
	}
	return error;
}
SYSCTL_PROC(_kern, OID_AUTO, collect_data,
	CTLFLAG_RD | CTLTYPE_STRUCT, 0, 0,
	sysctl_kcollect_data, "S,kcollect", "Dump collected statistics");

static
void
kcollect_thread_init(void)
{
	thread_t td = NULL;

	/*
	 * Autosize sample retention (10 second interval)
	 */
	if ((int)kcollect_samples < 0) {
		if (kmem_lim_size() < 1024)
			kcollect_samples = 1024;
		else
			kcollect_samples = 8192;
	}

	if (kcollect_samples) {
		kcollect_ary = kmalloc(kcollect_samples * sizeof(kcollect_t),
				       M_KCOLLECT, M_WAITOK | M_ZERO);
		lwkt_create(kcollect_thread, NULL, &td, NULL, 0, 0, "kcollect");
	}
}
SYSINIT(kcol, SI_SUB_HELPER_THREADS, SI_ORDER_ANY, kcollect_thread_init, 0);
