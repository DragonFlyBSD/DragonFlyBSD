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
 * $DragonFly: src/sys/vm/vm_vmspace.c,v 1.1 2006/09/03 17:11:51 dillon Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysproto.h>

/*
 * vmspace_ctl {void *id, void *ctx, int what }
 *
 * Create, destroy, or execute a VM space.  This functions returns when
 * the VM space has run for a specified period of time, a signal occurs,
 * or the VM space traps or makes a system call.
 *
 * Execution of a VM space is accomplished by swapping out the caller's
 * current VM space.  Any signal or condition applicable to the caller
 * will swap the caller's VM space back in for processing, then return
 * EINTR.  A trap, system call, or page fault in the VM space will swap
 * the caller's VM space back in, adjust the context, and return the
 * appropriate code.
 *
 * A virtual kernel manages multiple 'process' VM spaces this way, the
 * real kernel only sees only the processes representing the virtual kernel
 * itself, typically one per virtual cpu.
 */
int
sys_vmspace_ctl(struct vmspace_ctl_args *uap)
{
	return (EINVAL);
}

/*
 * vmspace_map { void *id, off_t offset, void *ptr, int bytes, int prot }
 *
 * Map pages backing the specified memory in the caller's context into
 * the specified VM space and reduce their protection using 'prot'.  A
 * protection value of 0 removes the page mapping.  Page mappings can be
 * removed by the kernel at any time and cause execution of the VM space
 * to return with VMSPACE_PAGEFAULT.
 */
int
sys_vmspace_map(struct vmspace_map_args *uap)
{
	return (EINVAL);
}

/*
 * vmspace_protect { void *id, off_t offset, int bytes, int prot }
 *
 * Adjust the protection of mapped pages in the specified VM context.  Pages
 * that are not mapped or whos mapping was removed by the kernel are not
 * effected.
 */
int
sys_vmspace_protect(struct vmspace_protect_args *uap)
{
	return (EINVAL);
}

/*
 * vmspace_read { void *id, void *ptr, int bytes }
 *
 * Read data from the VM space.  Only data in mapped pages can be read.  If
 * an unmapped page is encountered this function will return fewer then the
 * requested number of bytes and the caller must map the additional pages
 * before restarting the call.
 */
int
sys_vmspace_read(struct vmspace_read_args *uap)
{
	return (EINVAL);
}

/*
 * vmspace_write { void *id, const void *ptr, int bytes }
 *
 * Write data to the VM space.  Only mapped, writable pages can be written.
 * If an unmapped or read-only page is encountered this function will return
 * fewer then the requested number of bytes and the caller must map the
 * additional pages before restarting the call.
 */
int
sys_vmspace_write(struct vmspace_write_args *uap)
{
	return (EINVAL);
}

