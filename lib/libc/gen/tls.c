/*-
 * Copyright (c) 2004 Doug Rabson
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
 *	$FreeBSD: src/lib/libc/gen/tls.c,v 1.7 2005/03/01 23:42:00 davidxu Exp $
 *	$DragonFly: src/lib/libc/gen/tls.c,v 1.6 2005/03/29 23:04:36 joerg Exp $
 */

/*
 * Define stubs for TLS internals so that programs and libraries can
 * link. These functions will be replaced by functional versions at
 * runtime from ld-elf.so.1.
 */

#include <sys/cdefs.h>
#include <sys/tls.h>

#include <machine/tls.h>

#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <assert.h>

#include "libc_private.h"

__weak_reference(__libc_allocate_tls, _rtld_allocate_tls);
__weak_reference(__libc_free_tls, _rtld_free_tls);
#ifdef __i386__
__weak_reference(___libc_tls_get_addr, ___tls_get_addr);
#endif
__weak_reference(__libc_tls_get_addr, __tls_get_addr);

struct tls_tcb *__libc_allocate_tls(struct tls_tcb *old_tcb);
void __libc_free_tls(struct tls_tcb *tcb);

#if !defined(RTLD_STATIC_TLS_VARIANT_II)
#error "Unsupported TLS layout"
#endif

#ifndef PIC

#define round(size, align) \
	(((size) + (align) - 1) & ~((align) - 1))

static size_t tls_static_space;
static size_t tls_init_size;
static void *tls_init;
#endif

#ifdef __i386__

/* GNU ABI */

void *___libc_tls_get_addr(void *ti) __attribute__((__regparm__(1)));

__attribute__((__regparm__(1)))
void *
___libc_tls_get_addr(void *ti __unused)
{
	return (0);
}

#endif

void *__libc_tls_get_addr(void *ti);

void *
__libc_tls_get_addr(void *ti __unused)
{
	return (0);
}

#ifndef PIC

/*
 * Free Static TLS
 */
void
__libc_free_tls(struct tls_tcb *tcb)
{
	size_t data_size;

	if (tcb->tcb_dtv)
		free(tcb->tcb_dtv);
	data_size = (tls_static_space + RTLD_STATIC_TLS_ALIGN_MASK) &
		    ~RTLD_STATIC_TLS_ALIGN_MASK;
	free((char *)tcb - data_size);
}

/*
 * Allocate Static TLS.
 */
struct tls_tcb *
__libc_allocate_tls(struct tls_tcb *old_tcb)
{
	size_t data_size;
	struct tls_tcb *tcb;
	Elf_Addr *dtv;

	data_size = (tls_static_space + RTLD_STATIC_TLS_ALIGN_MASK) &
		    ~RTLD_STATIC_TLS_ALIGN_MASK;
	tcb = malloc(data_size + sizeof(*tcb));
	tcb = (struct tls_tcb *)((char *)tcb + data_size);
	dtv = malloc(3 * sizeof(Elf_Addr));

#ifdef RTLD_TCB_HAS_SELF_POINTER
	tcb->tcb_self = tcb;
#endif
	tcb->tcb_dtv = dtv;
	tcb->tcb_pthread = NULL;

	dtv[0] = 1;
	dtv[1] = 1;
	dtv[2] = (Elf_Addr)((char *)tcb - tls_static_space);

	if (old_tcb) {
		/*
		 * Copy the static TLS block over whole.
		 */
		memcpy((char *)tcb - tls_static_space,
			(char *)old_tcb - tls_static_space,
			tls_static_space);

		/*
		 * We assume that this block was the one we created with
		 * allocate_initial_tls().
		 */
		_rtld_free_tls(old_tcb);
	} else {
		memcpy((char *)tcb - tls_static_space,
			tls_init, tls_init_size);
		memset((char *)tcb - tls_static_space + tls_init_size,
			0, tls_static_space - tls_init_size);
	}
	return (tcb);
}

#else

struct tls_tcb *
__libc_allocate_tls(struct tls_tcb *old_tls __unused)
{
	return (0);
}

void
__libc_free_tls(struct tls_tcb *tcb __unused)
{
}

#endif /* PIC */

extern char **environ;

void
_init_tls()
{
#ifndef PIC
	Elf_Addr *sp;
	Elf_Auxinfo *aux, *auxp;
	Elf_Phdr *phdr;
	size_t phent, phnum;
	int i;
	struct tls_tcb *tcb;

	sp = (Elf_Addr *) environ;
	while (*sp++ != 0)
		;
	aux = (Elf_Auxinfo *) sp;
	phdr = 0;
	phent = phnum = 0;
	for (auxp = aux; auxp->a_type != AT_NULL; auxp++) {
		switch (auxp->a_type) {
		case AT_PHDR:
			phdr = auxp->a_un.a_ptr;
			break;

		case AT_PHENT:
			phent = auxp->a_un.a_val;
			break;

		case AT_PHNUM:
			phnum = auxp->a_un.a_val;
			break;
		}
	}
	if (phdr == 0 || phent != sizeof(Elf_Phdr) || phnum == 0)
		return;

	for (i = 0; (unsigned)i < phnum; i++) {
		if (phdr[i].p_type == PT_TLS) {
			tls_static_space = round(phdr[i].p_memsz,
			    phdr[i].p_align);
			tls_init_size = phdr[i].p_filesz;
			tls_init = (void*) phdr[i].p_vaddr;
		}
	}

	if (tls_static_space) {
		tcb = _rtld_allocate_tls(NULL);
		tls_set_tcb(tcb);
	}
#endif
}
