/* $FreeBSD: src/sys/compat/linux/linux_getcwd.c,v 1.2.2.3 2001/11/05 19:08:22 marcel Exp $ */
/* $DragonFly: src/sys/emulation/linux/linux_getcwd.c,v 1.23 2006/12/23 00:27:02 swildner Exp $ */
/* $OpenBSD: linux_getcwd.c,v 1.2 2001/05/16 12:50:21 ho Exp $ */
/* $NetBSD: vfs_getcwd.c,v 1.3.2.3 1999/07/11 10:24:09 sommerfeld Exp $ */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Bill Sommerfeld.
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
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/kern_syscall.h>
#include <vfs/ufs/dir.h>	/* XXX only for DIRBLKSIZ */

#include <sys/mplock2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_util.h"

/*
 * Find pathname of process's current directory.
 *
 * Use vfs vnode-to-name reverse cache; if that fails, fall back
 * to reading directory contents.
 *
 * MPALMOSTSAFE
 */
int
sys_linux_getcwd(struct linux_getcwd_args *args)
{
	int buflen;
	int error;
	char *buf;
	char *bp;

#ifdef DEBUG
	kprintf("Linux-emul(%d): getcwd(%p, %d)\n", 
	    (curthread->td_proc ? (int)curthread->td_proc->p_pid : -1),
	    args->buf, args->bufsize);
#endif
	buflen = args->bufsize;
	if (buflen < 2)
		return (EINVAL);
	if (buflen > MAXPATHLEN)
		buflen = MAXPATHLEN;

	buf = kmalloc(buflen, M_TEMP, M_WAITOK);
	get_mplock();
	bp = kern_getcwd(buf, buflen, &error);
	rel_mplock();
	if (error == 0) {
		buflen = strlen(bp) + 1;
		error = copyout(bp, args->buf, buflen);
		args->sysmsg_result = buflen;
	}
	kfree(buf, M_TEMP);
	return (error);
}
