	/*
	 * $DragonFly: src/test/sysperf/upcall.S,v 1.1 2003/11/21 06:58:28 dillon Exp $
	 */
	.text
	.globl	callused_wrapper

	/*
	 * On entry: 	%eax contains function
	 *		%ecx contains data
	 * 		Stack: [eax,ecx,eflags,oldip]
	 */
callused_wrapper:
	pushl	%edx		/* save %edx */
	pushl	%ecx		/* func(data) */
	call	*%eax
	addl	$4,%esp
	incl	upc+8		/* set pending bit (prevents upcalls) */
	subl	$32,upc+4	/* cleanup critical section count */
	popl	%edx
	pushl	%esp		/* sp pointing to os supplied frame */
	pushl	$-1		/* upcid */
	pushl	$2		/* FETCH next */
	call	upc_control
	popl	%eax
	popl	%ecx
	popfl
	ret

