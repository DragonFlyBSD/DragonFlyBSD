/*
 * Based on the i387 version written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $NetBSD: e_remainderf.S,v 1.2 1995/05/08 23:49:47 jtc Exp $
 * $FreeBSD: head/lib/msun/amd64/e_remainderf.S 217108 2011-01-07 16:13:12Z kib $
 */

#include <machine/asm.h>

ENTRY(remainderf)
	movss	%xmm0,-4(%rsp)
	movss	%xmm1,-8(%rsp)
	flds	-8(%rsp)
	flds	-4(%rsp)
1:	fprem1
	fstsw	%ax
	testw	$0x400,%ax
	jne	1b
	fstps	-4(%rsp)
	movss	-4(%rsp),%xmm0
	fstp	%st
	ret
END(remainderf)

	.section .note.GNU-stack,"",%progbits
