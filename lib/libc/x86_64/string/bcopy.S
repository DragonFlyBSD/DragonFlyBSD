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

/*
 * bcopy(src:%rdi, dst:%rsi, cnt:%rdx)
 */
#ifdef MEMCOPY
ENTRY(memcpy)
#else
#ifdef MEMMOVE
ENTRY(memmove)
#else
ENTRY(bcopy)
#endif
#endif
#if defined(MEMCOPY) || defined(MEMMOVE)
	movq	%rdi,%rax	/* return dst */
#else
	xchgq	%rdi,%rsi
#endif
        movq    %rdx,%rcx

        movq    %rdi,%r11
        subq    %rsi,%r11
        cmpq    %rcx,%r11                       /* overlapping && src < dst? */
        jb      2f

        cld                                     /* nope, copy forwards */
        shrq    $3,%rcx                         /* copy by 64-bit words */
        rep
        movsq
        movq    %rdx,%rcx
        andq    $7,%rcx                         /* any bytes left? */
        jnz     1f
        ret
1:      rep
        movsb
        ret

	.align	4
2:
        addq    %rcx,%rdi                       /* copy backwards */
        addq    %rcx,%rsi
        std
        decq    %rdi
        decq    %rsi
        andq    $7,%rcx                         /* any fractional bytes? */
        jz      3f
        rep
        movsb
3:      movq    %rdx,%rcx                       /* copy by 32-bit words */
        shrq    $3,%rcx
        subq    $7,%rsi
        subq    $7,%rdi
        rep
        movsq
        cld
        ret
#ifdef MEMCOPY
END(memcpy)
#else
#ifdef MEMMOVE
END(memmove)
#else
END(bcopy)
#endif
#endif

	.section .note.GNU-stack,"",%progbits
