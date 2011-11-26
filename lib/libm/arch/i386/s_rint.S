/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_rint.S,v 1.6 2003/07/26 19:25:03 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(rint)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldl	ARG_DOUBLE_ONE
	frndint
	XMM_DOUBLE_EPILOGUE
	ret
END(rint)
