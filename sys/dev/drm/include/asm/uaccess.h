/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2015 François Tigeot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */
#ifndef _ASM_UACCESS_H_
#define _ASM_UACCESS_H_

#define	get_user(_x, _p)	-copyin((_p), &(_x), sizeof(*(_p)))
#define	put_user(_x, _p)	-copyout(&(_x), (_p), sizeof(*(_p)))

static inline long
copy_to_user(void *to, const void *from, unsigned long n)
{
	if (copyout(from, to, n) != 0)
		return n;
	return 0;
}

static inline long
copy_from_user(void *to, const void *from, unsigned long n)
{
	if (copyin(from, to, n) != 0)
		return n;
	return 0;
}

static inline int
__copy_to_user(void *to, const void *from, unsigned len)
{
	if (copyout(from, to, len))
		return len;
	return 0;
}

static inline unsigned long
__copy_from_user(void *to, const void *from, unsigned len)
{
	if (copyin(from, to, len))
		return len;
	return 0;
}

static inline int
__copy_from_user_inatomic(void *dst, const void __user *src, unsigned size)
{
	if (copyin_nofault(src, dst, size))
		return size;
	return 0;
}

static inline int
__copy_to_user_inatomic(void __user *to, const void *from, unsigned n)
{
	return (copyout_nofault(from, to, n) != 0 ? n : 0);
}

static inline unsigned long
__copy_from_user_inatomic_nocache(void *to, const void __user *from,
    unsigned long n)
{
	/*
	 * XXXKIB.  Equivalent Linux function is implemented using
	 * MOVNTI for aligned moves.  For unaligned head and tail,
	 * normal move is performed.  As such, it is not incorrect, if
	 * only somewhat slower, to use normal copyin.  All uses
	 * except shmem_pwrite_fast() have the destination mapped WC.
	 */
	return ((copyin_nofault(__DECONST(void *, from), to, n) != 0 ? n : 0));
}

#endif	/* _ASM_UACCESS_H_ */
