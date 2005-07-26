/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_ilogbf.S,v 1.6 2003/07/26 19:25:02 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_ilogbf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

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
