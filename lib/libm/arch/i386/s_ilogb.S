/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_ilogb.S,v 1.7 2003/07/26 19:25:02 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_ilogb.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

ENTRY(ilogb)
#ifdef __i386__
	pushl	%ebp
	movl	%esp,%ebp
	subl	$4,%esp

	fldl	8(%ebp)
	fxtract
	fstp	%st

	fistpl	-4(%ebp)
	movl	-4(%ebp),%eax

	leave
#else
	movsd	%xmm0,-8(%rsp)
	fldl	-8(%rsp)
	fxtract
	fstp	%st
	fistpl	-8(%rsp)
	movl	-8(%rsp),%eax
#endif
	ret
