/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

#if 0
RCSID("$NetBSD: s_scalbnf.S,v 1.5 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/s_scalbnf.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

ENTRY(scalbnf)
	XMM_TWO_ARG_FLOAT_PROLOGUE
	fildl	ARG_FLOAT_TWO
	flds	ARG_FLOAT_ONE
	fscale
	fstp	%st(1)		/* bug fix for fp stack overflow */
	XMM_FLOAT_EPILOGUE
	ret
