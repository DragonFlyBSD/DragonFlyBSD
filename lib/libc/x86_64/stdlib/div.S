/*	$NetBSD: div.S,v 1.1 2001/06/19 00:25:04 fvdl Exp $	*/

/*-
 * Written by Frank van der Linden (fvdl@wasabisystems.com)
 * Public domain.
 *
 * $FreeBSD: src/lib/libc/amd64/stdlib/div.S,v 1.2 2008/11/02 01:10:54 peter Exp $
 */

#include <machine/asm.h>

ENTRY(div)
	movl	%edi,%eax
	cltd
	idivl	%esi
	salq	$32,%rdx
	orq	%rdx,%rax
	ret
END(div)

	.section .note.GNU-stack,"",%progbits
