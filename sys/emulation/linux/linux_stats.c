/*-
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/linux/linux_stats.c,v 1.22.2.3 2001/11/05 19:08:23 marcel Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/nlookup.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/device.h>
#include <sys/kern_syscall.h>
#include <emulation/43bsd/stat.h>

#include <sys/file2.h>
#include <sys/mplock2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_util.h"

static int
newstat_copyout(struct stat *buf, void *ubuf)
{
	struct l_newstat tbuf;
	int error;

	bzero(&tbuf, sizeof(tbuf));
	tbuf.st_dev = uminor(buf->st_dev) | (umajor(buf->st_dev) << 8);
	tbuf.st_ino = INO64TO32(buf->st_ino);
	tbuf.st_mode = buf->st_mode;
	tbuf.st_nlink = buf->st_nlink;
	tbuf.st_uid = buf->st_uid;
	tbuf.st_gid = buf->st_gid;
	tbuf.st_rdev = buf->st_rdev;
	tbuf.st_size = buf->st_size;
	tbuf.st_atime = buf->st_atime;
	tbuf.st_mtime = buf->st_mtime;
	tbuf.st_ctime = buf->st_ctime;
	tbuf.st_blksize = buf->st_blksize;
	tbuf.st_blocks = buf->st_blocks;

	error = copyout(&tbuf, ubuf, sizeof(tbuf));
	return (error);
}

static int
ostat_copyout(struct stat *st, struct ostat *uaddr)
{
	struct ostat ost;
	int error;

	ost.st_dev = st->st_dev;
	ost.st_ino = st->st_ino;
	ost.st_mode = st->st_mode;
	ost.st_nlink = st->st_nlink;
	ost.st_uid = st->st_uid;
	ost.st_gid = st->st_gid;
	ost.st_rdev = st->st_rdev;
	if (st->st_size < (quad_t)1 << 32)
		ost.st_size = st->st_size;
	else
		ost.st_size = -2;
	ost.st_atime = st->st_atime;
	ost.st_mtime = st->st_mtime;
	ost.st_ctime = st->st_ctime;
	ost.st_blksize = st->st_blksize;
	ost.st_blocks = st->st_blocks;
	ost.st_flags = st->st_flags;
	ost.st_gen = st->st_gen;

	error = copyout(&ost, uaddr, sizeof(ost));
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_newstat(struct linux_newstat_args *args)
{
	struct stat buf;
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(newstat))
		kprintf(ARGS(newstat, "%s, *"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_stat(&nd, &buf);
		if (error == 0)
			error = newstat_copyout(&buf, args->buf);
		nlookup_done(&nd);
	}
	rel_mplock();
	linux_free_path(&path);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_newlstat(struct linux_newlstat_args *args)
{
	struct stat sb;
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(newlstat))
		kprintf(ARGS(newlstat, "%s, *"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0) {
		error = kern_stat(&nd, &sb);
		if (error == 0)
			error = newstat_copyout(&sb, args->buf);
		nlookup_done(&nd);
	}
	rel_mplock();
	linux_free_path(&path);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_newfstat(struct linux_newfstat_args *args)
{
	struct stat buf;
	int error;

#ifdef DEBUG
	if (ldebug(newfstat))
		kprintf(ARGS(newfstat, "%d, *"), args->fd);
#endif
	get_mplock();
	error = kern_fstat(args->fd, &buf);
	rel_mplock();

	if (error == 0)
		error = newstat_copyout(&buf, args->buf);
	return (error);
}

/* XXX - All fields of type l_int are defined as l_long on i386 */
struct l_statfs {
	l_int		f_type;
	l_int		f_bsize;
	l_int		f_blocks;
	l_int		f_bfree;
	l_int		f_bavail;
	l_int		f_files;
	l_int		f_ffree;
	l_fsid_t	f_fsid;
	l_int		f_namelen;
	l_int		f_spare[6];
};

#define	LINUX_CODA_SUPER_MAGIC	0x73757245L
#define	LINUX_EXT2_SUPER_MAGIC	0xEF53L
#define	LINUX_HPFS_SUPER_MAGIC	0xf995e849L
#define	LINUX_ISOFS_SUPER_MAGIC	0x9660L
#define	LINUX_MSDOS_SUPER_MAGIC	0x4d44L
#define	LINUX_NCP_SUPER_MAGIC	0x564cL
#define	LINUX_NFS_SUPER_MAGIC	0x6969L
#define	LINUX_NTFS_SUPER_MAGIC	0x5346544EL
#define	LINUX_PROC_SUPER_MAGIC	0x9fa0L
#define	LINUX_UFS_SUPER_MAGIC	0x00011954L	/* XXX - UFS_MAGIC in Linux */

static long
bsd_to_linux_ftype(const char *fstypename)
{
	int i;
	static struct {const char *bsd_name; long linux_type;} b2l_tbl[] = {
		{"ufs",     LINUX_UFS_SUPER_MAGIC},
		{"cd9660",  LINUX_ISOFS_SUPER_MAGIC},
		{"nfs",     LINUX_NFS_SUPER_MAGIC},
		{"ext2fs",  LINUX_EXT2_SUPER_MAGIC},
		{"procfs",  LINUX_PROC_SUPER_MAGIC},
		{"msdosfs", LINUX_MSDOS_SUPER_MAGIC},
		{"ntfs",    LINUX_NTFS_SUPER_MAGIC},
		{"hpfs",    LINUX_HPFS_SUPER_MAGIC},
		{NULL,      0L}};

	for (i = 0; b2l_tbl[i].bsd_name != NULL; i++)
		if (strcmp(b2l_tbl[i].bsd_name, fstypename) == 0)
			return (b2l_tbl[i].linux_type);

	return (0L);
}

static int
statfs_copyout(struct statfs *statfs, struct l_statfs_buf *buf, l_int namelen)
{
	struct l_statfs linux_statfs;
	int error;

	linux_statfs.f_type = bsd_to_linux_ftype(statfs->f_fstypename);
	linux_statfs.f_bsize = statfs->f_bsize;
	linux_statfs.f_blocks = statfs->f_blocks;
	linux_statfs.f_bfree = statfs->f_bfree;
	linux_statfs.f_bavail = statfs->f_bavail;
  	linux_statfs.f_ffree = statfs->f_ffree;
	linux_statfs.f_files = statfs->f_files;
	linux_statfs.f_fsid.val[0] = statfs->f_fsid.val[0];
	linux_statfs.f_fsid.val[1] = statfs->f_fsid.val[1];
	linux_statfs.f_namelen = namelen;

	error = copyout(&linux_statfs, buf, sizeof(linux_statfs));
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_statfs(struct linux_statfs_args *args)
{
	struct statfs statfs;
	struct nlookupdata nd;
	char *path;
	int error, namelen;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(statfs))
		kprintf(ARGS(statfs, "%s, *"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_statfs(&nd, &statfs);
	if (error == 0) {
		if (nd.nl_nch.ncp->nc_vp != NULL)
			error = vn_get_namelen(nd.nl_nch.ncp->nc_vp, &namelen);
		else
			error = EINVAL;
	}
	nlookup_done(&nd);
	rel_mplock();
	if (error == 0)
		error = statfs_copyout(&statfs, args->buf, (l_int)namelen);
	linux_free_path(&path);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_fstatfs(struct linux_fstatfs_args *args)
{
	struct proc *p = curthread->td_proc;
	struct file *fp;
	struct statfs statfs;
	int error, namelen;

#ifdef DEBUG
	if (ldebug(fstatfs))
		kprintf(ARGS(fstatfs, "%d, *"), args->fd);
#endif
	get_mplock();
	if ((error = kern_fstatfs(args->fd, &statfs)) != 0)
		return (error);
	if ((error = holdvnode(p->p_fd, args->fd, &fp)) != 0)
		return (error);
	error = vn_get_namelen((struct vnode *)fp->f_data, &namelen);
	rel_mplock();
	fdrop(fp);
	if (error == 0)
		error = statfs_copyout(&statfs, args->buf, (l_int)namelen);
	return (error);
}

struct l_ustat 
{
	l_daddr_t	f_tfree;
	l_ino_t		f_tinode;
	char		f_fname[6];
	char		f_fpack[6];
};

/*
 * MPALMOSTSAFE
 */
int
sys_linux_ustat(struct linux_ustat_args *args)
{
	struct thread *td = curthread;
	struct l_ustat lu;
	cdev_t dev;
	struct vnode *vp;
	struct statfs *stat;
	int error;

#ifdef DEBUG
	if (ldebug(ustat))
		kprintf(ARGS(ustat, "%d, *"), args->dev);
#endif

	/*
	 * lu.f_fname and lu.f_fpack are not used. They are always zeroed.
	 * lu.f_tinode and lu.f_tfree are set from the device's super block.
	 */
	bzero(&lu, sizeof(lu));

	/*
	 * XXX - Don't return an error if we can't find a vnode for the
	 * device. Our cdev_t is 32-bits whereas Linux only has a 16-bits
	 * cdev_t. The dev_t that is used now may as well be a truncated
	 * cdev_t returned from previous syscalls. Just return a bzeroed
	 * ustat in that case.
	 */
	get_mplock();
	dev = udev2dev(makeudev(args->dev >> 8, args->dev & 0xFF), 0);
	if (dev != NULL && vfinddev(dev, VCHR, &vp)) {
		if (vp->v_mount == NULL) {
			vrele(vp);
			error = EINVAL;
			goto done;
		}
		stat = &(vp->v_mount->mnt_stat);
		error = VFS_STATFS(vp->v_mount, stat, td->td_ucred);
		vrele(vp);
		if (error == 0) {
			lu.f_tfree = stat->f_bfree;
			lu.f_tinode = stat->f_ffree;
		}
	} else {
		error = 0;
	}
done:
	rel_mplock();
	if (error == 0)
		error = copyout(&lu, args->ubuf, sizeof(lu));
	return (error);
}

#if defined(__i386__)

static int
stat64_copyout(struct stat *buf, void *ubuf)
{
	struct l_stat64 lbuf;
	int error;

	bzero(&lbuf, sizeof(lbuf));
	lbuf.st_dev = uminor(buf->st_dev) | (umajor(buf->st_dev) << 8);
	lbuf.st_ino = INO64TO32(buf->st_ino);
	lbuf.st_mode = buf->st_mode;
	lbuf.st_nlink = buf->st_nlink;
	lbuf.st_uid = buf->st_uid;
	lbuf.st_gid = buf->st_gid;
	lbuf.st_rdev = buf->st_rdev;
	lbuf.st_size = buf->st_size;
	lbuf.st_atime = buf->st_atime;
	lbuf.st_mtime = buf->st_mtime;
	lbuf.st_ctime = buf->st_ctime;
	lbuf.st_blksize = buf->st_blksize;
	lbuf.st_blocks = buf->st_blocks;

	/*
	 * The __st_ino field makes all the difference. In the Linux kernel
	 * it is conditionally compiled based on STAT64_HAS_BROKEN_ST_INO,
	 * but without the assignment to __st_ino the runtime linker refuses
	 * to mmap(2) any shared libraries. I guess it's broken alright :-)
	 */
	lbuf.__st_ino = INO64TO32(buf->st_ino);

	error = copyout(&lbuf, ubuf, sizeof(lbuf));
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_stat64(struct linux_stat64_args *args)
{
	struct nlookupdata nd;
	struct stat buf;
	char *path;
	int error;

	error = linux_copyin_path(args->filename, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(stat64))
		kprintf(ARGS(stat64, "%s, *"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_stat(&nd, &buf);
		nlookup_done(&nd);
	}
	rel_mplock();
	if (error == 0)
		error = stat64_copyout(&buf, args->statbuf);
	linux_free_path(&path);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_lstat64(struct linux_lstat64_args *args)
{
	struct nlookupdata nd;
	struct stat sb;
	char *path;
	int error;

	error = linux_copyin_path(args->filename, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(lstat64))
		kprintf(ARGS(lstat64, "%s, *"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0) {
		error = kern_stat(&nd, &sb);
		nlookup_done(&nd);
	}
	rel_mplock();
	if (error == 0)
		error = stat64_copyout(&sb, args->statbuf);
	linux_free_path(&path);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_fstat64(struct linux_fstat64_args *args)
{
	struct stat buf;
	int error;

#ifdef DEBUG
	if (ldebug(fstat64))
		kprintf(ARGS(fstat64, "%d, *"), args->fd);
#endif
	get_mplock();
	error = kern_fstat(args->fd, &buf);
	rel_mplock();

	if (error == 0)
		error = stat64_copyout(&buf, args->statbuf);
	return (error);
}

int
sys_linux_fstatat64(struct linux_fstatat64_args *args)
{
	struct nlookupdata nd;
	struct file *fp;
	struct stat st;
	char *path;
	int error, flags, dfd;

	if (args->flag & ~LINUX_AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(fstatat64))
		kprintf(ARGS(fstatat64, "%s"), path);
#endif
	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	flags = (args->flag & LINUX_AT_SYMLINK_NOFOLLOW) ? 0 : NLC_FOLLOW;

	error = nlookup_init_at(&nd, &fp, dfd, path, UIO_SYSSPACE, flags);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0)
			error = stat64_copyout(&st, args->statbuf);
	}
	nlookup_done_at(&nd, fp);
	linux_free_path(&path);
	return (error);
}

int
sys_linux_ostat(struct linux_ostat_args *args)
{
	struct nlookupdata nd;
	struct stat buf;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(ostat))
		kprintf(ARGS(ostat, "%s, *"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_stat(&nd, &buf);
		nlookup_done(&nd);
	}
	rel_mplock();
	if (error == 0)
		error = ostat_copyout(&buf, args->statbuf);
	linux_free_path(&path);
	return (error);
}

#endif /* __i386__ */
