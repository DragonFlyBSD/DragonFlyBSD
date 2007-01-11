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
 * $DragonFly: src/sys/platform/vkernel/platform/copyio.c,v 1.7 2007/01/11 10:15:18 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/sfbuf.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/mman.h>

/*
 * A bcopy that works dring low level boot, before FP is working
 */
void
ovbcopy(const void *src, void *dst, size_t len)
{
	bcopy(src, dst, len);
}

void
bcopyi(const void *src, void *dst, size_t len)
{
	bcopy(src, dst, len);
}

int
copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *lencopied)
{
	size_t i;

	for (i = 0; i < len; ++i) {
		if ((((char *)kdaddr)[i] = ((const char *)kfaddr)[i]) == 0) {
			if (lencopied)
				*lencopied = i + 1;
			return(0);
		}
	}
	return (ENAMETOOLONG);
}

/*
 * Copies a NUL-terminated string from user space to kernel space.
 * The number of bytes copied, including the terminator, is returned in
 * (*res).
 *
 * Returns 0 on success, EFAULT or ENAMETOOLONG on failure.
 */
int
copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *res)
{
	int error;
	size_t n;
	const char *uptr = udaddr;
	char *kptr = kaddr;

	if (res)
		*res = 0;
	while (len) {
		n = PAGE_SIZE - ((vm_offset_t)uptr & PAGE_MASK);
		if (n > 32)
			n = 32;
		if (n > len)
			n = len;
		if ((error = copyin(uptr, kptr, n)) != 0)
			return(error);
		while (n) {
			if (res)
				++*res;
			if (*kptr == 0)
				return(0);
			++kptr;
			++uptr;
			--n;
			--len;
		}

	}
	return(ENAMETOOLONG);
}

/*
 * Copy a binary buffer from user space to kernel space.
 *
 * Returns 0 on success, EFAULT on failure.
 */
int
copyin(const void *udaddr, void *kaddr, size_t len)
{
	struct vmspace *vm = curproc->p_vmspace;
	struct sf_buf *sf;
	vm_page_t m;
	int error;
	size_t n;

	error = 0;
	while (len) {
		m = vm_fault_page(&vm->vm_map, trunc_page((vm_offset_t)udaddr),
				  VM_PROT_READ,
				  VM_FAULT_NORMAL, &error);
		if (error)
			break;
		n = PAGE_SIZE - ((vm_offset_t)udaddr & PAGE_MASK);
		if (n > len)
			n = len;
		sf = sf_buf_alloc(m, SFB_CPUPRIVATE);
		bcopy((char *)sf_buf_kva(sf)+((vm_offset_t)udaddr & PAGE_MASK),
		      kaddr, n);
		len -= n;
		udaddr = (const char *)udaddr + n;
		kaddr = (char *)kaddr + n;
		vm_page_unhold(m);
		sf_buf_free(sf);
	}
	return (error);
}

/*
 * Copy a binary buffer from kernel space to user space.
 *
 * Returns 0 on success, EFAULT on failure.
 */
int
copyout(const void *kaddr, void *udaddr, size_t len)
{
	struct vmspace *vm = curproc->p_vmspace;
	struct sf_buf *sf;
	vm_page_t m;
	int error;
	size_t n;

	error = 0;
	while (len) {
		m = vm_fault_page(&vm->vm_map, trunc_page((vm_offset_t)udaddr),
				  VM_PROT_READ|VM_PROT_WRITE, 
				  VM_FAULT_NORMAL, &error);
		if (error)
			break;
		n = PAGE_SIZE - ((vm_offset_t)udaddr & PAGE_MASK);
		if (n > len)
			n = len;
		sf = sf_buf_alloc(m, SFB_CPUPRIVATE);
		bcopy(kaddr, (char *)sf_buf_kva(sf) +
			     ((vm_offset_t)udaddr & PAGE_MASK), n);
		len -= n;
		udaddr = (char *)udaddr + n;
		kaddr = (const char *)kaddr + n;
		vm_page_unhold(m);
		sf_buf_free(sf);
	}
	return (error);
}
 
/*
 * Fetch the byte at the specified user address.  Returns -1 on failure.
 */
int
fubyte(const void *base)
{
	unsigned char c;
	int error;

	if ((error = copyin(base, &c, 1)) == 0)
		return((int)c);
	return(-1);
}

/*
 * Store a byte at the specified user address.  Returns -1 on failure.
 */
int
subyte (void *base, int byte)
{
	unsigned char c = byte;
	int error;

	if ((error = copyout(&c, base, 1)) == 0)
		return(0);
	return(-1);
}

/*
 * Fetch a word (integer, 32 bits) from user space
 */
long
fuword(const void *base)
{
	long v;
	int error;

	if ((error = copyin(base, &v, sizeof(v))) == 0)
		return((long)v);
	return(-1);
}

/*
 * Store a word (integer, 32 bits) to user space
 */
int
suword(void *base, long word)
{
	int error;

	if ((error = copyout(&word, base, sizeof(word))) == 0)
		return(0);
	return(-1);
}

/*
 * Fetch an short word (16 bits) from user space
 */
int
fusword(void *base)
{
	unsigned short sword;
	int error;

	if ((error = copyin(base, &sword, sizeof(sword))) == 0)
		return((int)sword);
	return(-1);
}

/*
 * Store a short word (16 bits) to user space
 */
int
susword (void *base, int word)
{
	unsigned short sword = word;
	int error;

	if ((error = copyout(&sword, base, sizeof(sword))) == 0)
		return(0);
	return(-1);
}

