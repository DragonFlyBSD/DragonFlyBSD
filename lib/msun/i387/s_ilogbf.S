/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#if 0
RCSID("$NetBSD: s_ilogbf.S,v 1.5 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/s_ilogbf.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

ENTRY(ilogbf)
#ifdef __i386__
	pushl	%ebp
	movl	%esp,%ebp
	subl	$4,%esp

	flds	8(%ebp)
	fxtract
	fstp	%st

	fistpl	-4(%ebp)
	movl	-4(%ebp),%eax

	leave
#else
	movss	%xmm0,-4(%rsp)
	flds	-4(%rsp)
	fxtract
	fstp	%st
	fistpl	-4(%rsp)
	movl	-4(%rsp),%eax
#endif
	ret
