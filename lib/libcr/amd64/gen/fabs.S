/*
 * $NetBSD: fabs.S,v 1.4 1997/07/16 14:37:16 christos Exp $
 * $FreeBSD: src/lib/libc/amd64/gen/fabs.S,v 1.2 2003/09/26 01:49:48 peter Exp $
 * $DragonFly: src/lib/libcr/amd64/gen/Attic/fabs.S,v 1.1 2004/03/13 19:46:55 eirikn Exp $
 */

#include <machine/asm.h>

/*
 * Ok, this sucks. Is there really no way to push an xmm register onto
 * the FP stack directly?
 */

ENTRY(fabs)
	movsd	%xmm0, -8(%rsp)
	fldl	-8(%rsp)
	fabs
	fstpl	-8(%rsp)
	movsd	-8(%rsp),%xmm0
	ret
