/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/e_log10f.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: e_log10f.S,v 1.1 1996/07/03 16:50:22 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(log10f)
	fldlg2
	flds	4(%esp)
	fyl2x
	ret
END(log10f)

	.section .note.GNU-stack,"",%progbits
