/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_logb.S,v 1.6 2003/07/26 19:25:02 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/s_logb.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(logb)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	fxtract
	fstp	%st
	XMM_DOUBLE_EPILOGUE
	ret
