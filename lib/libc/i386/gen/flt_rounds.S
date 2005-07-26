/*
 * Written by J.T. Conklin, Apr 4, 1995
 * Public domain.
 *
 * $NetBSD: flt_rounds.S,v 1.6 1999/08/23 08:45:09 kleink Exp $
 * $DragonFly: src/lib/libc/i386/gen/flt_rounds.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

_START_ENTRY
_map:
	.byte 1		/* round to nearest */
	.byte 3		/* round to negative infinity */
	.byte 2		/* round to positive infinity */
	.byte 0		/* round to zero */

ENTRY(__flt_rounds)
	subl $4,%esp
	fnstcw (%esp)
	movl (%esp),%eax
	shrl $10,%eax
	andl $3,%eax
#ifdef PIC
	PIC_PROLOGUE
	leal PIC_GOTOFF(_map),%ecx
	PIC_EPILOGUE
	movb (%ecx,%eax,1),%al
#else
	movb _map(,%eax,1),%al
#endif
	addl $4,%esp
	ret
