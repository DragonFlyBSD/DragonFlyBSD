/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 *
 * $NetBSD: memcmp.S,v 1.2 2003/07/26 19:24:39 salo Exp $
 * $FreeBSD: src/lib/libc/amd64/string/memcmp.S,v 1.2 2008/11/02 01:10:54 peter Exp $
 */

#include <machine/asm.h>
#include <machine/asm_mjgmacros.h>

	.text

#ifdef DO_BCMP
ENTRY(bcmp)
#else
ENTRY(memcmp)
#endif
	MEMCMP	end=ret
#ifdef DO_BCMP
END(bcmp)
#else
END(memcmp)
#endif

	.section .note.GNU-stack,"",%progbits
