/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"

#if 0
RCSID("$NetBSD: e_remainderf.S,v 1.4 2001/06/25 16:47:05 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/e_remainderf.S,v 1.1 2004/12/29 12:55:50 asmodai Exp $")

ENTRY(__ieee754_remainderf)
	XMM_TWO_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_TWO
	flds	ARG_FLOAT_ONE
1:	fprem1
	fstsw	%ax
	btw	$10,%ax
	jc	1b
	fstp	%st(1)
	XMM_FLOAT_EPILOGUE
	ret
