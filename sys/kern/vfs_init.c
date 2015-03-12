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
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed
 * to Berkeley by John Heidemann of the UCLA Ficus project.
 *
 * Source: * @(#)i405_init.c 2.10 92/04/27 UCLA Ficus project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)vfs_init.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/kern/vfs_init.c,v 1.59 2002/04/30 18:44:32 dillon Exp $
 */
/*
 * Manage vnode VOP operations vectors
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/objcache.h>

static MALLOC_DEFINE(M_VNODEOP, "vnodeops", "vnode operations vectors");
static MALLOC_DEFINE(M_NAMEI, "nameibufs", "namei path buffers");

/*
 * Zone for namei
 */
struct objcache *namei_oc;

static TAILQ_HEAD(, vnodeopv_node) vnodeopv_list;
static void vfs_calc_vnodeops(struct vop_ops *ops);


/*
 * Add a vnode operations (vnops) vector to the global list.
 */
void
vfs_nadd_vnodeops_sysinit(void *data)
{
	struct vop_ops *ops = data;

	vfs_add_vnodeops(NULL, ops, NULL);	/* mount, template, newcopy */
}

/*
 * Unlink previously added vnode operations vector.
 */
void
vfs_nrm_vnodeops_sysinit(void *data)
{
	struct vop_ops *ops = data;

	vfs_rm_vnodeops(NULL, ops, NULL);
}

void
vfs_add_vnodeops(struct mount *mp, struct vop_ops *template,
		 struct vop_ops **ops_pp)
{
	struct vop_ops *ops;

	if (ops_pp) {
		KKASSERT(*ops_pp == NULL);
		*ops_pp = kmalloc(sizeof(*ops), M_VNODEOP, M_WAITOK);
		ops = *ops_pp;
		bcopy(template, ops, sizeof(*ops));
	} else {
		ops = template;
	}

	vfs_calc_vnodeops(ops);
	ops->head.vv_mount = mp;

	if (mp) {
		if (mp->mnt_vn_coherency_ops)
			mp->mnt_vn_use_ops = mp->mnt_vn_coherency_ops;
		else if (mp->mnt_vn_journal_ops)
			mp->mnt_vn_use_ops = mp->mnt_vn_journal_ops;
		else
			mp->mnt_vn_use_ops = mp->mnt_vn_norm_ops;
	}
}

/*
 * Remove a previously installed operations vector.
 *
 * NOTE: Either template or ops_pp may be NULL, but not both.
 */
void
vfs_rm_vnodeops(struct mount *mp, struct vop_ops *template,
		struct vop_ops **ops_pp)
{
	struct vop_ops *ops;

	if (ops_pp) {
		ops = *ops_pp;
		*ops_pp = NULL;
	} else {
		ops = template;
	}
	if (ops == NULL)
		return;
	KKASSERT(mp == ops->head.vv_mount);
	if (mp) {
		if (mp->mnt_vn_coherency_ops)
			mp->mnt_vn_use_ops = mp->mnt_vn_coherency_ops;
		else if (mp->mnt_vn_journal_ops)
			mp->mnt_vn_use_ops = mp->mnt_vn_journal_ops;
		else
			mp->mnt_vn_use_ops = mp->mnt_vn_norm_ops;
	}
	if (ops_pp)
		kfree(ops, M_VNODEOP);
}

/*
 * Calculate the VFS operations vector array.  This function basically
 * replaces any NULL entry with the default entry.
 */
static void
vfs_calc_vnodeops(struct vop_ops *ops)
{
	int off;

	for (off = __offsetof(struct vop_ops, vop_ops_first_field);
	     off <= __offsetof(struct vop_ops, vop_ops_last_field);
	     off += sizeof(void *)
	) {
		if (*(void **)((char *)ops + off) == NULL)
		    *(void **)((char *)ops + off) = ops->vop_default;
	}
}

/*
 * Routines having to do with the management of the vnode table.
 */
struct vattr va_null;

/*
 * Initialize the vnode structures and initialize each file system type.
 */
/* ARGSUSED*/
static void
vfsinit(void *dummy)
{
	TAILQ_INIT(&vnodeopv_list);
	namei_oc = objcache_create_simple(M_NAMEI, MAXPATHLEN);

	/*
	 * Initialize the vnode table
	 */
	vfs_subr_init();
	vfs_mount_init();
	vfs_lock_init();

	/*
	 * Initialize the vnode name cache
	 */
	nchinit();

	/*
	 * Initialize each file system type.
	 * Vfs type numbers must be distinct from VFS_GENERIC (and VFS_VFSCONF).
	 */
	vattr_null(&va_null);
}
SYSINIT(vfs, SI_SUB_VFS, SI_ORDER_FIRST, vfsinit, NULL);

/*
 * vfsconf related functions/data.
 */

/* highest defined filesystem type */
static int vfsconf_maxtypenum = VFS_GENERIC + 1; 

/* head of list of filesystem types */
static STAILQ_HEAD(, vfsconf) vfsconf_list = 
	STAILQ_HEAD_INITIALIZER(vfsconf_list);

struct vfsconf *
vfsconf_find_by_name(const char *name) 
{
	struct vfsconf *vfsp;

	STAILQ_FOREACH(vfsp, &vfsconf_list, vfc_next) {
		if (strcmp(name, vfsp->vfc_name) == 0)
			break;
	}
	return vfsp;
}

struct vfsconf *
vfsconf_find_by_typenum(int typenum) 
{
	struct vfsconf *vfsp;

	STAILQ_FOREACH(vfsp, &vfsconf_list, vfc_next) {
		if (typenum == vfsp->vfc_typenum)
			break;
	}
	return vfsp;
}

static void
vfsconf_add(struct vfsconf *vfc)
{
	vfc->vfc_typenum = vfsconf_maxtypenum++;
	STAILQ_INSERT_TAIL(&vfsconf_list, vfc, vfc_next);
}

static void
vfsconf_remove(struct vfsconf *vfc)
{
	int maxtypenum;

	STAILQ_REMOVE(&vfsconf_list, vfc, vfsconf, vfc_next); 

	maxtypenum = VFS_GENERIC;
	STAILQ_FOREACH(vfc, &vfsconf_list, vfc_next) {
		if (maxtypenum < vfc->vfc_typenum)
			maxtypenum = vfc->vfc_typenum;
	}
	vfsconf_maxtypenum = maxtypenum + 1;
}

int
vfsconf_get_maxtypenum(void)
{
	return vfsconf_maxtypenum;
}

/*
 * Iterate over all vfsconf entries. Break out of the iterator
 * by returning != 0.
 */
int
vfsconf_each(int (*iter)(struct vfsconf *element, void *data), void *data)
{
	int error;
	struct vfsconf *vfsp;

	STAILQ_FOREACH(vfsp, &vfsconf_list, vfc_next) {
		error = iter(vfsp, data);
		if (error)
			return (error);
	}
	return (0);
}

/*
 * Register a VFS.
 *
 * After doing general initialisation, this function will
 * call the filesystem specific initialisation vector op,
 * i.e. vfsops->vfs_init().
 */
int
vfs_register(struct vfsconf *vfc)
{
	struct sysctl_oid *oidp;
	struct vfsops *vfsops = NULL;

	if (vfsconf_find_by_name(vfc->vfc_name) != NULL)
		return EEXIST;

	vfsconf_add(vfc);

	/*
	 * If this filesystem has a sysctl node under vfs
	 * (i.e. vfs.xxfs), then change the oid number of that node to 
	 * match the filesystem's type number.  This allows user code
	 * which uses the type number to read sysctl variables defined
	 * by the filesystem to continue working. Since the oids are
	 * in a sorted list, we need to make sure the order is
	 * preserved by re-registering the oid after modifying its
	 * number.
	 */
	SLIST_FOREACH(oidp, &sysctl__vfs_children, oid_link)
		if (strcmp(oidp->oid_name, vfc->vfc_name) == 0) {
			sysctl_unregister_oid(oidp);
			oidp->oid_number = vfc->vfc_typenum;
			sysctl_register_oid(oidp);
		}
	
	/*
	 * Initialise unused fields in the file system's vfsops vector.
	 *
	 * NOTE the file system should provide the mount and unmount ops
	 * at the least.  In order for unmount to succeed, we also need
	 * the file system to provide us with vfsops->vfs_root otherwise
	 * the unmount(2) operation will not succeed.
	 */
	vfsops = vfc->vfc_vfsops;
	KKASSERT(vfc->vfc_vfsops != NULL);
	KKASSERT(vfsops->vfs_mount != NULL);
	KKASSERT(vfsops->vfs_root != NULL);
	KKASSERT(vfsops->vfs_unmount != NULL);

	if (vfsops->vfs_root == NULL) {
		/* return file system's root vnode */
		vfsops->vfs_root = vfs_stdroot;
	}
	if (vfsops->vfs_start == NULL) {
		/* 
		 * Make file system operational before first use.  This
		 * routine is called at mount-time for initialising MFS,
		 * not used by other file systems.
		 */
		vfsops->vfs_start = vfs_stdstart;
	}
	if (vfsops->vfs_quotactl == NULL) {
		/* quota control */
		vfsops->vfs_quotactl = vfs_stdquotactl;
	}
	if (vfsops->vfs_statfs == NULL) {
		/* return file system's status */
		vfsops->vfs_statfs = vfs_stdstatfs;
	}
	if (vfsops->vfs_statvfs == NULL) {
		/* return file system's status */
		vfsops->vfs_statvfs = vfs_stdstatvfs;
	}
	if (vfsops->vfs_sync == NULL) {
		/*
		 * Flush dirty buffers.  File systems can use vfs_stdsync()
		 * by explicitly setting it in the vfsops->vfs_sync vector
		 * entry.
		 */
		vfsops->vfs_sync = vfs_stdnosync;
	}
	if (vfsops->vfs_vget == NULL) {
		/* convert an inode number to a vnode */
		vfsops->vfs_vget = vfs_stdvget;
	}
	if (vfsops->vfs_fhtovp == NULL) {
		/* turn an NFS file handle into a vnode */
		vfsops->vfs_fhtovp = vfs_stdfhtovp;
	}
	if (vfsops->vfs_checkexp == NULL) {
		/* check if file system is exported */
		vfsops->vfs_checkexp = vfs_stdcheckexp;
	}
	if (vfsops->vfs_vptofh == NULL) {
		/* turn a vnode into an NFS file handle */
		vfsops->vfs_vptofh = vfs_stdvptofh;
	}
	if (vfsops->vfs_init == NULL) {
		/* file system specific initialisation */
		vfsops->vfs_init = vfs_stdinit;
	}
	if (vfsops->vfs_uninit == NULL) {
		/* file system specific uninitialisation */
		vfsops->vfs_uninit = vfs_stduninit;
	}
	if (vfsops->vfs_extattrctl == NULL) {
		/* extended attribute control */
		vfsops->vfs_extattrctl = vfs_stdextattrctl;
	}

	if (vfsops->vfs_ncpgen_set == NULL) {
		/* namecache generation number */
		vfsops->vfs_ncpgen_set = vfs_stdncpgen_set;
	}

	if (vfsops->vfs_ncpgen_test == NULL) {
		/* check namecache generation */
		vfsops->vfs_ncpgen_test = vfs_stdncpgen_test;
	}

	/* VFS quota uid and gid accounting */
	if (vfs_quota_enabled && vfsops->vfs_acinit == NULL) {
		vfsops->vfs_acinit = vfs_stdac_init;
	}
	if (vfs_quota_enabled && vfsops->vfs_acdone == NULL) {
		vfsops->vfs_acdone = vfs_stdac_done;
	}

	/*
	 * Call init function for this VFS...
	 */
	vfs_init(vfc);
	return 0;
}


/*
 * Remove previously registered VFS.
 *
 * After doing general de-registration like removing sysctl
 * nodes etc, it will call the filesystem specific vector
 * op, i.e. vfsops->vfs_uninit().
 * 
 */
int
vfs_unregister(struct vfsconf *vfc)
{
	struct vfsconf *vfsp;
	int error;

	vfsp = vfsconf_find_by_name(vfc->vfc_name);

	if (vfsp == NULL)
		return EINVAL;

	if (vfsp->vfc_refcount != 0)
		return EBUSY;

	if (vfc->vfc_vfsops->vfs_uninit != NULL) {
		error = vfs_uninit(vfc, vfsp);
		if (error)
			return (error);
	}

	vfsconf_remove(vfsp);
	return 0;
}

int
vfs_modevent(module_t mod, int type, void *data)
{
	struct vfsconf *vfc;
	int error = 0;

	vfc = (struct vfsconf *)data;

	switch (type) {
	case MOD_LOAD:
		if (vfc)
			error = vfs_register(vfc);
		break;

	case MOD_UNLOAD:
		if (vfc)
			error = vfs_unregister(vfc);
		break;
	default:	/* including MOD_SHUTDOWN */
		break;
	}
	return (error);
}
