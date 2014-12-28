/*-
 * Copyright (c) 1989, 1993
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
 *	@(#)dirent.h	8.3 (Berkeley) 8/10/94
 * $FreeBSD: src/sys/sys/dirent.h,v 1.11 1999/12/29 04:24:39 peter Exp $
 */

#ifndef	_SYS_DIRENT_H_
#define	_SYS_DIRENT_H_

/*
 * The dirent structure defines the format of directory entries returned by
 * the getdirentries(2) system call.
 *
 * A directory entry has a struct dirent at the front of it, containing its
 * inode number, the length of the entry, and the length of the name
 * contained in the entry.  These are followed by the name padded to a 8
 * byte boundary with null bytes.  All names are guaranteed null terminated.
 */

/*
 * XXX Temporary bandaids to keep changes small:
 * XXX - for userland programs which don't specify any C or POSIX options,
 * XXX   keep the old d_fileno and map d_ino via macro.  Everything else gets
 * XXX   the POSIX d_ino and only that.
 * XXX - d_name is declared with the current maximum directory entry length,
 * XXX   instead of being incomplete. Code must allocate space for the
 * XXX   directory itself.
 */

#include <sys/cdefs.h>
#include <sys/types.h>

struct dirent {
#if defined(_KERNEL) || !__BSD_VISIBLE
	ino_t		d_ino;		/* file number of entry */
#else
	ino_t		d_fileno;	/* file number of entry */
#endif
	uint16_t	d_namlen;	/* strlen(d_name) */
	uint8_t		d_type;		/* file type, see blow */
	uint8_t		d_unused1;	/* padding, reserved */
	uint32_t	d_unused2;	/* reserved */
	char		d_name[255 + 1];
					/* name, NUL-terminated */
};

/*
 * Linux compatibility, but its a good idea anyhow
 */
#define _DIRENT_HAVE_D_NAMLEN
#define _DIRENT_HAVE_D_TYPE

#if !defined(_KERNEL) && __BSD_VISIBLE
#define	d_ino		d_fileno
#endif

/*
 * File types
 */
#define	DT_UNKNOWN	 0
#define	DT_FIFO		 1
#define	DT_CHR		 2
#define	DT_DIR		 4
#define	DT_BLK		 6
#define	DT_REG		 8
#define	DT_LNK		10
#define	DT_SOCK		12
#define	DT_WHT		14
#define DT_DBF		15	/* database record file */

/*
 * The _DIRENT_DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This requires the amount of space in struct dirent
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to an 8 byte boundary.
 *
 * The _DIRENT_MINSIZ macro gives space needed for the directory entry without
 * the padding _DIRENT_DIRSIZ adds at the end.
 */
#define	_DIRENT_MINSIZ(dp) \
	(__offsetof(struct dirent, d_name) + (dp)->d_namlen + 1)
#define	_DIRENT_RECLEN(namelen) \
	((__offsetof(struct dirent, d_name) + (namelen) + 1 + 7) & ~7)
#define	_DIRENT_DIRSIZ(dp)	_DIRENT_RECLEN((dp)->d_namlen)
#define	_DIRENT_NEXT(dp) \
	((struct dirent *)((uint8_t *)(dp) + _DIRENT_DIRSIZ(dp)))

#endif /* !_SYS_DIRENT_H_ */
