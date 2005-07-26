/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_scalbnf.S,v 1.6 2003/07/26 19:25:03 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_scalbnf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(scalbnf)
	XMM_TWO_ARG_FLOAT_PROLOGUE
	fildl	ARG_FLOAT_TWO
	flds	ARG_FLOAT_ONE
	fscale
	fstp	%st(1)		/* bug fix for fp stack overflow */
	XMM_FLOAT_EPILOGUE
	ret
