/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_ilogbl.S,v 1.1 2011/07/28 22:32:28 joerg Exp $
 */

#include <machine/asm.h>

#include "abi.h"

ENTRY(ilogbl)
	fldt	ARG_LONG_DOUBLE_ONE
	fxtract
	fstp	%st
#ifdef __i386__
	pushl	%eax
	fistpl	0(%esp)
	popl	%eax
#else
	fistpl	-4(%rsp)
	movl	-4(%rsp), %eax
#endif
	ret
END(ilogbl)

	.section .note.GNU-stack,"",%progbits
