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
 * $DragonFly: src/sys/sys/device.h,v 1.6 2006/07/28 02:17:41 dillon Exp $
 */

#ifndef _SYS_DEVICE_H_
#define _SYS_DEVICE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_SYSLINK_H_
#include <sys/syslink.h>
#endif

/*
 * This structure is at the base of every device args structure
 */
struct dev_generic_args {
	struct syslink_desc *a_desc;
	dev_t		a_dev;
};

typedef struct dev_generic_args dev_default_args;

/*
 * int d_open(dev_t dev, int oflags, int devtype, struct ucred *cred)
 */
struct dev_open_args {
	struct dev_generic_args a_head;
	int		a_oflags;
	int		a_devtype;
	struct ucred	*a_cred;
};

/*
 * int d_close(dev_t dev, int fflag, int devtype)
 */
struct dev_close_args {
	struct dev_generic_args a_head;
	int		a_fflag;
	int		a_devtype;
};

/*
 * int d_read(dev_t dev, struct uio *uio, int ioflag)
 */
struct dev_read_args {
	struct dev_generic_args	a_head;
	struct uio	*a_uio;
	int		a_ioflag;
};

/*
 * int d_write(dev_t dev, struct uio *uio, int ioflag)
 */
struct dev_write_args {
	struct dev_generic_args a_head;
	struct uio 	*a_uio;
	int		a_ioflag;
};

/*
 * int d_ioctl(dev_t dev, u_long cmd, caddr_t data, int fflag,
 *	       struct ucred *cred)
 */
struct dev_ioctl_args {
	struct dev_generic_args a_head;
	u_long		a_cmd;
	caddr_t		a_data;
	int		a_fflag;
	struct ucred	*a_cred;
};

/*
 * int d_poll(dev_t dev, int events)
 */
struct dev_poll_args {
	struct dev_generic_args a_head;
	int		a_events;
};

/*
 * int d_mmap(dev_t dev, vm_offset_t offset, int nprot)
 */
struct dev_mmap_args {
	struct dev_generic_args a_head;
	vm_offset_t	a_offset;
	int		a_nprot;
	int		a_result;	/* page number */
};

/*
 * void d_strategy(dev_t dev, struct bio *bio)
 */
struct dev_strategy_args {
	struct dev_generic_args a_head;
	struct bio	*a_bio;
};

/*
 * void d_dump(dev_t dev)
 */
struct dev_dump_args {
	struct dev_generic_args a_head;
	u_int		a_count;
	u_int		a_blkno;
	u_int		a_secsize;
};

/*
 * int d_psize(dev_t dev)
 */
struct dev_psize_args {
	struct dev_generic_args	a_head;
	int		a_result;
};

/*
 * int d_kqfilter(dev_t dev, struct knote *kn)
 */
struct dev_kqfilter_args {
	struct dev_generic_args a_head;
	struct knote	*a_kn;
	int		a_result;
};

struct dev_clone_args {
	struct dev_generic_args a_head;
};

/*
 * Typedefs to help drivers declare the driver routines and such
 */
typedef int d_default_t (struct dev_generic_args *ap);
typedef int d_open_t (struct dev_open_args *ap);
typedef int d_close_t (struct dev_close_args *ap);
typedef int d_read_t (struct dev_read_args *ap);
typedef int d_write_t (struct dev_write_args *ap);
typedef int d_ioctl_t (struct dev_ioctl_args *ap);
typedef int d_poll_t (struct dev_poll_args *ap);
typedef int d_mmap_t (struct dev_mmap_args *ap);
typedef int d_strategy_t (struct dev_strategy_args *ap);
typedef int d_dump_t (struct dev_dump_args *ap);
typedef int d_psize_t (struct dev_psize_args *ap);
typedef int d_kqfilter_t (struct dev_kqfilter_args *ap);
typedef int d_clone_t (struct dev_clone_args *ap);

/*
 * Character device switch table.
 *
 * NOTE: positions are hard coded for static structure initialization.
 */
struct dev_ops {
	struct {
		const char	*name;	/* base name, e.g. 'da' */
		int		maj;	/* major device number */
		u_int		flags;	/* D_XXX flags */
		void            *data;	/* custom driver data */
		int             refs;	/* ref count */
	} head;

#define dev_ops_first_field	d_default
	d_default_t	*d_default;
	d_open_t	*d_open;
	d_close_t	*d_close;
	d_read_t	*d_read;
	d_write_t	*d_write;
	d_ioctl_t	*d_ioctl;
	d_poll_t	*d_poll;
	d_mmap_t	*d_mmap;
	d_strategy_t	*d_strategy;
	d_dump_t	*d_dump;
	d_psize_t	*d_psize;
	d_kqfilter_t	*d_kqfilter;
	d_clone_t	*d_clone;	/* clone from base dev_ops */
#define dev_ops_last_field	d_clone
};

/*
 * Types for d_flags.
 */
#define D_TAPE		0x0001
#define D_DISK		0x0002
#define D_TTY		0x0004
#define D_MEM		0x0008

#define D_TYPEMASK	0xffff

/*
 * Flags for d_flags.
 */
#define D_MEMDISK	0x00010000	/* memory type disk */
#define D_NAGGED	0x00020000	/* nagged about missing make_dev() */
#define D_CANFREE	0x00040000	/* can free blocks */
#define D_TRACKCLOSE	0x00080000	/* track all closes */
#define D_MASTER	0x00100000	/* used by pty/tty code */
#define D_KQFILTER	0x00200000	/* has kqfilter entry */

/*
 * A union of all possible argument structures.
 */
union dev_args_union {
	struct dev_generic_args	du_head;
	struct dev_open_args	du_open;
	struct dev_close_args	du_close;
	struct dev_read_args	du_read;
	struct dev_write_args	du_write;
	struct dev_ioctl_args	du_ioctl;
	struct dev_poll_args	du_poll;
	struct dev_mmap_args	du_mmap;
	struct dev_strategy_args du_strategy;
	struct dev_dump_args	du_dump;
	struct dev_psize_args	du_psize;
	struct dev_kqfilter_args du_kqfilter;
	struct dev_clone_args	du_clone;
};

/*
 * Linking structure for mask/match registration
 */
struct dev_ops_link {
	struct dev_ops_link *next;
	u_int		mask;
	u_int		match;
	struct dev_ops	*ops;
};

#ifdef _KERNEL

extern struct dev_ops dead_dev_ops;

struct disk;

int dev_dopen(dev_t dev, int oflags, int devtype, struct ucred *cred);
int dev_dclose(dev_t dev, int fflag, int devtype);
void dev_dstrategy(dev_t dev, struct bio *bio);
void dev_dstrategy_chain(dev_t dev, struct bio *bio);
int dev_dioctl(dev_t dev, u_long cmd, caddr_t data, int fflag,
		struct ucred *cred);
int dev_ddump(dev_t dev);
int dev_dpsize(dev_t dev);
int dev_dread(dev_t dev, struct uio *uio, int ioflag);
int dev_dwrite(dev_t dev, struct uio *uio, int ioflag);
int dev_dpoll(dev_t dev, int events);
int dev_dkqfilter(dev_t dev, struct knote *kn);
int dev_dmmap(dev_t dev, vm_offset_t offset, int nprot);
int dev_dclone(dev_t dev);

const char *dev_dname(dev_t dev);
int dev_dmaj(dev_t dev);
int dev_dflags(dev_t dev);
int dev_doperate(struct dev_generic_args *ap);
int dev_doperate_ops(struct dev_ops *, struct dev_generic_args *ap);

d_default_t	nodefault;
d_open_t	noopen;
d_close_t	noclose;
d_read_t	noread;
d_write_t	nowrite;
d_ioctl_t	noioctl;
d_poll_t	nopoll;
d_mmap_t	nommap;
d_strategy_t	nostrategy;
d_dump_t	nodump;
d_psize_t	nopsize;
d_kqfilter_t	nokqfilter;
d_clone_t	noclone;

d_open_t	nullopen;
d_close_t	nullclose;

extern struct syslink_desc dev_default_desc;
extern struct syslink_desc dev_open_desc;
extern struct syslink_desc dev_close_desc;
extern struct syslink_desc dev_read_desc;
extern struct syslink_desc dev_write_desc;
extern struct syslink_desc dev_ioctl_desc;
extern struct syslink_desc dev_dump_desc;
extern struct syslink_desc dev_psize_desc;
extern struct syslink_desc dev_poll_desc;
extern struct syslink_desc dev_mmap_desc;
extern struct syslink_desc dev_strategu_desc;
extern struct syslink_desc dev_kqfilter_desc;
extern struct syslink_desc dev_clone_desc;

void compile_dev_ops(struct dev_ops *);
int dev_ops_add(struct dev_ops *, u_int mask, u_int match);
int dev_ops_remove(struct dev_ops *, u_int mask, u_int match);
void dev_ops_release(struct dev_ops *);
struct dev_ops *dev_ops_add_override(dev_t, struct dev_ops *, u_int, u_int);

struct dev_ops *dev_ops_intercept(dev_t, struct dev_ops *);
void dev_ops_restore(dev_t, struct dev_ops *);
struct dev_ops *dev_ops_get(int x, int y);

void destroy_all_devs(struct dev_ops *, u_int mask, u_int match);
dev_t make_dev(struct dev_ops *ops, int minor, uid_t uid, gid_t gid,
		int perms, const char *fmt, ...) __printflike(6, 7);
dev_t make_adhoc_dev (struct dev_ops *ops, int minor);

#endif

#endif

