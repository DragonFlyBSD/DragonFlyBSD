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
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <cpu/lwbuf.h>
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

u_long
casuword(volatile u_long *p, u_long oldval, u_long newval)
{
	struct vmspace *vm = curproc->p_vmspace;
	vm_offset_t kva;
	vm_page_t m;
	volatile u_long *dest;
	u_long res;
	int error;

	/* XXX No idea how to handle this case in a simple way, just abort */
	if (PAGE_SIZE - ((vm_offset_t)p & PAGE_MASK) < sizeof(u_long))
		return -1;

	m = vm_fault_page(&vm->vm_map, trunc_page((vm_offset_t)p),
			  VM_PROT_READ|VM_PROT_WRITE,
			  VM_FAULT_NORMAL, &error);
	if (error)
		return -1;

	kva = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));
	dest = (u_long *)(kva + ((vm_offset_t)p & PAGE_MASK));
	res = oldval;
	__asm __volatile(MPLOCKED "cmpxchgq %2,%1; " \
			 : "+a" (res), "=m" (*dest) \
			 : "r" (newval), "m" (*dest) \
			 : "memory");

	if (res == oldval)
		vm_page_dirty(m);
	vm_page_unhold(m);

	return res;
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
 * NOTE: on a real system copyin/copyout are MP safe, but the current
 * implementation on a vkernel is not so we get the mp lock.
 *
 * Returns 0 on success, EFAULT on failure.
 */
int
copyin(const void *udaddr, void *kaddr, size_t len)
{
	struct vmspace *vm = curproc->p_vmspace;
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
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
		lwb = lwbuf_alloc(m, &lwb_cache);
		bcopy((char *)lwbuf_kva(lwb)+((vm_offset_t)udaddr & PAGE_MASK),
		      kaddr, n);
		len -= n;
		udaddr = (const char *)udaddr + n;
		kaddr = (char *)kaddr + n;
		lwbuf_free(lwb);
		vm_page_unhold(m);
	}
	if (error)
		error = EFAULT;
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
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
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
		lwb = lwbuf_alloc(m, &lwb_cache);
		bcopy(kaddr, (char *)lwbuf_kva(lwb) +
			     ((vm_offset_t)udaddr & PAGE_MASK), n);
		len -= n;
		udaddr = (char *)udaddr + n;
		kaddr = (const char *)kaddr + n;
		vm_page_dirty(m);
		lwbuf_free(lwb);
		vm_page_unhold(m);
	}
	if (error)
		error = EFAULT;
	return (error);
}

/*
 * Fetch the byte at the specified user address.  Returns -1 on failure.
 */
int
fubyte(const void *base)
{
	unsigned char c;

	if (copyin(base, &c, 1) == 0)
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

	if (copyout(&c, base, 1) == 0)
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

	if (copyin(base, &v, sizeof(v)) == 0)
		return(v);
	return(-1);
}

/*
 * Store a word (integer, 32 bits) to user space
 */
int
suword(void *base, long word)
{
	if (copyout(&word, base, sizeof(word)) == 0)
		return(0);
	return(-1);
}

int
suword32(void *base, int word)
{
	if (copyout(&word, base, sizeof(word)) == 0)
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

	if (copyin(base, &sword, sizeof(sword)) == 0)
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

	if (copyout(&sword, base, sizeof(sword)) == 0)
		return(0);
	return(-1);
}
