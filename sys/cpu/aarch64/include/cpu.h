/*-
 * Copyright (c) 2026 The DragonFly Project.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CPU_CPU_H_
#define	_CPU_CPU_H_

/* Placeholder for arm64 cpu definitions. */

#ifndef _CPU_FRAME_H_
#include <machine/frame.h>
#endif

#define	need_ipiq()					\
	atomic_set_int(&mycpu->gd_reqflags, RQF_IPIQ)

#define	need_user_resched()				\
	atomic_set_int(&mycpu->gd_reqflags, RQF_AST_USER_RESCHED)

#define	clear_user_resched()			\
	atomic_clear_int(&mycpu->gd_reqflags, RQF_AST_USER_RESCHED)

#define	user_resched_wanted()			\
	(mycpu->gd_reqflags & RQF_AST_USER_RESCHED)

#define	lwkt_resched_wanted()			\
	(mycpu->gd_reqflags & RQF_AST_LWKT_RESCHED)

#define	any_resched_wanted()			\
	(mycpu->gd_reqflags & (RQF_AST_LWKT_RESCHED | RQF_AST_USER_RESCHED))

#define	sched_action_wanted_gd(gd)	\
	((gd)->gd_reqflags & RQF_SCHED_MASK)

#define	need_lwkt_resched()			\
	atomic_set_int(&mycpu->gd_reqflags, RQF_AST_LWKT_RESCHED)

#define	need_proftick()			\
	atomic_set_int(&mycpu->gd_reqflags, RQF_AST_OWEUPC)

#define	clear_lwkt_resched()			\
	atomic_clear_int(&mycpu->gd_reqflags, RQF_AST_LWKT_RESCHED)

#define	signotify()				\
	atomic_set_int(&mycpu->gd_reqflags, RQF_AST_SIGNAL)

#define	CLKF_INTR(intr_nest)	((intr_nest) > 1)
#define	CLKF_USERMODE(framep)	((framep)->spsr & (1U << 4))
#define	CLKF_PC(framep)		((framep)->elr)

#endif /* _CPU_CPU_H_ */
