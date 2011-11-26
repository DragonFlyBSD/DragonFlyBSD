	/*
	 * $DragonFly: src/test/sysperf/upcall.S,v 1.2 2003/11/21 08:32:45 dillon Exp $
	 */
	.text
	.globl	callused_wrapper

#define CRIT_COUNT	4	/* not a good idea to hardwire this XXX */
#define PENDING		8	/* not a good idea to hardwire this XXX */

	/*
	 * On entry: 	%eax contains function
	 *		%ecx contains data
	 * 		Stack: [eax,ecx,eflags,oldip]
	 */
callused_wrapper:
	pushl	%edx		/* save %edx (upcall pointer) */
	pushl	%ecx		/* func(data) */
	call	*%eax
	addl	$4,%esp
	popl	%edx		/* upcall pointer */
	incl	PENDING(%edx)	/* set pending bit (prevents upcalls) */
	subl	$32,CRIT_COUNT(%edx) /* cleanup critical section count */
	pushl	%esp		/* sp pointing to os supplied frame */
	pushl	$-1		/* upcid */
	pushl	$2		/* FETCH next */
	call	upc_control
	popl	%eax
	popl	%ecx
	popl	%edx
	popfl
	ret

