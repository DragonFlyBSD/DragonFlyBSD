/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_scalb.S,v 1.7 2003/07/26 19:25:00 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(scalb)
	XMM_TWO_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_TWO
	fldl	ARG_DOUBLE_ONE
	fscale
	fstp	%st(1)			/* bug fix for fp stack overflow */
	XMM_DOUBLE_EPILOGUE
	ret
END(scalb)

	.section .note.GNU-stack,"",%progbits
