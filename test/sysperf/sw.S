
		/*
		 * sw.S
		 *
		 * $DragonFly: src/test/sysperf/sw.S,v 1.1 2003/08/12 02:29:44 dillon Exp $
		 */
		.text
		.globl	qswitch, qrestore, qinit, qirestore, qstart

#if USE_ALL

#define PUSHAL		pushal
#define POPAL		popal

#endif

#if USE_CALLU1

#define PUSHAL		\
	pushl	%ebx ;	\
	pushl	%esi ;	\
	pushl	%edi ;	\
	pushl	%ebp 

#define POPAL		\
	popl	%ebp ;	\
	popl	%edi ;	\
	popl	%esi ;	\
	popl	%ebx

#endif

#if USE_CALLU2

#define PUSHAL			\
	subl	$16,%esp ;	\
	movl	%ebx,(%esp) ;	\
	movl	%esi,4(%esp) ;	\
	movl	%edi,8(%esp) ;	\
	movl	%ebp,12(%esp)

#define POPAL			\
	movl	(%esp),%ebx ;	\
	movl	4(%esp),%esi ;  \
	movl	8(%esp),%edi ;  \
	movl	12(%esp),%ebp ; \
	addl	$16,%esp

#endif

		/* qswitch(&olddesc, newdesc) */
qswitch:
		movl	8(%esp),%eax	/* new state */
		movl	4(%esp),%edx	/* save old state */
		PUSHAL
		pushl	$qrestore
		movl	%esp,(%edx)
		movl	%eax,%esp
		ret

		/* qstart(newdesc) */
qstart:
		movl	4(%esp),%esp
		ret

qrestore:
		POPAL
		ret

		/* newdesc = qinit(topofstk, func, arg) */
qinit:
		movl	4(%esp),%eax
		subl	$16,%eax
		movl	$qirestore,0(%eax)
		movl	8(%esp),%ecx
		movl	%ecx,4(%eax)
		movl	$0,8(%eax)
		movl	12(%esp),%ecx
		movl	%ecx,12(%eax)
		ret /* return eax */

qirestore:
		ret
