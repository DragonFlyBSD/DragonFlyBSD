/* $DragonFly: src/test/cpuperf/cpu_call.S,v 1.1 2004/02/09 18:08:54 dillon Exp $ */

	.globl	test_dummy
	.globl	test_load
	.globl	test_str

	.p2align 5
test_dummy:
	movl	4(%esp),%ecx
	movl	$0,%edx
	movl	$1,%eax
	movl	%ebx,%edx
	movl	$0,(%ecx)
	addl	$3,%eax
	ret

	.p2align 5
test_load:
	movl	4(%esp),%ecx
	movl	$0,%edx
	movl	$1,%eax
	call	dummy
	movl	%ebx,%edx
	movl	$0,(%ecx)
	addl	$3,%eax
	ret

	.p2align 5
dummy:
	ret

	.p2align 5
test_str:
	.asciz	"call nop in pipeline"

