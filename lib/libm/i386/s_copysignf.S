/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_copysignf.S 217108 2011-01-07 16:13:12Z kib $
 * $NetBSD: s_copysignf.S,v 1.3 1995/05/08 23:53:25 jtc Exp $
 */

#include <machine/asm.h>

ENTRY(copysignf)
	movl	8(%esp),%edx
	andl	$0x80000000,%edx
	movl	4(%esp),%eax
	andl	$0x7fffffff,%eax
	orl	%edx,%eax
	movl	%eax,4(%esp)
	flds	4(%esp)
	ret
END(copysignf)

	.section .note.GNU-stack,"",%progbits
