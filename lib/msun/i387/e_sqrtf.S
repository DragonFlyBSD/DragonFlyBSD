/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#if 0
RCSID("$NetBSD: e_sqrtf.S,v 1.3 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/e_sqrtf.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

ENTRY(__ieee754_sqrtf)
#ifdef __i386__
	flds	4(%esp)
	fsqrt
#else
	sqrtss	%xmm0,%xmm0
#endif
	ret
