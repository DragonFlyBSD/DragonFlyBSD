/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: s_scalbnl.S,v 1.1 2011/07/26 17:03:23 joerg Exp $
 */

#include <machine/asm.h>

ENTRY(scalbnl)
#ifdef __x86_64__
	movl	%edi,-4(%rsp)
	fildl	-4(%rsp)
	fldt	8(%rsp)
	fscale
	fstp	%st(1)
#else
	fildl 16(%esp)
	fldt 4(%esp)
	fscale
	fstp %st(1)		/* clean up stack */
#endif
	ret
END(scalbnl)
