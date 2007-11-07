/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_inode.c,v 1.3 2007/11/07 00:43:24 dillon Exp $
 */

#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

int
hammer_vop_inactive(struct vop_inactive_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);

	if (ip == NULL)
		vrecycle(ap->a_vp);
	return(0);
}

int
hammer_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct hammer_inode *ip;
	struct vnode *vp;

	vp = ap->a_vp;
	if ((ip = vp->v_data) != NULL)
		hammer_unload_inode(ip, NULL);
	return(0);
}

/*
 * Obtain a vnode for the specified inode number.  An exclusively locked
 * vnode is returned.
 *
 * To avoid deadlocks we cannot hold the inode lock while we are creating
 * a new vnode.  We can prevent the inode from going away, however.  If
 * we race another vget we just throw away our newly created vnode.
 */
int
hammer_vfs_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	struct hammer_inode *ip;
	int error;

	/*
	 * Get/allocate the hammer_inode structure.  The structure must be
	 * unlocked while we manipulate the related vnode to avoid a
	 * deadlock.
	 */
	ip = hammer_get_inode(hmp, ino, &error);
	if (ip == NULL) {
		*vpp = NULL;
		return(error);
	}
	hammer_lock_to_ref(&ip->lock);
	error = hammer_get_vnode(ip, LK_EXCLUSIVE, vpp);
	hammer_put_inode_ref(ip);
	return (error);
}

/*
 * Return a locked vnode for the specified inode.  The inode must be
 * referenced but NOT LOCKED on entry and will remain referenced on
 * return.
 */
int
hammer_get_vnode(struct hammer_inode *ip, int lktype, struct vnode **vpp)
{
	struct vnode *vp;
	int error = 0;

	for (;;) {
		if ((vp = ip->vp) == NULL) {
			error = getnewvnode(VT_HAMMER, ip->hmp->mp, vpp, 0, 0);
			if (error)
				break;
			if (ip->vp == NULL) {
				vp = *vpp;
				ip->vp = vp;
				vp->v_type = hammer_get_vnode_type(
						ip->ino_rec.base.base.obj_type);
				vp->v_data = (void *)ip;
				/* vnode locked by getnewvnode() */
				break;
			}
			vp->v_type = VBAD;
			vx_put(vp);
		} else {
			/*
			 * loop if the vget fails (aka races), or if the vp
			 * no longer matches ip->vp.
			 */
			if (vget(vp, LK_EXCLUSIVE) == 0) {
				if (vp == ip->vp)
					break;
				vput(vp);
			}
		}
	}
	return(error);
}

/*
 * Get and lock a HAMMER inode.  These functions do not attach or detach
 * the related vnode.
 */
struct hammer_inode *
hammer_get_inode(struct hammer_mount *hmp, u_int64_t obj_id, int *errorp)
{
	struct hammer_btree_info binfo;
	struct hammer_inode_info iinfo;
	struct hammer_base_elm key;
	struct hammer_inode *ip;

	/*
	 * Determine if we already have an inode cached.  If we do then
	 * we are golden.
	 */
	iinfo.obj_id = obj_id;
	iinfo.obj_asof = HAMMER_MAX_TID;	/* XXX */
loop:
	ip = hammer_ino_rb_tree_RB_LOOKUP_INFO(&hmp->rb_inos_root, &iinfo);
	if (ip) {
		hammer_lock(&ip->lock);
		*errorp = 0;
		return(ip);
	}

	ip = kmalloc(sizeof(*ip), M_HAMMER, M_WAITOK|M_ZERO);
	ip->obj_id = obj_id;
	ip->obj_asof = iinfo.obj_asof;
	ip->hmp = hmp;

	/*
	 * If we do not have an inode cached search the HAMMER on-disk B-Tree
	 * for it.
	 */
	hammer_btree_info_init(&binfo, hmp->rootcl);
	key.obj_id = ip->obj_id;
	key.key = 0;
	key.create_tid = iinfo.obj_asof;
	key.delete_tid = 0;
	key.rec_type = HAMMER_RECTYPE_INODE;
	key.obj_type = 0;

	*errorp = hammer_btree_lookup(&binfo, &key, HAMMER_BTREE_GET_RECORD |
					            HAMMER_BTREE_GET_DATA);

	/*
	 * On success the B-Tree lookup will hold the appropriate
	 * buffer cache buffers and provide a pointer to the requested
	 * information.  Copy the information to the in-memory inode.
	 */
	if (*errorp == 0) {
		ip->ino_rec = binfo.rec->inode;
		ip->ino_data = binfo.data->inode;
	}
	hammer_btree_info_done(&binfo);

	/*
	 * On success load the inode's record and data and insert the
	 * inode into the B-Tree.  It is possible to race another lookup
	 * insertion of the same inode so deal with that condition too.
	 */
	if (*errorp == 0) {
		if (RB_INSERT(hammer_ino_rb_tree, &hmp->rb_inos_root, ip)) {
			kfree(ip, M_HAMMER);
			goto loop;
		}
	} else {
		kfree(ip, M_HAMMER);
		ip = NULL;
	}
	return (ip);
}

void
hammer_lock_inode(struct hammer_inode *ip)
{
	hammer_lock(&ip->lock);
}

void
hammer_put_inode(struct hammer_inode *ip)
{
	hammer_unlock(&ip->lock);
}

void
hammer_put_inode_ref(struct hammer_inode *ip)
{
	hammer_unref(&ip->lock);
}

/*
 * (called via RB_SCAN)
 */
int
hammer_unload_inode(struct hammer_inode *ip, void *data __unused)
{
	struct vnode *vp;

	KKASSERT(ip->lock.refs == 0);
	if ((vp = ip->vp) != NULL) {
		ip->vp = NULL;
		vp->v_data = NULL;
		/* XXX */
	}
	RB_REMOVE(hammer_ino_rb_tree, &ip->hmp->rb_inos_root, ip);
	kfree(ip, M_HAMMER);
	return(0);
}

/*
 * A transaction has modified an inode, requiring a new record and possibly
 * also data to be written out.
 */
void
hammer_modify_inode(struct hammer_transaction *trans,
		    struct hammer_inode *ip, int flags)
{
	ip->flags |= flags;
	KKASSERT(0);
}

/*
 * Access the filesystem buffer containing the cluster-relative byte
 * offset, validate the buffer type, load *bufferp and return a
 * pointer to the requested data.
 *
 * If buf_type is 0 the buffer is assumed to be a pure-data buffer and
 * no type or crc check is performed.
 *
 * XXX add a flag for the buffer type and check the CRC here XXX
 */
void *
hammer_bread(struct hammer_cluster *cluster, int32_t cloff,
	     u_int64_t buf_type,
	     int *errorp, struct hammer_buffer **bufferp)
{
	struct hammer_buffer *buffer;
	int32_t buf_no;
	int32_t buf_off;

	/*
	 * Load the correct filesystem buffer, replacing *bufferp.
	 */
	buf_no = cloff / HAMMER_BUFSIZE;
	buffer = *bufferp;
	if (buffer == NULL || buffer->cluster != cluster ||
	    buffer->buf_no != buf_no) {
		if (buffer)
			hammer_put_buffer(buffer, 0);
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
		if (buffer == NULL)
			return(NULL);
	}

	/*
	 * Validate the buffer type and crc XXX
	 */
	buf_off = cloff & HAMMER_BUFMASK;
	if (buf_type) {
		if (buf_type != buffer->ondisk->head.buf_type) {
			kprintf("BUFFER HEAD TYPE MISMATCH %llx %llx\n",
				buf_type, buffer->ondisk->head.buf_type);
			*errorp = EIO;
			return(NULL);
		}
		if (buf_off < sizeof(buffer->ondisk->head)) {
			kprintf("BUFFER OFFSET TOO LOW %d\n", buf_off);
			*errorp = EIO;
			return(NULL);
		}
		/* XXX crc */
	}

	/*
	 * Return a pointer to the buffer data.
	 */
	*errorp = 0;
	return((char *)buffer->ondisk + buf_off);
}

