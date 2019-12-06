/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/upmap.h>
#include <sys/time.h>
#include <sys/lwp.h>
#include <machine/cpufunc.h>
#include <machine/tls.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/*#include "un-namespace.h"*/
#include "libc_private.h"
#include "upmap.h"

extern int __sys_lwp_setname(lwpid_t tid, const char *name);
extern int __sys_lwp_getname(lwpid_t tid, char *name, size_t len);
int __lwp_setname(lwpid_t tid, const char *name);
int __lwp_getname(lwpid_t tid, char *name, size_t len);

static int fast_setname_count;
static __thread int fast_setname_state TLS_ATTRIBUTE;
static __thread char *thread_title TLS_ATTRIBUTE;
static __thread lwpid_t *thread_tid TLS_ATTRIBUTE;

/*
 * Optimize lwp_setname() if it is called a lot by using the lpmap to
 * directly copy the name without making a system call.  The optimization
 * only operates when setting the name for the calling thread.
 */
int
__lwp_setname(lwpid_t tid, const char *name)
{
	if (fast_setname_state == 0 && fast_setname_count++ >= 10) {
		__lpmap_map(&thread_title, &fast_setname_state,
			    LPTYPE_THREAD_TITLE);
		__lpmap_map(&thread_tid, &fast_setname_state,
			    LPTYPE_THREAD_TID);
		__lpmap_map(NULL, &fast_setname_state, 0);
	}
	if (fast_setname_state > 0 && tid == *thread_tid) {
		size_t len = strlen(name);

		if (len >= LPMAP_MAXTHREADTITLE)
			len = LPMAP_MAXTHREADTITLE - 1;
		thread_title[len] = 0;
		bcopy(name, thread_title, len);
		return 0;
	} else {
		return(__sys_lwp_setname(tid, name));
	}
}

__weak_reference(__lwp_setname, lwp_setname);

/*
 * Optimize lwp_getname() the same way, and as with lwp_setname() the
 * optimization is only applicable for the current thread.
 */
int
__lwp_getname(lwpid_t tid, char *name, size_t len)
{
	if (fast_setname_state == 0 && fast_setname_count++ >= 10) {
		__lpmap_map(&thread_title, &fast_setname_state,
			    LPTYPE_THREAD_TITLE);
		__lpmap_map(&thread_tid, &fast_setname_state,
			    LPTYPE_THREAD_TID);
		__lpmap_map(NULL, &fast_setname_state, 0);
	}
	if (fast_setname_state > 0 && tid == *thread_tid) {
		snprintf(name, len, "%s", thread_title);
		return 0;
	} else {
		return __sys_lwp_getname(tid, name, len);
	}
}

__weak_reference(__lwp_getname, lwp_getname);
