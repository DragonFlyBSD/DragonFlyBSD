/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

#include "abi.h"


#if 0
RCSID("$NetBSD: e_fmod.S,v 1.6 2001/06/25 16:44:34 fvdl Exp $")
#endif
RCSID("$DragonFly: src/lib/msun/i387/Attic/e_fmod.S,v 1.3 2004/12/29 11:40:17 asmodai Exp $")

ENTRY(__ieee754_fmod)
	XMM_TWO_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_TWO
	fldl	ARG_DOUBLE_ONE
1:	fprem
	fstsw	%ax
	btw	$10,%ax
	jc	1b
	fstp	%st(1)
	XMM_DOUBLE_EPILOGUE
	ret
