/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_atan2f.S,v 1.3 2003/07/26 19:24:58 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(atan2f)
	XMM_TWO_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	flds	ARG_FLOAT_TWO
	fpatan
	XMM_FLOAT_EPILOGUE
	ret
END(atan2f)

	.section .note.GNU-stack,"",%progbits
