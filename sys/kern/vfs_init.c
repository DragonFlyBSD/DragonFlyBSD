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
 *	@(#)vfs_init.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/kern/vfs_init.c,v 1.59 2002/04/30 18:44:32 dillon Exp $
 * $DragonFly: src/sys/kern/vfs_init.c,v 1.6 2004/08/17 18:57:32 dillon Exp $
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
#include <vm/vm_zone.h>

MALLOC_DEFINE(M_VNODE, "vnodes", "Dynamically allocated vnodes");

/*
 * Zone for namei
 */
struct vm_zone *namei_zone;

/*
 * vfs_init() will set maxvfsconf
 * to the highest defined type number.
 */
int maxvfsconf;
struct vfsconf *vfsconf;

static TAILQ_HEAD(, vnodeopv_node) vnodeopv_list;
static void vfs_recalc_vnodeops(void);

/*
 * Add a vnode operations (vnops) vector to the global list.
 */
void
vfs_add_vnodeops_sysinit(const void *data)
{
	const struct vnodeopv_desc *vdesc = data;

	vfs_add_vnodeops(vdesc->opv_desc_vector, vdesc->opv_desc_ops);
}

/*
 * Unlink previously added vnode operations vector.
 */
void
vfs_rm_vnodeops_sysinit(const void *data)
{
	const struct vnodeopv_desc *vdesc = data;

	vfs_rm_vnodeops(vdesc->opv_desc_vector);
}

void
vfs_add_vnodeops(struct vop_ops **vops_pp, struct vnodeopv_entry_desc *descs)
{
	struct vnodeopv_node *node;
	struct vop_ops *ops;

	node = malloc(sizeof(*node), M_VNODE, M_ZERO|M_WAITOK);
	if ((ops = *vops_pp) == NULL) {
		ops = malloc(sizeof(struct vop_ops), M_VNODE, M_ZERO|M_WAITOK);
		*vops_pp = ops;
	}
	node->ops = ops;
	node->descs = descs;
	++ops->vv_refs;
	TAILQ_INSERT_TAIL(&vnodeopv_list, node, entry);
	vfs_recalc_vnodeops();
}

void
vfs_rm_vnodeops(struct vop_ops **vops_pp)
{
	struct vop_ops *ops = *vops_pp;
	struct vnodeopv_node *node;

	if (ops == NULL)
		return;

	TAILQ_FOREACH(node, &vnodeopv_list, entry) {
		if (node->ops == ops)
			break;
	}
	if (node == NULL) {
		printf("vfs_rm_vnodeops: unable to find ops: %p\n", ops);
		return;
	}
	TAILQ_REMOVE(&vnodeopv_list, node, entry);
	free(node, M_VNODE);
	KKASSERT(ops != NULL && ops->vv_refs > 0);
	if (--ops->vv_refs == 0) {
		*vops_pp = NULL;
		free(ops, M_VNODE);
	}
	vfs_recalc_vnodeops();
}

/*
 * Recalculate VFS operations vectors
 */
static void
vfs_recalc_vnodeops(void)
{
	struct vnodeopv_node *node;
	struct vnodeopv_entry_desc *desc;
	struct vop_ops *ops;
	struct vop_ops *vnew;
	int off;

	/*
	 * Because vop_ops may be active we can't just blow them away, we
	 * have to generate new vop_ops and then copy them into the running
	 * vop_ops.  Any missing entries will be assigned to the default
	 * entry.  If the default entry itself is missing it will be assigned
	 * to vop_eopnotsupp.
	 */
	TAILQ_FOREACH(node, &vnodeopv_list, entry) {
		ops = node->ops;
		if ((vnew = ops->vv_new) == NULL) {
			vnew = malloc(sizeof(struct vop_ops),
					M_VNODE, M_ZERO|M_WAITOK);
			ops->vv_new = vnew;
			vnew->vop_default = vop_eopnotsupp;
		}
		for (desc = node->descs; desc->opve_op; ++desc) {
			off = desc->opve_op->vdesc_offset;
			*(void **)((char *)vnew + off) = desc->opve_func;
		}
		for (off = __offsetof(struct vop_ops, vop_ops_first_field);
		     off <= __offsetof(struct vop_ops, vop_ops_last_field);
		     off += sizeof(void **)
		) {
			if (*(void **)((char *)vnew + off) == NULL)
			    *(void **)((char *)vnew + off) = vnew->vop_default;
		}
	}

	/*
	 * Copy the temporary ops into the running configuration and then
	 * delete them.
	 */
	TAILQ_FOREACH(node, &vnodeopv_list, entry) {
		ops = node->ops;
		if ((vnew = ops->vv_new) == NULL)
			continue;
		for (off = __offsetof(struct vop_ops, vop_ops_first_field);
		     off <= __offsetof(struct vop_ops, vop_ops_last_field);
		     off += sizeof(void **)
		) {
			*(void **)((char *)ops + off) = 
				*(void **)((char *)vnew + off);
		}
		ops->vv_new = NULL;
		free(vnew, M_VNODE);
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
	namei_zone = zinit("NAMEI", MAXPATHLEN, 0, 0, 2);

	/*
	 * Initialize the vnode table
	 */
	vntblinit();
	/*
	 * Initialize the vnode name cache
	 */
	nchinit();
	/*
	 * Initialize each file system type.
	 * Vfs type numbers must be distinct from VFS_GENERIC (and VFS_VFSCONF).
	 */
	vattr_null(&va_null);
	maxvfsconf = VFS_GENERIC + 1;
}
SYSINIT(vfs, SI_SUB_VFS, SI_ORDER_FIRST, vfsinit, NULL)

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
	struct vfsconf *vfsp;

	vfsp = NULL;
	if (vfsconf)
		for (vfsp = vfsconf; vfsp->vfc_next; vfsp = vfsp->vfc_next)
			if (strcmp(vfc->vfc_name, vfsp->vfc_name) == 0)
				return EEXIST;

	vfc->vfc_typenum = maxvfsconf++;
	if (vfsp)
		vfsp->vfc_next = vfc;
	else
		vfsconf = vfc;
	vfc->vfc_next = NULL;

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
	 * Call init function for this VFS...
	 */
	(*(vfc->vfc_vfsops->vfs_init))(vfc);

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
	struct vfsconf *vfsp, *prev_vfsp;
	int error, i, maxtypenum;

	i = vfc->vfc_typenum;

	prev_vfsp = NULL;
	for (vfsp = vfsconf; vfsp;
			prev_vfsp = vfsp, vfsp = vfsp->vfc_next) {
		if (!strcmp(vfc->vfc_name, vfsp->vfc_name))
			break;
	}
	if (vfsp == NULL)
		return EINVAL;
	if (vfsp->vfc_refcount)
		return EBUSY;
	if (vfc->vfc_vfsops->vfs_uninit != NULL) {
		error = (*vfc->vfc_vfsops->vfs_uninit)(vfsp);
		if (error)
			return (error);
	}
	if (prev_vfsp)
		prev_vfsp->vfc_next = vfsp->vfc_next;
	else
		vfsconf = vfsp->vfc_next;
	maxtypenum = VFS_GENERIC;
	for (vfsp = vfsconf; vfsp != NULL; vfsp = vfsp->vfc_next)
		if (maxtypenum < vfsp->vfc_typenum)
			maxtypenum = vfsp->vfc_typenum;
	maxvfsconf = maxtypenum + 1;
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
