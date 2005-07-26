/*
 * Written by Charles M. Hannum, Apr 9, 1995
 * Public domain.
 *
 * $NetBSD: fpsetmask.S,v 1.4 2002/01/13 21:45:43 thorpej Exp $
 * $DragonFly: src/lib/libc/i386/gen/fpsetmask.S,v 1.1 2005/07/26 21:15:19 joerg Exp $
 */

#include <machine/asm.h>

#ifdef WEAK_ALIAS
WEAK_ALIAS(fpsetmask, _fpsetmask)
ENTRY(_fpsetmask)
#else
ENTRY(fpsetmask)
#endif
	subl $4,%esp

	fnstcw (%esp)
	movl (%esp),%eax
	movl %eax,%edx

	notl %eax
	andl $63,%eax

	addl %eax,%edx
	movl 8(%esp),%ecx
	andl $63,%ecx
	subl %ecx,%edx
	movl %edx,(%esp)
	fldcw (%esp)

	addl $4,%esp
	ret
