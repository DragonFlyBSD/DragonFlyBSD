/*-
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 *
 * $DragonFly: src/sys/emulation/dragonfly12/dfbsd12_getdirentries.c,v 1.3 2006/09/05 00:55:44 dillon Exp $
 */

#include "opt_compatdf12.h"

#include <sys/param.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/kern_syscall.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/uio.h>

#include <sys/mplock2.h>

#define	PADDED_SIZE(x)	\
	((sizeof(struct dfbsd12_dirent) + (x) + 1 + 3) & ~3)
#define	MAX_NAMELEN	255

struct dfbsd12_dirent {
	uint32_t	df12_d_fileno;
	uint16_t	df12_d_reclen;
	uint8_t		df12_d_type;
	uint8_t		df12_d_namlen;
	char		df12_d_name[];
};

/*
 * MPALMOSTSAFE
 */
static int
common_getdirentries(long *base, int *result, int fd, char *usr_buf, size_t count)
{
	int error, bytes_transfered;
	char *buf, *outbuf;
	size_t len;
	struct dirent *ndp;
	struct dfbsd12_dirent *destdp;

	if (count > 16384)
		len = 16384;
	else
		len = count;

	buf = kmalloc(len, M_TEMP, M_WAITOK);

	get_mplock();
	error = kern_getdirentries(fd, buf, len, base,
				   &bytes_transfered, UIO_SYSSPACE);
	rel_mplock();

	if (error) {
		kfree(buf, M_TEMP);
		return(error);
	}

	ndp = (struct dirent *)buf;
	outbuf = usr_buf;
	destdp = kmalloc(PADDED_SIZE(MAX_NAMELEN), M_TEMP, M_WAITOK);

	for (; (char *)ndp < buf + bytes_transfered; ndp = _DIRENT_NEXT(ndp)) {
		if ((char *)_DIRENT_NEXT(ndp) > buf + bytes_transfered)
			break;
		if (ndp->d_namlen > MAX_NAMELEN)
			continue;
		destdp->df12_d_fileno = ndp->d_ino;
		destdp->df12_d_type = ndp->d_type;
		destdp->df12_d_namlen = ndp->d_namlen;
		destdp->df12_d_reclen = PADDED_SIZE(destdp->df12_d_namlen);
		if (destdp->df12_d_reclen > len)
			break; /* XXX can not happen */
		bcopy(ndp->d_name, destdp->df12_d_name, destdp->df12_d_namlen);
		bzero(destdp->df12_d_name + destdp->df12_d_namlen,
		    PADDED_SIZE(destdp->df12_d_namlen) - sizeof(*destdp) -
		    destdp->df12_d_namlen);
		error = copyout(destdp, outbuf,
		    PADDED_SIZE(destdp->df12_d_namlen));
		if (error)
			break;
		outbuf += PADDED_SIZE(destdp->df12_d_namlen);
		len -= PADDED_SIZE(destdp->df12_d_namlen);
	}

	kfree(destdp, M_TEMP);
	kfree(buf, M_TEMP);
	*result = outbuf - usr_buf;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_dfbsd12_getdirentries(struct dfbsd12_getdirentries_args *uap)
{
	long base;
	int error;

	error = common_getdirentries(&base, &uap->sysmsg_iresult, uap->fd,
				     uap->buf, uap->count);

	if (error == 0)
		error = copyout(&base, uap->basep, sizeof(*uap->basep));
	return (error);
}

/*
 * MPSAFE
 */
int
sys_dfbsd12_getdents(struct dfbsd12_getdents_args *uap)
{
	return(common_getdirentries(NULL, &uap->sysmsg_iresult, uap->fd,
				    uap->buf, uap->count));
}
