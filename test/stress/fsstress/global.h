/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
/*
 * $DragonFly: src/test/stress/fsstress/global.h,v 1.1 2004/05/07 17:51:02 dillon Exp $
 */
 
#ifndef GLOBAL_H
#define GLOBAL_H

/* xfs-specific includes */

#ifndef NO_XFS
#include <libxfs.h>
#include <attributes.h>
#else
#include <xfscompat.h>
#endif

/* libc includes */

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/param.h>
#endif
#include <sys/stat.h>
#ifndef __FreeBSD__
#include <sys/statvfs.h>
#endif
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#ifndef __FreeBSD__
#include <malloc.h>
#endif
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <string.h>
#ifndef O_DIRECT
#define O_DIRECT        040000
#endif

#ifdef __FreeBSD__
typedef off_t	off64_t;
#define stat64	stat
#define lseek64	lseek
#define lstat64	lstat
#define fstat64	fstat
#define ftruncate64	ftruncate
#define truncate64	truncate
#define readdir64	readdir
#define fdatasync	fsync

static __inline
void *
memalign(int blksize, int bytes)
{
    void *ptr;
    int blkmask;
    static int pagesize;

    if (pagesize == 0)
	pagesize = getpagesize();
    if (blksize < pagesize)
	blksize = pagesize;
    blkmask = blksize - 1;
    ptr = malloc((bytes + blkmask) & ~blkmask);
    bzero(ptr, bytes);
    return(ptr);
}

#endif

#endif

