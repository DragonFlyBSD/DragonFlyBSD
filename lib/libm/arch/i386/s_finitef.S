/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_finitef.S,v 1.6 2003/07/26 19:25:01 salo Exp $
 */

#include <machine/asm.h>

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
END(finitef)
