/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ARM64 support functions - memory operations and copy routines.
 * These are simple C implementations for the MVP; optimized assembly
 * versions can be added later.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>

/*
 * Function prototypes - declared here to avoid macro expansion from
 * sys/systm.h which redefines memset/memcpy/memmove as __builtin_*.
 */
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int bcmp(const void *, const void *, size_t);
int copyin(const void *, void *, size_t);
int copyout(const void *, void *, size_t);
int copyinstr(const void *, void *, size_t, size_t *);
int copystr(const void *, void *, size_t, size_t *);
int subyte(volatile void *, int);
int suword32(volatile void *, int);
int suword64(volatile void *, long);
long fuword64(const volatile void *);

/*
 * memset - fill memory with a constant byte
 */
void *
memset(void *b, int c, size_t len)
{
	unsigned char *p = b;

	while (len-- > 0)
		*p++ = (unsigned char)c;
	return (b);
}

/*
 * memcpy - copy memory (non-overlapping)
 */
void *
memcpy(void *dst, const void *src, size_t len)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (len-- > 0)
		*d++ = *s++;
	return (dst);
}

/*
 * memmove - copy memory (handles overlapping regions)
 */
void *
memmove(void *dst, const void *src, size_t len)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d < s) {
		while (len-- > 0)
			*d++ = *s++;
	} else if (d > s) {
		d += len;
		s += len;
		while (len-- > 0)
			*--d = *--s;
	}
	return (dst);
}

/*
 * bcmp - compare byte strings
 */
int
bcmp(const void *b1, const void *b2, size_t len)
{
	const unsigned char *p1 = b1;
	const unsigned char *p2 = b2;

	while (len-- > 0) {
		if (*p1++ != *p2++)
			return (1);
	}
	return (0);
}

/*
 * copyout - copy data from kernel space to user space
 *
 * For the MVP, we don't have proper user/kernel separation yet,
 * so this is a simple memcpy.  A real implementation would need
 * proper fault handling and address validation.
 */
int
copyout(const void *kaddr, void *uaddr, size_t len)
{
	memcpy(uaddr, kaddr, len);
	return (0);
}

/*
 * copyin - copy data from user space to kernel space
 *
 * For the MVP, we don't have proper user/kernel separation yet,
 * so this is a simple memcpy.  A real implementation would need
 * proper fault handling and address validation.
 */
int
copyin(const void *uaddr, void *kaddr, size_t len)
{
	memcpy(kaddr, uaddr, len);
	return (0);
}

/*
 * copyinstr - copy a null-terminated string from user space
 *
 * Returns 0 on success, ENAMETOOLONG if string doesn't fit,
 * EFAULT if address is invalid (not implemented for MVP).
 */
int
copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
	const char *src = uaddr;
	char *dst = kaddr;
	size_t i;

	if (len == 0) {
		if (done)
			*done = 0;
		return (ENAMETOOLONG);
	}

	for (i = 0; i < len; i++) {
		dst[i] = src[i];
		if (src[i] == '\0') {
			if (done)
				*done = i + 1;
			return (0);
		}
	}

	/* String didn't fit */
	dst[len - 1] = '\0';
	if (done)
		*done = len;
	return (ENAMETOOLONG);
}

/*
 * subyte - store a byte to user space
 */
int
subyte(volatile void *base, int byte)
{
	*(volatile unsigned char *)base = (unsigned char)byte;
	return (0);
}

/*
 * suword32 - store a 32-bit word to user space
 */
int
suword32(volatile void *base, int word)
{
	*(volatile int *)base = word;
	return (0);
}

/*
 * suword64 - store a 64-bit word to user space
 */
int
suword64(volatile void *base, long word)
{
	*(volatile long *)base = word;
	return (0);
}

/*
 * fuword64 - fetch a 64-bit word from user space
 */
long
fuword64(const volatile void *base)
{
	return (*(const volatile long *)base);
}

/*
 * copystr - copy a null-terminated string (kernel to kernel)
 */
int
copystr(const void *src, void *dst, size_t len, size_t *done)
{
	const char *s = src;
	char *d = dst;
	size_t i;

	if (len == 0) {
		if (done)
			*done = 0;
		return (ENAMETOOLONG);
	}

	for (i = 0; i < len; i++) {
		d[i] = s[i];
		if (s[i] == '\0') {
			if (done)
				*done = i + 1;
			return (0);
		}
	}

	d[len - 1] = '\0';
	if (done)
		*done = len;
	return (ENAMETOOLONG);
}
