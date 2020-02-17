/*-
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 2003 Peter Wemm.
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
 */
#include <machine/asm.h>
#include <machine/asm_mjgmacros.h>

/*
 * memcpy(dst:%rdi, src:%rsi, cnt:%rdx)		overlap not supported
 * memmove(dst:%rdi, src:%rsi, cnt:%rdx)	w/overlap
 * bcopy(src:%rdi, dst:%rsi, cnt:%rdx)		w/overlap
 */
#ifdef DO_MEMCOPY
ENTRY(memcpy)
#else
#ifdef DO_MEMMOVE
ENTRY(memmove)
#else
ENTRY(bcopy)
#endif
#endif

#if defined(DO_MEMCOPY)
	movq	%rdi,%rax	/* return dst */
	MEMMOVE	erms=0 overlap=0 end=ret
#elif defined(DO_MEMMOVE)
	movq	%rdi,%rax	/* return dst */
	MEMMOVE	erms=0 overlap=1 end=ret
#else
	xchgq	%rdi,%rsi
	MEMMOVE	erms=0 overlap=1 end=ret
#endif

#ifdef DO_MEMCOPY
END(memcpy)
#else
#ifdef DO_MEMMOVE
END(memmove)
#else
END(bcopy)
#endif
#endif

	.section .note.GNU-stack,"",%progbits
