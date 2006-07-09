/*
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/gnu/usr.bin/gdb/kgdb/kthr.c,v 1.3 2005/09/10 18:25:53 marcel Exp $
 * $DragonFly: src/gnu/usr.bin/gdb/kgdb/kthr.c,v 1.2 2006/07/09 01:38:57 corecode Exp $
 */

#define _KERNEL_STRUCTURES

#include <sys/cdefs.h>

#include <sys/param.h>
#include <machine/globaldata.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <err.h>
#include <inttypes.h>
#include <kvm.h>
#include <stdio.h>
#include <stdlib.h>

#include <defs.h>
#include <frame-unwind.h>

#include "kgdb.h"

static uintptr_t dumppcb;
static int dumptid;

static struct kthr *first;
struct kthr *curkthr;

static uintptr_t
lookup(const char *sym)
{
	struct nlist nl[2];

	nl[0].n_name = (char *)(uintptr_t)sym;
	nl[1].n_name = NULL;
	if (kvm_nlist(kvm, nl) != 0) {
		warnx("kvm_nlist(%s): %s", sym, kvm_geterr(kvm));
		return (0);
	}
	return (nl[0].n_value);
}

struct kthr *
kgdb_thr_first(void)
{
	return (first);
}

struct kthr *
kgdb_thr_init(void)
{
	struct proc p;
	struct lwp lwp;
	struct thread td;
	struct mdglobaldata gd;
	struct kthr *kt;
	uintptr_t addr, paddr, prvspace;
	int cpu, ncpus;

	addr = lookup("_ncpus");
	if (addr == 0)
		return (NULL);
	kvm_read(kvm, addr, &ncpus, sizeof(ncpus));

	dumppcb = lookup("_dumppcb");
	if (dumppcb == 0)
		return (NULL);

	prvspace = lookup("CPU_prvspace");
	if (prvspace == 0)
		return (NULL);

	addr = lookup("_dumpthread");
	if (addr != 0) {
		kvm_read(kvm, addr, &dumptid, sizeof(dumptid));
	} else {
		/*
		 * XXX Well then.  We don't know who dumped us.
		 * We could do some fancy stack matching, but
		 * I doubt this will work.  For now just use
		 * cpu0's curthread.
		 *
		 * Actually we don't even know if we were dumped
		 * or if we are life.  Find out by querying "dumping".
		 */
		int dumping = 0;

		addr = lookup("_dumping");
		kvm_read(kvm, addr, &dumping, sizeof(dumping));
		if (dumping) {
			kvm_read(kvm, prvspace +
				 offsetof(struct privatespace, mdglobaldata),
				 &gd, sizeof(struct mdglobaldata));
			dumptid = gd.mi.gd_curthread;
		} else {
			/* We must be a live system */
			dumptid = -1;
		}
	}

	for (cpu = 0; cpu < ncpus; cpu++) {
		kvm_read(kvm, prvspace +
			 cpu * sizeof(struct privatespace) +
			 offsetof(struct privatespace, mdglobaldata),
			 &gd, sizeof(struct mdglobaldata));

		addr = (uintptr_t)TAILQ_FIRST(&gd.mi.gd_tdallq);
		while (addr != 0) {
			if (kvm_read(kvm, addr, &td, sizeof(td)) != sizeof(td))
				warnx("kvm_read: %s", kvm_geterr(kvm));
			kt = malloc(sizeof(*kt));
			kt->next = first;
			kt->kaddr = addr;
			kt->tid = addr;		/* XXX do we have tids? */
			kt->pcb = (kt->tid == dumptid) ? dumppcb :
			    (uintptr_t)td.td_pcb;
			kt->kstack = (uintptr_t)td.td_kstack;
			if (td.td_proc != NULL) {
				paddr = (uintptr_t)td.td_proc;
				if (kvm_read(kvm, paddr, &p, sizeof(p)) != sizeof(p))
					warnx("kvm_read: %s", kvm_geterr(kvm));
				kt->pid = p.p_pid;
				kt->paddr = paddr;
			} else {
				/*
				 * XXX for some stupid reason, gdb uses pid == -1
				 * as a marker for "dead" threads, so we have to
				 * hook all kernel threads on a different pid :/
				 */
				kt->pid = -2;
				kt->paddr = 0;
				/*
				 * We are a kernel thread, so our td_pcb is
				 * not used anyways.  An exception is the
				 * dumping thread.
				 * kt->pcb == NULL is a marker for
				 * "non-dumping kernel thread".
				 */
				if (kt->tid != dumptid)
					kt->pcb = NULL;
			}
			first = kt;
			addr = (uintptr_t)TAILQ_NEXT(&td, td_allq);
		}
	}

	curkthr = kgdb_thr_lookup_tid(dumptid);
	if (curkthr == NULL)
		curkthr = first;
	return (first);
}

struct kthr *
kgdb_thr_lookup_tid(int tid)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->tid != tid)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_lookup_taddr(uintptr_t taddr)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->kaddr != taddr)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_lookup_pid(int pid)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->pid != pid)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_lookup_paddr(uintptr_t paddr)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->paddr != paddr)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_next(struct kthr *kt)
{
	return (kt->next);
}

struct kthr *
kgdb_thr_select(struct kthr *kt)
{
	struct kthr *pcur;

	pcur = curkthr;
	curkthr = kt;
	return (pcur);
}

char *
kgdb_thr_extra_thread_info(int tid)
{
	struct kthr *kt;
	static char comm[MAXCOMLEN + 1];

	kt = kgdb_thr_lookup_tid(tid);
	if (kt == NULL)
		return (NULL);
	if (kvm_read(kvm, kt->kaddr + offsetof(struct thread, td_comm), &comm,
	    sizeof(comm)) != sizeof(comm))
		return (NULL);

	return (comm);
}
