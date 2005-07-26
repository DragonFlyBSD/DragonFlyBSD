/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_sqrt.S,v 1.6 2003/07/26 19:25:00 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/e_sqrt.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

ENTRY(sqrt)
#ifdef __i386__
	fldl	4(%esp)
	fsqrt
#else
	sqrtsd	%xmm0,%xmm0
#endif
	ret
