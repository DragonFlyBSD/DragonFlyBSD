/* $DragonFly: src/test/cpuperf/cpu_cmpadd.S,v 1.1 2004/02/09 18:08:54 dillon Exp $ */

	.globl	test_dummy
	.globl	test_load
	.globl	test_str

	.p2align 5
test_dummy:
	movl	4(%esp),%ecx
	addl	%ebx,%edx
	movl	%ebx,%edx
	ret

	.p2align 5
test_load:
	movl	4(%esp),%ecx
	addl	%ebx,%edx
	cmpl	$0,%ecx
	jz	1f		/* instruction under test */
	nop			/* instruction under test */
1:
	addl	$1,(%ecx)	/* instruction under test */
	movl	%ebx,%edx
	ret

	.p2align 5
test_str:
	.asciz	"cmp/jz/add (compare fails) in pipeline"

