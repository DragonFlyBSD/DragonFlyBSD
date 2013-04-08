/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_rintf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: s_rintf.S,v 1.3 1995/05/09 00:17:22 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(rintf)
	flds	4(%esp)
	frndint
	ret
END(rintf)

	.section .note.GNU-stack,"",%progbits
