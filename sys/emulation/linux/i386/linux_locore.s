/* $FreeBSD: src/sys/i386/linux/linux_locore.s,v 1.5.2.3 2001/11/05 19:08:23 marcel Exp $ */
/* $DragonFly: src/sys/emulation/linux/i386/linux_locore.s,v 1.3 2003/08/07 21:17:18 dillon Exp $ */

#include "linux_assym.h"			/* system definitions */
#include <machine/asmacros.h>			/* miscellaneous asm macros */
#include "linux_syscall.h"			/* system call numbers */

NON_GPROF_ENTRY(linux_sigcode)
	call	*LINUX_SIGF_HANDLER(%esp)
	leal	LINUX_SIGF_SC(%esp),%ebx	/* linux scp */
	movl	LINUX_SC_GS(%ebx),%gs
	movl	%esp, %ebx			/* pass sigframe */
	push	%eax				/* fake ret addr */
	movl	$LINUX_SYS_linux_sigreturn,%eax	/* linux_sigreturn() */
	int	$0x80				/* enter kernel with args */
0:	jmp	0b
	ALIGN_TEXT
/* XXXXX */
linux_rt_sigcode:
	call	*LINUX_RT_SIGF_HANDLER(%esp)
	leal	LINUX_RT_SIGF_UC(%esp),%ebx	/* linux ucp */
	movl	LINUX_SC_GS(%ebx),%gs
	push	%eax				/* fake ret addr */
	movl	$LINUX_SYS_linux_rt_sigreturn,%eax   /* linux_rt_sigreturn() */
	int	$0x80				/* enter kernel with args */
0:	jmp	0b
	ALIGN_TEXT
/* XXXXX */
linux_esigcode:

	.data
	.globl	linux_szsigcode, linux_sznonrtsigcode
linux_szsigcode:
	.long	linux_esigcode-linux_sigcode
linux_sznonrtsigcode:
	.long	linux_rt_sigcode-linux_sigcode
