
		/*
		 * memcpy.S
		 *
		 * AMD64: MOVNTQ vs MOVQ, MOVNTDQ vs MOVDQ[A/U], PREFETCH[x]
		 *
		 *	NT stands for 'non-temportal', which basically means
		 *	'bypass L1 cache on write'.  Write bandwidth is
		 *	effectively reduced to the L2 cache bandwidth but
		 *	the L1 cache will not be wiped out by the copy.
		 *
		 *	DO NOT MIX 'nt' and standard writes!  Your performance
		 *	will go poof.
		 *
		 * PREFETCH[NTA,T0,T1,T2]
		 *
		 *	These instructions prefetch a cache line (typically
		 *	128 bytes).  'NT' means 'non-temporal', which bypasses
		 *	the L1 cache if the data is not already in the L1 
		 *	cache.  HOWEVER, using PREFETCHNT can put a slow memory
		 *	op in the cpu's memory request queue if a L1 or L2
		 *	miss occurs, and it can stall an L1-cache-hit access
		 *	for a small but noticeable period of time, so it is
		 *	a good idea not to put a memory op just after a 
		 *	prefetchnta instruction.
		 *
		 *	You can get better L2 bandwidth using prefetchnt but
		 *	it will not be much more then prefetcht0 and 
		 *	'prefetcht0' will give you better cache-miss
		 *	bandwidth.
		 *
		 *	The prefetch has to be done far enough ahead to do
		 *	some good, but it only has a significant effect when
		 *	it is able to move date from L2 to L1.  Prefetching
		 *	from main memory does not have a significant effect
		 *	durign a copy or zeroing operation because main
		 *	memory bandwidth is already saturated.
		 *
		 * $DragonFly: src/test/sysperf/memcpy.S,v 1.1 2004/04/29 16:14:53 dillon Exp $
		 */
		.text
		.globl	docopy1
		.globl	docopy2
		.globl	docopy3
		.globl	docopy4
		.globl	docopy5
		.globl	docopy6
		.globl	docopy7
		.globl	fpcleanup

		.p2align 4,0x90
docopy1:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx
		shrl	$2,%ecx
		cld
		rep
		movsl
		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
docopy2:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx
		addl	%ecx,%esi
		addl	%ecx,%edi
		shrl	$2,%ecx
		std
		rep
		movsl
		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
docopy3:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx

		.p2align 4,0x90
1:
		movl	(%esi),%eax
		movl	4(%esi),%ebx
		movl	8(%esi),%edx
		movl	%eax,(%edi)
		movl	12(%esi),%eax
		movl	%ebx,4(%edi)
		movl	16(%esi),%ebx
		movl	%edx,8(%edi)
		movl	20(%esi),%edx
		movl	%eax,12(%edi)
		movl	24(%esi),%eax
		movl	%ebx,16(%edi)
		movl	28(%esi),%ebx
		movl	%edx,20(%edi)
		prefetcht0 96(%esi)
		subl	$32,%ecx
		movl	%eax,24(%edi)
		addl	$32,%esi
		movl	%ebx,28(%edi)
		addl	$32,%edi

		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
docopy4:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx

		.p2align 4,0x90
1:
		movl	(%esi),%eax
		movl	4(%esi),%ebx
		addl	$8,%esi
		prefetcht0 64(%esi)
		subl	$8,%ecx
		movl	%eax,(%edi)
		movl	%ebx,4(%edi)
		addl	$8,%edi
		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
docopy5:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx

		.p2align 4,0x90
1:
		movq	(%esi),%mm0
		movq	8(%esi),%mm1
		movq	16(%esi),%mm2
		movq	24(%esi),%mm3
		movq	32(%esi),%mm4
		movq	40(%esi),%mm5
		movq	48(%esi),%mm6
		movq	56(%esi),%mm7
		prefetchnta 128(%esi)
		subl	$64,%ecx
		addl	$64,%esi
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

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
docopy6:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx
		movl	$16,%eax

		.p2align 4,0x90
1:
		prefetcht0 96(%esi)
		subl	%eax,%ecx
		movq	(%esi),%mm0
		movq	8(%esi),%mm1
		addl	%eax,%esi
		movntq	%mm0,(%edi)
		movntq	%mm1,8(%edi)
		addl	%eax,%edi
		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
docopy7:
		pushl	%esi
		pushl	%edi
		pushl	%ecx
		pushl	%ebx

		movl	4+16(%esp),%esi
		movl	8+16(%esp),%edi
		movl	12+16(%esp),%ecx
		movl	$128,%eax

		.p2align 4,0x90
1:
		movdqa	(%esi),%xmm0
		movdqa	16(%esi),%xmm1
		movdqa	32(%esi),%xmm2
		movdqa	48(%esi),%xmm3
		movdqa	64(%esi),%xmm4
		movdqa	80(%esi),%xmm5
		movdqa	96(%esi),%xmm6
		movdqa	112(%esi),%xmm7
		subl	%eax,%ecx
		addl	%eax,%esi
		movntdq  %xmm0,(%edi)
		movntdq  %xmm1,16(%edi)
		movntdq  %xmm2,32(%edi)
		movntdq  %xmm3,48(%edi)
		movntdq  %xmm4,64(%edi)
		movntdq  %xmm5,80(%edi)
		movntdq  %xmm6,96(%edi)
		movntdq  %xmm7,112(%edi)
		addl	%eax,%edi
		testl	%ecx,%ecx
		jnz	1b

		popl	%ebx
		popl	%ecx
		popl	%edi
		popl	%esi
		ret

		.p2align 4,0x90
fpcleanup:
		fninit
		ret

