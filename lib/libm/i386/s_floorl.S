/*
 * Based on code written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $FreeBSD: head/lib/msun/i387/s_floorl.S 217108 2011-01-07 16:13:12Z kib $
 */

#include <machine/asm.h>

ENTRY(floorl)
	pushl	%ebp
	movl	%esp,%ebp
	subl	$8,%esp

	fstcw	-4(%ebp)		/* store fpu control word */
	movw	-4(%ebp),%dx
	orw	$0x0400,%dx		/* round towards -oo */
	andw	$0xf7ff,%dx
	movw	%dx,-8(%ebp)
	fldcw	-8(%ebp)		/* load modfied control word */

	fldt	8(%ebp)			/* round */
	frndint

	fldcw	-4(%ebp)		/* restore original control word */

	leave
	ret
END(floorl)

	.section .note.GNU-stack,"",%progbits
