/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
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
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/spinlock2.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/vfscache.h>
#include <sys/vnode.h>

#include "dirfs.h"

/*
 * Allocate and setup all is needed for the dirfs node to hold the filename.
 * Note: dn_name is NULL terminated.
 */
void
dirfs_node_setname(dirfs_node_t dnp, const char *name, int len)
{
	debug_called();

	if (dnp->dn_name)
		kfree(dnp->dn_name, M_DIRFS_MISC);
	dnp->dn_name = kmalloc(len + 1, M_DIRFS_MISC, M_WAITOK | M_ZERO);
	bcopy(name, dnp->dn_name, len);
	dnp->dn_name[len] = 0;
	dnp->dn_namelen = len;
}

/*
 * Allocate enough space to hold a dirfs node structure.
 * Note: Node name and length isn't handled here.
 */
dirfs_node_t
dirfs_node_alloc(struct mount *mp)
{
        dirfs_node_t dnp;

        debug_called();

        dnp = kmalloc(sizeof(*dnp), M_DIRFS_NODE, M_WAITOK | M_ZERO);
        lockinit(&dnp->dn_lock, "dfsnode", 0, LK_CANRECURSE);

	dnp->dn_fd = DIRFS_NOFD;

        return dnp;
}

/*
 * Drops a reference to the node and. Node is freed when in the last reference.
 */
void
dirfs_node_drop(dirfs_mount_t dmp, dirfs_node_t dnp)
{
	if (dirfs_node_unref(dnp))
		dirfs_node_free(dmp, dnp);
}

/*
 * Removes the association with its parent. Before freeing up its resources
 * the node will be removed from the per-mount passive fd cache and its fd
 * will be closed, either normally or forced.
 */
int
dirfs_node_free(dirfs_mount_t dmp, dirfs_node_t dnp)
{
	struct vnode *vp;

	debug_called();

	KKASSERT(dnp != NULL);
	debug_node2(dnp);

	KKASSERT(dirfs_node_refcnt(dnp) == 0);

	vp = NODE_TO_VP(dnp);
	/*
	 * Remove the inode from the passive fds list
	 * as we are tearing down the node.
	 * Root inode will be removed on VOP_UNMOUNT()
	 */
	dirfs_mount_gettoken(dmp);

	if (dnp->dn_parent) {	/* NULL when children reaped parents */
		dirfs_node_drop(dmp, dnp->dn_parent);
		dnp->dn_parent = NULL;
	}
	dirfs_node_setpassive(dmp, dnp, 0);
	if (dnp->dn_name) {
		kfree(dnp->dn_name, M_DIRFS_MISC);
		dnp->dn_name = NULL;
	}

	/*
	 * The file descriptor should have been closed already by the
	 * previous call to dirfs_set-passive. If not, force a sync and
	 * close it.
	 */
	if (dnp->dn_fd != DIRFS_NOFD) {
		if (dnp->dn_vnode)
			VOP_FSYNC(vp, MNT_WAIT, 0);
		close(dnp->dn_fd);
		dnp->dn_fd = DIRFS_NOFD;
	}

	lockuninit(&dnp->dn_lock);
	kfree(dnp, M_DIRFS_NODE);
	dnp = NULL;

	dirfs_mount_reltoken(dmp);

	return 0;
}

/*
 * Do all the operations needed to get a resulting inode <--> host file
 * association. This or may not include opening the file, which should be
 * only needed when creating it.
 *
 * In the case vap is not NULL and openflags are specified, open the file.
 */
int
dirfs_alloc_file(dirfs_mount_t dmp, dirfs_node_t *dnpp, dirfs_node_t pdnp,
    struct namecache *ncp, struct vnode **vpp, struct vattr *vap,
    int openflags)
{
	dirfs_node_t dnp;
	dirfs_node_t pathnp;
	struct vnode *vp;
	struct mount *mp;
	char *tmp;
	char *pathfree;
	int error;

	debug_called();

	error = 0;
	vp = NULL;
	mp = DIRFS_TO_VFS(dmp);

	/* Sanity check */
	if (pdnp == NULL)
		return EINVAL;

	dnp = dirfs_node_alloc(mp);
	KKASSERT(dnp != NULL);

	dirfs_node_lock(dnp);
	dirfs_node_setname(dnp, ncp->nc_name, ncp->nc_nlen);
	dnp->dn_parent = pdnp;
	dirfs_node_ref(pdnp);   /* Children ref */
	dirfs_node_unlock(dnp);

	pathnp = dirfs_findfd(dmp, dnp, &tmp, &pathfree);

	if (openflags && vap != NULL) {
		dnp->dn_fd = openat(pathnp->dn_fd, tmp,
				    openflags, vap->va_mode);
		if (dnp->dn_fd == -1) {
			dirfs_dropfd(dmp, pathnp, pathfree);
			return errno;
		}
	}

	error = dirfs_node_stat(pathnp->dn_fd, tmp, dnp);
	if (error) {		/* XXX Handle errors */
		error = errno;
		if (vp)
			dirfs_free_vp(dmp, dnp);
		dirfs_node_free(dmp, dnp);
		dirfs_dropfd(dmp, pathnp, pathfree);
		return error;
	}

	dirfs_alloc_vp(mp, &vp, LK_CANRECURSE, dnp);
	*vpp = vp;
	*dnpp = dnp;

	dbg(5, "tmp=%s dnp=%p allocated\n", tmp, dnp);
	dirfs_dropfd(dmp, pathnp, pathfree);

	return error;
}

/*
 * Requires an already dirfs_node_t that has been already lstat(2)
 * for the type comparison
 */
void
dirfs_alloc_vp(struct mount *mp, struct vnode **vpp, int lkflags,
	       dirfs_node_t dnp)
{
	struct vnode *vp;
	dirfs_mount_t dmp = VFS_TO_DIRFS(mp);

	debug_called();

	/*
	 * Handle vnode reclaim/alloc races
	 */
	for (;;) {
		vp = dnp->dn_vnode;
		if (vp) {
			if (vget(vp, LK_EXCLUSIVE) == 0)
				break;	/* success */
			/* vget raced a reclaim, retry */
		} else {
			getnewvnode(VT_UNUSED10, mp, &vp, 0, lkflags);
			if (dnp->dn_vnode == NULL) {
				dnp->dn_vnode = vp;
				vp->v_data = dnp;
				vp->v_type = dnp->dn_type;
				if (dmp->dm_root == dnp)
					vsetflags(vp, VROOT);
				dirfs_node_ref(dnp);	/* ref for dnp<->vp */

				/* Type-specific initialization. */
				switch (dnp->dn_type) {
				case VBLK:
				case VCHR:
				case VSOCK:
					break;
				case VREG:
					vinitvmio(vp, dnp->dn_size, BMASK, -1);
					break;
				case VLNK:
					break;
				case VFIFO:
			//              vp->v_ops = &mp->mnt_vn_fifo_ops;
					break;
				case VDIR:
					break;
				default:
					panic("dirfs_alloc_vp: dnp=%p vp=%p "
					      "type=%d",
					      dnp, vp, dnp->dn_type);
					/* NOT REACHED */
					break;
				}
				break;	/* success */
			}
			vp->v_type = VBAD;
			vx_put(vp);
			/* multiple dirfs_alloc_vp calls raced, retry */
		}
	}
	KKASSERT(vp != NULL);
	*vpp = vp;
	dbg(5, "dnp=%p vp=%p type=%d\n", dnp, vp, vp->v_type);
}

/*
 * Do not call locked!
 */
void
dirfs_free_vp(dirfs_mount_t dmp, dirfs_node_t dnp)
{
	struct vnode *vp = NODE_TO_VP(dnp);

	dnp->dn_vnode = NULL;
	vp->v_data = NULL;
	dirfs_node_drop(dmp, dnp);
}

int
dirfs_nodetype(struct stat *st)
{
	int ret;
	mode_t mode = st->st_mode;

	debug_called();

	if (S_ISDIR(mode))
		ret = VDIR;
	else if (S_ISBLK(mode))
		ret = VBLK;
	else if (S_ISCHR(mode))
		ret = VCHR;
	else if (S_ISFIFO(mode))
		ret = VFIFO;
	else if (S_ISSOCK(mode))
		ret = VSOCK;
	else if (S_ISLNK(mode))
		ret = VLNK;
	else if (S_ISREG(mode))
		ret = VREG;
	else
		ret = VBAD;

	return ret;
}

int
dirfs_node_stat(int fd, const char *path, dirfs_node_t dnp)
{
	struct stat st;
	int error;

	debug_called();
	if (fd == DIRFS_NOFD)
		error = lstat(path, &st);
	else
		error = fstatat(fd, path, &st, AT_SYMLINK_NOFOLLOW);

	if (error)
		return errno;

	/* Populate our dirfs node struct with stat data */
	dnp->dn_uid = st.st_uid;
	dnp->dn_gid = st.st_gid;
	dnp->dn_mode = st.st_mode;
	dnp->dn_flags = st.st_flags;
	dnp->dn_links = st.st_nlink;
	dnp->dn_atime = st.st_atime;
	dnp->dn_atimensec = (st.st_atime * 1000000000L);
	dnp->dn_mtime = st.st_mtime;
	dnp->dn_mtimensec = (st.st_mtime * 1000000000L);
	dnp->dn_ctime = st.st_ctime;
	dnp->dn_ctimensec = (st.st_ctime * 1000000000L);
	dnp->dn_gen = st.st_gen;
	dnp->dn_ino = st.st_ino;
	dnp->dn_st_dev = st.st_dev;
	dnp->dn_size = st.st_size;
	dnp->dn_type = dirfs_nodetype(&st);

	return 0;
}

char *
dirfs_node_absolute_path(dirfs_mount_t dmp, dirfs_node_t cur, char **pathfreep)
{
	return(dirfs_node_absolute_path_plus(dmp, cur, NULL, pathfreep));
}

char *
dirfs_node_absolute_path_plus(dirfs_mount_t dmp, dirfs_node_t cur,
			      char *last, char **pathfreep)
{
	size_t len;
	dirfs_node_t dnp1;
	char *buf;
	int count;

	debug_called();

	KKASSERT(dmp->dm_root);	/* Sanity check */
	*pathfreep = NULL;
	if (cur == NULL)
		return NULL;
	buf = kmalloc(MAXPATHLEN + 1, M_DIRFS_MISC, M_WAITOK);

	/*
	 * Passed-in trailing element.
	 */
	count = 0;
	buf[MAXPATHLEN] = 0;
	if (last) {
		len = strlen(last);
		count += len;
		if (count <= MAXPATHLEN)
			bcopy(last, &buf[MAXPATHLEN - count], len);
		++count;
		if (count <= MAXPATHLEN)
			buf[MAXPATHLEN - count] = '/';
	}

	/*
	 * Iterate through the parents until we hit the root.
	 */
	dnp1 = cur;
	while (dirfs_node_isroot(dnp1) == 0) {
		count += dnp1->dn_namelen;
		if (count <= MAXPATHLEN) {
			bcopy(dnp1->dn_name, &buf[MAXPATHLEN - count],
			      dnp1->dn_namelen);
		}
		++count;
		if (count <= MAXPATHLEN)
			buf[MAXPATHLEN - count] = '/';
		dnp1 = dnp1->dn_parent;
		if (dnp1 == NULL)
			break;
	}

	/*
	 * Prefix with the root mount path.  If the element was unlinked
	 * dnp1 will be NULL and there is no path.
	 */
	len = strlen(dmp->dm_path);
	count += len;
	if (dnp1 && count <= MAXPATHLEN) {
		bcopy(dmp->dm_path, &buf[MAXPATHLEN - count], len);
		*pathfreep = buf;
		dbg(5, "absolute_path %s\n", &buf[MAXPATHLEN - count]);
		return (&buf[MAXPATHLEN - count]);
	} else {
		kfree(buf, M_DIRFS_MISC);
		*pathfreep = NULL;
		return (NULL);
	}
}

/*
 * Return a dirfs_node with a valid descriptor plus an allocated
 * relative path which can be used in openat(), fstatat(), etc calls
 * to locate the requested inode.
 */
dirfs_node_t
dirfs_findfd(dirfs_mount_t dmp, dirfs_node_t cur,
	     char **pathto, char **pathfreep)
{
	dirfs_node_t dnp1;
	int count;
	char *buf;

	debug_called();

	*pathfreep = NULL;
	*pathto = NULL;

	if (cur == NULL)
		return NULL;

	buf = kmalloc(MAXPATHLEN + 1, M_DIRFS_MISC, M_WAITOK | M_ZERO);
	count = 0;

	dnp1 = cur;
	while (dnp1 == cur || dnp1->dn_fd == DIRFS_NOFD) {
		count += dnp1->dn_namelen;
		if (count <= MAXPATHLEN) {
			bcopy(dnp1->dn_name, &buf[MAXPATHLEN - count],
			      dnp1->dn_namelen);
		}
		++count;
		if (count <= MAXPATHLEN)
			buf[MAXPATHLEN - count] = '/';
		dnp1 = dnp1->dn_parent;
		KKASSERT(dnp1 != NULL);
	}

	if (dnp1 && count <= MAXPATHLEN) {
		*pathfreep = buf;
		*pathto = &buf[MAXPATHLEN - count + 1];	/* skip '/' prefix */
		dirfs_node_ref(dnp1);
		dbg(5, "fd=%d dnp1=%p dnp1->dn_name=%d &buf[off]=%s\n",
		    dnp1->dn_fd, dnp1, dnp1->dn_name, *pathto);
	} else {
		dbg(5, "failed too long\n");
		kfree(buf, M_DIRFS_MISC);
		*pathfreep = NULL;
		*pathto = NULL;
		dnp1 = NULL;
	}
	return (dnp1);
}

void
dirfs_dropfd(dirfs_mount_t dmp, dirfs_node_t dnp1, char *pathfree)
{
	if (pathfree)
		kfree(pathfree, M_DIRFS_MISC);
	if (dnp1)
		dirfs_node_drop(dmp, dnp1);
}

int
dirfs_node_getperms(dirfs_node_t dnp, int *flags)
{
	dirfs_mount_t dmp;
	struct vnode *vp = dnp->dn_vnode;
	int isowner;
	int isgroup;

	/*
	 * There must be an active vnode anyways since that
	 * would indicate the dirfs node has valid data for
	 * for dnp->dn_mode (via lstat syscall).
	 */
	KKASSERT(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	isowner = (dmp->dm_uid == dnp->dn_uid);
	isgroup = (dmp->dm_gid == dnp->dn_gid);

	if (isowner) {
		if (dnp->dn_mode & S_IRUSR)
			*flags |= DIRFS_NODE_RD;
		if (dnp->dn_mode & S_IWUSR)
			*flags |= DIRFS_NODE_WR;
		if (dnp->dn_mode & S_IXUSR)
			*flags |= DIRFS_NODE_EXE;
	} else if (isgroup) {
		if (dnp->dn_mode & S_IRGRP)
			*flags |= DIRFS_NODE_RD;
		if (dnp->dn_mode & S_IWGRP)
			*flags |= DIRFS_NODE_WR;
		if (dnp->dn_mode & S_IXGRP)
			*flags |= DIRFS_NODE_EXE;
	} else {
		if (dnp->dn_mode & S_IROTH)
			*flags |= DIRFS_NODE_RD;
		if (dnp->dn_mode & S_IWOTH)
			*flags |= DIRFS_NODE_WR;
		if (dnp->dn_mode & S_IXOTH)
			*flags |= DIRFS_NODE_EXE;
	}

	return 0;
}

/*
 * This requires an allocated node and vnode, otherwise it'll panic
 */
int
dirfs_open_helper(dirfs_mount_t dmp, dirfs_node_t dnp, int parentfd,
		  char *relpath)
{
	dirfs_node_t pathnp;
	char *pathfree;
	char *tmp;
	int flags;
	int perms;
	int error;

	debug_called();

	flags = error = perms = 0;
	tmp = NULL;

	KKASSERT(dnp);
	KKASSERT(dnp->dn_vnode);

	/*
	 * XXX Besides VDIR and VREG there are other file
	 * types, y'know?
	 * Also, O_RDWR alone might not be the best mode to open
	 * a file with, need to investigate which suits better.
	 */
	dirfs_node_getperms(dnp, &perms);

	if (dnp->dn_type & VDIR) {
		flags |= O_DIRECTORY;
	} else {
		if (perms & DIRFS_NODE_WR)
			flags |= O_RDWR;
		else
			flags |= O_RDONLY;
	}
	if (relpath != NULL) {
		tmp = relpath;
		pathnp = NULL;
		KKASSERT(parentfd != DIRFS_NOFD);
	} else if (parentfd == DIRFS_NOFD) {
		pathnp = dirfs_findfd(dmp, dnp, &tmp, &pathfree);
		parentfd = pathnp->dn_fd;
	} else {
		pathnp = NULL;
	}

	dnp->dn_fd = openat(parentfd, tmp, flags);
	if (dnp->dn_fd == -1)
		error = errno;

	dbg(5, "dnp=%p tmp2=%s parentfd=%d flags=%d error=%d "
	    "flags=%08x w=%d x=%d\n", dnp, tmp, parentfd, flags, error,
	    perms);

	if (pathnp)
		dirfs_dropfd(dmp, pathnp, pathfree);

	return error;
}

int
dirfs_close_helper(dirfs_node_t dnp)
{
	int error = 0;

	debug_called();


	if (dnp->dn_fd != DIRFS_NOFD) {
		dbg(5, "closed fd on dnp=%p\n", dnp);
#if 0
		/* buffer cache buffers may still be present */
		error = close(dnp->dn_fd); /* XXX EINTR should be checked */
		dnp->dn_fd = DIRFS_NOFD;
#endif
	}

	return error;
}

int
dirfs_node_refcnt(dirfs_node_t dnp)
{
	return dnp->dn_refcnt;
}

int
dirfs_node_chtimes(dirfs_node_t dnp)
{
	struct vnode *vp;
	dirfs_mount_t dmp;
	int error = 0;
	char *tmp;
	char *pathfree;

	debug_called();

	vp = NODE_TO_VP(dnp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	KKASSERT(vn_islocked(vp));

	if (dnp->dn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
	KKASSERT(tmp);
	if((lutimes(tmp, NULL)) == -1)
		error = errno;

	dirfs_node_stat(DIRFS_NOFD, tmp, dnp);
	dirfs_dropfd(dmp, NULL, pathfree);

	KKASSERT(vn_islocked(vp));


	return error;
}

int
dirfs_node_chflags(dirfs_node_t dnp, int vaflags, struct ucred *cred)
{
	struct vnode *vp;
	dirfs_mount_t dmp;
	int error = 0;
	int flags;
	char *tmp;
	char *pathfree;

	debug_called();

	vp = NODE_TO_VP(dnp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	KKASSERT(vn_islocked(vp));

	flags = dnp->dn_flags;

	error = vop_helper_setattr_flags(&flags, vaflags, dnp->dn_uid, cred);
	/*
	 * When running vkernels with non-root it is not possible to set
	 * certain flags on host files, such as SF* flags. chflags(2) call
	 * will spit an error in that case.
	 */
	if (error == 0) {
		tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
		KKASSERT(tmp);
		if((lchflags(tmp, flags)) == -1)
			error = errno;
		dirfs_node_stat(DIRFS_NOFD, tmp, dnp);
		dirfs_dropfd(dmp, NULL, pathfree);
	}

	KKASSERT(vn_islocked(vp));

	return error;
}

int
dirfs_node_chmod(dirfs_mount_t dmp, dirfs_node_t dnp, mode_t mode)
{
	char *tmp;
	char *pathfree;
	int error = 0;

	tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
	KKASSERT(tmp);
	if (lchmod(tmp, mode) < 0)
		error = errno;
	dirfs_node_stat(DIRFS_NOFD, tmp, dnp);
	dirfs_dropfd(dmp, NULL, pathfree);

	return error;
}

int
dirfs_node_chown(dirfs_mount_t dmp, dirfs_node_t dnp,
		 uid_t uid, uid_t gid, mode_t mode)
{
	char *tmp;
	char *pathfree;
	int error = 0;

	tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
	KKASSERT(tmp);
	if (lchown(tmp, uid, gid) < 0)
		error = errno;
	if (mode != dnp->dn_mode)
		lchmod(tmp, mode);
	dirfs_node_stat(DIRFS_NOFD, tmp, dnp);
	dirfs_dropfd(dmp, NULL, pathfree);

	return error;
}


int
dirfs_node_chsize(dirfs_node_t dnp, off_t nsize)
{
	dirfs_mount_t dmp;
	struct vnode *vp;
	int error = 0;
	char *tmp;
	char *pathfree;
	off_t osize;
	int biosize;

	debug_called();

	KKASSERT(dnp);

	vp = NODE_TO_VP(dnp);
	dmp = VFS_TO_DIRFS(vp->v_mount);
	biosize = BSIZE;
	osize = dnp->dn_size;

	KKASSERT(vn_islocked(vp));

	switch (vp->v_type) {
	case VDIR:
		return (EISDIR);
	case VREG:
		break;
	default:
		return (EOPNOTSUPP);

	}

	tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
	if (nsize < osize) {
		error = nvtruncbuf(vp, nsize, biosize, -1, 0);
	} else {
		error = nvextendbuf(vp, osize, nsize,
				    biosize, biosize,
				    -1, -1, 0);
	}
	if (error == 0 && truncate(tmp, nsize) < 0)
		error = errno;
	if (error == 0)
		dnp->dn_size = nsize;
	dbg(5, "TRUNCATE %016jx %016jx\n", (intmax_t)nsize, dnp->dn_size);
	/*dirfs_node_stat(DIRFS_NOFD, tmp, dnp); don't need to do this*/

	dirfs_dropfd(dmp, NULL, pathfree);


	KKASSERT(vn_islocked(vp));

	return error;
}

void
dirfs_node_setpassive(dirfs_mount_t dmp, dirfs_node_t dnp, int state)
{
	struct vnode *vp;

	if (state && (dnp->dn_state & DIRFS_PASVFD) == 0 &&
	    dnp->dn_fd != DIRFS_NOFD) {
		dirfs_node_ref(dnp);
		dirfs_node_setflags(dnp, DIRFS_PASVFD);
		TAILQ_INSERT_TAIL(&dmp->dm_fdlist, dnp, dn_fdentry);
		++dirfs_fd_used;
		++dmp->dm_fd_used;

		/*
		 * If we are over our limit remove nodes from the
		 * passive fd cache.
		 */
		while (dmp->dm_fd_used > dirfs_fd_limit) {
			dnp = TAILQ_FIRST(&dmp->dm_fdlist);
			dirfs_node_setpassive(dmp, dnp, 0);
		}
	}
	if (state == 0 && (dnp->dn_state & DIRFS_PASVFD)) {
		dirfs_node_clrflags(dnp, DIRFS_PASVFD);
		TAILQ_REMOVE(&dmp->dm_fdlist, dnp, dn_fdentry);
		--dirfs_fd_used;
		--dmp->dm_fd_used;
		dbg(5, "dnp=%p removed from fdlist. %d used\n",
		    dnp, dirfs_fd_used);

		/*
		 * Attempt to close the descriptor.  We can only do this
		 * if the related vnode is inactive and has exactly two
		 * refs (representing the vp<->dnp and PASVFD).  Otherwise
		 * someone might have ref'd the node in order to use the
		 * dn_fd.
		 *
		 * Also, if the vnode is in any way dirty we leave the fd
		 * open for the buffer cache code.  The syncer will eventually
		 * come along and fsync the vnode, and the next inactive
		 * transition will deal with the descriptor.
		 *
		 * The descriptor for the root node is NEVER closed by
		 * this function.
		 */
		vp = dnp->dn_vnode;
		if (dirfs_node_refcnt(dnp) == 2 && vp &&
		    dnp->dn_fd != DIRFS_NOFD &&
		    !dirfs_node_isroot(dnp) &&
		    (vp->v_flag & (VINACTIVE|VOBJDIRTY)) == VINACTIVE &&
		    RB_EMPTY(&vp->v_rbdirty_tree)) {
			dbg(5, "passive cache: closing %d\n", dnp->dn_fd);
			close(dnp->dn_fd);
			dnp->dn_fd = DIRFS_NOFD;
		} else {
			if (dirfs_node_refcnt(dnp) == 1 && dnp->dn_vnode == NULL &&
			    dnp->dn_fd != DIRFS_NOFD &&
			    dnp != dmp->dm_root) {
				dbg(5, "passive cache: closing %d\n", dnp->dn_fd);
				close(dnp->dn_fd);
				dnp->dn_fd = DIRFS_NOFD;
			}
		}
		dirfs_node_drop(dmp, dnp);
	}
}

char *
dirfs_flag2str(dirfs_node_t dnp)
{
	const char *txtflg[] = { DIRFS_TXTFLG };
	static char str[512] = {0};

	if (dnp->dn_state & DIRFS_PASVFD)
		ksprintf(str, "%s ", txtflg[0]);

	return str;
}

void
debug(int level, const char *fmt, ...)
{
	__va_list ap;

	if (debuglvl >= level) {
		__va_start(ap, fmt);
		kvprintf(fmt, ap);
		__va_end(ap);
	}
}

