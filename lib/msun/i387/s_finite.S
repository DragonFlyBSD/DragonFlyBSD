/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#if 0
RCSID("$NetBSD: s_finite.S,v 1.6 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/s_finite.S,v 1.3 2004/12/29 11:40:18 asmodai Exp $")

ENTRY(finite)
#ifdef __i386__
	movl	8(%esp),%eax
	andl	$0x7ff00000, %eax
	cmpl	$0x7ff00000, %eax
	setne	%al
	andl	$0x000000ff, %eax
#else
	xorl	%eax,%eax
	movq	$0x7ff0000000000000,%rsi
	movq	%rsi,%rdi
	movsd	%xmm0,-8(%rsp)
	andq	-8(%rsp),%rsi
	cmpq	%rdi,%rsi
	setne	%al
#endif
	ret
