/*-
 * Copyright (c) 1990, 1993
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
 *	@(#)conf.h	8.5 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/conf.h,v 1.103.2.6 2002/03/11 01:14:55 dd Exp $
 * $DragonFly: src/sys/sys/conf.h,v 1.5 2003/07/22 17:03:34 dillon Exp $
 */

#ifndef _SYS_CONF_H_
#define	_SYS_CONF_H_

#include <sys/queue.h>

#define SPECNAMELEN	15

struct tty;
struct disk;
struct vnode;

struct specinfo {
	u_int		si_flags;
	udev_t		si_udev;
	LIST_ENTRY(specinfo)	si_hash;
	SLIST_HEAD(, vnode) si_hlist;
	char		si_name[SPECNAMELEN + 1];
	void		*si_drv1;
	void		*si_drv2;
	struct cdevsw	*si_devsw;	/* cached */
	int		si_iosize_max;	/* maximum I/O size (for physio &al) */
	union {
		struct {
			struct tty *__sit_tty;
		} __si_tty;
		struct {
			struct disk *__sid_disk;
			struct mount *__sid_mountpoint;
			int __sid_bsize_phys; /* min physical block size */
			int __sid_bsize_best; /* optimal block size */
		} __si_disk;
	} __si_u;
};

#define SI_STASHED	0x0001	/* created in stashed storage */

#define si_tty		__si_u.__si_tty.__sit_tty
#define si_disk		__si_u.__si_disk.__sid_disk
#define si_mountpoint	__si_u.__si_disk.__sid_mountpoint
#define si_bsize_phys	__si_u.__si_disk.__sid_bsize_phys
#define si_bsize_best	__si_u.__si_disk.__sid_bsize_best

/*
 * Exported shorthand
 */
#define v_hashchain v_rdev->si_hlist
#define v_specmountpoint v_rdev->si_mountpoint

/*
 * Special device management
 */
#define	SPECHSZ	64
#define	SPECHASH(rdev)	(((unsigned)(minor(rdev)))%SPECHSZ)

/*
 * Definitions of device driver entry switches
 */

struct buf;
struct proc;
struct uio;
struct knote;

/*
 * Note: d_thread_t is provided as a transition aid for those drivers
 * that treat struct proc/struct thread as an opaque data type and
 * exist in substantially the same form in both 4.x and 5.x.  Writers
 * of drivers that dips into the d_thread_t structure should use
 * struct thread or struct proc as appropriate for the version of the
 * OS they are using.  It is provided in lieu of each device driver
 * inventing its own way of doing this.  While it does violate style(9)
 * in a number of ways, this violation is deemed to be less
 * important than the benefits that a uniform API between releases
 * gives.
 *
 * Users of struct thread/struct proc that aren't device drivers should
 * not use d_thread_t.
 */

struct thread;
struct lwkt_port;

typedef struct thread d_thread_t;
typedef int d_open_t (dev_t dev, int oflags, int devtype, d_thread_t *td);
typedef int d_close_t (dev_t dev, int fflag, int devtype, d_thread_t *td);
typedef void d_strategy_t (struct buf *bp);
typedef int d_ioctl_t (dev_t dev, u_long cmd, caddr_t data,
			   int fflag, d_thread_t *td);
typedef int d_dump_t (dev_t dev);
typedef int d_psize_t (dev_t dev);

typedef int d_read_t (dev_t dev, struct uio *uio, int ioflag);
typedef int d_write_t (dev_t dev, struct uio *uio, int ioflag);
typedef int d_poll_t (dev_t dev, int events, d_thread_t *td);
typedef int d_kqfilter_t (dev_t dev, struct knote *kn);
typedef int d_mmap_t (dev_t dev, vm_offset_t offset, int nprot);

typedef int l_open_t (dev_t dev, struct tty *tp);
typedef int l_close_t (struct tty *tp, int flag);
typedef int l_read_t (struct tty *tp, struct uio *uio, int flag);
typedef int l_write_t (struct tty *tp, struct uio *uio, int flag);
typedef int l_ioctl_t (struct tty *tp, u_long cmd, caddr_t data,
			   int flag, d_thread_t *td);
typedef int l_rint_t (int c, struct tty *tp);
typedef int l_start_t (struct tty *tp);
typedef int l_modem_t (struct tty *tp, int flag);

/*
 * XXX: The dummy argument can be used to do what strategy1() never
 * did anywhere:  Create a per device flag to lock the device during
 * label/slice surgery, all calls with a dummy == 0 gets stalled on
 * a queue somewhere, whereas dummy == 1 are let through.  Once out
 * of surgery, reset the flag and restart all the stuff on the stall
 * queue.
 */
#define BUF_STRATEGY(bp, dummy) dev_dstrategy((bp)->b_dev, bp)
/*
 * Types for d_flags.
 */
#define	D_TAPE	0x0001
#define	D_DISK	0x0002
#define	D_TTY	0x0004
#define	D_MEM	0x0008

#define	D_TYPEMASK	0xffff

/*
 * Flags for d_flags.
 */
#define	D_MEMDISK	0x00010000	/* memory type disk */
#define	D_NAGGED	0x00020000	/* nagged about missing make_dev() */
#define	D_CANFREE	0x00040000	/* can free blocks */
#define	D_TRACKCLOSE	0x00080000	/* track all closes */
#define D_MASTER	0x00100000	/* used by pty/tty code */
#define	D_KQFILTER	0x00200000	/* has kqfilter entry */

/*
 * Character device switch table
 */
struct cdevsw {
	const char	*d_name;	/* base device name, e.g. 'vn' */
	int		d_maj;		/* major (char) device number */
	u_int		d_flags;	/* D_ flags */
	struct lwkt_port *d_port;	/* port (template only) */
	u_int		d_autoq;	/* thread safe (old style) vec mask */

	/*
	 * Old style vectors are used only if d_port is NULL when the cdevsw
	 * is added to the system.   They have been renamed to prevent misuse.
	 */
	d_open_t	*old_open;
	d_close_t	*old_close;
	d_read_t	*old_read;
	d_write_t	*old_write;
	d_ioctl_t	*old_ioctl;
	d_poll_t	*old_poll;
	d_mmap_t	*old_mmap;
	d_strategy_t	*old_strategy;
	d_dump_t	*old_dump;
	d_psize_t	*old_psize;
	d_kqfilter_t	*old_kqfilter;
};

/*
 * Line discipline switch table
 */
struct linesw {
	l_open_t	*l_open;
	l_close_t	*l_close;
	l_read_t	*l_read;
	l_write_t	*l_write;
	l_ioctl_t	*l_ioctl;
	l_rint_t	*l_rint;
	l_start_t	*l_start;
	l_modem_t	*l_modem;
	u_char		l_hotchar;
};

#ifdef _KERNEL
extern struct linesw linesw[];
extern int nlinesw;

int ldisc_register (int , struct linesw *);
void ldisc_deregister (int);
#define LDISC_LOAD 	-1		/* Loadable line discipline */
#endif

/*
 * Swap device table
 */
struct swdevt {
	udev_t	sw_dev;			/* For quasibogus swapdev reporting */
	int	sw_flags;
	int	sw_nblks;
	struct	vnode *sw_vp;
	dev_t	sw_device;
};
#define	SW_FREED	0x01
#define	SW_SEQUENTIAL	0x02
#define	sw_freed	sw_flags	/* XXX compat */

#ifdef _KERNEL
d_open_t	noopen;
d_close_t	noclose;
d_read_t	noread;
d_write_t	nowrite;
d_ioctl_t	noioctl;
d_mmap_t	nommap;
d_kqfilter_t	nokqfilter;
#define	nostrategy	((d_strategy_t *)NULL)
#define	nopoll	seltrue

d_dump_t	nodump;

#define NUMCDEVSW 256

/*
 * nopsize is little used, so not worth having dummy functions for.
 */
#define	nopsize	((d_psize_t *)NULL)

d_open_t	nullopen;
d_close_t	nullclose;

l_ioctl_t	l_nullioctl;
l_read_t	l_noread;
l_write_t	l_nowrite;

struct module;

struct devsw_module_data {
	int	(*chainevh)(struct module *, int, void *); /* next handler */
	void	*chainarg;	/* arg for next event handler */
	/* Do not initialize fields hereafter */
};

#define DEV_MODULE(name, evh, arg)					\
static moduledata_t name##_mod = {					\
    #name,								\
    evh,								\
    arg									\
};									\
DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE)

void	compile_devsw(struct cdevsw *devsw);
int	cdevsw_add (struct cdevsw *new);
struct lwkt_port *cdevsw_add_override (struct cdevsw *new, struct lwkt_port *port);
struct lwkt_port *cdevsw_dev_override(dev_t dev, struct lwkt_port *port);

int	cdevsw_remove (struct cdevsw *old);
int	count_dev (dev_t dev);
void	destroy_dev (dev_t dev);
struct cdevsw *devsw (dev_t dev);
const char *devtoname (dev_t dev);
void	freedev (dev_t dev);
int	iszerodev (dev_t dev);
dev_t	make_dev (struct cdevsw *devsw, int minor, uid_t uid, gid_t gid, int perms, const char *fmt, ...) __printflike(6, 7);
int	lminor (dev_t dev);
void	setconf (void);
dev_t	getdiskbyname(char *name);

/*
 * XXX: This included for when DEVFS resurfaces 
 */

#define		UID_ROOT	0
#define		UID_BIN		3
#define		UID_UUCP	66

#define		GID_WHEEL	0
#define		GID_KMEM	2
#define		GID_OPERATOR	5
#define		GID_BIN		7
#define		GID_GAMES	13
#define		GID_DIALER	68

#endif /* _KERNEL */

#endif /* !_SYS_CONF_H_ */
