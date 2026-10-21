/*
 * Copyright (c) 2018-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _LINUX_FILE_H_
#define _LINUX_FILE_H_

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/posix_types.h>

#ifdef __DragonFly__
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/spinlock2.h>
#endif

static inline int
get_unused_fd_flags(unsigned flags)
{
	struct filedesc *fdp = curthread->td_proc->p_fd;
	int error;
	int fd;

	error = fdalloc(curthread->td_proc, 0, &fd);
	if (error)
		return -error;

	if (flags & O_CLOEXEC) {
		spin_lock(&fdp->fd_spin);
		if ((unsigned)fd < fdp->fd_nfiles &&
		    fdp->fd_files[fd].reserved)
			fdp->fd_files[fd].fileflags |= UF_EXCLOSE;
		spin_unlock(&fdp->fd_spin);
	}

	return fd;
}

static inline void
fd_install(unsigned int fd, struct file *file)
{
	fsetfd(curthread->td_proc->p_fd, file, fd);
	fdrop(file);
}

static inline void
fput(struct file *file)
{
	if (file)
		fdrop(file);
}

static inline void
put_unused_fd(unsigned int fd)
{
	fsetfd(curthread->td_proc->p_fd, NULL, fd);
}

#endif	/* _LINUX_FILE_H_ */
