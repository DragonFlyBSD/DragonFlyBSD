/*
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 1999 Marcel Moolenaar
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/amd64/include/ucontext.h,v 1.18 2003/11/08 04:39:22 peter Exp $
 */

#ifndef _CPU_UCONTEXT_H_
#define	_CPU_UCONTEXT_H_

#include <sys/cdefs.h>

#include <sys/types.h>

struct __mcontext {
	register_t	mc_onstack;
	register_t	mc_sp;
	register_t	mc_pc;
	register_t	mc_x[31];
	register_t	mc_spsr;
	register_t	mc_esr;
	register_t	mc_far;

	unsigned int	mc_len;
};

typedef struct __mcontext mcontext_t;

#define _MC_FPFMT_NODEV		0x10000
#define _MC_FPFMT_387		0x10001

#define _MC_FPOWNED_NONE	0x20000
#define _MC_FPOWNED_FPU		0x20001

#endif /* !_CPU_UCONTEXT_H_ */
