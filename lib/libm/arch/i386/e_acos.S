/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_acos.S,v 1.8 2003/07/26 19:24:57 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/e_acos.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

/* acos = atan (sqrt(1 - x^2) / x) */
ENTRY(acos)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE		/* x */
	fld	%st(0)
	fmul	%st(0)			/* x^2 */
	fld1
	fsubp				/* 1 - x^2 */
	fsqrt				/* sqrt (1 - x^2) */
	fxch	%st(1)
	fpatan
	XMM_DOUBLE_EPILOGUE
	ret
