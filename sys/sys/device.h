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
 * $DragonFly: src/sys/sys/device.h,v 1.3 2004/07/16 05:51:57 dillon Exp $
 */

#ifndef _SYS_DEVICE_H_
#define _SYS_DEVICE_H_

#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif

/*
 * This structure is at the base of every CDEVSW port message
 */
struct cdevmsg  {
    lwkt_msg	msg;
    dev_t	dev;
};

/*
 * int d_open(dev_t dev, int oflags, int devtype, thread_t td)
 */
struct cdevmsg_open {
    struct cdevmsg msg;
    int		oflags;
    int		devtype;
    struct thread *td;
};

/*
 * int d_close(dev_t dev, int fflag, int devtype, thread_t td)
 */
struct cdevmsg_close {
    struct cdevmsg msg;
    int		fflag;
    int		devtype;
    struct thread *td;
};

/*
 * void d_strategy(struct buf *bp)
 */
struct cdevmsg_strategy {
    struct cdevmsg msg;
    struct buf	*bp;
};

/*
 * int d_ioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, thread_t td)
 */
struct cdevmsg_ioctl {
    struct cdevmsg msg;
    u_long	cmd;
    caddr_t	data;
    int		fflag;
    struct thread *td;
};

/*
 * void d_dump(dev_t dev)
 */
struct cdevmsg_dump {
    struct cdevmsg msg;
    u_int count;
    u_int blkno;
    u_int secsize;
};

/*
 * int d_psize(dev_t dev)
 */
struct cdevmsg_psize {
    struct cdevmsg msg;
    int		result;
};

/*
 * int d_read(dev_t dev, struct uio *uio, int ioflag)
 */
struct cdevmsg_read {
    struct cdevmsg msg;
    struct uio 	*uio;
    int		ioflag;
};

/*
 * int d_write(dev_t dev, struct uio *uio, int ioflag)
 */
struct cdevmsg_write {
    struct cdevmsg msg;
    struct uio 	*uio;
    int		ioflag;
};

/*
 * int d_poll(dev_t dev, int events, thread_t td)
 */
struct cdevmsg_poll {
    struct cdevmsg msg;
    int		events;
    struct thread *td;
};

/*
 * int d_kqfilter(dev_t dev, struct knote *kn)
 */
struct cdevmsg_kqfilter {
    struct cdevmsg msg;
    struct knote *kn;
    int		result;
};

/*
 * int d_mmap(dev_t dev, vm_offset_t offset, int nprot)
 */
struct cdevmsg_mmap {
    struct cdevmsg msg;
    vm_offset_t	offset;
    int		nprot;
    int		result;	/* page number */
};

union cdevallmsg {
    struct lwkt_msg		am_lmsg;
    struct cdevmsg 		am_msg;
    struct cdevmsg_open		am_open;
    struct cdevmsg_close	am_close;
    struct cdevmsg_strategy	am_strategy;
    struct cdevmsg_ioctl	am_ioctl;
    struct cdevmsg_dump		am_dump;
    struct cdevmsg_psize	am_psize;
    struct cdevmsg_read		am_read;
    struct cdevmsg_write	am_write;
    struct cdevmsg_poll		am_poll;
    struct cdevmsg_kqfilter	am_kqfilter;
    struct cdevmsg_mmap		am_mmap;
};

typedef union cdevallmsg *cdevallmsg_t;
typedef struct cdevmsg		*cdevmsg_t;
typedef struct cdevmsg_open	*cdevmsg_open_t;
typedef struct cdevmsg_close	*cdevmsg_close_t;
typedef struct cdevmsg_strategy	*cdevmsg_strategy_t;
typedef struct cdevmsg_ioctl	*cdevmsg_ioctl_t;
typedef struct cdevmsg_dump	*cdevmsg_dump_t;
typedef struct cdevmsg_psize	*cdevmsg_psize_t;
typedef struct cdevmsg_read	*cdevmsg_read_t;
typedef struct cdevmsg_write	*cdevmsg_write_t;
typedef struct cdevmsg_poll	*cdevmsg_poll_t;
typedef struct cdevmsg_kqfilter	*cdevmsg_kqfilter_t;
typedef struct cdevmsg_mmap	*cdevmsg_mmap_t;

#define CDEV_CMD_OPEN		(MSG_CMD_CDEV|0x0001)
#define CDEV_CMD_CLOSE		(MSG_CMD_CDEV|0x0002)
#define CDEV_CMD_STRATEGY	(MSG_CMD_CDEV|0x0003)
#define CDEV_CMD_IOCTL		(MSG_CMD_CDEV|0x0004)
#define CDEV_CMD_DUMP		(MSG_CMD_CDEV|0x0005)
#define CDEV_CMD_PSIZE		(MSG_CMD_CDEV|0x0006)
#define CDEV_CMD_READ		(MSG_CMD_CDEV|0x0007)
#define CDEV_CMD_WRITE		(MSG_CMD_CDEV|0x0008)
#define CDEV_CMD_POLL		(MSG_CMD_CDEV|0x0009)
#define CDEV_CMD_KQFILTER	(MSG_CMD_CDEV|0x000A)
#define CDEV_CMD_MMAP		(MSG_CMD_CDEV|0x000B)

#ifdef _KERNEL

struct disk;

const char *dev_dname(dev_t dev);
struct lwkt_port *dev_dport(dev_t dev);
int dev_dflags(dev_t dev);
int dev_dmaj(dev_t dev);
int dev_dopen(dev_t dev, int oflags, int devtype, struct thread *td);
int dev_dclose(dev_t dev, int fflag, int devtype, struct thread *td);
void dev_dstrategy(dev_t dev, struct buf *bp);
int dev_dioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct thread *td);
int dev_ddump(dev_t dev);
int dev_dpsize(dev_t dev);
int dev_dread(dev_t dev, struct uio *uio, int ioflag);
int dev_dwrite(dev_t dev, struct uio *uio, int ioflag);
int dev_dpoll(dev_t dev, int events, struct thread *td);
int dev_dkqfilter(dev_t dev, struct knote *kn);
int dev_dmmap(dev_t dev, vm_offset_t offset, int nprot);

#endif

#endif

