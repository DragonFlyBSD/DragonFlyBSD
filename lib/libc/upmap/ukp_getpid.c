/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
#include <machine/cpufunc.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
/*#include "un-namespace.h"*/
#include "libc_private.h"
#include "upmap.h"

extern pid_t __sys_getpid(void);
int __getpid(void);

__weak_reference(__getpid, getpid);

static int fast_getpid_state;
static int fast_getpid_count;
static pid_t *pidp;
static int   *invforkp;

/*
 * Optimize getpid() if it is called a lot.  We have to be careful when
 * getpid() is called in a vfork child prior to exec as the shared area
 * is still the parent's shared area.
 */
pid_t
__getpid(void)
{
	if (fast_getpid_state == 0 && fast_getpid_count++ >= 10) {
		__upmap_map(&pidp, &fast_getpid_state, UPTYPE_PID);
		__upmap_map(&invforkp, &fast_getpid_state, UPTYPE_INVFORK);
		__upmap_map(NULL, &fast_getpid_state, 0);
	}
	if (fast_getpid_state > 0 && *invforkp == 0)
		return(*pidp);
	else
		return(__sys_getpid());
}

#if 0 /* DEBUG */
pid_t xxx_getpid(void);

pid_t
xxx_getpid(void)
{
	return(__sys_getpid());
}
#endif
