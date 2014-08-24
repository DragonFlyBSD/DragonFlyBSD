/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
/*
 * The following copyright applies to the DDB command code:
 *
 * Copyright (c) 2000 John Baldwin <jhb@FreeBSD.org>
 * All rights reserved.
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

/*
 * Kernel tracepoint facility.
 */

#include "opt_ddb.h"
#include "opt_ktr.h"

#include <sys/param.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/spinlock.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/ctype.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/md_var.h>

#include <ddb/ddb.h>

#ifndef KTR_ENTRIES
#define	KTR_ENTRIES		2048
#elif (KTR_ENTRIES & KTR_ENTRIES - 1)
#error KTR_ENTRIES must be a power of two
#endif
#define KTR_ENTRIES_MASK	(KTR_ENTRIES - 1)

/*
 * test logging support.  When ktr_testlogcnt is non-zero each synchronization
 * interrupt will issue six back-to-back ktr logging messages on cpu 0
 * so the user can determine KTR logging overheads.
 */
#if !defined(KTR_TESTLOG)
#define KTR_TESTLOG	KTR_ALL
#endif
KTR_INFO_MASTER(testlog);
#if KTR_TESTLOG
KTR_INFO(KTR_TESTLOG, testlog, test1, 0, "test1 %d %d %d %d", int dummy1, int dummy2, int dummy3, int dummy4);
KTR_INFO(KTR_TESTLOG, testlog, test2, 1, "test2 %d %d %d %d", int dummy1, int dummy2, int dummy3, int dummy4);
KTR_INFO(KTR_TESTLOG, testlog, test3, 2, "test3 %d %d %d %d", int dummy1, int dummy2, int dummy3, int dummy4);
KTR_INFO(KTR_TESTLOG, testlog, test4, 3, "test4");
KTR_INFO(KTR_TESTLOG, testlog, test5, 4, "test5");
KTR_INFO(KTR_TESTLOG, testlog, test6, 5, "test6");
KTR_INFO(KTR_TESTLOG, testlog, pingpong, 6, "pingpong");
KTR_INFO(KTR_TESTLOG, testlog, pipeline, 7, "pipeline");
KTR_INFO(KTR_TESTLOG, testlog, crit_beg, 8, "crit_beg");
KTR_INFO(KTR_TESTLOG, testlog, crit_end, 9, "crit_end");
KTR_INFO(KTR_TESTLOG, testlog, spin_beg, 10, "spin_beg");
KTR_INFO(KTR_TESTLOG, testlog, spin_end, 11, "spin_end");
#define logtest(name)	KTR_LOG(testlog_ ## name, 0, 0, 0, 0)
#define logtest_noargs(name)	KTR_LOG(testlog_ ## name)
#endif

MALLOC_DEFINE(M_KTR, "ktr", "ktr buffers");

SYSCTL_NODE(_debug, OID_AUTO, ktr, CTLFLAG_RW, 0, "ktr");

int		ktr_entries = KTR_ENTRIES;
SYSCTL_INT(_debug_ktr, OID_AUTO, entries, CTLFLAG_RD, &ktr_entries, 0,
    "Size of the event buffer");

int		ktr_version = KTR_VERSION;
SYSCTL_INT(_debug_ktr, OID_AUTO, version, CTLFLAG_RD, &ktr_version, 0, "");

static int	ktr_stacktrace = 1;
SYSCTL_INT(_debug_ktr, OID_AUTO, stacktrace, CTLFLAG_RD, &ktr_stacktrace, 0, "");

static int	ktr_resynchronize = 0;
SYSCTL_INT(_debug_ktr, OID_AUTO, resynchronize, CTLFLAG_RW,
    &ktr_resynchronize, 0, "Resynchronize TSC 10 times a second");

#if KTR_TESTLOG
static int	ktr_testlogcnt = 0;
SYSCTL_INT(_debug_ktr, OID_AUTO, testlogcnt, CTLFLAG_RW, &ktr_testlogcnt, 0, "");
static int	ktr_testipicnt = 0;
static int	ktr_testipicnt_remainder;
SYSCTL_INT(_debug_ktr, OID_AUTO, testipicnt, CTLFLAG_RW, &ktr_testipicnt, 0, "");
static int	ktr_testcritcnt = 0;
SYSCTL_INT(_debug_ktr, OID_AUTO, testcritcnt, CTLFLAG_RW, &ktr_testcritcnt, 0, "");
static int	ktr_testspincnt = 0;
SYSCTL_INT(_debug_ktr, OID_AUTO, testspincnt, CTLFLAG_RW, &ktr_testspincnt, 0, "");
#endif

/*
 * Give cpu0 a static buffer so the tracepoint facility can be used during
 * early boot (note however that we still use a critical section, XXX).
 */
static struct	ktr_entry ktr_buf0[KTR_ENTRIES];

struct ktr_cpu ktr_cpu[MAXCPU] = {
	{ .core.ktr_buf = &ktr_buf0[0] }
};

static int64_t	ktr_sync_tsc;
struct callout	ktr_resync_callout;

#ifdef KTR_VERBOSE
int	ktr_verbose = KTR_VERBOSE;
TUNABLE_INT("debug.ktr.verbose", &ktr_verbose);
SYSCTL_INT(_debug_ktr, OID_AUTO, verbose, CTLFLAG_RW, &ktr_verbose, 0,
    "Log events to the console as well");
#endif

static void ktr_resync_callback(void *dummy __unused);

extern int64_t tsc_offsets[];

static void
ktr_sysinit(void *dummy)
{
	struct ktr_cpu_core *kcpu;
	int i;

	for(i = 1; i < ncpus; ++i) {
		kcpu = &ktr_cpu[i].core;
		kcpu->ktr_buf = kmalloc(KTR_ENTRIES * sizeof(struct ktr_entry),
					M_KTR, M_WAITOK | M_ZERO);
	}
	callout_init_mp(&ktr_resync_callout);
	callout_reset(&ktr_resync_callout, hz / 10, ktr_resync_callback, NULL);
}
SYSINIT(ktr_sysinit, SI_BOOT2_KLD, SI_ORDER_ANY, ktr_sysinit, NULL);

/*
 * Try to resynchronize the TSC's for all cpus.  This is really, really nasty.
 * We have to send an IPIQ message to all remote cpus, wait until they 
 * get into their IPIQ processing code loop, then do an even stricter hard
 * loop to get the cpus as close to synchronized as we can to get the most
 * accurate reading.
 *
 * This callback occurs on cpu0.
 */
#if KTR_TESTLOG
static void ktr_pingpong_remote(void *dummy);
static void ktr_pipeline_remote(void *dummy);
#endif

#ifdef _RDTSC_SUPPORTED_

static void ktr_resync_remote(void *dummy);

/*
 * We use a callout callback instead of a systimer because we cannot afford
 * to preempt anyone to do this, or we might deadlock a spin-lock or 
 * serializer between two cpus.
 */
static
void 
ktr_resync_callback(void *dummy __unused)
{
	struct lwkt_cpusync cs;
#if KTR_TESTLOG
	int count;
#endif

	KKASSERT(mycpu->gd_cpuid == 0);

#if KTR_TESTLOG
	/*
	 * Test logging
	 */
	if (ktr_testlogcnt) {
		--ktr_testlogcnt;
		cpu_disable_intr();
		logtest(test1);
		logtest(test2);
		logtest(test3);
		logtest_noargs(test4);
		logtest_noargs(test5);
		logtest_noargs(test6);
		cpu_enable_intr();
	}

	/*
	 * Test IPI messaging
	 */
	if (ktr_testipicnt && ktr_testipicnt_remainder == 0 && ncpus > 1) {
		ktr_testipicnt_remainder = ktr_testipicnt;
		ktr_testipicnt = 0;
		lwkt_send_ipiq_bycpu(1, ktr_pingpong_remote, NULL);
	}

	/*
	 * Test critical sections
	 */
	if (ktr_testcritcnt) {
		crit_enter();
		crit_exit();
		logtest_noargs(crit_beg);
		for (count = ktr_testcritcnt; count; --count) {
			crit_enter();
			crit_exit();
		}
		logtest_noargs(crit_end);
		ktr_testcritcnt = 0;
	}

	/*
	 * Test spinlock sections
	 */
	if (ktr_testspincnt) {
		struct spinlock spin;

		spin_init(&spin, "ktrresync");
		spin_lock(&spin);
		spin_unlock(&spin);
		logtest_noargs(spin_beg);
		for (count = ktr_testspincnt; count; --count) {
			spin_lock(&spin);
			spin_unlock(&spin);
		}
		logtest_noargs(spin_end);
		ktr_testspincnt = 0;
	}
#endif

	/*
	 * Resynchronize the TSC
	 */
	if (ktr_resynchronize == 0)
		goto done;
	if ((cpu_feature & CPUID_TSC) == 0)
		return;

	crit_enter();
	lwkt_cpusync_init(&cs, smp_active_mask, ktr_resync_remote,
			  (void *)(intptr_t)mycpu->gd_cpuid);
	lwkt_cpusync_interlock(&cs);
	ktr_sync_tsc = rdtsc();
	lwkt_cpusync_deinterlock(&cs);
	crit_exit();
done:
	callout_reset(&ktr_resync_callout, hz / 10, ktr_resync_callback, NULL);
}

/*
 * The remote-end of the KTR synchronization protocol runs on all cpus.
 * The one we run on the controlling cpu updates its tsc continuously
 * until the others have finished syncing (theoretically), but we don't
 * loop forever.
 *
 * This is a bit ad-hoc but we need to avoid livelocking inside an IPI
 * callback.  rdtsc() is a synchronizing instruction (I think).
 */
static void
ktr_resync_remote(void *arg)
{
	globaldata_t gd = mycpu;
	int64_t delta;
	int i;

	if (gd->gd_cpuid == (int)(intptr_t)arg) {
		for (i = 0; i < 2000; ++i)
			ktr_sync_tsc = rdtsc();
	} else {
		delta = rdtsc() - ktr_sync_tsc;
		if (tsc_offsets[gd->gd_cpuid] == 0)
			tsc_offsets[gd->gd_cpuid] = delta;
		tsc_offsets[gd->gd_cpuid] =
			(tsc_offsets[gd->gd_cpuid] * 7 + delta) / 8;
	}
}

#if KTR_TESTLOG

static
void
ktr_pingpong_remote(void *dummy __unused)
{
	int other_cpu;

	logtest_noargs(pingpong);
	other_cpu = 1 - mycpu->gd_cpuid;
	if (ktr_testipicnt_remainder) {
		--ktr_testipicnt_remainder;
		lwkt_send_ipiq_bycpu(other_cpu, ktr_pingpong_remote, NULL);
	} else {
		lwkt_send_ipiq_bycpu(other_cpu, ktr_pipeline_remote, NULL);
		lwkt_send_ipiq_bycpu(other_cpu, ktr_pipeline_remote, NULL);
		lwkt_send_ipiq_bycpu(other_cpu, ktr_pipeline_remote, NULL);
		lwkt_send_ipiq_bycpu(other_cpu, ktr_pipeline_remote, NULL);
		lwkt_send_ipiq_bycpu(other_cpu, ktr_pipeline_remote, NULL);
	}
}

static
void
ktr_pipeline_remote(void *dummy __unused)
{
	logtest_noargs(pipeline);
}

#endif

#else	/* !_RDTSC_SUPPORTED_ */

/*
 * The resync callback for UP doesn't do anything other then run the test
 * log messages.  If test logging is not enabled, don't bother resetting
 * the callout.
 */
static
void 
ktr_resync_callback(void *dummy __unused)
{
#if KTR_TESTLOG
	/*
	 * Test logging
	 */
	if (ktr_testlogcnt) {
		--ktr_testlogcnt;
		cpu_disable_intr();
		logtest(test1);
		logtest(test2);
		logtest(test3);
		logtest_noargs(test4);
		logtest_noargs(test5);
		logtest_noargs(test6);
		cpu_enable_intr();
	}
	callout_reset(&ktr_resync_callout, hz / 10, ktr_resync_callback, NULL);
#endif
}

#endif

/*
 * Setup the next empty slot and return it to the caller to store the data
 * directly.
 */
struct ktr_entry *
ktr_begin_write_entry(struct ktr_info *info, const char *file, int line)
{
	struct ktr_cpu_core *kcpu;
	struct ktr_entry *entry;
	int cpu;

	cpu = mycpu->gd_cpuid;
	kcpu = &ktr_cpu[cpu].core;
	if (panicstr)			/* stop logging during panic */
		return NULL;
	if (kcpu->ktr_buf == NULL)	/* too early in boot */
		return NULL;

	crit_enter();
	entry = kcpu->ktr_buf + (kcpu->ktr_idx & KTR_ENTRIES_MASK);
	++kcpu->ktr_idx;
#ifdef _RDTSC_SUPPORTED_
	if (cpu_feature & CPUID_TSC) {
		entry->ktr_timestamp = rdtsc() - tsc_offsets[cpu];
	} else
#endif
	{
		entry->ktr_timestamp = get_approximate_time_t();
	}
	entry->ktr_info = info;
	entry->ktr_file = file;
	entry->ktr_line = line;
	crit_exit();
	return entry;
}

int
ktr_finish_write_entry(struct ktr_info *info, struct ktr_entry *entry)
{
	if (ktr_stacktrace)
		cpu_ktr_caller(entry);
#ifdef KTR_VERBOSE
	if (ktr_verbose && info->kf_format) {
		kprintf("cpu%d ", mycpu->gd_cpuid);
		if (ktr_verbose > 1) {
			kprintf("%s.%d\t", entry->ktr_file, entry->ktr_line);
		}
		return !0;
	}
#endif
	return 0;
}

#ifdef DDB

#define	NUM_LINES_PER_PAGE	19

struct tstate {
	int	cur;
	int	first;
};

static	int db_ktr_verbose;
static	int db_mach_vtrace(int cpu, struct ktr_entry *kp, int idx);

DB_SHOW_COMMAND(ktr, db_ktr_all)
{
	struct ktr_cpu_core *kcpu;
	int a_flag = 0;
	int c;
	int nl = 0;
	int i;
	struct tstate tstate[MAXCPU];
	int printcpu = -1;

	for(i = 0; i < ncpus; i++) {
		kcpu = &ktr_cpu[i].core;
		tstate[i].first = -1;
		tstate[i].cur = (kcpu->ktr_idx - 1) & KTR_ENTRIES_MASK;
	}
	db_ktr_verbose = 0;
	while ((c = *(modif++)) != '\0') {
		if (c == 'v') {
			db_ktr_verbose = 1;
		}
		else if (c == 'a') {
			a_flag = 1;
		}
		else if (c == 'c') {
			printcpu = 0;
			while ((c = *(modif++)) != '\0') {
				if (isdigit(c)) {
					printcpu *= 10;
					printcpu += c - '0';
				}
				else {
					modif++;
					break;
				}
			}
			modif--;
		}
	}
	if (printcpu > ncpus - 1) {
		db_printf("Invalid cpu number\n");
		return;
	}
	/*
	 * Lopp throug all the buffers and print the content of them, sorted
	 * by the timestamp.
	 */
	while (1) {
		int counter;
		u_int64_t highest_ts;
		int highest_cpu;
		struct ktr_entry *kp;

		if (a_flag == 1 && cncheckc() != -1)
			return;
		highest_ts = 0;
		highest_cpu = -1;
		/*
		 * Find the lowest timestamp
		 */
		for (i = 0, counter = 0; i < ncpus; i++) {
			kcpu = &ktr_cpu[i].core;
			if (kcpu->ktr_buf == NULL)
				continue;
			if (printcpu != -1 && printcpu != i)
				continue;
			if (tstate[i].cur == -1) {
				counter++;
				if (counter == ncpus) {
					db_printf("--- End of trace buffer ---\n");
					return;
				}
				continue;
			}
			if (kcpu->ktr_buf[tstate[i].cur].ktr_timestamp > highest_ts) {
				highest_ts = kcpu->ktr_buf[tstate[i].cur].ktr_timestamp;
				highest_cpu = i;
			}
		}
		if (highest_cpu < 0) {
			db_printf("no KTR data available\n");
			break;
		}
		i = highest_cpu;
		kcpu = &ktr_cpu[i].core;
		kp = &kcpu->ktr_buf[tstate[i].cur];
		if (tstate[i].first == -1)
			tstate[i].first = tstate[i].cur;
		if (--tstate[i].cur < 0)
			tstate[i].cur = KTR_ENTRIES - 1;
		if (tstate[i].first == tstate[i].cur) {
			db_mach_vtrace(i, kp, tstate[i].cur + 1);
			tstate[i].cur = -1;
			continue;
		}
		if (kcpu->ktr_buf[tstate[i].cur].ktr_info == NULL)
			tstate[i].cur = -1;
		if (db_more(&nl) == -1)
			break;
		if (db_mach_vtrace(i, kp, tstate[i].cur + 1) == 0)
			tstate[i].cur = -1;
	}
}

static int
db_mach_vtrace(int cpu, struct ktr_entry *kp, int idx)
{
	if (kp->ktr_info == NULL)
		return(0);
	db_printf("cpu%d ", cpu);
	db_printf("%d: ", idx);
	if (db_ktr_verbose) {
		db_printf("%10.10lld %s.%d\t", (long long)kp->ktr_timestamp,
		    kp->ktr_file, kp->ktr_line);
	}
	db_printf("%s\t", kp->ktr_info->kf_name);
	db_printf("from(%p,%p) ", kp->ktr_caller1, kp->ktr_caller2);
#ifdef __i386__
	if (kp->ktr_info->kf_format)
		db_vprintf(kp->ktr_info->kf_format, (__va_list)kp->ktr_data);
#endif
	db_printf("\n");

	return(1);
}

#endif	/* DDB */
