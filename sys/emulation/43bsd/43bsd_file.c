/*
 * 43BSD_FILE.C		- 4.3BSD compatibility file syscalls
 *
 * Copyright (c) 1989, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 * These syscalls used to live in kern/vfs_syscalls.c.  They are modified
 * to use the new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>

#include <sys/mplock2.h>

/*
 * MPALMOSTSAFE
 */
int
sys_ocreat(struct ocreat_args *uap)
{
	struct nlookupdata nd;
	int error;

	get_mplock();
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_open(&nd, O_WRONLY | O_CREAT | O_TRUNC, 
				    uap->mode, &uap->sysmsg_iresult);
	}
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_oftruncate(struct oftruncate_args *uap)
{
	int error;

	get_mplock();
	error = kern_ftruncate(uap->fd, uap->length);
	rel_mplock();

	return (error);
}

/*
 * MPSAFE
 */
int
sys_olseek(struct olseek_args *uap)
{
	int error;

	error = kern_lseek(uap->fd, uap->offset, uap->whence,
			   &uap->sysmsg_offset);

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_otruncate(struct otruncate_args *uap)
{
	struct nlookupdata nd;
	int error;

	get_mplock();
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_truncate(&nd, uap->length);
	nlookup_done(&nd);
	rel_mplock();
	return (error);
}

#define	PADDED_SIZE(x)	\
	((sizeof(struct odirent) + (x) + 1 + 3) & ~3)
#define	MAX_NAMELEN	255

struct odirent {
	uint32_t	od_fileno;
	uint16_t	od_reclen;
	uint8_t		od_type;
	uint8_t		od_namlen;
	char		od_name[];
};

/*
 * MPALMOSTSAFE
 */
int
sys_ogetdirentries(struct ogetdirentries_args *uap)
{
	int error, bytes_transfered;
	char *buf, *outbuf;
	size_t len;
	struct dirent *ndp;
	struct odirent *destdp;
	long base;

	if (uap->count > 16384)
		len = 16384;
	else
		len = uap->count;

	buf = kmalloc(len, M_TEMP, M_WAITOK);

	get_mplock();
	error = kern_getdirentries(uap->fd, buf, len,
				   &base, &bytes_transfered, UIO_SYSSPACE);
	rel_mplock();

	if (error) {
		kfree(buf, M_TEMP);
		return(error);
	}

	ndp = (struct dirent *)buf;
	outbuf = uap->buf;
	destdp = kmalloc(PADDED_SIZE(MAX_NAMELEN), M_TEMP, M_WAITOK);

	for (; (char *)ndp < buf + bytes_transfered; ndp = _DIRENT_NEXT(ndp)) {
		if ((char *)_DIRENT_NEXT(ndp) > buf + bytes_transfered)
			break;
		if (ndp->d_namlen > MAX_NAMELEN)
			continue;
		destdp->od_fileno = ndp->d_ino;
#if BYTE_ORDER == LITTLE_ENDIAN
		destdp->od_type = ndp->d_namlen;
		destdp->od_namlen = ndp->d_type;
#else
		destdp->od_type = ndp->d_type;
		destdp->od_namlen = ndp->d_namlen;
#endif
		destdp->od_reclen = PADDED_SIZE(destdp->od_namlen);
		if (destdp->od_reclen > len)
			break; /* XXX can not happen */
		bcopy(ndp->d_name, destdp->od_name, destdp->od_namlen);
		bzero(destdp->od_name + destdp->od_namlen,
		    PADDED_SIZE(destdp->od_namlen) - sizeof(*destdp) -
		    destdp->od_namlen);
		error = copyout(destdp, outbuf,
		    PADDED_SIZE(destdp->od_namlen));
		if (error)
			break;
		outbuf += PADDED_SIZE(destdp->od_namlen);
		len -= PADDED_SIZE(destdp->od_namlen);
	}

	kfree(destdp, M_TEMP);
	kfree(buf, M_TEMP);
	uap->sysmsg_iresult = (int)(outbuf - uap->buf);
	return (0);
}
