/* $DragonFly: src/test/cpuperf/cpu_ladd.S,v 1.1 2004/02/09 18:15:35 dillon Exp $ */

	.globl	test_dummy
	.globl	test_load
	.globl	test_str

	.p2align 5
test_dummy:
	movl	4(%esp),%ecx
	addl	%ebx,%edx
	nop
	nop
	movl	%ebx,%edx
	ret

	.p2align 5
test_load:
	movl	4(%esp),%ecx
	addl	%ebx,%edx
	lock; addl	$1,(%ecx)	/* instruction under test */
	movl	%ebx,%edx
	ret

	.p2align 5
test_str:
	.asciz	"3 x addl $1,%eax in pipeline"

