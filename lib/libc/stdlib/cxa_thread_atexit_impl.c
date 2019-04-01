/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/queue.h>
#include "namespace.h"
#include <link.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"

#include "libc_private.h"

#define DTORS_ITERATIONS	5

struct thread_dtorfn {
	void (*func)(void *);
	void *arg;
	void *dso;
	LIST_ENTRY(thread_dtorfn) entry;
};
static __thread LIST_HEAD(dtor_list, thread_dtorfn) dtors =
    LIST_HEAD_INITIALIZER(dtors);

int __cxa_thread_atexit_impl(void (*func)(void *), void *arg, void *dso);

void
_thread_finalize(void)
{
	struct dl_phdr_info phdr_info;
	struct thread_dtorfn *fnp, *tdtor;
	int i;

	/*
	 * It is possible to get more destructors registered while
	 * unregistering them on thread exit.  Use maximum DTORS_ITERATIONS
	 * loops.  If dso is no longer available (dlclose()), skip it.
	 */
	for (i = 0; i < DTORS_ITERATIONS && !LIST_EMPTY(&dtors); i++) {
		LIST_FOREACH_MUTABLE(fnp, &dtors, entry, tdtor) {
			LIST_REMOVE(fnp, entry);
			if (_rtld_addr_phdr(fnp->dso, &phdr_info) &&
			    __elf_phdr_match_addr(&phdr_info, fnp->func))
				fnp->func(fnp->arg);
			free(fnp);
		}
	}
}

int
__cxa_thread_atexit_impl(void (*func)(void *), void *arg, void *dso)
{
	struct thread_dtorfn *fnp;

	fnp = calloc(1, sizeof(*fnp));
	if (fnp == NULL)
		return -1;

	fnp->func = func;
	fnp->arg = arg;
	fnp->dso = dso;
	LIST_INSERT_HEAD(&dtors, fnp, entry);

	return 0;
}
