/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sys/kern/lwkt_token.c,v 1.4 2004/02/18 16:31:37 joerg Exp $
 */

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/caps.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <machine/stdarg.h>
#include <machine/ipl.h>
#include <machine/smp.h>

#define THREAD_STACK	(UPAGES * PAGE_SIZE)

#else

#include <sys/stdint.h>
#include <libcaps/thread.h>
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include <libcaps/globaldata.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <machine/cpufunc.h>
#include <machine/lock.h>

#endif

#ifdef INVARIANTS
static int token_debug = 0;
#endif

#ifdef _KERNEL

#ifdef INVARIANTS
SYSCTL_INT(_lwkt, OID_AUTO, token_debug, CTLFLAG_RW, &token_debug, 0, "");
#endif

#endif

typedef struct lwkt_gettoken_req {
    lwkt_token_t tok;
    globaldata_t cpu;
} lwkt_gettoken_req;

/*
 * Acquire ownership of a token
 *
 * Acquire ownership of a token.  The token may have spl and/or critical
 * section side effects, depending on its purpose.  These side effects
 * guarentee that you will maintain ownership of the token as long as you
 * do not block.  If you block you may lose access to the token (but you
 * must still release it even if you lose your access to it).
 *
 * YYY for now we use a critical section to prevent IPIs from taking away
 * a token, but do we really only need to disable IPIs ?
 *
 * YYY certain tokens could be made to act like mutexes when performance
 * would be better (e.g. t_cpu == NULL).  This is not yet implemented.
 *
 * YYY the tokens replace 4.x's simplelocks for the most part, but this
 * means that 4.x does not expect a switch so for now we cannot switch
 * when waiting for an IPI to be returned.  
 *
 * YYY If the token is owned by another cpu we may have to send an IPI to
 * it and then block.   The IPI causes the token to be given away to the
 * requesting cpu, unless it has already changed hands.  Since only the
 * current cpu can give away a token it owns we do not need a memory barrier.
 * This needs serious optimization.
 */

#ifdef SMP

static
void
lwkt_gettoken_remote(void *arg)
{
    lwkt_gettoken_req *req = arg;
    if (req->tok->t_cpu == mycpu) {
#ifdef INVARIANTS
	if (token_debug)
	    printf("GT(%d,%d) ", req->tok->t_cpu->gd_cpuid, req->cpu->gd_cpuid);
#endif
	req->tok->t_cpu = req->cpu;
	req->tok->t_reqcpu = req->cpu;	/* YYY leave owned by target cpu */
	/* else set reqcpu to point to current cpu for release */
    }
}

#endif

int
lwkt_gettoken(lwkt_token_t tok)
{
    /*
     * Prevent preemption so the token can't be taken away from us once
     * we gain ownership of it.  Use a synchronous request which might
     * block.  The request will be forwarded as necessary playing catchup
     * to the token.
     */

    crit_enter();
#ifdef INVARIANTS
    if (curthread->td_pri > 1800) {
	printf("lwkt_gettoken: %p called from %p: crit sect nesting warning\n",
	    tok, ((int **)&tok)[-1]);
    }
    if (curthread->td_pri > 2000) {
	curthread->td_pri = 1000;
	panic("too HIGH!");
    }
#endif
#ifdef SMP
    while (tok->t_cpu != mycpu) {
	struct lwkt_gettoken_req req;
	int seq;
	globaldata_t dcpu;

	req.cpu = mycpu;
	req.tok = tok;
	dcpu = tok->t_cpu;
#ifdef INVARIANTS
	if (token_debug)
	    printf("REQT%d ", dcpu->gd_cpuid);
#endif
	seq = lwkt_send_ipiq(dcpu, lwkt_gettoken_remote, &req);
	lwkt_wait_ipiq(dcpu, seq);
#ifdef INVARIANTS
	if (token_debug)
	    printf("REQR%d ", tok->t_cpu->gd_cpuid);
#endif
    }
#endif
    /*
     * leave us in a critical section on return.  This will be undone
     * by lwkt_reltoken().  Bump the generation number.
     */
    return(++tok->t_gen);
}

/*
 * Attempt to acquire ownership of a token.  Returns 1 on success, 0 on
 * failure.
 */
int
lwkt_trytoken(lwkt_token_t tok)
{
    crit_enter();
#ifdef SMP
    if (tok->t_cpu != mycpu) {
	crit_exit();
	return(0);
    } 
#endif
    /* leave us in the critical section */
    ++tok->t_gen;
    return(1);
}

/*
 * Release your ownership of a token.  Releases must occur in reverse
 * order to aquisitions, eventually so priorities can be unwound properly
 * like SPLs.  At the moment the actual implemention doesn't care.
 *
 * We can safely hand a token that we own to another cpu without notifying
 * it, but once we do we can't get it back without requesting it (unless
 * the other cpu hands it back to us before we check).
 *
 * We might have lost the token, so check that.
 *
 * Return the token's generation number.  The number is useful to callers
 * who may want to know if the token was stolen during potential blockages.
 */
int
lwkt_reltoken(lwkt_token_t tok)
{
    int gen;

    if (tok->t_cpu == mycpu) {
	tok->t_cpu = tok->t_reqcpu;
    }
    gen = tok->t_gen;
    crit_exit();
    return(gen);
}

/*
 * Reacquire a token that might have been lost.  0 is returned if the 
 * generation has not changed (nobody stole the token from us), -1 is 
 * returned otherwise.  The token is reacquired regardless but the
 * generation number is not bumped further if we already own the token.
 *
 * For efficiency we inline the best-case situation for lwkt_regettoken()
 * (i.e .we still own the token).
 */
int
lwkt_gentoken(lwkt_token_t tok, int *gen)
{
    if (tok->t_cpu == mycpu && tok->t_gen == *gen)
	return(0);
    *gen = lwkt_regettoken(tok);
    return(-1);
}

/*
 * Re-acquire a token that might have been lost.   The generation number
 * is bumped and returned regardless of whether the token had been lost
 * or not (because we only have cpu granularity we have to bump the token
 * either way).
 */
int
lwkt_regettoken(lwkt_token_t tok)
{
    /* assert we are in a critical section */
    if (tok->t_cpu != mycpu) {
#ifdef SMP
	while (tok->t_cpu != mycpu) {
	    struct lwkt_gettoken_req req;
	    int seq;
	    globaldata_t dcpu;

	    req.cpu = mycpu;
	    req.tok = tok;
	    dcpu = tok->t_cpu;
#ifdef INVARIANTS
	    if (token_debug)
		printf("REQT%d ", dcpu->gd_cpuid);
#endif
	    seq = lwkt_send_ipiq(dcpu, lwkt_gettoken_remote, &req);
	    lwkt_wait_ipiq(dcpu, seq);
#ifdef INVARIANTS
	    if (token_debug)
		printf("REQR%d ", tok->t_cpu->gd_cpuid);
#endif
	}
#endif
    }
    ++tok->t_gen;
    return(tok->t_gen);
}

void
lwkt_inittoken(lwkt_token_t tok)
{
    /*
     * Zero structure and set cpu owner and reqcpu to cpu 0.
     */
    tok->t_cpu = tok->t_reqcpu = mycpu;
    tok->t_gen = 0;
}

