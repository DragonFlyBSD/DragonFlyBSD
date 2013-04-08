/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_scalbnf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: s_scalbnf.S,v 1.4 1999/01/02 05:15:40 kristerw Exp $
 */

#include <machine/asm.h>

ENTRY(scalbnf)
	fildl	8(%esp)
	flds	4(%esp)
	fscale
	fstp	%st(1)		/* bug fix for fp stack overflow */
	ret
END(scalbnf)

.globl CNAME(ldexpf)
.set	CNAME(ldexpf),CNAME(scalbnf)

	.section .note.GNU-stack,"",%progbits
