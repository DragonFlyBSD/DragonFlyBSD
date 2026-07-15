/*
 * Copyright (c) 2001 Daniel Eischen <deischen@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL EISCHEN AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
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
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include "libc_private.h"

void	_nmalloc_thr_init(void);
void	_nmalloc_thr_exit(void);
void	_upmap_thr_init(void);
#ifndef __LIBC_RTLD
void	_pthread_init_early(void);
#endif

int	_thread_autoinit_dummy_decl_stub = 0;

/*
 * Threading bootstrap, run for every process.
 * Keep this constructor as a no-op so processes that never use pthreads do
 * not initialize thread state.  The first pthread API initializes it lazily.
 */
void _thread_init(void) __constructor(101);
/* Old name of the early-init constructor, kept for ABI compatibility. */
__strong_reference(_thread_init, _pthread_init);
void
_thread_init(void)
{
}

/*
 * Pre-initialization prior to thread startup to avoid certain
 * chicken-and-egg issues with low-level allocations.
 */
void
_libc_thr_init(void)
{
	_nmalloc_thr_init();
	_upmap_thr_init();
}

/*
 * Per-thread teardown, called by pthreads when a thread exits, after
 * the application's TSD destructors have run.
 */
void
_libc_thr_exit(void)
{
	_nmalloc_thr_exit();
}

__weak_reference(_thread_autoinit_dummy_decl_stub, _thread_autoinit_dummy_decl);
