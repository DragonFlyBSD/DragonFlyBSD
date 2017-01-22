/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)cpu.h	5.4 (Berkeley) 5/9/91
 * $FreeBSD: src/sys/i386/include/cpu.h,v 1.43.2.2 2001/06/15 09:37:57 scottl Exp $
 */

#ifndef _CPU_CPU_H_
#define	_CPU_CPU_H_

/*
 * Definitions unique to x86_64 cpu support.
 */
#ifndef _CPU_PSL_H_
#include <machine/psl.h>
#endif
#ifndef _CPU_FRAME_H_
#include <machine/frame.h>
#endif
#ifndef _CPU_SEGMENTS_H_
#include <machine/segments.h>
#endif

/*
 * definitions of cpu-dependent requirements
 * referenced in generic code
 */

#define	cpu_exec(p)	/* nothing */
#define cpu_swapin(p)	/* nothing */
#define cpu_setstack(lp, ap)		((lp)->lwp_md.md_regs[SP] = (ap))

#define CLKF_INTR(intr_nest)	((intr_nest) > 1)
#define CLKF_INTR_TD(td)	((td)->td_flags & TDF_INTTHREAD)
#define	CLKF_PC(framep)		((framep)->if_rip)

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 *
 * We do not have to use a locked bus cycle but we do have to use an
 * atomic instruction because an interrupt on the local cpu can modify
 * the gd_reqflags field.
 */
#define	need_lwkt_resched()	\
    atomic_set_int(&mycpu->gd_reqflags, RQF_AST_LWKT_RESCHED)
#define	need_user_resched()	\
    atomic_set_int(&mycpu->gd_reqflags, RQF_AST_USER_RESCHED)
#define	need_proftick()		\
    atomic_set_int(&mycpu->gd_reqflags, RQF_AST_OWEUPC)
#define	need_ipiq()		\
    atomic_set_int(&mycpu->gd_reqflags, RQF_IPIQ)
#define	need_timer()		\
    atomic_set_int(&mycpu->gd_reqflags, RQF_TIMER)
#ifdef _KERNEL_VIRTUAL
#define	need_kqueue()		\
    atomic_set_int(&mycpu->gd_reqflags, RQF_KQUEUE)
#endif
#define	signotify()		\
    atomic_set_int(&mycpu->gd_reqflags, RQF_AST_SIGNAL)
#define	clear_user_resched()	\
    atomic_clear_int(&mycpu->gd_reqflags, RQF_AST_USER_RESCHED)
#define	clear_lwkt_resched()	\
    atomic_clear_int(&mycpu->gd_reqflags, RQF_AST_LWKT_RESCHED)
#define	clear_quickret()	\
    atomic_clear_int(&mycpu->gd_reqflags, RQF_QUICKRET)
#define	user_resched_wanted()	\
    (mycpu->gd_reqflags & RQF_AST_USER_RESCHED)
#define	lwkt_resched_wanted()	\
    (mycpu->gd_reqflags & RQF_AST_LWKT_RESCHED)
#define	any_resched_wanted()	\
    (mycpu->gd_reqflags & (RQF_AST_LWKT_RESCHED|RQF_AST_USER_RESCHED))

/*
 * CTL_MACHDEP definitions.
 */
#define CPU_CONSDEV		1	/* cdev_t: console terminal device */
#define	CPU_ADJKERNTZ		2	/* int:	timezone offset	(seconds) */
#define	CPU_DISRTCSET		3	/* int: disable resettodr() call */
#define CPU_BOOTINFO		4	/* struct: bootinfo */
#define	CPU_WALLCLOCK		5	/* int:	indicates wall CMOS clock */
#define	CPU_MAXID		6	/* number of valid machdep ids */

#define CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
	{ "adjkerntz", CTLTYPE_INT }, \
	{ "disable_rtc_set", CTLTYPE_INT }, \
	{ "bootinfo", CTLTYPE_STRUCT }, \
	{ "wall_cmos_clock", CTLTYPE_INT }, \
}

#ifdef _KERNEL

struct lwp;

extern char	btext[];
extern char	etext[];

void	fork_trampoline (void);
void	generic_lwp_return (struct lwp *, struct trapframe *);
void	fork_return (struct lwp *, struct trapframe *);

#endif

#endif /* !_CPU_CPU_H_ */
