/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_cosf.S,v 1.6 2003/07/26 19:25:01 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_cosf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(cosf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fcos
	XMM_FLOAT_EPILOGUE
	ret
