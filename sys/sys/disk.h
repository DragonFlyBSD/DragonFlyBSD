/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * 
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/sys/disk.h,v 1.16.2.3 2001/06/20 16:11:01 scottl Exp $
 * $DragonFly: src/sys/sys/disk.h,v 1.7 2004/12/30 07:01:52 cpressey Exp $
 */

#ifndef _SYS_DISK_H_
#define	_SYS_DISK_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

#ifndef _SYS_DISKSLICE_H_
#include <sys/diskslice.h>
#endif

#ifndef _SYS_DISKLABEL
#include <sys/disklabel.h>
#endif

#ifndef _SYS_DISKLABEL
#include <sys/msgport.h>
#endif

struct disk {
	struct lwkt_port	d_port;		/* interception port */
	struct cdevsw		*d_devsw;	/* our device switch */
	struct cdevsw		*d_rawsw;	/* the raw device switch */
	u_int			d_flags;
	u_int			d_dsflags;
	dev_t			d_rawdev;	/* backing raw device */
	dev_t			d_cdev;		/* special whole-disk part */
	struct diskslices	*d_slice;
	struct disklabel	d_label;
	LIST_ENTRY(disk)	d_list;
};

#define DISKFLAG_LOCK		0x1
#define DISKFLAG_WANTED		0x2

#ifdef _KERNEL
dev_t disk_create (int unit, struct disk *disk, int flags, struct cdevsw *sw);
void disk_destroy (struct disk *disk);
int disk_dumpcheck (dev_t dev, u_int *count, u_int *blkno, u_int *secsize);
struct disk *disk_enumerate (struct disk *disk);
void disk_invalidate (struct disk *disk);
#endif /* _KERNEL */

#endif /* _SYS_DISK_H_ */
