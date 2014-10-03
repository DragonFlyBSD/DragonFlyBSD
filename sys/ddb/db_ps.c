/*-
 * Copyright (c) 1993 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * $FreeBSD: src/sys/ddb/db_ps.c,v 1.20 1999/08/28 00:41:09 peter Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/cons.h>

#include <ddb/ddb.h>

static void db_dump_td_tokens(thread_t td);

void
db_ps(db_expr_t dummy1, boolean_t dummy2, db_expr_t dummy3, char *dummy4)
{
	int np;
	int cpuidx;
	int nl = 0;

	np = nprocs;

#if 0
	if (allproc.lh_first != NULL)
		p = allproc.lh_first;
	else
		p = &proc0;
	lp = FIRST_LWP_IN_PROC(__DEVOLATILE(struct proc *, p));

	if (db_more(&nl) < 0)
	    return;
	db_printf("  pid      lwp  uid  ppid  pgrp  pflags lflags stat  wmesg    wchan cmd\n");
	for (;;) {
		while (lp == NULL) {
			--np;
			p = p->p_list.le_next;
			if (p == NULL && np > 0)
				p = zombproc.lh_first;
			if (p == NULL)
				break;
			lp = FIRST_LWP_IN_PROC(__DEVOLATILE(struct proc *, p));
		}
		/*
		 * XXX just take 20 for now...
		 */
		if (db_more(&nl) < 0)
			return;
		if (p == NULL) {
			kprintf("oops, ran out of processes early!\n");
			break;
		}
		pp = p->p_pptr;
		if (pp == NULL)
			pp = p;

		db_printf("%5d %8p %4d %5d %5d %06x %06x  %d %d",
		    p->p_pid, (volatile void *)lp,
		    p->p_ucred ? p->p_ucred->cr_ruid : 0, pp->p_pid,
		    p->p_pgrp ? p->p_pgrp->pg_id : 0, p->p_flags,
		    lp->lwp_flags, p->p_stat, lp->lwp_stat);
		if (lp->lwp_wchan) {
			db_printf(" %6s %8p",
				(lp->lwp_wmesg ? lp->lwp_wmesg : "?"),
				lp->lwp_wchan);
		} else {
			db_printf("                 ");
		}
		db_printf(" %s\n", p->p_comm ? p->p_comm : "");
		db_dump_td_tokens(lp->lwp_thread);

		lp = lwp_rb_tree_RB_NEXT(lp);
    	}
#endif

	/*
	 * Dump running threads
	 */
	for (cpuidx = 0; cpuidx < ncpus; ++cpuidx) {
	    struct globaldata *gd = globaldata_find(cpuidx);
	    thread_t td;
	    int j;

	    if (db_more(&nl) < 0)
		return;
	    db_printf("cpu %d curthread %p reqflags %04x\n",
		    gd->gd_cpuid, gd->gd_curthread, gd->gd_reqflags);
	    if (gd->gd_curthread && gd->gd_curthread->td_preempted) {
		    db_printf("       PREEMPTING THREAD %p\n",
				gd->gd_curthread->td_preempted);
	    }

	    if (db_more(&nl) < 0)
		return;
	    db_printf("      INCOMING IPIQS:");
	    for (j = 0; j < ncpus; ++j) {
		lwkt_ipiq_t ip = globaldata_find(j)->gd_ipiq;
		if (ip != NULL) {
		    ip = &ip[cpuidx];
		    if (ip->ip_windex != ip->ip_rindex)
			db_printf(" cpu%d:%d", j, ip->ip_windex - ip->ip_rindex);
		}
	    }
	    db_printf("\n");

	    if (db_more(&nl) < 0)
		return;
	    db_printf("  tdq     thread pid    flags pri/cs/mp        sp    wmesg wchan comm\n");
	    TAILQ_FOREACH(td, &gd->gd_tdrunq, td_threadq) {
		if (db_more(&nl) < 0)
		    return;
		db_printf("  %p %3d %08x %2d/%02d %p %8.8s %p %s\n",
		    td,
		    (td->td_proc ? td->td_proc->p_pid : -1),
		    td->td_flags,
		    td->td_pri,
		    td->td_critcount,
		    td->td_sp,
		    td->td_wmesg ? td->td_wmesg : "-",
		    td->td_wchan,
		    td->td_proc ? td->td_proc->p_comm : td->td_comm);
		if (td->td_preempted)
		    db_printf("  PREEMPTING THREAD %p\n", td->td_preempted);
		db_dump_td_tokens(td);
	    }
	    if (db_more(&nl) < 0)
		return;
	    db_printf("\n");
	    if (db_more(&nl) < 0)
		return;
	    db_printf("  tdq     thread pid    flags pri/cs/mp        sp    wmesg wchan comm\n");
	    TAILQ_FOREACH(td, &gd->gd_tdallq, td_allq) {
		if (td->td_flags & TDF_MARKER)
		    continue;
		if (db_more(&nl) < 0)
		    return;
		db_printf("  %3d %p %3d %08x %2d/%02d %p %8.8s %p %s\n",
		    np, td, 
		    (td->td_proc ? td->td_proc->p_pid : -1),
		    td->td_flags,
		    td->td_pri,
		    td->td_critcount,
		    td->td_sp,
		    td->td_wmesg ? td->td_wmesg : "-",
		    td->td_wchan,
		    td->td_proc ? td->td_proc->p_comm : td->td_comm);
		db_dump_td_tokens(td);
	    }
	}
	if (db_more(&nl) < 0)
	    return;
	db_printf("CURCPU %d CURTHREAD %p (%d)\n",
	    mycpu->gd_cpuid,
	    curthread,
	    (curthread->td_proc ? curthread->td_proc->p_pid : -1));
	db_dump_td_tokens(curthread);
}

static void
db_dump_td_tokens(thread_t td)
{
	lwkt_tokref_t ref;
	lwkt_token_t tok;

	if (TD_TOKS_NOT_HELD(td))
		return;
	db_printf("    TOKENS:");
	for (ref = &td->td_toks_base; ref < td->td_toks_stop; ++ref) {
		tok = ref->tr_tok;

		db_printf(" %p[tok=%p", ref, ref->tr_tok);
		if (tok->t_ref && td == tok->t_ref->tr_owner)
		    db_printf(",held");
		db_printf("]");
	}
	db_printf("\n");
}
