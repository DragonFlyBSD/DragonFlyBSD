/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_log.S,v 1.6 2003/07/26 19:24:58 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(log)
	XMM_ONE_ARG_DOUBLE_PROLOGUE
	fldln2
	fldl	ARG_DOUBLE_ONE
	fyl2x
	XMM_DOUBLE_EPILOGUE
	ret
END(log)
