/*-
 * Copyright (c) 1999, 2000 John D. Polstra.
 * All rights reserved.
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
 * $FreeBSD: src/libexec/rtld-elf/i386/rtld_machdep.h,v 1.3.2.2 2002/07/02 04:10:51 jdp Exp $
 * $DragonFly: src/libexec/rtld-elf/i386/rtld_machdep.h,v 1.3 2003/09/18 21:23:02 dillon Exp $
 */

#ifndef RTLD_MACHDEP_H
#define RTLD_MACHDEP_H	1

/* Return the address of the .dynamic section in the dynamic linker. */
#define rtld_dynamic(obj) \
    ((const Elf_Dyn *)((obj)->relocbase + (Elf_Addr)&_DYNAMIC))

/* Fixup the jump slot at "where" to transfer control to "target". */
#define reloc_jmpslot(where, target)			\
    do {						\
	dbg("reloc_jmpslot: *%p = %p", (void *)(where),	\
	  (void *)(target));				\
	(*(Elf_Addr *)(where) = (Elf_Addr)(target));	\
    } while (0)

static inline void
atomic_decr_int(volatile int *p)
{
    __asm __volatile ("lock; decl %0" : "+m"(*p) : : "cc");
}

static inline void
atomic_incr_int(volatile int *p)
{
    __asm __volatile ("lock; incl %0" : "+m"(*p) : : "cc");
}

static inline void
atomic_add_int(volatile int *p, int val)
{
    __asm __volatile ("lock; addl %1, %0"
	: "+m"(*p)
	: "ri"(val)
	: "cc");
}

/*
 * Optimized version of the calculation y = ky + m (mod p)
 * See elf_uniqid() for details
 */
static inline u_int32_t
uniqid_hash_block(u_int32_t hash, u_int32_t key, u_int32_t block)
{
    int ret;

    __asm __const (	"mull	%2\n\t"
			"lea	(%%edx,%%edx,4),%%edx\n\t"
			"addl	%%edx,%%eax\n\t"
			"lea	0x5(%%eax),%%edx\n\t"
			"cmovcl	%%edx,%%eax\n\t"
			"addl	%3,%%eax\n\t"
			"lea	0x5(%%eax),%%edx\n\t"
			"cmovcl	%%edx,%%eax\n\t"
	: "=a"(ret) : "a"(hash), "rm"(key), "g"(block) : "cc", "dx");

    return ret;
}

#endif
