/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)subr_prof.c	8.3 (Berkeley) 9/23/93
 * $FreeBSD: src/sys/kern/subr_prof.c,v 1.32.2.2 2000/08/03 00:09:32 ps Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmsg.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>

#include <sys/thread2.h>

#include <machine/cpu.h>

/*
 * Profiling system call.
 *
 * The scale factor is a fixed point number with 16 bits of fraction, so that
 * 1.0 is represented as 0x10000.  A scale factor of 0 turns off profiling.
 *
 * MPALMOSTSAFE
 */
int
sys_profil(struct sysmsg *sysmsg, const struct profil_args *uap)
{
	struct proc *p = curproc;
	struct uprof *upp;

	if (uap->scale > (1 << 16))
		return (EINVAL);
	lwkt_gettoken(&p->p_token);
	if (uap->scale == 0) {
		stopprofclock(p);
	} else {
		upp = &p->p_prof;

		/* Block profile interrupts while changing state. */
		crit_enter();
		upp->pr_off = uap->offset;
		upp->pr_scale = uap->scale;
		upp->pr_base = uap->samples;
		upp->pr_size = uap->size;
		startprofclock(p);
		crit_exit();
	}
	lwkt_reltoken(&p->p_token);

	return (0);
}

/*
 * Scale is a fixed-point number with the binary point 16 bits
 * into the value, and is <= 1.0.  pc is at most 32 bits, so the
 * intermediate result is at most 48 bits.
 */
#define	PC_TO_INDEX(pc, prof) \
	((int)(((u_quad_t)((pc) - (prof)->pr_off) * \
	    (u_quad_t)((prof)->pr_scale)) >> 16) & ~1)

/*
 * Collect user-level profiling statistics; called on a profiling tick,
 * when a process is running in user-mode.  This routine may be called
 * from an interrupt context.
 *
 * Note that we may (rarely) not get around to the AST soon enough, and
 * lose profile ticks when the next tick overwrites this one, but in this
 * case the system is overloaded and the profile is probably already
 * inaccurate.
 */
void
addupc_intr(struct proc *p, u_long pc, u_int ticks)
{
	struct uprof *prof;
	u_int i;

	if (ticks == 0)
		return;
	prof = &p->p_prof;
	if (pc < prof->pr_off ||
	    (i = PC_TO_INDEX(pc, prof)) >= prof->pr_size)
		return;			/* out of range; ignore */

	prof->pr_addr = pc;
	prof->pr_ticks = ticks;
	need_proftick();
}

/*
 * Much like before, but we can afford to take faults here.  If the
 * update fails, we simply turn off profiling.
 */
void
addupc_task(struct proc *p, u_long pc, u_int ticks)
{
	struct uprof *prof;
	caddr_t addr;
	u_int i;
	u_short v;

	/* Testing P_PROFIL may be unnecessary, but is certainly safe. */
	if ((p->p_flags & P_PROFIL) == 0 || ticks == 0)
		return;

	prof = &p->p_prof;
	if (pc < prof->pr_off ||
	    (i = PC_TO_INDEX(pc, prof)) >= prof->pr_size)
		return;

	addr = prof->pr_base + i;
	if (copyin(addr, (caddr_t)&v, sizeof(v)) == 0) {
		v += ticks;
		if (copyout((caddr_t)&v, addr, sizeof(v)) == 0)
			return;
	}
	stopprofclock(p);
}
