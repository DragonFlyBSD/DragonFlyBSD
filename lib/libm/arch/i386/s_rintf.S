/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_rintf.S,v 1.5 2003/07/26 19:25:03 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_rintf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(rintf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	frndint
	XMM_FLOAT_EPILOGUE
	ret
