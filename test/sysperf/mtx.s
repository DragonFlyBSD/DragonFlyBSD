		/*
		 * $DragonFly: src/test/sysperf/mtx.s,v 1.1 2003/08/12 02:29:44 dillon Exp $
		 */
		.text
		.globl	get_mtx
		.globl	rel_mtx

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
