/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_scalbn.S,v 1.7 2003/07/26 19:25:03 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_scalbn.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(scalbn)
	XMM_TWO_ARG_DOUBLE_PROLOGUE
	fildl	ARG_DOUBLE_TWO
	fldl	ARG_DOUBLE_ONE
	fscale
	fstp	%st(1)		/* bug fix for fp stack overflow */
	XMM_DOUBLE_EPILOGUE
	ret
