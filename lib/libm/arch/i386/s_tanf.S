/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_tanf.S,v 1.5 2003/07/26 19:25:04 salo Exp $
 */

#include <machine/asm.h>

#include "abi.h"

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(tanf)
	XMM_ONE_ARG_FLOAT_PROLOGUE
	flds	ARG_FLOAT_ONE
	fptan
	fstp	%st(0)
	XMM_FLOAT_EPILOGUE
	ret
END(tanf)
