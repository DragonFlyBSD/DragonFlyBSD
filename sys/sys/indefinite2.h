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
#ifndef _SYS_INDEFINITE2_H_
#define _SYS_INDEFINITE2_H_

/*
 * Indefinite info collection and handling code for contention loops
 */
#ifndef _SYS_INDEFINITE_H_
#include <sys/indefinite.h>
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif

/*
 * Initialize the indefinite state (only if the TSC is supported)
 */
static __inline void
indefinite_init(indefinite_info_t *info, const char *ident, char now, char type)
{
	info->ident = ident;
	info->secs = 0;
	info->count = 0;
	info->reported = now;

	if (tsc_frequency) {
		info->type = type;
		/* info->base = rdtsc(); (see indefinite_check()) */
	} else {
		info->type = 0;
		info->base = 0;
	}
	if (now && info->ident) {
		mycpu->gd_cnt.v_lock_name[0] = info->type;
		strncpy(mycpu->gd_cnt.v_lock_name + 1, info->ident,
			sizeof(mycpu->gd_cnt.v_lock_name) - 2);
	}
}

/*
 * Update the state during any loop, record collision time in microseconds.
 */
static __inline int
indefinite_check(indefinite_info_t *info)
{
	tsc_uclock_t delta;
	const char *str;
	int doreport;

#ifdef _KERNEL_VIRTUAL
	vkernel_yield();
#else
	cpu_pause();
#endif
	if (info->type == 0)
		return FALSE;
	if (info->count == INDEF_INFO_START) {	/* start recording time */
		if (indefinite_uses_rdtsc)
			info->base = rdtsc();
		else
			info->base = ticks;
		if (info->reported == 0 && info->ident) {
			mycpu->gd_cnt.v_lock_name[0] = info->type;
			strncpy(mycpu->gd_cnt.v_lock_name + 1, info->ident,
				sizeof(mycpu->gd_cnt.v_lock_name) - 2);
			info->reported = 1;
		}
	}
	if ((++info->count & 127) != 127)
		return FALSE;
	info->count = 128;
	if (indefinite_uses_rdtsc)
		delta = rdtsc() - info->base;
	else
		delta = ticks - info->base;

#if defined(INVARIANTS)
	if (lock_test_mode > 0) {
		--lock_test_mode;
		print_backtrace(8);
	}
#endif

	/*
	 * Ignore minor one-second interval error accumulation in
	 * favor of ensuring that info->base is fully synchronized.
	 */
	doreport = 0;
	if (indefinite_uses_rdtsc) {
		if (delta >= tsc_frequency) {
			info->secs += delta / tsc_frequency;
			info->base += delta;
			mycpu->gd_cnt.v_lock_colls += 1000000U;
			doreport = 1;
		}
	} else {
		if (delta >= hz) {
			info->secs += delta / hz;
			info->base += delta;
			mycpu->gd_cnt.v_lock_colls += 1000000U;
			doreport = 1;
		}
	}
	if (doreport) {
		switch(info->type) {
		case 's':
			str = "spin_lock_sh";
			break;
		case 'S':
			str = "spin_lock_ex";
			break;
		case 'm':
			str = "mutex_sh";
			break;
		case 'M':
			str = "mutex_ex";
			break;
		case 'l':
			str = "lock_sh";
			break;
		case 'L':
			str = "lock_ex";
			break;
		case 't':
			str = "token";
			break;
		default:
			str = "lock(?)";
			break;
		}
		kprintf("%s: %s, indefinite wait (%d secs)!\n",
			str, info->ident, info->secs);
		if (panicstr)
			return TRUE;
#if defined(INVARIANTS)
		if (lock_test_mode) {
			print_backtrace(-1);
			return TRUE;
		}
#endif
#if defined(INVARIANTS)
		if (info->secs == 11 &&
		    (info->type == 's' || info->type == 'S')) {
			print_backtrace(-1);
		}
#endif
		if (info->secs == 60 &&
		    (info->type == 's' || info->type == 'S')) {
			panic("%s: %s, indefinite wait!", str, info->ident);
		}

	}
	return FALSE;
}

/*
 * Finalize the state, record collision time in microseconds if
 * we got past the initial load.
 */
static __inline void
indefinite_done(indefinite_info_t *info)
{
	tsc_uclock_t delta;
	globaldata_t gd;

	if (info->type && info->count > INDEF_INFO_START) {
		gd = mycpu;
		if (indefinite_uses_rdtsc) {
			delta = rdtsc() - info->base;
			delta = delta * 1000000U / tsc_frequency;
			gd->gd_cnt.v_lock_colls += delta;
		} else {
			delta = ticks - info->base;
			delta = delta * 1000000U / hz;
			gd->gd_cnt.v_lock_colls += delta;
		}
	}
	info->type = 0;
}

#endif
