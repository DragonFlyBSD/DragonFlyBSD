/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_atan2.S,v 1.6 2003/07/26 19:24:58 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(atan2)
	XMM_TWO_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	fldl	ARG_DOUBLE_TWO
	fpatan
	XMM_DOUBLE_EPILOGUE
	ret
END(atan2)
