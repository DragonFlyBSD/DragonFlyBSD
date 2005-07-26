/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_atanf.S,v 1.5 2003/07/26 19:25:00 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_atanf.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(atanf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fld1
	fpatan
	XMM_FLOAT_EPILOGUE
	ret
