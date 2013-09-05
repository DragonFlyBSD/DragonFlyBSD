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
 * $DragonFly: src/sys/sys/conf.h,v 1.18 2007/05/09 00:53:35 dillon Exp $
 */

#ifndef _SYS_CONF_H_
#define	_SYS_CONF_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif
#ifndef _SYS_BIOTRACK_H_
#include <sys/biotrack.h>
#endif
#ifndef _SYS_SYSREF_H_
#include <sys/sysref.h>
#endif
#ifndef _SYS_EVENT_H_
#include <sys/event.h>
#endif
#include <libprop/proplib.h>

#define SPECNAMELEN	63

struct tty;
struct disk;
struct vnode;
struct dev_ops;
struct vm_object;

struct cdev {
	u_int		si_flags;
	__uint64_t	si_inode;
	uid_t		si_uid;
	gid_t		si_gid;
	int		si_perms;
	TAILQ_ENTRY(cdev) link;
	int		si_uminor;
	int		si_umajor;
	struct cdev	*si_parent;
	LIST_ENTRY(cdev)	si_hash;
	SLIST_HEAD(, vnode) si_hlist;
	char		si_name[SPECNAMELEN + 1];
	void		*si_drv1;
	void		*si_drv2;
	struct dev_ops	*si_ops;	/* device operations vector */
	struct dev_ops	*si_bops;	/* backing devops vector */
	int		si_iosize_max;	/* maximum I/O size (for physio &al) */
	struct sysref	si_sysref;
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
	struct bio_track si_track_read;
	struct bio_track si_track_write;
	time_t		si_lastread;	/* time_uptime */
	time_t		si_lastwrite;	/* time_uptime */
	struct vm_object *si_object;	/* vm_pager support */
	prop_dictionary_t si_dict;
	struct kqinfo	si_kqinfo;	/* degenerate delegated knotes */
};

#define SI_UNUSED01	0x0001
#define SI_HASHED	0x0002	/* in (maj,min) hash table */
#define SI_OVERRIDE	0x0004	/* override uid, gid, and perms */
#define SI_INTERCEPTED	0x0008	/* device ops was intercepted */
#define SI_DEVFS_LINKED	0x0010
#define	SI_REPROBE_TEST	0x0020
#define SI_CANFREE	0x0040	/* basically just a propagated D_CANFREE */

#define si_tty		__si_u.__si_tty.__sit_tty
#define si_disk		__si_u.__si_disk.__sid_disk
#define si_mountpoint	__si_u.__si_disk.__sid_mountpoint
#define si_bsize_phys	__si_u.__si_disk.__sid_bsize_phys
#define si_bsize_best	__si_u.__si_disk.__sid_bsize_best

#define CDEVSW_ALL_MINORS	0	/* mask of 0 always matches 0 */

/*
 * Special device management
 */
#define	SPECHSZ	64
#define	SPECHASH(rdev)	(((unsigned)(minor(rdev)))%SPECHSZ)

/*
 * Definitions of device driver entry switches
 */

struct buf;
struct bio;
struct proc;
struct uio;
struct knote;
struct ucred;

struct thread;

typedef int l_open_t (struct cdev *dev, struct tty *tp);
typedef int l_close_t (struct tty *tp, int flag);
typedef int l_read_t (struct tty *tp, struct uio *uio, int flag);
typedef int l_write_t (struct tty *tp, struct uio *uio, int flag);
typedef int l_ioctl_t (struct tty *tp, u_long cmd, caddr_t data, int flag,
			struct ucred *cred);
typedef int l_rint_t (int c, struct tty *tp);
typedef int l_start_t (struct tty *tp);
typedef int l_modem_t (struct tty *tp, int flag);

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
	int	sw_nblks;		/* Number of swap blocks on device */
	int	sw_nused;		/* swap blocks used on device */
	struct	vnode *sw_vp;
	struct cdev *sw_device;
};

#define	SW_FREED	0x01
#define	SW_SEQUENTIAL	0x02
#define SW_CLOSING	0x04
#define	sw_freed	sw_flags	/* XXX compat */

#ifdef _KERNEL

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

int	count_dev (cdev_t dev);
void	destroy_dev (cdev_t dev);
void	release_dev (cdev_t dev);
cdev_t	get_dev (int x, int y);
cdev_t	reference_dev (cdev_t dev);
struct dev_ops *devsw (cdev_t dev);
const char *devtoname (cdev_t dev);
void	freedev (cdev_t dev);
int	iszerodev (cdev_t dev);

int	lminor (cdev_t dev);
void	setconf (void);
cdev_t	kgetdiskbyname(const char *name);
int	dev_is_good(cdev_t dev);

/*
 * XXX: This included for when DEVFS resurfaces
 */

#define		UID_ROOT	0
#define		UID_BIN		3
#define		UID_UUCP	66

#define		GID_WHEEL	0
#define		GID_KMEM	2
#define		GID_TTY		4
#define		GID_OPERATOR	5
#define		GID_BIN		7
#define		GID_GAMES	13
#define		GID_DIALER	68

#endif /* _KERNEL */
#endif /* _KERNEL || _KERNEL_STRUCTURES */

#endif /* !_SYS_CONF_H_ */
