/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)file.h	8.3 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/file.h,v 1.22.2.7 2002/11/21 23:39:24 sam Exp $
 * $DragonFly: src/sys/sys/file2.h,v 1.8 2006/08/02 01:25:28 dillon Exp $
 */

#ifndef _SYS_FILE2_H_
#define	_SYS_FILE2_H_

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/file.h>
#endif

static __inline int
fo_read(
	struct file *fp,
	struct uio *uio,
	struct ucred *cred,
	int flags
) {
	int error;

	fhold(fp);
	error = (*fp->f_ops->fo_read)(fp, uio, cred, flags);
	fdrop(fp);
	return (error);
}

static __inline int
fo_write(
	struct file *fp,
	struct uio *uio,
	struct ucred *cred,
	int flags
) {
	int error;

	fhold(fp);
	error = (*fp->f_ops->fo_write)(fp, uio, cred, flags);
	fdrop(fp);
	return (error);
}

static __inline int
fo_ioctl(
	struct file *fp,
	u_long com,
	caddr_t data,
	struct ucred *cred,
	struct sysmsg *msg
) {
	int error;

	fhold(fp);
	error = (*fp->f_ops->fo_ioctl)(fp, com, data, cred, msg);
	fdrop(fp);
	return (error);
}

static __inline int
fo_stat(struct file *fp, struct stat *sb, struct ucred *cred)
{
	int error;

	fhold(fp);
	error = (*fp->f_ops->fo_stat)(fp, sb, cred);
	fdrop(fp);
	return (error);
}

static __inline int
fo_close(struct file *fp)
{
	return ((*fp->f_ops->fo_close)(fp));
}

static __inline int
fo_shutdown(struct file *fp, int how)
{
	return ((*fp->f_ops->fo_shutdown)(fp, how));
}

static __inline int
fo_kqfilter(struct file *fp, struct knote *kn)
{
	return ((*fp->f_ops->fo_kqfilter)(fp, kn));
}

#endif /* _KERNEL */

#endif /* !SYS_FILE2_H */
