/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 *
 * $NetBSD: memcmp.S,v 1.2 2003/07/26 19:24:39 salo Exp $
 * $FreeBSD: src/lib/libc/amd64/string/memcmp.S,v 1.2 2008/11/02 01:10:54 peter Exp $
 */

#include <machine/asm.h>

ENTRY(memcmp)
	cld				/* set compare direction forward */
	movq	%rdx,%rcx		/* compare by longs */
	shrq	$3,%rcx
	repe
	cmpsq
	jne	L5			/* do we match so far? */

	movq	%rdx,%rcx		/* compare remainder by bytes */
	andq	$7,%rcx
	repe
	cmpsb
	jne	L6			/* do we match? */

	xorl	%eax,%eax		/* we match, return zero	*/
	ret

L5:	movl	$8,%ecx			/* We know that one of the next	*/
	subq	%rcx,%rdi		/* eight pairs of bytes do not	*/
	subq	%rcx,%rsi		/* match.			*/
	repe
	cmpsb
L6:	xorl	%eax,%eax		/* Perform unsigned comparison	*/
	movb	-1(%rdi),%al
	xorl	%edx,%edx
	movb	-1(%rsi),%dl
	subl	%edx,%eax
	ret
END(memcmp)

	.section .note.GNU-stack,"",%progbits
