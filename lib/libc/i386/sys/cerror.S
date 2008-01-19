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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $FreeBSD: src/lib/libc/i386/sys/cerror.S,v 1.10 1999/08/27 23:59:38 peter Exp $
 * $DragonFly: src/lib/libc/i386/sys/cerror.S,v 1.6 2008/01/19 13:30:51 corecode Exp $
 */

#include "SYS.h"

	.globl HIDENAME(cerror)

#if 0	/* POSSIBLE FUTURE */

HIDENAME(cerror):
#ifdef PIC
	/* The caller must execute the PIC prologue before jumping to cerror. */
#ifdef __thread
	movl	PIC_GOT(CNAME(errno)),%edx
#else
	movl	%gs:12,%edx
#endif
	movl	%eax,(%edx)
	PIC_EPILOGUE
#else

#ifdef __thread
	movl	%eax, errno
#else
	movl	%gs:12, %edx
	movl	%eax,(%edx)
#endif

#endif
	movl	$-1,%eax
	movl	$-1,%edx
	ret

#endif	/* #if 0 */

HIDENAME(cerror):
#ifdef PIC
	/* The caller must execute the PIC prologue before jumping to cerror. */
#ifdef __thread
	movl    PIC_GOT(CNAME(errno)),%ecx
	movl	%eax,(%ecx)
#else
	pushl   %eax
	leal    errno@TLSGD(,%ebx,1), %eax
	call    ___tls_get_addr@PLT
	popl    %ecx
	movl    %ecx, (%eax)
#endif
	PIC_EPILOGUE
#else
#ifdef __thread
	movl    %eax, errno
#else
	movl	%gs:0,%ecx
	movl	%eax,errno@NTPOFF(%ecx)
#endif
#endif
	movl    $-1,%eax
	movl    $-1,%edx
	ret

#if !defined(PIC) && !defined(__thread)
	/*
	 * Return the direct TLS offset for errno.  TLS non-PIC version
	 * of libc only (i.e. statically linked programs).
	 */
	.globl __get_errno_GS_offset
__get_errno_GS_offset:
	movl	errno@INDNTPOFF,%eax
	ret
#endif

#if defined(PIC) && !defined(__thread)
	/*
	 * Return a pointer to the GOT table entry for errno.  TLS PIC version
	 * of libc only (i.e. the dynamic libc).
	 */
	.globl __get_errno_GOT_ptr
__get_errno_GOT_ptr:
	PIC_PROLOGUE
	leal	errno@TLSGD(,%ebx,1),%eax
	PIC_EPILOGUE
	ret
#endif
