/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_atanf.S,v 1.5 2003/07/26 19:25:00 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(atanf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fld1
	fpatan
	XMM_FLOAT_EPILOGUE
	ret
END(atanf)
