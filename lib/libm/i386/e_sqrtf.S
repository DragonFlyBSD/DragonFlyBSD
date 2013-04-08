/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/e_sqrtf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: e_sqrtf.S,v 1.2 1995/05/08 23:50:14 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(sqrtf)
	flds	4(%esp)
	fsqrt
	ret
END(sqrtf)

	.section .note.GNU-stack,"",%progbits
