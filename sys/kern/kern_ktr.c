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
 * $DragonFly: src/sys/kern/kern_ktr.c,v 1.9 2005/12/10 18:16:37 dillon Exp $
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
#include <sys/thread2.h>
#include <sys/ctype.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/md_var.h>

#include <ddb/ddb.h>

#ifndef KTR_ENTRIES
#define	KTR_ENTRIES		2048
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
KTR_INFO(KTR_TESTLOG, testlog, test1, 0, "test1", sizeof(void *) * 4);
KTR_INFO(KTR_TESTLOG, testlog, test2, 1, "test2", sizeof(void *) * 4);
KTR_INFO(KTR_TESTLOG, testlog, test3, 2, "test3", sizeof(void *) * 4);
KTR_INFO(KTR_TESTLOG, testlog, test4, 3, "test4", 0);
KTR_INFO(KTR_TESTLOG, testlog, test5, 4, "test5", 0);
KTR_INFO(KTR_TESTLOG, testlog, test6, 5, "test6", 0);
#define logtest(name)	KTR_LOG(testlog_ ## name, 0, 0, 0, 0)
#define logtest_noargs(name)	KTR_LOG(testlog_ ## name)

MALLOC_DEFINE(M_KTR, "ktr", "ktr buffers");

SYSCTL_NODE(_debug, OID_AUTO, ktr, CTLFLAG_RW, 0, "ktr");

static int32_t	ktr_cpumask = -1;
TUNABLE_INT("debug.ktr.cpumask", &ktr_cpumask);
SYSCTL_INT(_debug_ktr, OID_AUTO, cpumask, CTLFLAG_RW, &ktr_cpumask, 0, "");

static int	ktr_entries = KTR_ENTRIES;
SYSCTL_INT(_debug_ktr, OID_AUTO, entries, CTLFLAG_RD, &ktr_entries, 0, "");

static int	ktr_version = KTR_VERSION;
SYSCTL_INT(_debug_ktr, OID_AUTO, version, CTLFLAG_RD, &ktr_version, 0, "");

static int	ktr_stacktrace = 1;
SYSCTL_INT(_debug_ktr, OID_AUTO, stacktrace, CTLFLAG_RD, &ktr_stacktrace, 0, "");

static int	ktr_resynchronize = 0;
SYSCTL_INT(_debug_ktr, OID_AUTO, resynchronize, CTLFLAG_RW, &ktr_resynchronize, 0, "");

#if KTR_TESTLOG
static int	ktr_testlogcnt = 0;
SYSCTL_INT(_debug_ktr, OID_AUTO, testlogcnt, CTLFLAG_RW, &ktr_testlogcnt, 0, "");
#endif

/*
 * Give cpu0 a static buffer so the tracepoint facility can be used during
 * early boot (note however that we still use a critical section, XXX).
 */
static struct	ktr_entry ktr_buf0[KTR_ENTRIES];
static struct	ktr_entry *ktr_buf[MAXCPU] = { &ktr_buf0[0] };
static int	ktr_idx[MAXCPU];
#ifdef SMP
static int	ktr_sync_state = 0;
static int	ktr_sync_count;
static int64_t	ktr_sync_tsc;
#endif
struct callout	ktr_resync_callout;

#ifdef KTR_VERBOSE
int	ktr_verbose = KTR_VERBOSE;
TUNABLE_INT("debug.ktr.verbose", &ktr_verbose);
SYSCTL_INT(_debug_ktr, OID_AUTO, verbose, CTLFLAG_RW, &ktr_verbose, 0, "");
#endif

#ifdef SMP
int64_t tsc_offsets[MAXCPU];
#else
int64_t tsc_offsets[1];
#endif

static void
ktr_sysinit(void *dummy)
{
	int i;

	for(i = 1; i < ncpus; ++i) {
		ktr_buf[i] = malloc(KTR_ENTRIES * sizeof(struct ktr_entry),
				    M_KTR, M_WAITOK | M_ZERO);
	}
}
SYSINIT(ktr_sysinit, SI_SUB_INTRINSIC, SI_ORDER_FIRST, ktr_sysinit, NULL);

#ifdef SMP

/*
 * Try to resynchronize the TSC's for all cpus.  This is really, really nasty.
 * We have to send an IPIQ message to all remote cpus, wait until they 
 * get into their IPIQ processing code loop, then do an even stricter hard
 * loop to get the cpus as close to synchronized as we can to get the most
 * accurate reading.
 *
 * This callback occurs on cpu0.
 */
static void ktr_resync_callback(void *dummy);
static void ktr_resync_remote(void *dummy);

static void
ktr_resyncinit(void *dummy)
{
	callout_init(&ktr_resync_callout);
	callout_reset(&ktr_resync_callout, hz / 10, ktr_resync_callback, NULL);
}
SYSINIT(ktr_resync, SI_SUB_FINISH_SMP+1, SI_ORDER_ANY, ktr_resyncinit, NULL);

extern cpumask_t smp_active_mask;

/*
 * We use a callout callback instead of a systimer because we cannot afford
 * to preempt anyone to do this, or we might deadlock a spin-lock or 
 * serializer between two cpus.
 */
static
void 
ktr_resync_callback(void *dummy __unused)
{
	int count;

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
#endif

	/*
	 * Resynchronize the TSC
	 */
	if (ktr_resynchronize == 0)
		goto done;
	if ((cpu_feature & CPUID_TSC) == 0)
		return;

	/*
	 * Send the synchronizing IPI and wait for all cpus to get into
	 * their spin loop.  We must process incoming IPIs while waiting
	 * to avoid a deadlock.
	 */
	crit_enter();
	ktr_sync_count = 0;
	ktr_sync_state = 1;
	ktr_sync_tsc = rdtsc();
	count = lwkt_send_ipiq_mask(mycpu->gd_other_cpus & smp_active_mask,
				    (ipifunc1_t)ktr_resync_remote, NULL);
	while (ktr_sync_count != count)
		lwkt_process_ipiq();

	/*
	 * Continuously update the TSC for cpu 0 while waiting for all other
	 * cpus to finish stage 2.
	 */
	cpu_disable_intr();
	ktr_sync_tsc = rdtsc();
	cpu_sfence();
	ktr_sync_state = 2;
	cpu_sfence();
	while (ktr_sync_count != 0) {
		ktr_sync_tsc = rdtsc();
		cpu_lfence();
		cpu_nop();
	}
	cpu_enable_intr();
	crit_exit();
	ktr_sync_state = 0;
done:
	callout_reset(&ktr_resync_callout, hz / 10, ktr_resync_callback, NULL);
}

/*
 * The remote-end of the KTR synchronization protocol runs on all cpus except
 * cpu 0.  Since this is an IPI function, it is entered with the current
 * thread in a critical section.
 */
static void
ktr_resync_remote(void *dummy __unused)
{
	volatile int64_t tsc1 = ktr_sync_tsc;
	volatile int64_t tsc2;

	/*
	 * Inform the master that we have entered our hard loop.
	 */
	KKASSERT(ktr_sync_state == 1);
	atomic_add_int(&ktr_sync_count, 1);
	while (ktr_sync_state == 1) {
		lwkt_process_ipiq();
	}

	/*
	 * Now the master is in a hard loop, synchronize the TSC and
	 * we are done.
	 */
	cpu_disable_intr();
	KKASSERT(ktr_sync_state == 2);
	tsc2 = ktr_sync_tsc;
	if (tsc2 > tsc1)
		tsc_offsets[mycpu->gd_cpuid] = rdtsc() - tsc2;
	atomic_subtract_int(&ktr_sync_count, 1);
	cpu_enable_intr();
}

#else	/* !SMP */

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
 * KTR_WRITE_ENTRY - Primary entry point for kernel trace logging
 */
static __inline
void
ktr_write_entry(struct ktr_info *info, const char *file, int line,
		const void *ptr)
{
	struct ktr_entry *entry;
	int cpu;

	cpu = mycpu->gd_cpuid;
	if (ktr_buf[cpu]) {
		crit_enter();
		entry = ktr_buf[cpu] + (ktr_idx[cpu] & KTR_ENTRIES_MASK);
		++ktr_idx[cpu];
		if (cpu_feature & CPUID_TSC) {
#ifdef SMP
			entry->ktr_timestamp = rdtsc() - tsc_offsets[cpu];
#else
			entry->ktr_timestamp = rdtsc();
#endif
		} else {
			entry->ktr_timestamp = get_approximate_time_t();
		}
		entry->ktr_info = info;
		entry->ktr_file = file;
		entry->ktr_line = line;
		crit_exit();
		if (info->kf_data_size > KTR_BUFSIZE)
			bcopyi(ptr, entry->ktr_data, KTR_BUFSIZE);
		else if (info->kf_data_size)
			bcopyi(ptr, entry->ktr_data, info->kf_data_size);
		if (ktr_stacktrace)
			cpu_ktr_caller(entry);
	}
#ifdef KTR_VERBOSE
	if (ktr_verbose && info->kf_format) {
#ifdef SMP
		printf("cpu%d ", cpu);
#endif
		if (ktr_verbose > 1) {
			printf("%s.%d\t", entry->ktr_file, entry->ktr_line);
		}
		vprintf(info->kf_format, ptr);
		printf("\n");
	}
#endif
}

void
ktr_log(struct ktr_info *info, const char *file, int line, ...)
{
	__va_list va;

	if (panicstr == NULL) {
		__va_start(va, line);
		ktr_write_entry(info, file, line, va);
		__va_end(va);
	}
}

void
ktr_log_ptr(struct ktr_info *info, const char *file, int line, const void *ptr)
{
	if (panicstr == NULL) {
		ktr_write_entry(info, file, line, ptr);
	}
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
	int a_flag = 0;
	int c;
	int nl = 0;
	int i;
	struct tstate tstate[MAXCPU];
	int printcpu = -1;

	for(i = 0; i < ncpus; i++) {
		tstate[i].first = -1;
		tstate[i].cur = ktr_idx[i] & KTR_ENTRIES_MASK;
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
			if (ktr_buf[i] == NULL)
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
			if (ktr_buf[i][tstate[i].cur].ktr_timestamp > highest_ts) {
				highest_ts = ktr_buf[i][tstate[i].cur].ktr_timestamp;
				highest_cpu = i;
			}
		}
		i = highest_cpu;
		KKASSERT(i != -1);
		kp = &ktr_buf[i][tstate[i].cur];
		if (tstate[i].first == -1)
			tstate[i].first = tstate[i].cur;
		if (--tstate[i].cur < 0)
			tstate[i].cur = KTR_ENTRIES - 1;
		if (tstate[i].first == tstate[i].cur) {
			db_mach_vtrace(i, kp, tstate[i].cur + 1);
			tstate[i].cur = -1;
			continue;
		}
		if (ktr_buf[i][tstate[i].cur].ktr_info == NULL)
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
#ifdef SMP
	db_printf("cpu%d ", cpu);
#endif
	db_printf("%d: ", idx);
	if (db_ktr_verbose) {
		db_printf("%10.10lld %s.%d\t", (long long)kp->ktr_timestamp,
		    kp->ktr_file, kp->ktr_line);
	}
	db_printf("%s\t", kp->ktr_info->kf_name);
	db_printf("from(%p,%p) ", kp->ktr_caller1, kp->ktr_caller2);
	if (kp->ktr_info->kf_format) {
		int32_t *args = kp->ktr_data;
		db_printf(kp->ktr_info->kf_format,
			  args[0], args[1], args[2], args[3],
			  args[4], args[5], args[6], args[7],
			  args[8], args[9], args[10], args[11]);
	    
	}
	db_printf("\n");

	return(1);
}

#endif	/* DDB */
