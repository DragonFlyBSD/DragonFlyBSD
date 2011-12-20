/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_sqrt.S,v 1.6 2003/07/26 19:25:00 salo Exp $
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
END(sqrt)

	.section .note.GNU-stack,"",%progbits
