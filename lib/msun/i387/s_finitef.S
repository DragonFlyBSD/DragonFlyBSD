/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#if 0
RCSID("$NetBSD: s_finitef.S,v 1.5 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/s_finitef.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

ENTRY(finitef)
#ifdef __i386__
	movl	4(%esp),%eax
	andl	$0x7f800000, %eax
	cmpl	$0x7f800000, %eax
	setne	%al
	andl	$0x000000ff, %eax
#else
	xorl	%eax,%eax
	movl	$0x7ff00000,%esi
	movss	%xmm0,-4(%rsp)
	andl	-4(%rsp),%esi
	cmpl	$0x7ff00000,%esi
	setne	%al
#endif
	ret
