/*-
 * Copyright 1996-1998 John D. Polstra.
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

#ifndef __GNUC__
#error "GCC is needed to compile this file"
#endif

#include <sys/cdefs.h>
#include <machine/tls.h>
#include <elf.h>
#include <stdlib.h>

#include "libc_private.h"
#include "initfini.c"
#include "crtbrand.c"

typedef void (*fptr)(void);

#ifdef GCRT
extern void _mcleanup(void);
extern void monstartup(void *, void *);
extern int eprol;
extern int etext;
#endif

/* static ifunc reloc support */
extern const Elf_Rela __rela_iplt_start[] __dso_hidden __weak_symbol;
extern const Elf_Rela __rela_iplt_end[] __dso_hidden __weak_symbol;
static void fix_iplta(void) __noinline;

static void
fix_iplta(void)
{
	const Elf_Rela *rela, *relalim;
	Elf_Addr *where, target, *ptr;

	rela = __rela_iplt_start;
	relalim = __rela_iplt_end;
	for (; rela < relalim; ++rela) {
		if (ELF_R_TYPE(rela->r_info) != R_X86_64_IRELATIVE)
			abort();
		ptr = (Elf_Addr *)rela->r_addend;
		where = (Elf_Addr *)rela->r_offset;
		target = ((Elf_Addr (*)(void))ptr)();
		*where = target;
	}
}

void _start(char **, void (*)(void));

/* The entry function. */
void
_start(char **ap, void (*cleanup)(void))
{
	int argc;
	char **argv;
	char **env;

	argc = *(long *)(void *)ap;
	argv = ap + 1;
	env = ap + 2 + argc;
	handle_argv(argc, argv, env);

	/*
	 * Setup the initial TLS space.  The RTLD does not set up our TLS
	 * (it can't, it doesn't know how our errno is declared).  It simply
	 * does all the groundwork required so that we can call
	 * _rtld_allocate_tls().
	 */
	_init_tls();
	_rtld_call_init();

	if (&_DYNAMIC != NULL)
		atexit(cleanup);
	else
		fix_iplta();

#ifdef GCRT
	atexit(_mcleanup);
	monstartup(&eprol, &etext);
#endif

	handle_static_init(argc, argv, env);
#ifdef GCRT
	/*
	 * This label was previously located just after the monstartup
	 * function.  It was relocated after the handle_static_init function
	 * to avoid an alternative conditional optimization performed by
	 * clang.  The optimization cause the compilation to fail with a
	 * label redefinition error.  The impact of the label relocation is
	 * the loss of profiling of handle_static_init function.
	 */
__asm__("eprol:");
#endif
	exit(main(argc, argv, env));
}
