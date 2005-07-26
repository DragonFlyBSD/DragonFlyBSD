/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_sinf.S,v 1.5 2003/07/26 19:25:04 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_sinf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(sinf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fsin
	XMM_FLOAT_EPILOGUE
	ret
