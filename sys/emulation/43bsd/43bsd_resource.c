/*
 * 43BSD_RESOURCE.C	- 4.3BSD compatibility exit syscalls
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/emulation/43bsd/43bsd_resource.c,v 1.1 2003/11/03 15:57:33 daver Exp $
 *	from: DragonFly kern/kern_resource.c,v 1.14
 *
 * These syscalls used to live in kern/kern_resource.c.  They are modified
 * to use the new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/resourcevar.h>

int
ogetrlimit(struct ogetrlimit_args *uap)
{
	struct orlimit olim;
	struct rlimit lim;
	int error;

	error = kern_getrlimit(uap->which, &lim);

	if (error == 0) {
		olim.rlim_cur = lim.rlim_cur;
		if (olim.rlim_cur == -1)
			olim.rlim_cur = 0x7fffffff;
		olim.rlim_max = lim.rlim_max;
		if (olim.rlim_max == -1)
			olim.rlim_max = 0x7fffffff;
		error = copyout(&olim, uap->rlp, sizeof(*uap->rlp));
	}
	return (error);

}

int
osetrlimit(struct osetrlimit_args *uap)
{
	struct orlimit olim;
	struct rlimit lim;
	int error;

	error = copyin(uap->rlp, &olim, sizeof(olim));
	if (error)
		return (error);
	lim.rlim_cur = olim.rlim_cur;
	lim.rlim_max = olim.rlim_max;

	error = kern_setrlimit(uap->which, &lim);

	return (error);

}
