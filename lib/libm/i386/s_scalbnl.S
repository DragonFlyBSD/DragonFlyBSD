/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_scalbnl.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: s_scalbnf.S,v 1.4 1999/01/02 05:15:40 kristerw Exp $
 */

#include <machine/asm.h>

ENTRY(scalbnl)
	fildl	16(%esp)
	fldt	4(%esp)
	fscale
	fstp	%st(1)
	ret
END(scalbnl)

.globl CNAME(ldexpl)
.set	CNAME(ldexpl),CNAME(scalbnl)

	.section .note.GNU-stack,"",%progbits
