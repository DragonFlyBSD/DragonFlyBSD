/*-
 * Copyright (c) 2003 Kip Macy All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/usr.bin/checkpt/Attic/freeze.S,v 1.1 2003/10/20 04:46:14 dillon Exp $
 */

#include "DEFS.h"

ENTRY(ckpt_checkpoint)
	pop	%ecx            /* rta */
	/* fd - on top of stack */	
	pushl   $0x1            /* CKPT_FREEZE */ ; 
	push	%ecx
	movl    $0xd2,%eax      /* ckpt_proc sysent offset */
	int	$0x80		/* jump to kernel */
	push	%ecx
	jb	1f
	ret
	mov	%esi,%esi
1:
	PIC_PROLOGUE
	jmp	PIC_PLT(HIDENAME(cerror))


ENTRY(ckpt_restore)
	pop	%ecx            /* rta */
	/* fd - on top of stack */	
	pushl   $0x2            /* CKPT_THAW */ ; 
	push	%ecx
	movl    $0xd2,%eax      /* ckpt_proc sysent offset */
	int	$0x80		/* jump to kernel */
	push	%ecx
	jb	1f
	ret
	mov	%esi,%esi
1:
	PIC_PROLOGUE
	jmp	PIC_PLT(HIDENAME(cerror))


