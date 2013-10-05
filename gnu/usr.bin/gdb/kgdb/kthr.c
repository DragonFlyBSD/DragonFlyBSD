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
 * $FreeBSD: src/gnu/usr.bin/gdb/kgdb/kthr.c,v 1.12 2008/05/01 20:36:48 jhb Exp $
 */

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
#include <string.h>

#include <defs.h>
#include <frame-unwind.h>
#include <inferior.h>

#include "kgdb.h"

static CORE_ADDR dumppcb;
static CORE_ADDR dumptid;

static struct kthr *first;
struct kthr *curkthr;

#define LIVESYS_DUMPTID	10

CORE_ADDR
kgdb_lookup(const char *sym)
{
	struct nlist nl[2];

	nl[0].n_name = (char *)(CORE_ADDR)sym;
	nl[1].n_name = NULL;
	if (kvm_nlist(kvm, nl) != 0)
		return (0);
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
	struct thread td;
	struct lwp lwp;
	struct mdglobaldata gd;
	struct kthr *kt;
	CORE_ADDR addr, paddr, prvspace;
	int cpu, ncpus;

	while (first != NULL) {
		kt = first;
		first = kt->next;
		free(kt);
	}

	addr = kgdb_lookup("_ncpus");
	if (addr == 0)
		return (NULL);
	kvm_read(kvm, addr, &ncpus, sizeof(ncpus));

	dumppcb = kgdb_lookup("_dumppcb");
	if (dumppcb == 0)
		return (NULL);

	prvspace = kgdb_lookup("_CPU_prvspace");
	if (prvspace == 0)
		return (NULL);

	addr = kgdb_lookup("_dumpthread");
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
		 * or if we are live.  Find out by querying "dumping".
		 */
		int dumping = 0;

		addr = kgdb_lookup("_dumping");
		kvm_read(kvm, addr, &dumping, sizeof(dumping));
		if (dumping) {
			kvm_read(kvm, prvspace +
				 offsetof(struct privatespace, mdglobaldata),
				 &gd, sizeof(struct mdglobaldata));
			dumptid = (CORE_ADDR)gd.mi.gd_curthread;
		} else {
			/* We must be a live system */
			dumptid = LIVESYS_DUMPTID;
		}
	}

	for (cpu = 0; cpu < ncpus; cpu++) {
		kvm_read(kvm, prvspace +
			 cpu * sizeof(struct privatespace) +
			 offsetof(struct privatespace, mdglobaldata),
			 &gd, sizeof(struct mdglobaldata));

		addr = (uintptr_t)TAILQ_FIRST(&gd.mi.gd_tdallq);
		while (addr != 0) {
			if (kvm_read(kvm, addr, &td, sizeof(td)) != sizeof(td)) {
				warnx("kvm_read: %s, while accessing thread",
				      kvm_geterr(kvm));
				break;
			}
			kt = malloc(sizeof(*kt));
			kt->next = first;
			kt->kaddr = addr;
			kt->tid = addr;
			kt->pcb = (kt->tid == dumptid) ? dumppcb :
			    (uintptr_t)td.td_pcb;
			kt->kstack = (uintptr_t)td.td_kstack;
			if (td.td_proc != NULL) {
				paddr = (uintptr_t)td.td_proc;
				if (kvm_read(kvm, paddr, &p, sizeof(p)) != sizeof(p))
					warnx("kvm_read: %s", kvm_geterr(kvm));
				kt->pid = p.p_pid;
				kt->paddr = paddr;
				addr = (uintptr_t)td.td_lwp;
				if (kvm_read(kvm, addr, &lwp, sizeof(lwp)) != sizeof(lwp))
					warnx("kvm_read: %s", kvm_geterr(kvm));
				kt->lwpid = lwp.lwp_tid;
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
				 * kt->pcb == 0 is a marker for
				 * "non-dumping kernel thread".
				 */
				if (kt->tid != dumptid)
					kt->pcb = 0;
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
kgdb_thr_lookup_tid(CORE_ADDR tid)
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
kgdb_thr_extra_thread_info(CORE_ADDR tid)
{
#if 0 /* Information already provided */
	struct kthr *kt;
	static char buf[64];
	struct proc *p;
	char comm[MAXCOMLEN + 1];

	kt = kgdb_thr_lookup_tid(tid);
	if (kt == NULL)
		return (NULL);

	snprintf(buf, sizeof(buf), "PID=%d", kt->pid);
	p = (struct proc *)kt->paddr;
	if (kvm_read(kvm, (uintptr_t)&p->p_comm[0], &comm, sizeof(comm)) ==
		sizeof(comm)) {
		strlcat(buf, ": ", sizeof(buf));
		strlcat(buf, comm, sizeof(buf));
	}
	return (buf);
#endif
	return (NULL);
}

char *
kgdb_thr_pid_to_str(ptid_t ptid)
{
	char comm[MAXCOMLEN + 1];
	struct kthr *kt;
	struct proc *p;
	struct thread *t;
	static char buf[64];
	CORE_ADDR tid;

	tid = ptid_get_tid(ptid);
	if (tid == 0)
		kt = kgdb_thr_lookup_pid(ptid_get_pid(ptid));
	else
		kt = kgdb_thr_lookup_tid(tid);

	if (kt == NULL)
		return (NULL);

	buf[0] = 0;

	if (kt->pid != -2) {
		snprintf(buf, sizeof(buf), "pid %d", kt->pid);

		if (tid != 0)
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
				 "/%ld", kt->lwpid);

		p = (struct proc *)kt->paddr;
		if (kvm_read(kvm, (uintptr_t)&p->p_comm[0], &comm, sizeof(comm)) !=
		    sizeof(comm))
			return (buf);

		strlcat(buf, ", ", sizeof(buf));
		strlcat(buf, comm, sizeof(buf));
	} else {
		strcpy(buf, "kernel");

		if (tid != 0) {
			t = (struct thread *)kt->kaddr;
			if (kvm_read(kvm, (uintptr_t)&t->td_comm[0], &comm,
			    sizeof(comm)) == sizeof(comm)) {
				strlcat(buf, " ", sizeof(buf));
				strlcat(buf, comm, sizeof(buf));
			}
		}
	}

	return (buf);
}
