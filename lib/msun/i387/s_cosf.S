/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

#if 0
RCSID("$NetBSD: s_cosf.S,v 1.5 2001/06/19 00:26:30 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/s_cosf.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(cosf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fcos
	XMM_FLOAT_EPILOGUE
	ret
