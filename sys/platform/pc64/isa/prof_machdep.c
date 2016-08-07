/*-
 * Copyright (c) 1996 Bruce D. Evans.
 * Copyright (c) 2008 The DragonFly Project.
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
 * $FreeBSD: src/sys/i386/isa/prof_machdep.c,v 1.14.2.1 2000/08/03 00:09:30 ps Exp $
 */

#ifdef GUPROF
#include "opt_i586_guprof.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/gmon.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/clock.h>
#include <machine/profile.h>
#undef MCOUNT
#endif

#include <machine/asmacros.h>

#include <bus/isa/isa.h>
#include <machine_base/isa/timerreg.h>

#ifdef GUPROF
#define	CPUTIME_CLOCK_UNINITIALIZED	0
#define	CPUTIME_CLOCK_I8254		1
#define	CPUTIME_CLOCK_TSC		2
#define	CPUTIME_CLOCK_I586_PMC		3
#define	CPUTIME_CLOCK_I8254_SHIFT	7

int	cputime_bias = 1;	/* initialize for locality of reference */

static int	cputime_clock = CPUTIME_CLOCK_UNINITIALIZED;
#endif /* GUPROF */

#ifdef __GNUC__
__asm("								\n\
GM_STATE	=	0					\n\
GMON_PROF_OFF	=	3					\n\
								\n\
	.text							\n\
	.p2align 4,0x90						\n\
	.globl	__mcount					\n\
	.type	__mcount,@function				\n\
__mcount:							\n\
	#							\n\
	# Check that we are profiling.  Do it early for speed.	\n\
	#							\n\
	cmpl	$GMON_PROF_OFF," __XSTRING(CNAME(_gmonparam)) "+GM_STATE \n\
 	je	.mcount_exit					\n\
 	#							\n\
 	# __mcount is the same as [.]mcount except the caller	\n\
 	# hasn't changed the stack except to call here, so the	\n\
	# caller's raddr is above our raddr.			\n\
	#							\n\
 	pushl	%eax						\n\
 	pushl	%ecx						\n\
 	pushl	%edx						\n\
 	movl	12+4(%esp),%edx					\n\
 	jmp	.got_frompc					\n\
 								\n\
 	.p2align 4,0x90						\n\
 	.globl	" __XSTRING(HIDENAME(mcount)) "			\n\
" __XSTRING(HIDENAME(mcount)) ":				\n\
	cmpl	$GMON_PROF_OFF," __XSTRING(CNAME(_gmonparam)) "+GM_STATE \n\
	je	.mcount_exit					\n\
	#							\n\
	# The caller's stack frame has already been built, so	\n\
	# %ebp is the caller's frame pointer.  The caller's	\n\
	# raddr is in the caller's frame following the caller's	\n\
	# caller's frame pointer.				\n\
	#							\n\
 	pushl	%eax						\n\
 	pushl	%ecx						\n\
 	pushl	%edx						\n\
	movl	4(%ebp),%edx					\n\
.got_frompc:							\n\
	#							\n\
	# Our raddr is the caller's pc.				\n\
	#							\n\
	movl	(%esp),%eax					\n\
								\n\
	pushfl							\n\
	pushl	%eax						\n\
	pushl	%edx						\n\
	cli							\n\
	call	" __XSTRING(CNAME(mcount)) "			\n\
	addl	$8,%esp						\n\
	popfl							\n\
 	popl	%edx						\n\
 	popl	%ecx						\n\
 	popl	%eax						\n\
.mcount_exit:							\n\
	ret							\n\
");
#else /* !__GNUC__ */
#error
#endif /* __GNUC__ */

#ifdef GUPROF
/*
 * [.]mexitcount saves the return register(s), loads selfpc and calls
 * mexitcount(selfpc) to do the work.  Someday it should be in a machine
 * dependent file together with cputime(), __mcount and [.]mcount.  cputime()
 * can't just be put in machdep.c because it has to be compiled without -pg.
 */
#ifdef __GNUC__
__asm("								\n\
	.text							\n\
#								\n\
# Dummy label to be seen when gprof -u hides [.]mexitcount.	\n\
#								\n\
	.p2align 4,0x90						\n\
	.globl	__mexitcount					\n\
	.type	__mexitcount,@function				\n\
__mexitcount:							\n\
	nop							\n\
								\n\
GMON_PROF_HIRES	=	4					\n\
								\n\
	.p2align 4,0x90						\n\
	.globl	" __XSTRING(HIDENAME(mexitcount)) "		\n\
" __XSTRING(HIDENAME(mexitcount)) ":				\n\
	cmpl	$GMON_PROF_HIRES," __XSTRING(CNAME(_gmonparam)) "+GM_STATE \n\
	jne	.mexitcount_exit				\n\
	pushl	%edx						\n\
	pushl	%ecx						\n\
	pushl	%eax						\n\
	movl	12(%esp),%eax					\n\
	pushfl							\n\
	pushl	%eax						\n\
	cli							\n\
	call	" __XSTRING(CNAME(mexitcount)) "		\n\
	addl	$4,%esp						\n\
	popfl							\n\
	popl	%eax						\n\
	popl	%ecx						\n\
	popl	%edx						\n\
.mexitcount_exit:						\n\
	ret							\n\
");
#else /* !__GNUC__ */
#error
#endif /* __GNUC__ */

/*
 * Return the time elapsed since the last call.  The units are machine-
 * dependent.
 */
int
cputime(void)
{
	u_int count;
	int delta;
	u_char high, low;
	static u_int prev_count;

	/*
	 * Read the current value of the 8254 timer counter 0.
	 */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	low = inb(TIMER_CNTR0);
	high = inb(TIMER_CNTR0);
	count = ((high << 8) | low) << CPUTIME_CLOCK_I8254_SHIFT;

	/*
	 * The timer counts down from TIMER_CNTR0_MAX to 0 and then resets.
	 * While profiling is enabled, this routine is called at least twice
	 * per timer reset (for mcounting and mexitcounting hardclock()),
	 * so at most one reset has occurred since the last call, and one
	 * has occurred iff the current count is larger than the previous
	 * count.  This allows counter underflow to be detected faster
	 * than in microtime().
	 */
	delta = prev_count - count;
	prev_count = count;
	if (delta <= 0)
		return (delta + (timer0_max_count << CPUTIME_CLOCK_I8254_SHIFT));
	return (delta);
}

static int
sysctl_machdep_cputime_clock(SYSCTL_HANDLER_ARGS)
{
	int clock;
	int error;

	clock = cputime_clock;
	error = sysctl_handle_opaque(oidp, &clock, sizeof clock, req);
	if (error == 0 && req->newptr != NULL) {
		if (clock < 0 || clock >= CPUTIME_CLOCK_I586_PMC)
			return (EINVAL);
		cputime_clock = clock;
	}
	return (error);
}

SYSCTL_PROC(_machdep, OID_AUTO, cputime_clock, CTLTYPE_INT | CTLFLAG_RW,
	    0, sizeof(u_int), sysctl_machdep_cputime_clock, "I", "");

/*
 * The start and stop routines need not be here since we turn off profiling
 * before calling them.  They are here for convenience.
 */

void
startguprof(struct gmonparam *gp)
{
	if (cputime_clock == CPUTIME_CLOCK_UNINITIALIZED)
		cputime_clock = CPUTIME_CLOCK_I8254;
	gp->profrate = timer_freq << CPUTIME_CLOCK_I8254_SHIFT;
	cputime_bias = 0;
	cputime();
}

void
stopguprof(struct gmonparam *gp)
{
}

#else /* !GUPROF */
#ifdef __GNUC__
__asm("								\n\
	.text							\n\
	.p2align 4,0x90						\n\
	.globl	" __XSTRING(HIDENAME(mexitcount)) "		\n\
" __XSTRING(HIDENAME(mexitcount)) ":				\n\
	ret							\n\
");
#else /* !__GNUC__ */
#error
#endif /* __GNUC__ */
#endif /* GUPROF */
