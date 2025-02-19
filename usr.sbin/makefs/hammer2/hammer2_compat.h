/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2011-2022 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 */

#ifndef _HAMMER2_HAMMER2_COMPAT_H
#define _HAMMER2_HAMMER2_COMPAT_H

#include <sys/statvfs.h>
#include <sys/spinlock.h>
#include <sys/uio.h> /* struct iovec */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h> /* ecalloc */
#include <assert.h>
#include <err.h>

#include "ffs/buf.h"

#define INVARIANTS

#define MALLOC_DECLARE(type)				struct __hack
#define MALLOC_DEFINE(type, shortdesc, longdesc)	struct __hack

#define SYSCTL_NODE(parent, nbr, name, access, handler, descr)	struct __hack
#define SYSCTL_INT(parent, nbr, name, access, ptr, val, descr)	struct __hack
#define SYSCTL_LONG(parent, nbr, name, access, ptr, val, descr)	struct __hack

#define VFS_SET(vfsops, fsname, flags)	struct __hack

#define MODULE_VERSION(module, version)	struct __hack

#define VOP_FSYNC(vp, waitfor, flags)	(0)

#define kprintf(s, ...)		printf(s, ## __VA_ARGS__)
#define krateprintf(r, X, ...)	kprintf(X, ## __VA_ARGS__)
#define ksnprintf(s, n, ...)	snprintf(s, n, ## __VA_ARGS__)
#define kstrdup(str, type)	strdup(str)

#define kmalloc_create(typep, descr)	do{}while(0)
#define kmalloc_destroy(typep)		do{}while(0)
#define kmalloc(size, type, flags)	ecalloc(1, size)
#define krealloc(addr, size, type, flags)	realloc(addr, size)
#define kfree(addr, type)		free(addr)

#define kmalloc_create_obj(typep, descr, objsize)	do{}while(0)
#define kmalloc_destroy_obj(type)			do{}while(0)
#define kmalloc_obj(size, type, flags)			ecalloc(1, size)
#define kfree_obj(addr, type)				free(addr)

#define kmalloc_raise_limit(typep, bytes)		do{}while(0)

#define KASSERT(exp, msg)	do { if (!(exp)) panic msg; } while(0)
//#define KASSERT(exp, msg)	assert(exp)
#define KKASSERT(exp)		assert(exp)

#define	__debugvar

#define panic(s, ...)		errx(1, s, ## __VA_ARGS__)

#define ERESTART	(-1)

#define LK_SHARED	0x00000001
#define LK_EXCLUSIVE	0x00000002
#define LK_RELEASE	0x00000006
#define LK_NOWAIT	0x00000010
#define LK_RETRY	0x00020000
#define LK_PCATCH	0x04000000

#define MTX_EXCLUSIVE	0x80000000
#define MTX_MASK	0x0FFFFFFF

#define IO_APPEND	0x0002
#define IO_SYNC		0x0004
#define IO_ASYNC	0x0080
#define IO_DIRECT	0x0100
#define IO_RECURSE	0x0200

#define IO_SEQMAX	0x7F
#define IO_SEQSHIFT	16

#define VROOT		0x00000001
#define VKVABIO		0x00000040
#define VLASTWRITETS	0x00080000

#define VNOVAL		(-1)

#define FORCECLOSE	0x0002

#define GETBLK_BHEAVY	0x0002
#define GETBLK_KVABIO	0x0010

#define NOOFFSET	(-1LL)

#define KNOTE(list, hint)	{}

#define NOTE_DELETE	0x0001
#define NOTE_WRITE	0x0002
#define NOTE_EXTEND	0x0004
#define NOTE_LINK	0x0010

extern int hz;
extern int ticks;
extern int64_t vnode_count;

struct thread {
	void *td_proc;
};

typedef struct thread *thread_t;
extern struct thread *curthread;

struct lwkt_token {
};

typedef struct lwkt_token *lwkt_token_t;

struct mount {
	int mnt_flag;
	int mnt_kern_flag;
	struct statfs mnt_stat;
	struct statvfs mnt_vstat;
	qaddr_t mnt_data;
	unsigned int mnt_iosize_max;
};

struct bio {
	struct m_buf *bio_buf;
	off_t bio_offset;
};

struct bio_track {
};

struct namecache {
	u_char nc_nlen;
	char *nc_name;
};

struct nchandle {
	struct namecache *ncp;
};

struct vop_generic_args {
	int a_reserved[4];
};

struct vop_open_args {
	struct m_vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct file **a_fpp;
};

struct vop_close_args {
	struct m_vnode *a_vp;
	int a_fflag;
	struct file *a_fp;
};

struct vop_access_args {
	struct m_vnode *a_vp;
	int a_mode;
	int a_flags;
	struct ucred *a_cred;
};

struct vop_getattr_args {
	struct m_vnode *a_vp;
	struct vattr *a_vap;
};

struct vop_getattr_lite_args {
	struct m_vnode *a_vp;
	struct vattr_lite *a_lvap;
};

struct vop_setattr_args {
	struct m_vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
};

struct vop_read_args {
	struct m_vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};

struct vop_write_args {
	struct m_vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};

struct vop_ioctl_args {
	struct m_vnode *a_vp;
	u_long a_command;
	caddr_t a_data;
	int a_fflag;
	struct ucred *a_cred;
	struct sysmsg *a_sysmsg;
};

struct vop_kqfilter_args {
	struct m_vnode *a_vp;
	struct knote *a_kn;
};

struct vop_fsync_args {
	struct m_vnode *a_vp;
	int a_waitfor;
	int a_flags;
};

struct vop_readdir_args {
	struct m_vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
	int *a_eofflag;
	int *a_ncookies;
	off_t **a_cookies;
	int *a_ndirent; /* makefs */
};

struct vop_readlink_args {
	struct m_vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
};

struct vop_inactive_args {
	struct m_vnode *a_vp;
};

struct vop_reclaim_args {
	struct m_vnode *a_vp;
};

struct vop_bmap_args {
	struct m_vnode *a_vp;
	off_t a_loffset;
	off_t *a_doffsetp;
	int *a_runp;
	int *a_runb;
	buf_cmd_t a_cmd;
};

struct vop_strategy_args {
	struct m_vnode *a_vp;
	struct bio *a_bio;
};

struct vop_advlock_args {
	struct m_vnode *a_vp;
	caddr_t a_id;
	int a_op;
	struct flock *a_fl;
	int a_flags;
};

struct vop_getpages_args {
	struct m_vnode *a_vp;
	int a_count;
	int a_reqpage;
	//vm_ooffset_t a_offset;
	int a_seqaccess;
};

struct vop_putpages_args {
	struct m_vnode *a_vp;
	int a_count;
	int a_flags;
	int *a_rtvals;
	//vm_ooffset_t a_offset;
};

struct vop_mountctl_args {
	int a_op;
	struct file *a_fp;
	const void *a_ctl;
	int a_ctllen;
	void *a_buf;
	int a_buflen;
	int *a_res;
	struct m_vnode *a_vp;
};

struct vop_markatime_args {
	int a_op;
	struct m_vnode *a_vp;
	struct ucred *a_cred;
};

struct vop_nresolve_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct ucred *a_cred;
	struct m_vnode **a_vpp; /* makefs */
};

struct vop_nlookupdotdot_args {
	struct m_vnode *a_dvp;
	struct m_vnode **a_vpp;
	struct ucred *a_cred;
	char **a_fakename;
};

struct vop_ncreate_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct m_vnode **a_vpp;
	struct ucred *a_cred;
	struct vattr *a_vap;
};

struct vop_nmkdir_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct m_vnode **a_vpp;
	struct ucred *a_cred;
	struct vattr *a_vap;
};

struct vop_nmknod_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct m_vnode **a_vpp;
	struct ucred *a_cred;
	struct vattr *a_vap;
};

struct vop_nlink_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct m_vnode *a_vp;
	struct ucred *a_cred;
};

struct vop_nsymlink_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct m_vnode **a_vpp;
	struct ucred *a_cred;
	struct vattr *a_vap;
	char *a_target;
};

struct vop_nremove_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct ucred *a_cred;
};

struct vop_nrmdir_args {
	struct nchandle *a_nch;
	struct m_vnode *a_dvp;
	struct ucred *a_cred;
};

struct vop_nrename_args {
	struct nchandle *a_fnch;
	struct nchandle *a_tnch;
	struct m_vnode *a_fdvp;
	struct m_vnode *a_tdvp;
	struct ucred *a_cred;
};

#define vop_defaultop	NULL
#define vop_stdgetpages	NULL
#define vop_stdputpages	NULL
#define vop_stdnoread	NULL
#define vop_stdnowrite	NULL
#define fifo_vnoperate	NULL

struct vop_ops {
	int (*vop_default)(struct vop_generic_args *);
	int (*vop_open)(struct vop_open_args *);
	int (*vop_close)(struct vop_close_args *);
	int (*vop_access)(struct vop_access_args *);
	int (*vop_getattr)(struct vop_getattr_args *);
	int (*vop_getattr_lite)(struct vop_getattr_lite_args *);
	int (*vop_setattr)(struct vop_setattr_args *);
	int (*vop_read)(struct vop_read_args *);
	int (*vop_write)(struct vop_write_args *);
	int (*vop_ioctl)(struct vop_ioctl_args *);
	int (*vop_kqfilter)(struct vop_kqfilter_args *);
	int (*vop_fsync)(struct vop_fsync_args *);
	int (*vop_readdir)(struct vop_readdir_args *);
	int (*vop_readlink)(struct vop_readlink_args *);
	int (*vop_inactive)(struct vop_inactive_args *);
	int (*vop_reclaim)(struct vop_reclaim_args *);
	int (*vop_bmap)(struct vop_bmap_args *);
	int (*vop_strategy)(struct vop_strategy_args *);
	int (*vop_advlock)(struct vop_advlock_args *);
	int (*vop_getpages)(struct vop_getpages_args *);
	int (*vop_putpages)(struct vop_putpages_args *);
	int (*vop_mountctl)(struct vop_mountctl_args *);
	int (*vop_markatime)(struct vop_markatime_args *);
	int (*vop_nresolve)(struct vop_nresolve_args *);
	int (*vop_nlookupdotdot)(struct vop_nlookupdotdot_args *);
	int (*vop_ncreate)(struct vop_ncreate_args *);
	int (*vop_nmkdir)(struct vop_nmkdir_args *);
	int (*vop_nmknod)(struct vop_nmknod_args *);
	int (*vop_nlink)(struct vop_nlink_args *);
	int (*vop_nsymlink)(struct vop_nsymlink_args *);
	int (*vop_nremove)(struct vop_nremove_args *);
	int (*vop_nrmdir)(struct vop_nrmdir_args *);
	int (*vop_nrename)(struct vop_nrename_args *);
};

enum uio_seg {
	UIO_USERSPACE,
	UIO_SYSSPACE,
	UIO_NOCOPY,
};

enum uio_rw {
	UIO_READ,
	UIO_WRITE
};

struct uio {
	struct iovec *uio_iov;
	int uio_iovcnt;
	off_t uio_offset;
	size_t uio_resid;
	enum uio_seg uio_segflg;
	enum uio_rw uio_rw;
	struct thread *uio_td;
};

/*
 * Since makefs(8) is a single thread program, there should never be any
 * lock contention.  Therefore, lock(9)/mutex(9)/spinlock(9) emulation always
 * succeed.  Similarly, tsleep(9) should never be called.
 */
struct lock {
	int reserved;
};

typedef struct {
	unsigned int mtx_lock;
} mtx_t;

typedef mtx_t hammer2_mtx_t;

typedef u_int mtx_state_t;
typedef mtx_state_t hammer2_mtx_state_t;

typedef struct spinlock hammer2_spin_t;

static __inline
void
cpu_pause(void)
{
}

static __inline
void
cpu_mfence(void)
{
}

static __inline
void
cpu_lfence(void)
{
}

static __inline
void
cpu_sfence(void)
{
}

static __inline
void
cpu_ccfence(void)
{
}

static __inline
void
trigger_syncer(struct mount *mp)
{
}

static __inline
void
trigger_syncer_start(struct mount *mp)
{
}

static __inline
void
trigger_syncer_stop(struct mount *mp)
{
}

static __inline
int
vfs_mountedon(struct m_vnode *vp)
{
	return (0);
}

static __inline
uid_t
vop_helper_create_uid(struct mount *mp, mode_t dmode, uid_t duid,
			struct ucred *cred, mode_t *modep)
{
	return (getuid());
}

static __inline
int
vinitvmio(struct m_vnode *vp, off_t filesize, int blksize, int boff)
{
	return (0);
}

static __inline
int
getnewvnode(enum vtagtype tag, struct mount *mp, struct m_vnode **vpp,
		int lktimeout, int lkflags)
{
	struct m_vnode *vp;

	vp = ecalloc(1, sizeof(*vp));
	vp->v_logical = 1;
	vp->v_malloced = 1;
	*vpp = vp;

	vnode_count++;

	return (0);
}

/* not freesomevnodes() */
static __inline
void
freevnode(struct m_vnode *vp)
{
	assert(vp->v_malloced);
	free(vp);

	vnode_count--;
}

static __inline
int
vn_lock(struct m_vnode *vp, int flags)
{
	return (0);
}

static __inline
void
vn_unlock(struct m_vnode *vp)
{
}

static __inline
int
vget(struct m_vnode *vp, int flags)
{
	return (0);
}

static __inline
void
vput(struct m_vnode *vp)
{
}

static __inline
void
vrele(struct m_vnode *vp)
{
}

static __inline
void
vhold(struct m_vnode *vp)
{
}

static __inline
void
vdrop(struct m_vnode *vp)
{
}

static __inline
void
vx_put(struct m_vnode *vp)
{
}

static __inline
void
vx_downgrade(struct m_vnode *vp)
{
}

static __inline
void
vfinalize(struct m_vnode *vp)
{
}

static __inline
void
vsetflags(struct m_vnode *vp, int flags)
{
}

static __inline
void
vclrflags(struct m_vnode *vp, int flags)
{
}

static __inline
void
vsetisdirty(struct m_vnode *vp)
{
}

static __inline
void
vclrisdirty(struct m_vnode *vp)
{
}

static __inline
int
vfsync(struct m_vnode *vp, int waitfor, int passes,
	int (*checkdef)(struct m_buf *),
	int (*waitoutput)(struct m_vnode *, struct thread *))
{
	return (0);
}

static __inline
int
nvtruncbuf(struct m_vnode *vp, off_t length, int blksize, int boff, int flags)
{
	return (0);
}

static __inline
int
nvextendbuf(struct m_vnode *vp, off_t olength, off_t nlength, int oblksize,
		int nblksize, int oboff, int nboff, int flags)
{
	return (0);
}

static __inline
void
addaliasu(struct m_vnode *vp, int x, int y)
{
}

static __inline
void
bheavy(struct m_buf *bp)
{
}

static __inline
void
bkvasync(struct m_buf *bp)
{
}

static __inline
void
bwillwrite(int bytes)
{
}

static __inline
void
BUF_KERNPROC(struct m_buf *bp)
{
}

static __inline
int
bio_track_wait(struct bio_track *track, int slp_flags, int slp_timo)
{
	return (0);
}

static __inline
void
cache_setvp(struct nchandle *nch, struct m_vnode *vp)
{
}

static __inline
void
cache_setunresolved(struct nchandle *nch)
{
}

static __inline
void
lockinit(struct lock *lkp, const char *wmesg, int timo, int flags)
{
}

static __inline
int
lockmgr(struct lock *lkp, uint32_t flags)
{
	return (0);
}

static __inline
int
hammer2_mtx_ex(hammer2_mtx_t *mtx)
{
	mtx->mtx_lock |= MTX_EXCLUSIVE;
	mtx->mtx_lock++;
	return (0);
}

static __inline
int
hammer2_mtx_ex_try(hammer2_mtx_t *mtx)
{
	mtx->mtx_lock |= MTX_EXCLUSIVE;
	mtx->mtx_lock++;
	return (0);
}

static __inline
int
hammer2_mtx_sh(hammer2_mtx_t *mtx)
{
	mtx->mtx_lock |= MTX_EXCLUSIVE;
	mtx->mtx_lock++;
	return (0);
}

static __inline
void
hammer2_mtx_sh_again(hammer2_mtx_t *mtx)
{
	mtx->mtx_lock |= MTX_EXCLUSIVE;
	mtx->mtx_lock++;
}

static __inline
int
hammer2_mtx_sh_try(hammer2_mtx_t *mtx)
{
	mtx->mtx_lock |= MTX_EXCLUSIVE;
	mtx->mtx_lock++;
	return (0);
}

static __inline
void
hammer2_mtx_unlock(hammer2_mtx_t *mtx)
{
	mtx->mtx_lock &= ~MTX_EXCLUSIVE;
	mtx->mtx_lock--;
}

static __inline
int
hammer2_mtx_upgrade_try(hammer2_mtx_t *mtx)
{
	return (0);
}

static __inline
int
hammer2_mtx_downgrade(hammer2_mtx_t *mtx)
{
	return (0);
}

static __inline
int
hammer2_mtx_owned(hammer2_mtx_t *mtx)
{
	return (1); /* XXX for asserts */
}

static __inline
void
hammer2_mtx_init(hammer2_mtx_t *mtx, const char *ident)
{
	mtx->mtx_lock = 0;
}

static __inline
hammer2_mtx_state_t
hammer2_mtx_temp_release(hammer2_mtx_t *mtx)
{
	return (0);
}

static __inline
void
hammer2_mtx_temp_restore(hammer2_mtx_t *mtx, hammer2_mtx_state_t state)
{
}

static __inline
int
hammer2_mtx_refs(hammer2_mtx_t *mtx)
{
	return (mtx->mtx_lock & MTX_MASK);
}

static __inline
void
hammer2_spin_init(hammer2_spin_t *mtx, const char *ident)
{
	mtx->lock = 0;
}

static __inline
void
hammer2_spin_sh(hammer2_spin_t *mtx)
{
	mtx->lock++;
}

static __inline
void
hammer2_spin_ex(hammer2_spin_t *mtx)
{
	mtx->lock++;
}

static __inline
void
hammer2_spin_unsh(hammer2_spin_t *mtx)
{
	mtx->lock--;
}

static __inline
void
hammer2_spin_unex(hammer2_spin_t *mtx)
{
	mtx->lock--;
}

static __inline
void
lwkt_gettoken(lwkt_token_t tok)
{
}

static __inline
void
lwkt_reltoken(lwkt_token_t tok)
{
}

static __inline
void
lwkt_yield(void)
{
}

static __inline
int
tsleep(const volatile void *ident, int flags, const char *wmesg, int timo)
{
	assert(0);
}

static __inline
void
tsleep_interlock(const volatile void *ident, int flags)
{
	assert(0);
}

static __inline
void
wakeup(const volatile void *ident)
{
	assert(0);
}

static __inline
void
print_backtrace(int count)
{
}

static __inline
void
Debugger(const char *msg)
{
	panic("%s: %s", __func__, msg);
}

#endif /* _HAMMER2_HAMMER2_COMPAT_H */
