/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_sqrtf.S,v 1.4 2003/07/26 19:25:00 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/e_sqrtf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

ENTRY(sqrtf)
#ifdef __i386__
	flds	4(%esp)
	fsqrt
#else
	sqrtss	%xmm0,%xmm0
#endif
	ret
