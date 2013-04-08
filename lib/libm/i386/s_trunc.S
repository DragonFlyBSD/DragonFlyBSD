/*
 * Based on code written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_trunc.S 217108 2011-01-07 16:13:12Z kib $
 */

#include <machine/asm.h>

ENTRY(trunc)
	pushl	%ebp
	movl	%esp,%ebp
	subl	$8,%esp

	fstcw	-4(%ebp)		/* store fpu control word */
	movw	-4(%ebp),%dx
	orw	$0x0c00,%dx		/* round towards -oo */
	movw	%dx,-8(%ebp)
	fldcw	-8(%ebp)		/* load modfied control word */

	fldl	8(%ebp)			/* round */
	frndint

	fldcw	-4(%ebp)		/* restore original control word */

	leave
	ret
END(trunc)

	.section .note.GNU-stack,"",%progbits
