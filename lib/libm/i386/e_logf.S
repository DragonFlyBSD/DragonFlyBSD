/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/e_logf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: e_logf.S,v 1.2 1996/07/06 00:15:45 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(logf)
	fldln2
	flds	4(%esp)
	fyl2x
	ret
END(logf)

	.section .note.GNU-stack,"",%progbits
