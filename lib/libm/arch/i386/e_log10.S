/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_log10.S,v 1.6 2003/07/26 19:24:59 salo Exp $
 * $DragonFly: src/lib/libm/arch/i386/e_log10.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(log10)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldlg2
	fldl	ARG_DOUBLE_ONE
	fyl2x
	XMM_DOUBLE_EPILOGUE
	ret
