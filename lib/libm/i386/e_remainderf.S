/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/e_remainderf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: e_remainderf.S,v 1.2 1995/05/08 23:49:47 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(remainderf)
	flds	8(%esp)
	flds	4(%esp)
1:	fprem1
	fstsw	%ax
	sahf
	jp	1b
	fstp	%st(1)
	ret
END(remainderf)

	.section .note.GNU-stack,"",%progbits
