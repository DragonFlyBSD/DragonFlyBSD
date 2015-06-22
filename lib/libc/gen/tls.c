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
 */

/*
 * Define stubs for TLS internals so that programs and libraries can
 * link. These functions will be replaced by functional versions at
 * runtime from ld-elf.so.1.
 */

#include <sys/tls.h>

#include <machine/tls.h>

#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "libc_private.h"

__weak_reference(__libc_allocate_tls, _rtld_allocate_tls);
__weak_reference(__libc_free_tls, _rtld_free_tls);
__weak_reference(__libc_call_init, _rtld_call_init);
__weak_reference(__libc_tls_get_addr, __tls_get_addr);
__weak_reference(__libc_tls_get_addr_tcb, __tls_get_addr_tcb);
__weak_reference(_libc_init_tls, _init_tls);

struct tls_tcb *__libc_allocate_tls(void);
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
static struct tls_tcb *initial_tcb;
#endif

void *__libc_tls_get_addr(void *ti);
void *__libc_tls_get_addr_tcb(struct tls_tcb *, void *);

void *
__libc_tls_get_addr(void *ti __unused)
{
	return (NULL);
}

void *
__libc_tls_get_addr_tcb(struct tls_tcb *tcb __unused, void *got_ptr __unused)
{
	return (NULL);
}

#ifndef PIC

/*
 * Free Static TLS, weakly bound to _rtld_free_tls()
 */
void
__libc_free_tls(struct tls_tcb *tcb)
{
	size_t data_size;

	data_size = (tls_static_space + RTLD_STATIC_TLS_ALIGN_MASK) &
		    ~RTLD_STATIC_TLS_ALIGN_MASK;

	if (tcb == initial_tcb) {
		/* initial_tcb was allocated with sbrk(), cannot call free() */
	} else {
		free((char *)tcb - data_size);
	}
}

/*
 * Allocate Static TLS, weakly bound to _rtld_allocate_tls()
 *
 * NOTE!  There is a chicken-and-egg problem here because no TLS exists
 * on the first call into this function. 
 */
struct tls_tcb *
__libc_allocate_tls(void)
{
	size_t data_size;
	struct tls_tcb *tcb;
	Elf_Addr *dtv;

	data_size = (tls_static_space + RTLD_STATIC_TLS_ALIGN_MASK) &
		    ~RTLD_STATIC_TLS_ALIGN_MASK;

	/*
	 * Allocate space.  malloc() may require a working TLS segment
	 * so we use sbrk() for main's TLS.
	 */
	if (initial_tcb == NULL)
		tcb = sbrk(data_size + sizeof(*tcb) + 3 * sizeof(*dtv));
	else
		tcb = malloc(data_size + sizeof(*tcb) + 3 * sizeof(*dtv));

	tcb = (struct tls_tcb *)((char *)tcb + data_size);
	dtv = (Elf_Addr *)(tcb + 1);

	memset(tcb, 0, sizeof(*tcb));
#ifdef RTLD_TCB_HAS_SELF_POINTER
	tcb->tcb_self = tcb;
#endif
	tcb->tcb_dtv = dtv;

	/*
	 * Dummy-up the module array.  A static binary has only one.  This
	 * allows us to support the generic __tls_get_addr compiler ABI
	 * function.  However, if there is no RTLD linked in, nothing in
	 * the program should ever call __tls_get_addr (and our version
	 * of it doesn't do anything).
	 */
	dtv[0] = 1;
	dtv[1] = 1;
	dtv[2] = (Elf_Addr)((char *)tcb - tls_static_space);

	memcpy((char *)tcb - tls_static_space,
		tls_init, tls_init_size);
	memset((char *)tcb - tls_static_space + tls_init_size,
		0, tls_static_space - tls_init_size);

	/*
	 * Activate the initial TCB
	 */
	if (initial_tcb == NULL) {
		initial_tcb = tcb;
		tls_set_tcb(tcb);
	}
	return (tcb);
}

#else

struct tls_tcb *
__libc_allocate_tls(void)
{
	return (NULL);
}

void
__libc_free_tls(struct tls_tcb *tcb __unused)
{
}

#endif /* PIC */

void
__libc_call_init(void)
{
}

extern char **environ;

struct tls_tcb *
_libc_init_tls(void)
{
	struct tls_tcb *tcb;

#ifndef PIC
	/*
	 * If this is a static binary there is no RTLD and so we have not
	 * yet calculated the static space requirement.  Do so now.
	 */
	Elf_Addr *sp;
	Elf_Auxinfo *aux, *auxp;
	Elf_Phdr *phdr;
	size_t phent, phnum;
	int i;

	sp = (Elf_Addr *) environ;
	while (*sp++ != 0)
		;
	aux = (Elf_Auxinfo *) sp;
	phdr = NULL;
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
	if (phdr == NULL || phent != sizeof(Elf_Phdr) || phnum == 0)
		return(NULL);

	for (i = 0; (unsigned)i < phnum; i++) {
		if (phdr[i].p_type == PT_TLS) {
			tls_static_space = round(phdr[i].p_memsz,
			    phdr[i].p_align);
			tls_init_size = phdr[i].p_filesz;
			tls_init = (void*) phdr[i].p_vaddr;
		}
	}
#endif

	/*
	 * Allocate the initial TLS segment.  The TLS has not been set up
	 * yet for either the static or dynamic linked case (RTLD no longer
	 * sets up an initial TLS segment for us).
	 */
	tcb = _libc_allocate_tls();
	tls_set_tcb(tcb);
	return(tcb);
}

/*
 * Allocate a standard TLS.  This function is called by libc and by 
 * thread libraries to create a new TCB with libc-related fields properly
 * initialized (whereas _rtld_allocate_tls() is unable to completely set
 * up the TCB).
 *
 * Note that this is different from __libc_allocate_tls which is the
 * weakly bound symbol that handles the case where _rtld_allocate_tls
 * does not exist.
 */
struct tls_tcb *
_libc_allocate_tls(void)
{
	struct tls_tcb *tcb;

	tcb = _rtld_allocate_tls();

#if 0
#if defined(__thread)
	/* non-TLS libc */
	tcb->tcb_errno_p = &errno;
#elif defined(PIC)
	/* TLS libc dynamically linked */
	tcb->tcb_errno_p = __tls_get_addr_tcb(tcb, __get_errno_GOT_ptr());
#else
	/* TLS libc (threaded or unthreaded) */
	tcb->tcb_errno_p = (void *)((char *)tcb + __get_errno_GS_offset());
#endif
#endif
	return(tcb);
}

