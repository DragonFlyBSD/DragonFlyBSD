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
 * $DragonFly: src/sys/platform/vkernel/platform/copyio.c,v 1.1 2006/12/26 20:46:15 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <vm/vm_page.h>
#include <assert.h>

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
	assert(0);
}

/*
 * Copy a binary buffer from user space to kernel space.
 *
 * Returns 0 on success, EFAULT on failure.
 */
int
copyin (const void *udaddr, void *kaddr, size_t len)
{
	assert(0);
}

/*
 * Copy a binary buffer from kernel space to user space.
 *
 * Returns 0 on success, EFAULT on failure.
 */
int
copyout (const void *kaddr, void *udaddr, size_t len)
{
	assert(0);
}
 
/*
 * Fetch the byte at the specified user address.  Returns -1 on failure.
 */
int
fubyte(const void *base)
{
	assert(0);
}

/*
 * Store a byte at the specified user address.  Returns -1 on failure.
 */
int
subyte (void *base, int byte)
{
	assert(0);
}

/*
 * Fetch a word (integer, 32 bits) from user space
 */
long
fuword (const void *base)
{
	assert(0);
}

/*
 * Store a word (integer, 32 bits) to user space
 */
int
suword (void *base, long word)
{
	assert(0);
}

/*
 * Fetch an short word (16 bits) from user space
 */
int
fusword (void *base)
{
	assert(0);
}

/*
 * Store a short word (16 bits) to user space
 */
int
susword (void *base, int word)
{
	assert(0);
}

