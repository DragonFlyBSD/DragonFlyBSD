/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

#if 0
RCSID("$NetBSD: e_atan2.S,v 1.5 2001/06/19 00:26:29 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/e_atan2.S,v 1.3 2004/12/29 11:40:17 asmodai Exp $")

ENTRY(__ieee754_atan2)
	XMM_TWO_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	fldl	ARG_DOUBLE_TWO
	fpatan
	XMM_DOUBLE_EPILOGUE
	ret
