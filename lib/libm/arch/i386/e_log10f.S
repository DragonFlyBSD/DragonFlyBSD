/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: e_log10f.S,v 1.3 2003/07/26 19:24:59 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(log10f)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	fldlg2
	flds	ARG_FLOAT_ONE
	fyl2x
	XMM_FLOAT_EPILOGUE
	ret
END(log10f)
