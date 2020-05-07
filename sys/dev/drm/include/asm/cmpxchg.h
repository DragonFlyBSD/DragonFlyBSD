/*
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef	_ASM_CMPXCHG_H_
#define	_ASM_CMPXCHG_H_

#define xchg(ptr, value)				\
({							\
	__typeof(value) __ret = (value);		\
							\
	switch (sizeof(value)) {			\
	case 8:						\
		__asm __volatile("xchgq %0, %1"		\
		    : "+r" (__ret), "+m" (*(ptr))	\
		    : : "memory");			\
		break;					\
	case 4:						\
		__asm __volatile("xchgl %0, %1"		\
		    : "+r" (__ret), "+m" (*(ptr))	\
		    : : "memory");			\
		break;					\
	case 2:						\
		__asm __volatile("xchgw %0, %1"		\
		    : "+r" (__ret), "+m" (*(ptr))	\
		    : : "memory");			\
		break;					\
	case 1:						\
		__asm __volatile("xchgb %0, %1"		\
		    : "+r" (__ret), "+m" (*(ptr))	\
		    : : "memory");			\
		break;					\
	default:					\
		panic("xchg(): invalid size %ld\n", sizeof(value)); \
	}						\
							\
	__ret;						\
})

#endif	/* _ASM_CMPXCHG_H_ */
