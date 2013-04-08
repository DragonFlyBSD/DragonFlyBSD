/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_logbf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: s_logbf.S,v 1.3 1995/05/09 00:15:12 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(logbf)
	flds	4(%esp)
	fxtract
	fstp	%st
	ret
END(logbf)

	.section .note.GNU-stack,"",%progbits
