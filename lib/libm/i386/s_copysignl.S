/*
 * Based on code written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_copysignl.S 217108 2011-01-07 16:13:12Z kib $
 */

#include <machine/asm.h>

ENTRY(copysignl)
	movl	24(%esp),%edx
	andl	$0x8000,%edx
	movl	12(%esp),%eax
	andl	$0x7fff,%eax
	orl	%edx,%eax
	movl	%eax,12(%esp)
	fldt	4(%esp)
	ret
END(copysignl)

	.section .note.GNU-stack,"",%progbits
