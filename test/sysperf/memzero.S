
		/*
		 * memcpy.S
		 *
		 * $DragonFly: src/test/sysperf/memzero.S,v 1.1 2004/04/29 16:14:53 dillon Exp $
		 */
		.text
		.globl	dozero1
		.globl	dozero2
		.globl	dozero3
		.globl	dozero4
		.globl	dozero5
		.globl	dozero6
		.globl	dozero7
		.globl	fpcleanup

		.p2align 4,0x90
dozero1:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx
		shrl	$2,%ecx
		subl	%eax,%eax
		cld
		rep
		stosl
		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
dozero2:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx
		addl	%ecx,%esi
		addl	%ecx,%edi
		shrl	$2,%ecx
		subl	%eax,%eax
		std
		rep
		stosl
		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
dozero3:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx
		movl	$8,%edx
		subl	%eax,%eax
		.p2align 4,0x90
1:
		subl	%edx,%ecx
		movl	%eax,(%edi)
		movl	%eax,4(%edi)
		addl	%edx,%edi
		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
dozero4:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx
		subl	%eax,%eax
		.p2align 4,0x90
1:
		subl	$16,%ecx
		movnti	%eax,0(%edi)
		movnti	%eax,4(%edi)
		movnti	%eax,8(%edi)
		movnti	%eax,12(%edi)
		addl	$16,%edi
		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
dozero5:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx

		subl	$108,%esp
		fnsave	0(%esp)
		fninit
		fldz
		.p2align 4,0x90
1:
		subl	$64,%ecx
		movq	%mm0,(%edi)
		movq	%mm1,8(%edi)
		movq	%mm2,16(%edi)
		movq	%mm3,24(%edi)
		movq	%mm4,32(%edi)
		movq	%mm5,40(%edi)
		movq	%mm6,48(%edi)
		movq	%mm7,56(%edi)
		addl	$64,%edi
		testl	%ecx,%ecx
		jnz	1b

		frstor	0(%esp)
		addl	$108,%esp

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
dozero6:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx
		movl	$16,%eax
		.p2align 4,0x90
1:
		subl	%eax,%ecx
		movq	%mm0,(%edi)
		movq	%mm1,8(%edi)
		addl	%eax,%edi
		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
dozero7:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%edi
		movl	8+16(%esp),%ecx
		movl	$32,%eax
		.p2align 4,0x90
1:
		subl	%eax,%ecx
		movntdq	%xmm0,(%edi)
		movntdq	%xmm1,16(%edi)
		addl	%eax,%edi
		testl	%ecx,%ecx
		jnz	1b
		sfence

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
fpcleanup:
		fninit
		ret

