/*-
 * Copyright (c) 2018-2019 The FreeBSD Foundation
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Portions of this software were developed by
 * Konstantin Belousov <kib@FreeBSD.org> under sponsorship from
 * the FreeBSD Foundation.
 *
 * Primarily rewritten and redeveloped by Mateusz Guzik
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
 * $FreeBSD$
 */
/*
 * Macros to help implement memcmp(), bcmp(),
 *			    bzero(), memset(),
 *			    memcpy(), bcopy(), memmove()
 */

/*
 * memcmp(b1, b2, len)
 *	  rdi,rsi,rdx
 */
.macro MEMCMP end
	xorl	%eax,%eax
10:
	cmpq	$16,%rdx
	ja	101632f

100816:
	cmpb	$8,%dl
	jl	100408f
	movq	(%rdi),%r8
	movq	(%rsi),%r9
	cmpq	%r8,%r9
	jne	80f
	movq	-8(%rdi,%rdx),%r8
	movq	-8(%rsi,%rdx),%r9
	cmpq	%r8,%r9
	jne	10081608f
	\end
100408:
	cmpb	$4,%dl
	jl	100204f
	movl	(%rdi),%r8d
	movl	(%rsi),%r9d
	cmpl	%r8d,%r9d
	jne	80f
	movl	-4(%rdi,%rdx),%r8d
	movl	-4(%rsi,%rdx),%r9d
	cmpl	%r8d,%r9d
	jne	10040804f
	\end
100204:
	cmpb	$2,%dl
	jl	100001f
	movzwl	(%rdi),%r8d
	movzwl	(%rsi),%r9d
	cmpl	%r8d,%r9d
	jne	1f
	movzwl	-2(%rdi,%rdx),%r8d
	movzwl	-2(%rsi,%rdx),%r9d
	cmpl	%r8d,%r9d
	jne	1f
	\end
100001:
	cmpb	$1,%dl
	jl	100000f
	movzbl	(%rdi),%eax
	movzbl	(%rsi),%r8d
	subl	%r8d,%eax
100000:
	\end
ALIGN_TEXT
101632:
	cmpq	$32,%rdx
	ja	103200f
	movq	(%rdi),%r8
	movq	(%rsi),%r9
	cmpq	%r8,%r9
	jne	80f
	movq	8(%rdi),%r8
	movq	8(%rsi),%r9
	cmpq	%r8,%r9
	jne	10163208f
	movq	-16(%rdi,%rdx),%r8
	movq	-16(%rsi,%rdx),%r9
	cmpq	%r8,%r9
	jne	10163216f
	movq	-8(%rdi,%rdx),%r8
	movq	-8(%rsi,%rdx),%r9
	cmpq	%r8,%r9
	jne	10163224f
	\end
ALIGN_TEXT
103200:
	movq	(%rdi),%r8
	movq	8(%rdi),%r9
	subq	(%rsi),%r8
	subq	8(%rsi),%r9
	orq	%r8,%r9
	jnz	10320000f

	movq    16(%rdi),%r8
	movq    24(%rdi),%r9
	subq    16(%rsi),%r8
	subq    24(%rsi),%r9
	orq	%r8,%r9
	jnz     10320016f

	leaq	32(%rdi),%rdi
	leaq	32(%rsi),%rsi
	subq	$32,%rdx
	cmpq	$32,%rdx
	jae	103200b
	cmpb	$0,%dl
	jne	10b
	\end

/*
 * Mismatch was found.
 *
 * Before we compute it we narrow down the range (16 -> 8 -> 4 bytes).
 */
ALIGN_TEXT
10320016:
	leaq	16(%rdi),%rdi
	leaq	16(%rsi),%rsi
10320000:
	movq	(%rdi),%r8
	movq	(%rsi),%r9
	cmpq	%r8,%r9
	jne	80f
	leaq	8(%rdi),%rdi
	leaq	8(%rsi),%rsi
	jmp	80f
ALIGN_TEXT
10081608:
10163224:
	leaq	-8(%rdi,%rdx),%rdi
	leaq	-8(%rsi,%rdx),%rsi
	jmp	80f
ALIGN_TEXT
10163216:
	leaq	-16(%rdi,%rdx),%rdi
	leaq	-16(%rsi,%rdx),%rsi
	jmp	80f
ALIGN_TEXT
10163208:
	leaq	8(%rdi),%rdi
	leaq	8(%rsi),%rsi
	jmp	80f
ALIGN_TEXT
10040804:
	leaq	-4(%rdi,%rdx),%rdi
	leaq	-4(%rsi,%rdx),%rsi
	jmp	1f

ALIGN_TEXT
80:
	movl	(%rdi),%r8d
	movl	(%rsi),%r9d
	cmpl	%r8d,%r9d
	jne	1f
	leaq	4(%rdi),%rdi
	leaq	4(%rsi),%rsi

/*
 * We have up to 4 bytes to inspect.
 */
1:
	movzbl	(%rdi),%eax
	movzbl	(%rsi),%r8d
	cmpb	%r8b,%al
	jne	2f

	movzbl	1(%rdi),%eax
	movzbl	1(%rsi),%r8d
	cmpb	%r8b,%al
	jne	2f

	movzbl	2(%rdi),%eax
	movzbl	2(%rsi),%r8d
	cmpb	%r8b,%al
	jne	2f

	movzbl	3(%rdi),%eax
	movzbl	3(%rsi),%r8d
2:
	subl	%r8d,%eax
	\end
.endm

/*
 * memmove(dst, src, cnt)
 *         rdi, rsi, rdx
 */

/*
 * Register state at entry is supposed to be as follows:
 * rdi - destination
 * rsi - source
 * rcx - count
 *
 * The macro possibly clobbers the above and: rcx, r8, r9, r10
 * It does not clobber rax nor r11.
 */
.macro MEMMOVE erms overlap end
	/*
	 * For sizes 0..32 all data is read before it is written, so there
	 * is no correctness issue with direction of copying.
	 */
	movq	%rdx,%rcx
	cmpq	$32,%rdx
	jbe	101632f

.if \overlap == 1
	movq	%rdi,%r8
	subq	%rsi,%r8
	cmpq	%rcx,%r8	/* overlapping && src < dst? */
	jb	2f
.endif

	/*
	 * AMD's movsq gets better at around 1024 bytes, Intel's gets
	 * better at around 256 bytes (Zen 2, 9900K era)
	 */
	cmpq	$1024,%rcx
	ja	1256f

103200:
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	movq	8(%rsi),%rdx
	movq	%rdx,8(%rdi)
	movq	16(%rsi),%rdx
	movq	%rdx,16(%rdi)
	movq	24(%rsi),%rdx
	movq	%rdx,24(%rdi)
	leaq	32(%rsi),%rsi
	leaq	32(%rdi),%rdi
	subq	$32,%rcx
	cmpq	$32,%rcx
	jae	103200b
	cmpb	$0,%cl
	jne	101632f
	\end
	ALIGN_TEXT
101632:
	cmpb	$16,%cl
	jl	100816f
	movq	(%rsi),%rdx
	movq	8(%rsi),%r8
	movq	-16(%rsi,%rcx),%r9
	movq	-8(%rsi,%rcx),%r10
	movq	%rdx,(%rdi)
	movq	%r8,8(%rdi)
	movq	%r9,-16(%rdi,%rcx)
	movq	%r10,-8(%rdi,%rcx)
	\end
	ALIGN_TEXT
100816:
	cmpb	$8,%cl
	jl	100408f
	movq	(%rsi),%rdx
	movq	-8(%rsi,%rcx),%r8
	movq	%rdx,(%rdi)
	movq	%r8,-8(%rdi,%rcx,)
	\end
	ALIGN_TEXT
100408:
	cmpb	$4,%cl
	jl	100204f
	movl	(%rsi),%edx
	movl	-4(%rsi,%rcx),%r8d
	movl	%edx,(%rdi)
	movl	%r8d,-4(%rdi,%rcx)
	\end
	ALIGN_TEXT
100204:
	cmpb	$2,%cl
	jl	100001f
	movzwl	(%rsi),%edx
	movzwl	-2(%rsi,%rcx),%r8d
	movw	%dx,(%rdi)
	movw	%r8w,-2(%rdi,%rcx)
	\end
	ALIGN_TEXT
100001:
	cmpb	$1,%cl
	jl	100000f
	movb	(%rsi),%dl
	movb	%dl,(%rdi)
100000:
	\end

	/*
	 * 256 or more bytes
	 */
	ALIGN_TEXT
1256:
	testb	$15,%dil
	jnz	100f
.if \erms == 1
	rep
	movsb
.else
	shrq	$3,%rcx                         /* copy by 64-bit words */
	rep
	movsq
	movq	%rdx,%rcx
	andl	$7,%ecx                         /* any bytes left? */
	jne	100408b
.endif
	\end
100:
	movq	(%rsi),%r8
	movq	8(%rsi),%r9
	movq	%rdi,%r10
	movq	%rdi,%rcx
	andq	$15,%rcx
	leaq	-16(%rdx,%rcx),%rdx
	neg	%rcx
	leaq	16(%rdi,%rcx),%rdi
	leaq	16(%rsi,%rcx),%rsi
	movq	%rdx,%rcx
.if \erms == 1
	rep
	movsb
	movq	%r8,(%r10)
	movq	%r9,8(%r10)
.else
	shrq	$3,%rcx                         /* copy by 64-bit words */
	rep
	movsq
	movq	%r8,(%r10)
	movq	%r9,8(%r10)
	movq	%rdx,%rcx
	andl	$7,%ecx                         /* any bytes left? */
	jne	100408b
.endif
	\end

.if \overlap == 1
	/*
	 * Copy backwards.
	 */
        ALIGN_TEXT
2:
	cmpq	$256,%rcx
	ja	2256f

	leaq	-8(%rdi,%rcx),%rdi
	leaq	-8(%rsi,%rcx),%rsi

	cmpq	$32,%rcx
	jb	2016f

2032:
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	movq	-8(%rsi),%rdx
	movq	%rdx,-8(%rdi)
	movq	-16(%rsi),%rdx
	movq	%rdx,-16(%rdi)
	movq	-24(%rsi),%rdx
	movq	%rdx,-24(%rdi)
	leaq	-32(%rsi),%rsi
	leaq	-32(%rdi),%rdi
	subq	$32,%rcx
	cmpq	$32,%rcx
	jae	2032b
	cmpb	$0,%cl
	jne	2016f
	\end
	ALIGN_TEXT
2016:
	cmpb	$16,%cl
	jl	2008f
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	movq	-8(%rsi),%rdx
	movq	%rdx,-8(%rdi)
	subb	$16,%cl
	jz	2000f
	leaq	-16(%rsi),%rsi
	leaq	-16(%rdi),%rdi
2008:
	cmpb	$8,%cl
	jl	2004f
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	subb	$8,%cl
	jz	2000f
	leaq	-8(%rsi),%rsi
	leaq	-8(%rdi),%rdi
2004:
	cmpb	$4,%cl
	jl	2002f
	movl	4(%rsi),%edx
	movl	%edx,4(%rdi)
	subb	$4,%cl
	jz	2000f
	leaq	-4(%rsi),%rsi
	leaq	-4(%rdi),%rdi
2002:
	cmpb	$2,%cl
	jl	2001f
	movw	6(%rsi),%dx
	movw	%dx,6(%rdi)
	subb	$2,%cl
	jz	2000f
	leaq	-2(%rsi),%rsi
	leaq	-2(%rdi),%rdi
2001:
	cmpb	$1,%cl
	jl	2000f
	movb	7(%rsi),%dl
	movb	%dl,7(%rdi)
2000:
	\end
	ALIGN_TEXT
2256:
	std
.if \erms == 1
	leaq	-1(%rdi,%rcx),%rdi
	leaq	-1(%rsi,%rcx),%rsi
	rep
	movsb
	cld
.else
	leaq	-8(%rdi,%rcx),%rdi
	leaq	-8(%rsi,%rcx),%rsi
	shrq	$3,%rcx
	rep
	movsq
	cld
	movq	%rdx,%rcx
	andb	$7,%cl
	jne	2004b
.endif
	\end
.endif
.endm

/*
 * memset(dst, c,   len)
 *        rdi, r10, rdx
 */
.macro MEMSET erms end
	movq	%rdi,%rax
	movq	%rdx,%rcx

	cmpq	$32,%rcx
	jbe	101632f

	cmpq	$256,%rcx
	ja	1256f

103200:
	movq	%r10,(%rdi)
	movq	%r10,8(%rdi)
	movq	%r10,16(%rdi)
	movq	%r10,24(%rdi)
	leaq	32(%rdi),%rdi
	subq	$32,%rcx
	cmpq	$32,%rcx
	ja	103200b
	cmpb	$16,%cl
	ja	201632f
	movq	%r10,-16(%rdi,%rcx)
	movq	%r10,-8(%rdi,%rcx)
	\end
	ALIGN_TEXT
101632:
	cmpb	$16,%cl
	jl	100816f
201632:
	movq	%r10,(%rdi)
	movq	%r10,8(%rdi)
	movq	%r10,-16(%rdi,%rcx)
	movq	%r10,-8(%rdi,%rcx)
	\end
	ALIGN_TEXT
100816:
	cmpb	$8,%cl
	jl	100408f
	movq	%r10,(%rdi)
	movq	%r10,-8(%rdi,%rcx)
	\end
	ALIGN_TEXT
100408:
	cmpb	$4,%cl
	jl	100204f
	movl	%r10d,(%rdi)
	movl	%r10d,-4(%rdi,%rcx)
	\end
	ALIGN_TEXT
100204:
	cmpb	$2,%cl
	jl	100001f
	movw	%r10w,(%rdi)
	movw	%r10w,-2(%rdi,%rcx)
	\end
	ALIGN_TEXT
100001:
	cmpb	$0,%cl
	je	100000f
	movb	%r10b,(%rdi)
100000:
	\end
	ALIGN_TEXT
1256:
	movq	%rdi,%r9
	movq	%r10,%rax
	testl	$15,%edi
	jnz	3f
1:
.if \erms == 1
	rep
	stosb
	movq	%r9,%rax
.else
	movq	%rcx,%rdx
	shrq	$3,%rcx
	rep
	stosq
	movq	%r9,%rax
	andl	$7,%edx
	jnz	2f
	\end
2:
	movq	%r10,-8(%rdi,%rdx)
.endif
	\end
	ALIGN_TEXT
3:
	movq	%r10,(%rdi)
	movq	%r10,8(%rdi)
	movq	%rdi,%r8
	andq	$15,%r8
	leaq	-16(%rcx,%r8),%rcx
	neg	%r8
	leaq	16(%rdi,%r8),%rdi
	jmp	1b
.endm

.macro DUMMYARG
.endm
