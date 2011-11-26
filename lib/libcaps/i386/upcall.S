	/*
	 * $DragonFly: src/lib/libcaps/i386/upcall.S,v 1.2 2003/12/07 04:21:54 dillon Exp $
	 */

#include "asmacros.h"
#include "asdefs.h"

	.text
	.globl	upc_callused_wrapper

	/*
	 * On entry: 	%eax contains function
	 *		%ecx contains data
	 * 		Stack: [eax,ecx,eflags,oldip]
	 */
NON_GPROF_ENTRY(upc_callused_wrapper)
	pushl	%edx		/* save %edx (upcall pointer) */
	pushl	%ecx		/* func(data) */
	call	*%eax
	addl	$4,%esp
	popl	%edx		/* upcall pointer */
	movl	UPC_UTHREAD(%edx),%eax
	incl	UPC_PENDING(%edx)    /* set pending bit (prevents upcalls) */
	subl	$TDPRI_CRIT,TD_PRI(%eax) /* cleanup critical section count */
	pushl	%esp		/* sp pointing to os supplied frame */
	pushl	$-1		/* upcid */
	pushl	$2		/* FETCH next */
	call	upc_control
	popl	%eax
	popl	%ecx
	popl	%edx
	popfl
	ret

