		/*
		 * $DragonFly: src/test/sysperf/mtx.s,v 1.2 2006/04/22 22:32:52 dillon Exp $
		 */
		.text
		.globl	get_mtx
		.globl	rel_mtx
		.globl	try_spin_mtx
		.globl	rel_spin_mtx

get_mtx:
		movl	mtx,%edx
		movl	4(%esp),%ecx
1:		subl	%eax,%eax
		lock cmpxchgl %ecx,(%edx)
		jnz	1b
		ret

rel_mtx:
		movl	mtx,%edx
		movl $0,(%edx)
		ret

try_spin_mtx:
		movl	mtx,%edx
		movl	$1,%eax
		xchgl	%eax,(%edx)
		ret

rel_spin_mtx:
		movl	mtx,%edx
		movl	$0,(%edx)
		ret
