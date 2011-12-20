/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_logf.S,v 1.4 2003/07/26 19:24:59 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(logf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	fldln2
	flds	ARG_FLOAT_ONE
	fyl2x
	XMM_FLOAT_EPILOGUE
	ret
END(logf)

	.section .note.GNU-stack,"",%progbits
