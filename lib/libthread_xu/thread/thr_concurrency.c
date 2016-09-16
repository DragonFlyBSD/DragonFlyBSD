/*
 * Copyright (c) 2003 Daniel M. Eischen <deischen@freebsd.org>
 * Copyright (c) 2003 Sergey Osokin <osa@freebsd.org.ru>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD: src/lib/libpthread/thread/thr_concurrency.c,v 1.8 2004/03/14 05:24:27 bde Exp $
 */

#include "namespace.h"
#include <errno.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

static int current_concurrency = 0;

int
_pthread_getconcurrency(void)
{
	return current_concurrency;
}

int
_pthread_setconcurrency(int new_level)
{
	if (new_level < 0)
		return (EINVAL);

	current_concurrency = new_level;
	return 0;
}

__strong_reference(_pthread_getconcurrency, pthread_getconcurrency);
__strong_reference(_pthread_setconcurrency, pthread_setconcurrency);
