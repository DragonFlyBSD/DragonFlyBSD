/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_logbf.S,v 1.5 2003/07/26 19:25:02 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(logbf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fxtract
	fstp	%st
	XMM_FLOAT_EPILOGUE
	ret
END(logbf)

	.section .note.GNU-stack,"",%progbits
