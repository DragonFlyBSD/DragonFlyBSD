/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

#if 0
RCSID("$NetBSD: e_logf.S,v 1.3 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/e_logf.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

ENTRY(__ieee754_logf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	fldln2
	flds	ARG_FLOAT_ONE
	fyl2x
	XMM_FLOAT_EPILOGUE
	ret
