/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * bcopy(source:%esi, target:%edi, count:%ecx)
 *
 *	note: esi, edi, eax, ecx, and edx may be destroyed
 */

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.s"

	.text

	/*
	 * bcopyb() is a 'dumb' byte-granular bcopy.  It is only used by
	 * devices which need to bcopy device-mapped memory which cannot
	 * otherwise handle 16 or 32 bit ops.
	 */
	ALIGN_TEXT
ENTRY(bcopyb)
	pushl	%esi
	pushl	%edi
	movl	12(%esp),%esi
	movl	16(%esp),%edi
	movl	20(%esp),%ecx
	movl	%edi,%eax
	subl	%esi,%eax
	cmpl	%ecx,%eax			/* overlapping && src < dst? */
	jb	1f
	cld					/* nope, copy forwards */
	rep
	movsb
	popl	%edi
	popl	%esi
	ret

	ALIGN_TEXT
1:
	addl	%ecx,%edi			/* copy backwards. */
	addl	%ecx,%esi
	decl	%edi
	decl	%esi
	std
	rep
	movsb
	popl	%edi
	popl	%esi
	cld
	ret

	/*
	 * bcopyi(s, d, len)	(NON OVERLAPPING)
	 *
	 * This is a dumb 32-bit-granular bcopy
	 */
	ALIGN_TEXT
ENTRY(bcopyi)
	pushl	%esi
	pushl	%edi
	movl	12(%esp),%esi
	movl	16(%esp),%edi
	movl	20(%esp),%ecx
	shrl	$2,%ecx
	cld
	rep
	movsl
	popl	%edi
	popl	%esi
	ret

	/*
	 * If memcpy/bcopy is called as part of a copyin or copyout, the
	 * on-fault routine is set up to do a 'ret'.  We have to restore
	 * %ebx and return to the copyin/copyout fault handler.
	 */
generic_onfault:
	popl	%ebx
	addl	$4,%esp		/* skip normal return vector */
	ret			/* return to copyin/copyout fault handler */

	/*
	 * GENERIC BCOPY() - COPY DIRECTION CHECK AND FORWARDS COPY
	 *
	 *	Reasonably optimal on all modern machines.
	 */

	SUPERALIGN_TEXT
ENTRY(asm_generic_memcpy)	/* memcpy() entry point use optimal copy */
	pushl	%ebx
	pushl	$generic_onfault
	jmp	2f

	SUPERALIGN_TEXT
ENTRY(asm_generic_bcopy)
	pushl	%ebx
	pushl	$generic_onfault
	cmpl	%esi,%edi	/* if (edi < esi) fwd copy ok */
	jb	2f
	addl	%ecx,%esi
	cmpl	%esi,%edi	/* if (edi < esi + count) do bkwrds copy */
	jb	10f
	subl	%ecx,%esi
	jmp	2f

	SUPERALIGN_TEXT
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
	movl	%eax,24(%edi)
	addl	$32,%esi
	movl	%ebx,28(%edi)
	addl	$32,%edi
2:
	subl	$32,%ecx
	jae	1b
	addl	$32,%ecx
	jz	3f
	cld
	rep
	movsb
3:
	addl	$4,%esp
	popl	%ebx
	ret

	/*
	 * GENERIC_BCOPY() - BACKWARDS COPY
	 */
	SUPERALIGN_TEXT
10:
	addl	%ecx,%edi
	jmp	12f

	SUPERALIGN_TEXT
11:
	movl    -4(%esi),%eax
	movl    -8(%esi),%ebx
	movl    -12(%esi),%edx
	movl    %eax,-4(%edi)
	movl    -16(%esi),%eax
	movl    %ebx,-8(%edi)
	movl    -20(%esi),%ebx
	movl    %edx,-12(%edi)
	movl    -24(%esi),%edx
	movl    %eax,-16(%edi)
	movl    -28(%esi),%eax
	movl    %ebx,-20(%edi)
	movl    -32(%esi),%ebx
	movl    %edx,-24(%edi)
	movl    %eax,-28(%edi)
	subl    $32,%esi
	movl    %ebx,-32(%edi)
	subl    $32,%edi
12:
	subl    $32,%ecx
	jae	11b
	addl	$32,%ecx
	jz	13f
	decl	%esi
	decl	%edi
	std
	rep
	movsb
	cld
13:
	addl	$4,%esp
	popl	%ebx
	ret

	/*
	 * MMX BCOPY() - COPY DIRECTION CHECK AND FORWARDS COPY
	 *
	 * note: esi, edi, eax, ecx, and edx are allowed to be destroyed.
	 *
	 * In order for the kernel to be able to use the FPU:
	 *
	 *	(1) The kernel may not already be using the fpu.
	 *
	 *	(2) If the fpu is owned by the application, we must save
	 *	    its state.  If the fpu is not owned by the application
	 *	    the application's saved fp state may already exist
	 *	    in TD_SAVEFPU.
	 *
	 *	(3) We cannot allow the kernel to overwrite the application's
	 *	    FPU state with our own, so we make sure the application's
	 *	    FPU state has been saved and then point TD_SAVEFPU at a
	 *	    temporary fpu save area in the globaldata structure.
	 *	    
	 * RACES/ALGORITHM:
	 *
	 *	If gd_npxthread is not NULL we must save the application's
	 *	current FP state to the current save area and then NULL
	 *	out gd_npxthread to interlock against new interruptions
	 *	changing the FP state further.
	 *
	 *	If gd_npxthread is NULL the FP unit is in a known 'safe'
	 *	state and may be used once the new save area is installed.
	 *
	 *	race(1): If an interrupt occurs just prior to calling fxsave
	 *	all that happens is that fxsave gets a npxdna trap, restores
	 *	the app's environment, and immediately traps, restores,
	 *	and saves it again.
	 *
	 *	race(2): No interrupt can safely occur after we NULL-out
	 *	npxthread until we fnclex, because the kernel assumes that
	 *	the FP unit is in a safe state when npxthread is NULL.  It's
	 *	more convenient to use a cli sequence here (it is not
	 *	considered to be in the critical path), but a critical
	 *	section would also work.
	 *
	 *	NOTE ON FNINIT vs FNCLEX - Making the FP unit safe here is
	 *	the goal.  It should be sufficient to just call FNCLEX rather
	 *	then having to FNINIT the entire unit.
	 *
	 *	race(3): The FP unit is in a known state (because npxthread
	 *	was either previously NULL or we saved and init'd and made
	 *	it NULL).  This is true even if we are preempted and the
	 *	preempting thread uses the FP unit, because it will be
	 *	fninit's again on return.  ANY STATE WE SAVE TO THE FPU MAY
	 *	BE DESTROYED BY PREEMPTION WHILE NPXTHREAD IS NULL!  However,
	 *	an interrupt occuring inbetween clts and the setting of
	 *	gd_npxthread may set the TS bit again and cause the next
	 *	npxdna() to panic when it sees a non-NULL gd_npxthread.
	 *	
	 *	We can safely set TD_SAVEFPU to point to a new uninitialized
	 *	save area and then set GD_NPXTHREAD to non-NULL.  If an
	 *	interrupt occurs after we set GD_NPXTHREAD, all that happens
	 *	is that the safe FP state gets saved and restored.  We do not
	 *	need to clex again.
	 *
	 *	We can safely clts after setting up the new save-area, before
	 *	installing gd_npxthread, even if we get preempted just after
	 *	calling clts.  This is because the FP unit will be in a safe
	 *	state while gd_npxthread is NULL.  Setting gd_npxthread will
	 *	simply lock-in that safe-state.  Calling clts saves
	 *	unnecessary trap overhead since we are about to use the FP
	 *	unit anyway and don't need to 'restore' any state prior to
	 *	that first use.
	 *
	 *  MMX+XMM (SSE2): Typical on Athlons, later P4s. 128 bit media insn.
	 *  MMX: Typical on XPs and P3s.  64 bit media insn.
	 */

#define MMX_SAVE_BLOCK(missfunc)					\
	cmpl	$2048,%ecx ;						\
	jb	missfunc ;						\
	movl	MYCPU,%eax ;			/* EAX = MYCPU */	\
	btsl	$1,GD_FPU_LOCK(%eax) ;					\
	jc	missfunc ;						\
	pushl	%ebx ;							\
	pushl	%ecx ;							\
	movl	GD_CURTHREAD(%eax),%edx ;	/* EDX = CURTHREAD */	\
	movl	TD_SAVEFPU(%edx),%ebx ;		/* save app save area */\
	incl	TD_CRITCOUNT(%edx) ;					\
	cmpl	$0,GD_NPXTHREAD(%eax) ;					\
	je	100f ;							\
	fxsave	0(%ebx) ;			/* race(1) */		\
	movl	$0,GD_NPXTHREAD(%eax) ;		/* interlock intr */	\
	clts ;								\
	fnclex ;				/* race(2) */		\
100: ;									\
	leal	GD_SAVEFPU(%eax),%ecx ;					\
	movl	%ecx,TD_SAVEFPU(%edx) ;					\
	orl	$TDF_KERNELFP,TD_FLAGS(%edx) ;				\
	clts ;								\
	movl	%edx,GD_NPXTHREAD(%eax) ;	/* race(3) */		\
	decl	TD_CRITCOUNT(%edx) ;		/* crit_exit() */	\
	cmpl	$0,GD_REQFLAGS(%eax) ;					\
	je	101f ;							\
	testl	$-1,TD_CRITCOUNT(%edx) ;				\
	jne	101f ;							\
	call	splz_check ;						\
	/* note: eax,ecx,edx destroyed */				\
101: ;									\
	movl	(%esp),%ecx ;						\
	movl	$mmx_onfault,(%esp) ;					\

	/*
	 * When restoring the application's FP state we must first clear
	 * npxthread to prevent further saves, then restore the pointer
	 * to the app's save area.  We do not have to (and should not)
	 * restore the app's FP state now.  Note that we do not have to
	 * call fnclex because our use of the FP guarentees that it is in
	 * a 'safe' state (at least for kernel use).
	 *
	 * NOTE: it is not usually safe to mess with CR0 outside of a
	 * critical section, because TS may get set by a preemptive
	 * interrupt.  However, we *can* race a load/set-ts/store against
	 * an interrupt doing the same thing.
	 *
	 * WARNING! A Virtual kernel depends on CR0_TS remaining set after
	 * we use the FP unit if it asked it to be set.
	 */

#define MMX_RESTORE_BLOCK			\
	addl	$4,%esp ;			\
	MMX_RESTORE_BLOCK2

#define MMX_RESTORE_BLOCK2			\
	movl	MYCPU,%ecx ;			\
	movl	GD_CURTHREAD(%ecx),%edx ;	\
	movl	$0,GD_NPXTHREAD(%ecx) ;		\
	andl	$~TDF_KERNELFP,TD_FLAGS(%edx) ;	\
	movl	%ebx,TD_SAVEFPU(%edx) ;		\
	smsw	%ax ;				\
	popl	%ebx ;				\
	orb	$CR0_TS,%al ;			\
	lmsw	%ax ;				\
	movl	$0,GD_FPU_LOCK(%ecx)

	/*
	 * xmm/mmx_onfault routine.  Restore the fpu state, skip the normal
	 * return vector, and return to the caller's on-fault routine
	 * (which was pushed on the callers stack just before he called us)
	 */
	ALIGN_TEXT
mmx_onfault:
	MMX_RESTORE_BLOCK2
	addl	$4,%esp
	ret

	/*
	 * MXX entry points - only support 64 bit media instructions
	 */
	SUPERALIGN_TEXT
ENTRY(asm_mmx_memcpy)		/* memcpy() entry point use optimal copy */
	MMX_SAVE_BLOCK(asm_generic_memcpy)
	jmp	5f

	SUPERALIGN_TEXT
ENTRY(asm_mmx_bcopy)
	MMX_SAVE_BLOCK(asm_generic_bcopy)
	cmpl	%esi,%edi	/* if (edi < esi) fwd copy ok */
	jb	5f
	addl	%ecx,%esi
	cmpl	%esi,%edi	/* if (edi < esi + count) do bkwrds copy */
	jb	10f
	subl	%ecx,%esi
	jmp	5f

	/*
	 * XMM entry points - support 128 bit media instructions
	 */
	SUPERALIGN_TEXT
ENTRY(asm_xmm_memcpy)		/* memcpy() entry point use optimal copy */
	MMX_SAVE_BLOCK(asm_generic_memcpy)
	jmp	1f

	SUPERALIGN_TEXT
ENTRY(asm_xmm_bcopy)
	MMX_SAVE_BLOCK(asm_generic_bcopy)
	cmpl	%esi,%edi	/* if (edi < esi) fwd copy ok */
	jb	1f
	addl	%ecx,%esi
	cmpl	%esi,%edi	/* if (edi < esi + count) do bkwrds copy */
	jb	10f
	subl	%ecx,%esi
1:
	movl	%esi,%eax	/* skip xmm if the data is not aligned */
	andl	$15,%eax
	jnz	5f
	movl	%edi,%eax
	andl	$15,%eax
	jz	3f
	jmp	5f

	SUPERALIGN_TEXT
2:
	movdqa	(%esi),%xmm0
	movdqa  16(%esi),%xmm1
	movdqa  32(%esi),%xmm2
	movdqa  48(%esi),%xmm3
	movdqa  64(%esi),%xmm4
	movdqa  80(%esi),%xmm5
	movdqa  96(%esi),%xmm6
	movdqa  112(%esi),%xmm7
	/*prefetchnta 128(%esi) 3dNOW */
	addl	$128,%esi

	/*
	 * movdqa or movntdq can be used.
	 */
	movdqa  %xmm0,(%edi)
	movdqa  %xmm1,16(%edi)
	movdqa  %xmm2,32(%edi)
	movdqa  %xmm3,48(%edi)
	movdqa  %xmm4,64(%edi)
	movdqa  %xmm5,80(%edi)
	movdqa  %xmm6,96(%edi)
	movdqa  %xmm7,112(%edi)
	addl	$128,%edi
3:
	subl	$128,%ecx
	jae	2b
	addl	$128,%ecx
	jz	6f
	jmp	5f
	SUPERALIGN_TEXT
4:
	movq	(%esi),%mm0
	movq	8(%esi),%mm1
	movq	16(%esi),%mm2
	movq	24(%esi),%mm3
	movq	32(%esi),%mm4
	movq	40(%esi),%mm5
	movq	48(%esi),%mm6
	movq	56(%esi),%mm7
	/*prefetchnta 128(%esi) 3dNOW */
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
5:
	subl	$64,%ecx
	jae	4b
	addl	$64,%ecx
	jz	6f
	cld
	rep
	movsb
6:
	MMX_RESTORE_BLOCK
	ret

	/*
	 * GENERIC_BCOPY() - BACKWARDS COPY
	 *
	 * Don't bother using xmm optimizations, just stick with mmx.
	 */
	SUPERALIGN_TEXT
10:
	addl	%ecx,%edi
	jmp	12f

	SUPERALIGN_TEXT
11:
	movq	-64(%esi),%mm0
	movq	-56(%esi),%mm1
	movq	-48(%esi),%mm2
	movq	-40(%esi),%mm3
	movq	-32(%esi),%mm4
	movq	-24(%esi),%mm5
	movq	-16(%esi),%mm6
	movq	-8(%esi),%mm7
	/*prefetchnta -128(%esi)*/
	subl	$64,%esi
	movq	%mm0,-64(%edi)
	movq	%mm1,-56(%edi)
	movq	%mm2,-48(%edi)
	movq	%mm3,-40(%edi)
	movq	%mm4,-32(%edi)
	movq	%mm5,-24(%edi)
	movq	%mm6,-16(%edi)
	movq	%mm7,-8(%edi)
	subl	$64,%edi
12:
	subl    $64,%ecx
	jae	11b
	addl	$64,%ecx
	jz	13f
	decl	%esi
	decl	%edi
	std
	rep
	movsb
	cld
13:
	MMX_RESTORE_BLOCK
	ret

