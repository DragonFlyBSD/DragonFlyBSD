/*-
 * Copyright 1996-1998 John D. Polstra.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Dynamic program start.  argv pointer is in %rdi.
 */
	.text
	.align	4
	.globl	resident_start
	.globl	.rtld_start
	.type	.rtld_start,@function
resident_start:
.rtld_start:
	xorq	%rbp,%rbp		# Clear frame pointer for good form
	subq	$24,%rsp		# A place to store exit procedure addr
	movq	%rdi,%r12
	movq	%rsp,%rsi		# save address of exit proc
	movq	%rsp,%rdx		# construct address of obj_main
	addq	$8,%rdx
	call	_rtld@PLT		# Call rtld(sp); returns entry point
	popq	%rsi			# Get exit procedure address
	movq	%r12,%rdi		# *ap
/*
 * At this point, %rax contains the entry point of the main program, and
 * %rdx contains a pointer to a termination function that should be
 * registered with atexit().  (crt1.o registers it.)
 */
.globl .rtld_goto_main
.rtld_goto_main:	# This symbol exists just to make debugging easier.
	jmp	*%rax			# Enter main program


/*
 * Binder entry point.  Control is transferred to here by code in the PLT.
 * On entry, there are two arguments on the stack.  In ascending address
 * order, they are (1) "obj", a pointer to the calling object's Obj_Entry,
 * and (2) "reloff", the byte offset of the appropriate relocation entry
 * in the PLT relocation table.
 *
 * We are careful to preserve all registers, even the caller-save
 * registers.  That is because this code may be invoked by low-level
 * assembly-language code that is not ABI-compliant.
 *
 * Stack map:
 * reloff       0x60
 * obj          0x58
 * spare	0x50
 * rflags       0x48
 * rax          0x40
 * rdx          0x38
 * rcx          0x30
 * rsi          0x28
 * rdi          0x20
 * r8           0x18
 * r9           0x10
 * r10          0x8
 * r11          0x0
 */
	.align	4
	.globl	_rtld_bind_start
	.type	_rtld_bind_start,@function
_rtld_bind_start:
	subq	$8,%rsp
	pushfq				# Save rflags
	pushq	%rax			# Save %rax
	pushq	%rdx			# Save %rdx
	pushq	%rcx			# Save %rcx
	pushq	%rsi			# Save %rsi
	pushq	%rdi			# Save %rdi
	pushq	%r8			# Save %r8
	pushq	%r9			# Save %r9
	pushq	%r10			# Save %r10
	pushq	%r11			# Save %r11
	subq	$128,%rsp
	movups	%xmm0,0(%rsp)		# Save xmm regs that might be
	movups	%xmm1,16(%rsp)		# used for scratch.  clang may
	movups	%xmm2,32(%rsp)		# use them for fast structural copies.
	movups	%xmm3,48(%rsp)
	movups	%xmm4,64(%rsp)
	movups	%xmm5,80(%rsp)
	movups	%xmm6,96(%rsp)
	movups	%xmm7,112(%rsp)

	movq	128+0x58(%rsp),%rdi	# Fetch obj argument    (arg 1)
	movq	128+0x60(%rsp),%rsi	# Fetch reloff argument (arg 2)
	leaq	128+0x68(%rsp),%rdx	# Fetch original stack pointer (arg 3)
	leaq	(%rsi,%rsi,2),%rsi	# multiply by 3
	leaq	(,%rsi,8),%rsi		# now 8, for 24 (sizeof Elf_Rela)

	call	_rtld_bind@PLT		# Transfer control to the binder
	/* Now %rax contains the entry point of the function being called. */

	movq	%rax,128+0x60(%rsp)	# Store target over reloff argument

	movups	0(%rsp),%xmm0
	movups	16(%rsp),%xmm1
	movups	32(%rsp),%xmm2
	movups	48(%rsp),%xmm3
	movups	64(%rsp),%xmm4
	movups	80(%rsp),%xmm5
	movups	96(%rsp),%xmm6
	movups	112(%rsp),%xmm7
	leaq	128(%rsp),%rsp

	popq	%r11			# Restore %r11
	popq	%r10			# Restore %r10
	popq	%r9			# Restore %r9
	popq	%r8			# Restore %r8
	popq	%rdi			# Restore %rdi
	popq	%rsi			# Restore %rsi
	popq	%rcx			# Restore %rcx
	popq	%rdx			# Restore %rdx
	popq	%rax			# Restore %rax
	popfq				# Restore rflags
	leaq	16(%rsp),%rsp		# Discard spare, obj,
					#  do not change rflags
	ret				# "Return" to target address

	.section .note.GNU-stack,"",%progbits
