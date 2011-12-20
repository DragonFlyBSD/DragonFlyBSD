/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_scalbf.S,v 1.3 2003/07/26 19:25:00 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(scalbf)
	XMM_TWO_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_TWO
	flds	ARG_FLOAT_ONE
	fscale
	XMM_FLOAT_EPILOGUE
	ret
END(scalbf)

	.section .note.GNU-stack,"",%progbits
