/*-
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 2008 The DragonFly Project.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 * $FreeBSD: src/sys/amd64/amd64/support.S,v 1.127 2007/05/23 08:33:04 kib Exp $
 */

#include <machine/asmacros.h>
#include <machine/pmap.h>

#include "assym.s"

	ALIGN_DATA

	.text

/*
 * bzero(ptr:%rdi, bytes:%rsi)
 *
 * Using rep stosq is 70% faster than a %rax loop and almost as fast as
 * a %xmm0 loop on a modern intel cpu.
 *
 * Do not use non-termportal instructions here as we do not know the caller's
 * intent.
 */
ENTRY(bzero)
	movq	%rsi,%rcx
	xorl	%eax,%eax
	shrq	$3,%rcx
	rep
	stosq
	movq	%rsi,%rcx
	andq	$7,%rcx
	jnz	1f
	ret
1:	rep
	stosb
	ret
END(bzero)

	.weak	_bzero
	.equ	_bzero, bzero

/*
 * void *memset(ptr:%rdi, char:%rsi, bytes:%rdx)
 *
 * Same as bzero except we load the char into all byte
 * positions of %rax.  Returns original (ptr).
 */
ENTRY(memset)
	movzbq	%sil,%r8
	movabs  $0x0101010101010101,%rax
	imulq   %r8,%rax

	movq	%rdi,%r9
	movq	%rdx,%rcx
	shrq	$3,%rcx
	rep
	stosq
	movq	%rdx,%rcx
	andq	$7,%rcx
	jnz	1f
	movq	%r9,%rax
	ret
1:	rep
	stosb
	movq	%r9,%rax
	ret
END(memset)

	.weak	_memset
	.equ	_memset, memset

/*
 * pagezero(ptr:%rdi)
 *
 * Using rep stosq is nearly as fast as using %xmm0 on a modern intel cpu,
 * and about 70% faster than a %rax loop.
 *
 * Do not use non-termportal instructions here as we do not know the caller's
 * intent.
 */
#if 0

ENTRY(pagezero)
	movq	$PAGE_SIZE>>3,%rcx
	xorl	%eax,%eax
	rep
	stosq
	ret
END(pagezero)

#endif

ENTRY(pagezero)
	addq	$4096,%rdi
	movq	$-4096,%rax
	ALIGN_TEXT
1:
	movq	$0,(%rdi,%rax,1)
	addq	$8,%rax
	jne	1b
	ret
END(pagezero)

/*
 * bcmp(ptr:%rdi, ptr:%rsi, bytes:%rdx)
 */
ENTRY(bcmp)
	movq	%rdx,%rcx
	shrq	$3,%rcx
	repe
	cmpsq
	jne	1f

	movq	%rdx,%rcx
	andq	$7,%rcx
	je	1f
	repe
	cmpsb
1:
	setne	%al
	movsbl	%al,%eax
	ret
END(bcmp)

/*
 * bcopy(src:%rdi, dst:%rsi, cnt:%rdx)
 *
 * ws@tools.de (Wolfgang Solfrank, TooLs GmbH) +49-228-985800
 */
ENTRY(bcopy)
	xchgq	%rsi,%rdi
	movq	%rdx,%rcx

	movq	%rdi,%rax
	subq	%rsi,%rax
	cmpq	%rcx,%rax			/* overlapping && src < dst? */
	jb	2f

	shrq	$3,%rcx				/* copy by 64-bit words */
	rep
	movsq
	movq	%rdx,%rcx
	andq	$7,%rcx				/* any bytes left? */
	jnz	1f
	ret
1:	rep
	movsb
	ret

	ALIGN_TEXT
2:
	addq	%rcx,%rdi			/* copy backwards */
	addq	%rcx,%rsi
	std
	decq	%rdi
	decq	%rsi
	andq	$7,%rcx				/* any fractional bytes? */
	jz	3f
	rep
	movsb
3:	movq	%rdx,%rcx			/* copy by 32-bit words */
	shrq	$3,%rcx
	subq	$7,%rsi
	subq	$7,%rdi
	rep
	movsq
	cld
	ret
END(bcopy)

	/*
	 * Use in situations where a bcopy function pointer is needed.
	 */
	.weak	_bcopy
	.equ	_bcopy, bcopy

	/*
	 * memmove(dst:%rdi, src:%rsi, cnt:%rdx)
	 * (same as bcopy but without the xchgq, and must return (dst)).
	 *
	 * NOTE: gcc builtin backs-off to memmove() call
	 *
	 * NOTE: We leave %rdi in %rax for the return value.
	 */
ENTRY(memmove)
	movq	%rdx,%rcx
	movq	%rdi,%rax			/* return value */
	movq	%rdi,%r8
	subq	%rsi,%r8
	cmpq	%rcx,%r8			/* overlapping && src < dst? */
	jb	2f

	shrq	$3,%rcx				/* copy by 64-bit words */
	rep
	movsq
	movq	%rdx,%rcx
	andq	$7,%rcx				/* any bytes left? */
	jnz	1f
	ret
1:	rep
	movsb
	ret

	ALIGN_TEXT
2:
	addq	%rcx,%rdi			/* copy backwards */
	addq	%rcx,%rsi
	std
	decq	%rdi
	decq	%rsi
	andq	$7,%rcx				/* any fractional bytes? */
	jz	3f
	rep
	movsb
3:	movq	%rdx,%rcx			/* copy by 32-bit words */
	shrq	$3,%rcx
	subq	$7,%rsi
	subq	$7,%rdi
	rep
	movsq
	cld
	ret
END(memmove)

	.weak	_memmove
	.equ	_memmove, memmove

ENTRY(reset_dbregs)
	movq	$0x200,%rax	/* the manual says that bit 10 must be set to 1 */
	movq	%rax,%dr7	/* disable all breapoints first */
	movq	$0,%rax
	movq	%rax,%dr0
	movq	%rax,%dr1
	movq	%rax,%dr2
	movq	%rax,%dr3
	movq	%rax,%dr6
	ret
END(reset_dbregs)

/*
 * memcpy(dst:%rdi, src:%rsi, bytes:%rdx)
 *
 * NOTE: memcpy does not support overlapping copies
 * NOTE: returns dst
 */
ENTRY(memcpy)
	movq	%rdi,%r8
	movq	%rdx,%rcx
	shrq	$3,%rcx				/* copy by 64-bit words */
	rep
	movsq
	movq	%rdx,%rcx
	andq	$7,%rcx				/* any bytes left? */
	jnz	1f
	movq	%r8,%rax
	ret
1:	rep
	movsb
	movq	%r8,%rax
	ret
END(memcpy)

/* fillw(pat, base, cnt) */
/*       %rdi,%rsi, %rdx */
ENTRY(fillw)
	movq	%rdi,%rax
	movq	%rsi,%rdi
	movq	%rdx,%rcx
	rep
	stosw
	ret
END(fillw)

/*****************************************************************************/
/* copyout and fubyte family                                                 */
/*****************************************************************************/
/*
 * Access user memory from inside the kernel. These routines should be
 * the only places that do this.
 *
 * These routines set curpcb->onfault for the time they execute. When a
 * protection violation occurs inside the functions, the trap handler
 * returns to *curpcb->onfault instead of the function.
 */

/*
 * uint64_t:%rax kreadmem64(addr:%rdi)
 *
 * Read kernel or user memory with fault protection.
 */
ENTRY(kreadmem64)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$kreadmem64fault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	(%rdi),%rax
	movq	$0,PCB_ONFAULT(%rcx)
	ret

kreadmem64fault:
	movq	PCPU(curthread),%rcx
	xorl	%eax,%eax
	movq	TD_PCB(%rcx),%rcx
	movq	%rax,PCB_ONFAULT(%rcx)
	decq	%rax
	ret
END(kreadmem64)

/*
 * std_copyout(from_kernel, to_user, len)  - MP SAFE
 *         %rdi,        %rsi,    %rdx
 */
ENTRY(std_copyout)
	movq	PCPU(curthread),%rax
	movq	TD_PCB(%rax), %rax
	movq	$copyout_fault,PCB_ONFAULT(%rax)
	movq	%rsp,PCB_ONFAULT_SP(%rax)
	testq	%rdx,%rdx			/* anything to do? */
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
	movq	%rsi,%rax
	addq	%rdx,%rax
	jc	copyout_fault
/*
 * XXX STOP USING VM_MAX_USER_ADDRESS.
 * It is an end address, not a max, so every time it is used correctly it
 * looks like there is an off by one error, and of course it caused an off
 * by one error in several places.
 */
	movq	$VM_MAX_USER_ADDRESS,%rcx
	cmpq	%rcx,%rax
	ja	copyout_fault

	xchgq	%rdi,%rsi
	/* bcopy(%rsi, %rdi, %rdx) */
	movq	%rdx,%rcx

	shrq	$3,%rcx
	jz	1f
	rep
	movsq
1:	movq	%rdx,%rcx
	andq	$7,%rcx
	jz	done_copyout
	rep
	movsb

done_copyout:
	xorl	%eax,%eax
	movq	PCPU(curthread),%rdx
	movq	TD_PCB(%rdx), %rdx
	movq	%rax,PCB_ONFAULT(%rdx)
	ret

	ALIGN_TEXT
copyout_fault:
	movq	PCPU(curthread),%rdx
	movq	TD_PCB(%rdx), %rdx
	movq	$0,PCB_ONFAULT(%rdx)
	movq	$EFAULT,%rax
	ret
END(std_copyout)

/*
 * std_copyin(from_user, to_kernel, len) - MP SAFE
 *        %rdi,      %rsi,      %rdx
 */
ENTRY(std_copyin)
	movq	PCPU(curthread),%rax
	movq	TD_PCB(%rax), %rax
	movq	$copyin_fault,PCB_ONFAULT(%rax)
	movq	%rsp,PCB_ONFAULT_SP(%rax)
	testq	%rdx,%rdx			/* anything to do? */
	jz	done_copyin

	/*
	 * make sure address is valid
	 */
	movq	%rdi,%rax
	addq	%rdx,%rax
	jc	copyin_fault
	movq	$VM_MAX_USER_ADDRESS,%rcx
	cmpq	%rcx,%rax
	ja	copyin_fault

	xchgq	%rdi,%rsi
	movq	%rdx,%rcx
	shrq	$3,%rcx				/* copy longword-wise */
	jz	1f
	rep
	movsq
1:	movq	%rdx,%rcx
	andq	$7,%rcx				/* copy remaining bytes */
	jz	done_copyin
	rep
	movsb

done_copyin:
	xorl	%eax,%eax
	movq	PCPU(curthread),%rdx
	movq	TD_PCB(%rdx), %rdx
	movq	%rax,PCB_ONFAULT(%rdx)
	ret

	ALIGN_TEXT
copyin_fault:
	movq	PCPU(curthread),%rdx
	movq	TD_PCB(%rdx), %rdx
	movq	$0,PCB_ONFAULT(%rdx)
	movq	$EFAULT,%rax
	ret
END(std_copyin)

/*
 * casu32 - Compare and set user integer.  Returns -1 or the current value.
 *          dst = %rdi, old = %rsi, new = %rdx
 */
ENTRY(casu32)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-4,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movl	%esi,%eax			/* old */
	lock
	cmpxchgl %edx,(%rdi)			/* new = %edx */

	/*
	 * The old value is in %eax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(casu32)

/*
 * swapu32 - Swap int in user space.  ptr = %rdi, val = %rsi
 */
ENTRY(std_swapu32)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-4,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movq	%rsi,%rax			/* old */
	xchgl	%eax,(%rdi)

	/*
	 * The old value is in %rax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(std_swapu32)

ENTRY(std_fuwordadd32)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-4,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movq	%rsi,%rax			/* qty to add */
	lock xaddl	%eax,(%rdi)

	/*
	 * The old value is in %rax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(std_fuwordadd32)

/*
 * casu64 - Compare and set user word.  Returns -1 or the current value.
 *          dst = %rdi, old = %rsi, new = %rdx
 */
ENTRY(casu64)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-8,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movq	%rsi,%rax			/* old */
	lock
	cmpxchgq %rdx,(%rdi)			/* new = %rdx */

	/*
	 * The old value is in %rax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(casu64)

/*
 * swapu64 - Swap long in user space.  ptr = %rdi, val = %rsi
 */
ENTRY(std_swapu64)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-8,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movq	%rsi,%rax			/* old */
	xchgq	%rax,(%rdi)

	/*
	 * The old value is in %rax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(std_swapu64)

ENTRY(std_fuwordadd64)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-8,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movq	%rsi,%rax			/* value to add */
	lock xaddq	%rax,(%rdi)

	/*
	 * The old value is in %rax.  If the store succeeded it will be the
	 * value we expected (old) from before the store, otherwise it will
	 * be the current value.
	 */

	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(std_fuwordadd64)

/*
 * Fetch (load) a 64-bit word, a 32-bit word, a 16-bit word, or an 8-bit
 * byte from user memory.  All these functions are MPSAFE.
 * addr = %rdi
 */

ENTRY(std_fuword64)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-8,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movq	(%rdi),%rax
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(std_fuword64)

ENTRY(std_fuword32)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-4,%rax
	cmpq	%rax,%rdi			/* verify address is valid */
	ja	fusufault

	movl	(%rdi),%eax
	movq	$0,PCB_ONFAULT(%rcx)
	ret
END(std_fuword32)

ENTRY(std_fubyte)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-1,%rax
	cmpq	%rax,%rdi
	ja	fusufault

	movzbl	(%rdi),%eax
	movq	$0,PCB_ONFAULT(%rcx)
	ret

	ALIGN_TEXT
fusufault:
	movq	PCPU(curthread),%rcx
	xorl	%eax,%eax
	movq	TD_PCB(%rcx), %rcx
	movq	%rax,PCB_ONFAULT(%rcx)
	decq	%rax
	ret
END(std_fubyte)

/*
 * Store a 64-bit word, a 32-bit word, a 16-bit word, or an 8-bit byte to
 * user memory.  All these functions are MPSAFE.
 *
 * addr = %rdi, value = %rsi
 *
 * Write a long
 */
ENTRY(std_suword64)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-8,%rax
	cmpq	%rax,%rdi			/* verify address validity */
	ja	fusufault

	movq	%rsi,(%rdi)
	xorl	%eax,%eax
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	%rax,PCB_ONFAULT(%rcx)
	ret
END(std_suword64)

/*
 * Write an int
 */
ENTRY(std_suword32)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-4,%rax
	cmpq	%rax,%rdi			/* verify address validity */
	ja	fusufault

	movl	%esi,(%rdi)
	xorl	%eax,%eax
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	%rax,PCB_ONFAULT(%rcx)
	ret
END(std_suword32)

ENTRY(std_subyte)
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$fusufault,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS-1,%rax
	cmpq	%rax,%rdi			/* verify address validity */
	ja	fusufault

	movl	%esi,%eax
	movb	%al,(%rdi)
	xorl	%eax,%eax
	movq	PCPU(curthread),%rcx		/* restore trashed register */
	movq	TD_PCB(%rcx), %rcx
	movq	%rax,PCB_ONFAULT(%rcx)
	ret
END(std_subyte)

/*
 * std_copyinstr(from, to, maxlen, int *lencopied) - MP SAFE
 *           %rdi, %rsi, %rdx, %rcx
 *
 *	copy a string from from to to, stop when a 0 character is reached.
 *	return ENAMETOOLONG if string is longer than maxlen, and
 *	EFAULT on protection violations. If lencopied is non-zero,
 *	return the actual length in *lencopied.
 */
ENTRY(std_copyinstr)
	movq	%rdx,%r8			/* %r8 = maxlen */
	movq	%rcx,%r9			/* %r9 = *len */
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$cpystrflt,PCB_ONFAULT(%rcx)
	movq	%rsp,PCB_ONFAULT_SP(%rcx)

	movq	$VM_MAX_USER_ADDRESS,%rax

	/* make sure 'from' is within bounds */
	subq	%rdi,%rax
	jbe	cpystrflt

	/* restrict maxlen to <= VM_MAX_USER_ADDRESS-from */
	cmpq	%rdx,%rax
	jae	1f
	movq	%rax,%rdx
	movq	%rax,%r8
1:
	incq	%rdx

2:
	decq	%rdx
	jz	3f

	movb	(%rdi),%al			/* faster than lodsb+stosb */
	movb	%al,(%rsi)
	leaq	1(%rdi),%rdi
	leaq	1(%rsi),%rsi
	testb	%al,%al
	jnz	2b

	/* Success -- 0 byte reached */
	decq	%rdx
	xorl	%eax,%eax
	jmp	cpystrflt_x
3:
	/* rdx is zero - return ENAMETOOLONG or EFAULT */
	movq	$VM_MAX_USER_ADDRESS,%rax
	cmpq	%rax,%rsi
	jae	cpystrflt
4:
	movq	$ENAMETOOLONG,%rax
	jmp	cpystrflt_x

cpystrflt:
	movq	$EFAULT,%rax

cpystrflt_x:
	/* set *lencopied and return %eax */
	movq	PCPU(curthread),%rcx
	movq	TD_PCB(%rcx), %rcx
	movq	$0,PCB_ONFAULT(%rcx)

	testq	%r9,%r9
	jz	1f
	subq	%rdx,%r8
	movq	%r8,(%r9)
1:
	ret
END(std_copyinstr)

/*
 * copystr(from, to, maxlen, int *lencopied) - MP SAFE
 *         %rdi, %rsi, %rdx, %rcx
 */
ENTRY(copystr)
	movq	%rdx,%r8			/* %r8 = maxlen */

	incq	%rdx
1:
	decq	%rdx
	jz	4f

	movb	(%rdi),%al			/* faster than lodsb+stosb */
	movb	%al,(%rsi)
	leaq	1(%rdi),%rdi
	leaq	1(%rsi),%rsi
	testb	%al,%al
	jnz	1b

	/* Success -- 0 byte reached */
	decq	%rdx
	xorl	%eax,%eax
	jmp	6f
4:
	/* rdx is zero -- return ENAMETOOLONG */
	movq	$ENAMETOOLONG,%rax

6:

	testq	%rcx,%rcx
	jz	7f
	/* set *lencopied and return %rax */
	subq	%rdx,%r8
	movq	%r8,(%rcx)
7:
	ret
END(copystr)

/*
 * Handling of special x86_64 registers and descriptor tables etc
 * %rdi
 */
/* void lgdt(struct region_descriptor *rdp); */
ENTRY(lgdt)
	/* reload the descriptor table */
	lgdt	(%rdi)

	/* flush the prefetch q */
	jmp	1f
	nop
1:
	movl	$KDSEL,%eax
	movl	%eax,%ds
	movl	%eax,%es
	movl	%eax,%fs	/* Beware, use wrmsr to set 64 bit base */
	movl	%eax,%gs	/* Beware, use wrmsr to set 64 bit base */
	movl	%eax,%ss

	/* reload code selector by turning return into intersegmental return */
	popq	%rax
	pushq	$KCSEL
	pushq	%rax
	MEXITCOUNT
	lretq
END(lgdt)

/*****************************************************************************/
/* setjmp, longjmp                                                           */
/*****************************************************************************/

ENTRY(setjmp)
	movq	%rbx,0(%rdi)			/* save rbx */
	movq	%rsp,8(%rdi)			/* save rsp */
	movq	%rbp,16(%rdi)			/* save rbp */
	movq	%r12,24(%rdi)			/* save r12 */
	movq	%r13,32(%rdi)			/* save r13 */
	movq	%r14,40(%rdi)			/* save r14 */
	movq	%r15,48(%rdi)			/* save r15 */
	movq	0(%rsp),%rdx			/* get rta */
	movq	%rdx,56(%rdi)			/* save rip */
	xorl	%eax,%eax			/* return(0); */
	ret
END(setjmp)

ENTRY(longjmp)
	movq	0(%rdi),%rbx			/* restore rbx */
	movq	8(%rdi),%rsp			/* restore rsp */
	movq	16(%rdi),%rbp			/* restore rbp */
	movq	24(%rdi),%r12			/* restore r12 */
	movq	32(%rdi),%r13			/* restore r13 */
	movq	40(%rdi),%r14			/* restore r14 */
	movq	48(%rdi),%r15			/* restore r15 */
	movq	56(%rdi),%rdx			/* get rta */
	movq	%rdx,0(%rsp)			/* put in return frame */
	xorl	%eax,%eax			/* return(1); */
	incl	%eax
	ret
END(longjmp)

/*
 * Support for reading MSRs in the safe manner.
 */
ENTRY(rdmsr_safe)
/* int rdmsr_safe(u_int msr, uint64_t *data) */
	movq	PCPU(curthread),%r8
	movq	TD_PCB(%r8), %r8
	movq	$msr_onfault,PCB_ONFAULT(%r8)
	movq	%rsp,PCB_ONFAULT_SP(%r8)
	movl	%edi,%ecx
	rdmsr			/* Read MSR pointed by %ecx. Returns
				   hi byte in edx, lo in %eax */
	salq	$32,%rdx	/* sign-shift %rdx left */
	movl	%eax,%eax	/* zero-extend %eax -> %rax */
	orq	%rdx,%rax
	movq	%rax,(%rsi)
	xorq	%rax,%rax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(rdmsr_safe)

/*
 * Support for writing MSRs in the safe manner.
 */
ENTRY(wrmsr_safe)
/* int wrmsr_safe(u_int msr, uint64_t data) */
	movq	PCPU(curthread),%r8
	movq	TD_PCB(%r8), %r8
	movq	$msr_onfault,PCB_ONFAULT(%r8)
	movq	%rsp,PCB_ONFAULT_SP(%r8)
	movl	%edi,%ecx
	movl	%esi,%eax
	sarq	$32,%rsi
	movl	%esi,%edx
	wrmsr			/* Write MSR pointed by %ecx. Accepts
				   hi byte in edx, lo in %eax. */
	xorq	%rax,%rax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(wrmsr_safe)

/*
 * MSR operations fault handler
 */
	ALIGN_TEXT
msr_onfault:
	movq	PCPU(curthread),%r8
	movq	TD_PCB(%r8), %r8
	movq	$0,PCB_ONFAULT(%r8)
	movl	$EFAULT,%eax
	ret
