/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#if 0
RCSID("$NetBSD: s_ilogb.S,v 1.6 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/s_ilogb.S,v 1.3 2004/12/29 11:40:18 asmodai Exp $")

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
