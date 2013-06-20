/*-
 * Copyright (c) 1988, 1993
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
 * $FreeBSD: src/usr.bin/fstat/fstat.h,v 1.1.2.1 2000/07/02 10:20:25 ps Exp $
 */

#ifndef	__FSTAT_H__
#define	__FSTAT_H__

#define dprintf	if (vflg) fprintf

typedef struct devs {
	struct	devs *next;
	long	fsid;
	ino_t	ino;
	const char	*name;
} DEVS;

struct  filestat {
	long	fsid;
	long	fileid;
	mode_t	mode;
	long long size;
	int64_t	offset;
	dev_t	rdev;
};

static inline mode_t
mtrans(enum vtype type)
{
	switch (type) {
	case VREG:
		return S_IFREG;
	case VDIR:
		return S_IFDIR;
	case VBLK:
		return S_IFBLK;
	case VCHR:
		return S_IFCHR;
	case VLNK:
		return S_IFLNK;
	case VSOCK:
		return S_IFSOCK;
	case VFIFO:
		return S_IFIFO;
	default:
		return 0;
	}
}

/* Ugh */
extern kvm_t *kd;
extern int vflg;
extern int Pid;

udev_t dev2udev(void *);
udev_t makeudev(int, int);

/* Additional filesystem types */
int ext2fs_filestat(struct vnode *, struct filestat *);
int hammer_filestat(struct vnode *, struct filestat *);
int isofs_filestat(struct vnode *, struct filestat *);
int msdosfs_filestat(struct vnode *, struct filestat *);
int ntfs_filestat(struct vnode *, struct filestat *);
ssize_t kread(const void *, void *, size_t);

#endif /* __FSTAT_H__ */
