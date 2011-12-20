/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_logbl.S,v 1.1 2011/08/03 14:13:07 joerg Exp
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(logbl)
	fldt	ARG_LONG_DOUBLE_ONE
	fxtract
	fstp	%st
	ret
END(logbl)

	.section .note.GNU-stack,"",%progbits
