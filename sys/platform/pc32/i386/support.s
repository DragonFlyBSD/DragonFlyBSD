/*-
 * Copyright (c) 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/support.s,v 1.67.2.5 2001/08/15 01:23:50 peter Exp $
 */

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.s"

#define IDXSHIFT	10

	.data

	.globl	memcpy_vector
memcpy_vector:
	.long	asm_generic_memcpy

	.globl	bcopy_vector
bcopy_vector:
	.long	asm_generic_bcopy

	.globl	ovbcopy_vector
ovbcopy_vector:
	.long	asm_generic_bcopy

	.text

/* fillw(pat, base, cnt) */
ENTRY(fillw)
	pushl	%edi
	movl	8(%esp),%eax
	movl	12(%esp),%edi
	movl	16(%esp),%ecx
	cld
	rep
	stosw
	popl	%edi
	ret

/*
 * void bcopy(const void *s, void *d, size_t count)
 *
 * Normal bcopy() vector, an optimized bcopy may be installed in
 * bcopy_vector.
 */
ENTRY(bcopy)
	pushl	%esi
	pushl	%edi
	movl	4+8(%esp),%esi			/* caddr_t from */
	movl	8+8(%esp),%edi			/* caddr_t to */
	movl	12+8(%esp),%ecx			/* size_t  len */
	call	*bcopy_vector
	popl	%edi
	popl	%esi
	ret

/*
 * Generic (integer-only) bcopy() vector.
 */
ENTRY(generic_bcopy)
	pushl	%esi
	pushl	%edi
	movl	4+8(%esp),%esi			/* caddr_t from */
	movl	8+8(%esp),%edi			/* caddr_t to */
	movl	12+8(%esp),%ecx			/* size_t  len */
	call	asm_generic_bcopy
	popl	%edi
	popl	%esi
	ret

ENTRY(ovbcopy)
	pushl	%esi
	pushl	%edi
	movl	4+8(%esp),%esi			/* caddr_t from */
	movl	8+8(%esp),%edi			/* caddr_t to */
	movl	12+8(%esp),%ecx			/* size_t  len */
	call	*ovbcopy_vector
	popl	%edi
	popl	%esi
	ret

/*
 * void *memcpy(void *d, const void *s, size_t count)
 *
 * Note: memcpy does not have to support overlapping copies.
 *
 * Note: (d, s) arguments reversed from bcopy, and memcpy() returns d
 * while bcopy() returns void.
 */
ENTRY(memcpy)
	pushl	%esi
	pushl	%edi
	movl	4+8(%esp),%edi
	movl	8+8(%esp),%esi
	movl	12+8(%esp),%ecx
	call	*memcpy_vector
	movl	4+8(%esp),%eax
	popl	%edi
	popl	%esi
	ret

/*
 * A stack-based on-fault routine is used for more complex PCB_ONFAULT
 * situations (such as memcpy/bcopy/bzero).  In this case the on-fault
 * routine must be pushed on the stack.
 */
stack_onfault:
	ret

/*****************************************************************************/
/* copyout and fubyte family                                                 */
/*****************************************************************************/
/*
 * Access user memory from inside the kernel. These routines and possibly
 * the math- and DOS emulators should be the only places that do this.
 *
 * We have to access the memory with user's permissions, so use a segment
 * selector with RPL 3. For writes to user space we have to additionally
 * check the PTE for write permission, because the 386 does not check
 * write permissions when we are executing with EPL 0. The 486 does check
 * this if the WP bit is set in CR0, so we can use a simpler version here.
 *
 * These routines set curpcb->onfault for the time they execute. When a
 * protection violation occurs inside the functions, the trap handler
 * returns to *curpcb->onfault instead of the function.
 */

/*
 * copyout(from_kernel, to_user, len)  - MP SAFE
 */
ENTRY(copyout)
	movl	PCPU(curthread),%eax
	movl	TD_PCB(%eax),%eax
	pushl	%esi
	pushl	%edi
	pushl	%ebx
	pushl	$copyout_fault2
	movl	$stack_onfault,PCB_ONFAULT(%eax)
	movl	%esp,PCB_ONFAULT_SP(%eax)
	subl	$12,PCB_ONFAULT_SP(%eax)	/* call,ebx,stackedfault */
						/* for *memcpy_vector */
	movl	4+16(%esp),%esi
	movl	8+16(%esp),%edi
	movl	12+16(%esp),%ebx
	testl	%ebx,%ebx			/* anything to do? */
	jz	done_copyout

	/*
	 * Check explicitly for non-user addresses.  If 486 write protection
	 * is being used, this check is essential because we are in kernel
	 * mode so the h/w does not provide any protection against writing
	 * kernel addresses.
	 */

	/*
	 * First, prevent address wrapping.
	 */
	movl	%edi,%eax
	addl	%ebx,%eax
	jc	copyout_fault1
/*
 * XXX STOP USING VM_MAX_USER_ADDRESS.
 * It is an end address, not a max, so every time it is used correctly it
 * looks like there is an off by one error, and of course it caused an off
 * by one error in several places.
 */
	cmpl	$VM_MAX_USER_ADDRESS,%eax
	ja	copyout_fault1

	/*
	 * Convert copyout to memcpy_vector(dest:%edi, src:%esi, count:%ecx)
	 */
	movl	%ebx,%ecx
	call	*memcpy_vector

done_copyout:
	/*
	 * non-error return
	 */
	addl	$4,%esp
	movl	PCPU(curthread),%edx
	xorl	%eax,%eax
	movl	TD_PCB(%edx),%edx
	popl	%ebx
	popl	%edi
	popl	%esi
	movl	%eax,PCB_ONFAULT(%edx)
	ret

	ALIGN_TEXT
copyout_fault1:
	addl	$4,%esp		/* skip pushed copyout_fault vector */
copyout_fault2:
	popl	%ebx
	popl	%edi
	popl	%esi
	movl	PCPU(curthread),%edx
	movl	TD_PCB(%edx),%edx
	movl	$0,PCB_ONFAULT(%edx)
	movl	$EFAULT,%eax
	ret

/*
 * copyin(from_user, to_kernel, len) - MP SAFE
 */

ENTRY(copyin)
	movl	PCPU(curthread),%eax
	movl	TD_PCB(%eax),%eax
	pushl	%esi
	pushl	%edi
	pushl	$copyin_fault2
	movl	$stack_onfault,PCB_ONFAULT(%eax)
	movl	%esp,PCB_ONFAULT_SP(%eax)
	subl	$12,PCB_ONFAULT_SP(%eax)	/* call,ebx,stackedfault */
						/* for *memcpy_vector */
	movl	4+12(%esp),%esi			/* caddr_t from */
	movl	8+12(%esp),%edi			/* caddr_t to */
	movl	12+12(%esp),%ecx		/* size_t  len */

	/*
	 * make sure address is valid
	 */
	movl	%esi,%edx
	addl	%ecx,%edx
	jc	copyin_fault1
	cmpl	$VM_MAX_USER_ADDRESS,%edx
	ja	copyin_fault1

	/*
	 * Call memcpy(destination:%edi, source:%esi, bytes:%ecx)
	 */
	call	*memcpy_vector

	/*
	 * return 0 (no error)
	 */
	addl	$4,%esp
	movl	PCPU(curthread),%edx
	xorl	%eax,%eax
	movl	TD_PCB(%edx),%edx
	popl	%edi
	popl	%esi
	movl	%eax,PCB_ONFAULT(%edx)
	ret

	/*
	 * return EFAULT
	 */
	ALIGN_TEXT
copyin_fault1:
	addl	$4,%esp		/* skip pushed copyin_fault vector */
copyin_fault2:
	popl	%edi
	popl	%esi
	movl	PCPU(curthread),%edx
	movl	TD_PCB(%edx),%edx
	movl	$0,PCB_ONFAULT(%edx)
	movl	$EFAULT,%eax
	ret

/*
 * casuword.  Compare and set user word.  Returns -1 or the current value.
 */

ENTRY(casuword)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx			/* dst */
	movl	8(%esp),%eax			/* old */
	movl	12(%esp),%ecx			/* new */

	cmpl	$VM_MAX_USER_ADDRESS-4,%edx	/* verify address is valid */
	ja	fusufault

	lock
	cmpxchgl %ecx,(%edx)			/* Compare and set. */

	/*
	 * The old value is in %eax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$0,PCB_ONFAULT(%ecx)
	ret
END(casuword)

/*
 * fu{byte,sword,word} - MP SAFE
 *
 *	Fetch a byte (sword, word) from user memory
 */
ENTRY(fuword)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx			/* from */

	cmpl	$VM_MAX_USER_ADDRESS-4,%edx	/* verify address is valid */
	ja	fusufault

	movl	(%edx),%eax
	movl	$0,PCB_ONFAULT(%ecx)
	ret

/*
 * fusword - MP SAFE
 */
ENTRY(fusword)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx

	cmpl	$VM_MAX_USER_ADDRESS-2,%edx
	ja	fusufault

	movzwl	(%edx),%eax
	movl	$0,PCB_ONFAULT(%ecx)
	ret

/*
 * fubyte - MP SAFE
 */
ENTRY(fubyte)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx

	cmpl	$VM_MAX_USER_ADDRESS-1,%edx
	ja	fusufault

	movzbl	(%edx),%eax
	movl	$0,PCB_ONFAULT(%ecx)
	ret

	ALIGN_TEXT
fusufault:
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	xorl	%eax,%eax
	movl	%eax,PCB_ONFAULT(%ecx)
	decl	%eax
	ret

/*
 * su{byte,sword,word,word32} - MP SAFE
 *
 *	Write a long to user memory
 */
ENTRY(suword)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx

	cmpl	$VM_MAX_USER_ADDRESS-4,%edx	/* verify address validity */
	ja	fusufault

	movl	8(%esp),%eax
	movl	%eax,(%edx)
	xorl	%eax,%eax
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

/*
 * Write an integer to user memory
 */
ENTRY(suword32)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx

	cmpl	$VM_MAX_USER_ADDRESS-4,%edx	/* verify address validity */
	ja	fusufault

	movl	8(%esp),%eax
	movl	%eax,(%edx)
	xorl	%eax,%eax
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

/*
 * susword - MP SAFE
 */
ENTRY(susword)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx

	cmpl	$VM_MAX_USER_ADDRESS-2,%edx	/* verify address validity */
	ja	fusufault

	movw	8(%esp),%ax
	movw	%ax,(%edx)
	xorl	%eax,%eax
	movl	PCPU(curthread),%ecx			/* restore trashed register */
	movl	TD_PCB(%ecx),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

/*
 * subyte - MP SAFE
 */
ENTRY(subyte)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$fusufault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)
	movl	4(%esp),%edx

	cmpl	$VM_MAX_USER_ADDRESS-1,%edx	/* verify address validity */
	ja	fusufault

	movb	8(%esp),%al
	movb	%al,(%edx)
	xorl	%eax,%eax
	movl	PCPU(curthread),%ecx		/* restore trashed register */
	movl	TD_PCB(%ecx),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

/*
 * copyinstr(from, to, maxlen, int *lencopied) - MP SAFE
 *
 *	copy a string from from to to, stop when a 0 character is reached.
 *	return ENAMETOOLONG if string is longer than maxlen, and
 *	EFAULT on protection violations. If lencopied is non-zero,
 *	return the actual length in *lencopied.
 */
ENTRY(copyinstr)
	pushl	%esi
	pushl	%edi
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$cpystrflt,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)

	movl	12(%esp),%esi			/* %esi = from */
	movl	16(%esp),%edi			/* %edi = to */
	movl	20(%esp),%edx			/* %edx = maxlen */

	movl	$VM_MAX_USER_ADDRESS,%eax

	/* make sure 'from' is within bounds */
	subl	%esi,%eax
	jbe	cpystrflt

	/* restrict maxlen to <= VM_MAX_USER_ADDRESS-from */
	cmpl	%edx,%eax
	jae	1f
	movl	%eax,%edx
	movl	%eax,20(%esp)
1:
	incl	%edx
	cld

2:
	decl	%edx
	jz	3f

	lodsb
	stosb
	orb	%al,%al
	jnz	2b

	/* Success -- 0 byte reached */
	decl	%edx
	xorl	%eax,%eax
	jmp	cpystrflt_x
3:
	/* edx is zero - return ENAMETOOLONG or EFAULT */
	cmpl	$VM_MAX_USER_ADDRESS,%esi
	jae	cpystrflt
4:
	movl	$ENAMETOOLONG,%eax
	jmp	cpystrflt_x

cpystrflt:
	movl	$EFAULT,%eax

cpystrflt_x:
	/* set *lencopied and return %eax */
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	$0,PCB_ONFAULT(%ecx)
	movl	20(%esp),%ecx
	subl	%edx,%ecx
	movl	24(%esp),%edx
	testl	%edx,%edx
	jz	1f
	movl	%ecx,(%edx)
1:
	popl	%edi
	popl	%esi
	ret


/*
 * copystr(from, to, maxlen, int *lencopied) - MP SAFE
 */
ENTRY(copystr)
	pushl	%esi
	pushl	%edi

	movl	12(%esp),%esi			/* %esi = from */
	movl	16(%esp),%edi			/* %edi = to */
	movl	20(%esp),%edx			/* %edx = maxlen */
	incl	%edx
	cld
1:
	decl	%edx
	jz	4f
	lodsb
	stosb
	orb	%al,%al
	jnz	1b

	/* Success -- 0 byte reached */
	decl	%edx
	xorl	%eax,%eax
	jmp	6f
4:
	/* edx is zero -- return ENAMETOOLONG */
	movl	$ENAMETOOLONG,%eax

6:
	/* set *lencopied and return %eax */
	movl	20(%esp),%ecx
	subl	%edx,%ecx
	movl	24(%esp),%edx
	testl	%edx,%edx
	jz	7f
	movl	%ecx,(%edx)
7:
	popl	%edi
	popl	%esi
	ret

ENTRY(bcmp)
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%edx
	xorl	%eax,%eax

	movl	%edx,%ecx
	shrl	$2,%ecx
	cld					/* compare forwards */
	repe
	cmpsl
	jne	1f

	movl	%edx,%ecx
	andl	$3,%ecx
	repe
	cmpsb
	je	2f
1:
	incl	%eax
2:
	popl	%esi
	popl	%edi
	ret


/*
 * Handling of special 386 registers and descriptor tables etc
 */
/* void lgdt(struct region_descriptor *rdp); */
ENTRY(lgdt)
	/* reload the descriptor table */
	movl	4(%esp),%eax
	lgdt	(%eax)

	/* flush the prefetch q */
	jmp	1f
	nop
1:
	/* reload "stale" selectors */
	movl	$KDSEL,%eax
	mov	%ax,%ds
	mov	%ax,%es
	mov	%ax,%gs
	mov	%ax,%ss
	movl	$KPSEL,%eax
	mov	%ax,%fs
	mov	%ax,%gs

	/* reload code selector by turning return into intersegmental return */
	movl	(%esp),%eax
	pushl	%eax
	movl	$KCSEL,4(%esp)
	lret

/*
 * void lidt(struct region_descriptor *rdp);
 */
ENTRY(lidt)
	movl	4(%esp),%eax
	lidt	(%eax)
	ret

/*
 * void lldt(u_short sel)
 */
ENTRY(lldt)
	lldt	4(%esp)
	ret

/*
 * void ltr(u_short sel)
 */
ENTRY(ltr)
	ltr	4(%esp)
	ret

/* ssdtosd(*ssdp,*sdp) */
ENTRY(ssdtosd)
	pushl	%ebx
	movl	8(%esp),%ecx
	movl	8(%ecx),%ebx
	shll	$16,%ebx
	movl	(%ecx),%edx
	roll	$16,%edx
	movb	%dh,%bl
	movb	%dl,%bh
	rorl	$8,%ebx
	movl	4(%ecx),%eax
	movw	%ax,%dx
	andl	$0xf0000,%eax
	orl	%eax,%ebx
	movl	12(%esp),%ecx
	movl	%edx,(%ecx)
	movl	%ebx,4(%ecx)
	popl	%ebx
	ret

/* load_cr0(cr0) */
ENTRY(load_cr0)
	movl	4(%esp),%eax
	movl	%eax,%cr0
	ret

/* rcr0() */
ENTRY(rcr0)
	movl	%cr0,%eax
	ret

/* rcr3() */
ENTRY(rcr3)
	movl	%cr3,%eax
	ret

/* void load_cr3(caddr_t cr3) */
ENTRY(load_cr3)
#if defined(SWTCH_OPTIM_STATS)
	incl	_tlb_flush_count
#endif
	movl	4(%esp),%eax
	movl	%eax,%cr3
	ret

/* rcr4() */
ENTRY(rcr4)
	movl	%cr4,%eax
	ret

/* void load_cr4(caddr_t cr4) */
ENTRY(load_cr4)
	movl	4(%esp),%eax
	movl	%eax,%cr4
	ret

/* void reset_dbregs() */
ENTRY(reset_dbregs)
	movl    $0,%eax
	movl    %eax,%dr7     /* disable all breapoints first */
	movl    %eax,%dr0
	movl    %eax,%dr1
	movl    %eax,%dr2
	movl    %eax,%dr3
	movl    %eax,%dr6
	ret

/*****************************************************************************/
/* setjmp, longjmp                                                           */
/*****************************************************************************/

ENTRY(setjmp)
	movl	4(%esp),%eax
	movl	%ebx,(%eax)			/* save ebx */
	movl	%esp,4(%eax)			/* save esp */
	movl	%ebp,8(%eax)			/* save ebp */
	movl	%esi,12(%eax)			/* save esi */
	movl	%edi,16(%eax)			/* save edi */
	movl	(%esp),%edx			/* get rta */
	movl	%edx,20(%eax)			/* save eip */
	xorl	%eax,%eax			/* return(0); */
	ret

ENTRY(longjmp)
	movl	4(%esp),%eax
	movl	(%eax),%ebx			/* restore ebx */
	movl	4(%eax),%esp			/* restore esp */
	movl	8(%eax),%ebp			/* restore ebp */
	movl	12(%eax),%esi			/* restore esi */
	movl	16(%eax),%edi			/* restore edi */
	movl	20(%eax),%edx			/* get rta */
	movl	%edx,(%esp)			/* put in return frame */
	xorl	%eax,%eax			/* return(1); */
	incl	%eax
	ret

/*
 * Support for reading MSRs in the safe manner.
 */
ENTRY(rdmsr_safe)
/* int rdmsr_safe(u_int msr, uint64_t *data) */
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx), %ecx
	movl	$msr_onfault,PCB_ONFAULT(%ecx)
	movl	%esp,PCB_ONFAULT_SP(%ecx)

	movl	4(%esp),%ecx
	rdmsr
	movl	8(%esp),%ecx
	movl	%eax,(%ecx)
	movl	%edx,4(%ecx)
	xorl	%eax,%eax

	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx), %ecx
	movl	%eax,PCB_ONFAULT(%ecx)

	ret

/*
 * MSR operations fault handler
 */
	ALIGN_TEXT
msr_onfault:
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx), %ecx
	movl	$0,PCB_ONFAULT(%ecx)
	movl	$EFAULT,%eax
	ret

/*
 * Support for BB-profiling (gcc -a).  The kernbb program will extract
 * the data from the kernel.
 */

	.data
	ALIGN_DATA
	.globl bbhead
bbhead:
	.long 0

	.text
NON_GPROF_ENTRY(__bb_init_func)
	movl	4(%esp),%eax
	movl	$1,(%eax)
	movl	bbhead,%edx
	movl	%edx,16(%eax)
	movl	%eax,bbhead
	.byte	0xc3				/* avoid macro for `ret' */
