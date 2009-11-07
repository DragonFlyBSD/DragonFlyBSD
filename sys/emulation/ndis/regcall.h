/*
 * 
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/ndis/pe_var.h,v 1.7 2004/04/14 07:48:02 wpaul Exp $
 * $DragonFly: src/sys/emulation/ndis/regcall.h,v 1.1 2004/07/29 20:51:34 dillon Exp $
 */

#ifndef _REGCALL_H_
#define _REGCALL_H_

/*
 * Note: Windows uses the _stdcall calling convention. This means
 * that the callback functions provided in the function table must
 * be declared using __attribute__((__stdcall__)), otherwise the
 * Windows code will likely screw up the %esp register and cause
 * us to jump to an invalid address when it returns.   With the
 * stdcall calling convention the target procedure is responsible for
 * popping the call arguments.
 *
 * Note: Windows often passes arguments in registers.  In windows
 * the order is: %ecx, %edx.  The regparm(3) attribute in GNU C will
 * pass arguments in the order: %eax, %edx, %ecx, with any remaining
 * arguments passed on the stack.
 */

#ifdef __amd64__
#define	__stdcall
#define __regcall
#define REGARGS1(decl1)		decl1
#define REGARGS2(decl1, decl2)	decl1, decl2
#define REGCALL1(arg1)		arg1
#define REGCALL2(arg1, arg2)	arg1, arg2
#else
#define	__stdcall __attribute__((__stdcall__))
#define __regcall __attribute__((__regparm__(3)))
#define REGARGS1(decl1)		int dummy1, int dummy2, decl1
#define REGARGS2(decl1, decl2)	int dummy1, decl2, decl1
#define REGCALL1(arg1)		0, 0, arg1
#define REGCALL2(arg1, arg2)	0, arg2, arg1
#endif


/*
 * This mess allows us to call a _fastcall style routine with our
 * version of gcc, which lacks __attribute__((__fastcall__)). Only
 * has meaning on x86; everywhere else, it's a no-op.
 */

#ifdef __i386__
typedef __stdcall __regcall int (*fcall1)(REGARGS1(uint32_t));
typedef __stdcall __regcall int (*fcall2)(REGARGS2(uint32_t, uint32_t));
typedef __stdcall __regcall int (*fcall3)(REGARGS2(uint32_t, uint32_t), uint32_t);
static __inline uint32_t 
fastcall1(fcall1 f, uint32_t a)
{
	return(f(REGCALL1(a)));
}

static __inline uint32_t 
fastcall2(fcall2 f, uint32_t a, uint32_t b)
{
	return(f(REGCALL2(a, b)));
}

static __inline uint32_t 
fastcall3(fcall3 f, uint32_t a, uint32_t b, uint32_t c)
{
	return(f(REGCALL2(a, b), c));
}

#define FASTCALL1(f, a)		\
	fastcall1((fcall1)(f), (uint32_t)(a))
#define FASTCALL2(f, a, b)	\
	fastcall2((fcall2)(f), (uint32_t)(a), (uint32_t)(b))
#define FASTCALL3(f, a, b, c)	\
	fastcall3((fcall3)(f), (uint32_t)(a), (uint32_t)(b), (uint32_t)(c))
#else
#define FASTCALL1(f, a) (f)((a))
#define FASTCALL2(f, a, b) (f)((a), (b))
#define FASTCALL3(f, a, b, c) (f)((a), (b), (c))
#endif /* __i386__ */

#endif
