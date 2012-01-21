/*-
 * Copyright (c) 1998 John D. Polstra
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
 * $FreeBSD: src/lib/libc/gen/dlfcn.c 217154 2011-01-08 17:13:43Z kib $
 */

#include <sys/mman.h>
#include <dlfcn.h>
#include <link.h>
#include <stddef.h>

void	_rtld_error(const char *, ...);

static char sorry[] = "Service unavailable";

/*
 * For ELF, the dynamic linker directly resolves references to its
 * services to functions inside the dynamic linker itself.  These
 * weak-symbol stubs are necessary so that "ld" won't complain about
 * undefined symbols.  The stubs are executed only when the program is
 * linked statically, or when a given service isn't implemented in the
 * dynamic linker.  They must return an error if called, and they must
 * be weak symbols so that the dynamic linker can override them.
 */

#pragma weak _rtld_error
void
_rtld_error(const char *fmt __unused, ...)
{
}

#pragma weak dladdr
int
dladdr(const void *addr __unused, Dl_info *dlip __unused)
{
	_rtld_error(sorry);
	return 0;
}

#pragma weak dlclose
int
dlclose(void *handle __unused)
{
	_rtld_error(sorry);
	return -1;
}

#pragma weak dlerror
char *
dlerror(void)
{
	return sorry;
}

#pragma weak dlopen
void *
dlopen(const char *name __unused, int mode __unused)
{
	_rtld_error(sorry);
	return NULL;
}

#pragma weak dlsym
void *
dlsym(void *handle __unused, const char *name __unused)
{
	_rtld_error(sorry);
	return NULL;
}

#pragma weak dlfunc
dlfunc_t
dlfunc(void * handle __unused, const char * name __unused)
{
	_rtld_error(sorry);
	return NULL;
}

#pragma weak dlvsym
void *
dlvsym(void *handle __unused,const char *name __unused,
    const char *version __unused)
{
	_rtld_error(sorry);
	return NULL;
}

#pragma weak dlinfo
int
dlinfo(void *handle __unused, int request __unused, void *p __unused)
{
	_rtld_error(sorry);
	return 0;
}

#pragma weak dl_iterate_phdr
int
dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
    void *data)
{
	_rtld_error(sorry);
	return 0;
}

#pragma weak _rtld_addr_phdr
int
_rtld_addr_phdr(const void *addr, struct dl_phdr_info *phdr_info)
{

	return (0);
}

#pragma weak _rtld_get_stack_prot
int
_rtld_get_stack_prot(void)
{
	return (PROT_EXEC | PROT_READ | PROT_WRITE);
}
