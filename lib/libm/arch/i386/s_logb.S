/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_logb.S,v 1.6 2003/07/26 19:25:02 salo Exp $
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
END(logb)
