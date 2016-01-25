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
 *	from: @(#)reg.h	5.5 (Berkeley) 1/18/91
 * $FreeBSD: src/sys/i386/include/reg.h,v 1.22.2.2 2002/11/07 22:47:55 alfred Exp $
 * $DragonFly: src/sys/sys/reg.h,v 1.3 2007/02/03 17:05:59 corecode Exp $
 */
/*
 * Machine independant interface to abstract cpu registers that a process
 * might have to manipulate (reg, fpreg, and dbreg structures).
 */

#ifndef _SYS_REG_H_
#define	_SYS_REG_H_

#include <machine/reg.h>

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

struct proc;
struct lwp;

int	fill_fpregs (struct lwp *, struct fpreg *);
int	fill_regs (struct lwp *lp, struct reg *regs);
int	fill_dbregs (struct lwp *lp, struct dbreg *dbregs);
int	set_fpregs (struct lwp *, struct fpreg *);
int	set_regs (struct lwp *lp, struct reg *regs);
void	exec_setregs (u_long, u_long, u_long);
int	set_dbregs (struct lwp *lp, struct dbreg *dbregs);

#endif

#endif /* !_SYS_REG_H_ */
