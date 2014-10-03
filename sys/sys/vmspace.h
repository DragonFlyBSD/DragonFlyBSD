/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/sys/vmspace.h,v 1.6 2007/01/08 03:33:43 dillon Exp $
 */
/*
 * VMSPACE - Virtualized Environment control from user mode.  The VMSPACE
 * subsystem allows a user mode application, such as a user-mode DragonFly
 * kernel, to create, manipulate, and execute code in a separate VM context.
 */

#ifndef _SYS_VMSPACE_H_
#define _SYS_VMSPACE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

struct trapframe;
struct vextframe;

#define VMSPACE_CTL_RUN		1

#define VMSPACE_PAGEFAULT	1
#define VMSPACE_TIMEOUT		2
#define VMSPACE_TRAP		3
#define VMSPACE_SYSCALL		4

int vmspace_create(void *, int, void *);
int vmspace_destroy(void *);
int vmspace_ctl (void *, int, struct trapframe *, struct vextframe *);

void *vmspace_mmap(void *, void *, size_t, int, int, int, off_t);
int vmspace_munmap(void *, void *, size_t);
int vmspace_mcontrol(void *, void *, size_t, int, off_t);
ssize_t vmspace_pread(void *, void *, size_t, int, off_t);
ssize_t vmspace_pwrite(void *, const void *, size_t, int, off_t);

#endif
