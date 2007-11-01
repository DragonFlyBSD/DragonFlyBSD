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
 * $DragonFly: src/sys/vfs/hammer/hammer_inode.c,v 1.1 2007/11/01 20:53:05 dillon Exp $
 */

#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

static enum vtype hammer_get_vnode_type(u_int16_t obj_type);

int
hammer_vop_inactive(struct vop_inactive_args *ap)
{
	return(0);
}

int
hammer_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct hammer_mount *hmp;
	struct hammer_inode *ip;
	struct vnode *vp;

	vp = ap->a_vp;
	hmp = (void *)vp->v_mount->mnt_data;
	if ((ip = vp->v_data) != NULL) {
		ip->vp = NULL;
		vp->v_data = NULL;
		RB_REMOVE(hammer_ino_rb_tree, &hmp->rb_inos_root, ip);
		kfree(ip, M_HAMMER);
	}
	return(0);
}

/*
 * Lookup or create the vnode associated with the specified inode number.
 * ino_t in DragonFly is 64 bits which matches the 64 bit HAMMER inode
 * number.
 */
int
hammer_vfs_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	struct hammer_btree_info binfo;
	struct hammer_inode_info iinfo;
	struct hammer_base_elm key;
	struct hammer_inode *ip;
	struct vnode *vp;
	int error;

	/*
	 * Determine if we already have an inode cached.  If we do then
	 * we are golden.
	 */
	iinfo.obj_id = ino;
	iinfo.obj_asof = 0;
loop:
	ip = hammer_ino_rb_tree_RB_LOOKUP_INFO(&hmp->rb_inos_root, &iinfo);
	if (ip) {
		vp = ip->vp;
		if (vget(vp, LK_EXCLUSIVE) != 0)
			goto loop;
		ip = hammer_ino_rb_tree_RB_LOOKUP_INFO(&hmp->rb_inos_root, 
						       &iinfo);
		if (ip == NULL || ip->vp != vp) {
			vput(vp);
			goto loop;
		}
		*vpp = vp;
		return(0);
	}

	/*
	 * Lookup failed, instantiate a new vnode and inode in-memory
	 * structure so we don't block in kmalloc later on when holding
	 * locked buffer cached buffers.
	 */
	error = getnewvnode(VT_HAMMER, mp, vpp, 0, 0);
	if (error) {
		*vpp = NULL;
		return (error);
	}
	vp = *vpp;
	ip = kmalloc(sizeof(*ip), M_HAMMER, M_WAITOK|M_ZERO);

	/*
	 * If we do not have an inode cached search the HAMMER on-disk B-Tree
	 * for it.
	 */
	hammer_btree_info_init(&binfo, hmp->rootcl);
	key.obj_id = ino;
	key.key = 0;
	key.create_tid = 0;
	key.delete_tid = 0;
	key.rec_type = HAMMER_RECTYPE_INODE;
	key.obj_type = 0;

	error = hammer_btree_lookup(&binfo, &key, HAMMER_BTREE_GET_RECORD |
					          HAMMER_BTREE_GET_DATA);

	/*
	 * On success the B-Tree lookup will hold the appropriate
	 * buffer cache buffers and provide a pointer to the requested
	 * information.  Copy the information to the in-memory inode.
	 */
	if (error == 0) {
		ip->ino_rec = binfo.rec->inode;
		ip->ino_data = binfo.data->inode;
	}
	hammer_btree_info_done(&binfo);

	/*
	 * On success load the inode's record and data and insert the
	 * inode into the B-Tree.  It is possible to race another lookup
	 * insertion of the same inode so deal with that condition too.
	 */
	if (error == 0) {
		if (RB_INSERT(hammer_ino_rb_tree, &hmp->rb_inos_root, ip)) {
			vp->v_type = VBAD;
			vx_put(vp);
			kfree(ip, M_HAMMER);
			goto loop;
		}
		ip->vp = vp;
		vp->v_data = (void *)ip;
		vp->v_type =
			hammer_get_vnode_type(ip->ino_rec.base.base.obj_type);
		*vpp = vp;
	} else {
		*vpp = NULL;
	}
	return (error);
}

/*
 * Convert a HAMMER filesystem object type to a vnode type
 */
static
enum vtype
hammer_get_vnode_type(u_int16_t obj_type)
{
	switch(obj_type) {
	case HAMMER_OBJTYPE_DIRECTORY:
		return(VDIR);
	case HAMMER_OBJTYPE_REGFILE:
		return(VREG);
	case HAMMER_OBJTYPE_DBFILE:
		return(VDATABASE);
	case HAMMER_OBJTYPE_FIFO:
		return(VFIFO);
	case HAMMER_OBJTYPE_CDEV:
		return(VCHR);
	case HAMMER_OBJTYPE_BDEV:
		return(VBLK);
	case HAMMER_OBJTYPE_SOFTLINK:
		return(VLNK);
	default:
		return(VBAD);
	}
	/* not reached */
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
			hammer_put_buffer(buffer);
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
			*errorp = EIO;
			return(NULL);
		}
		if (buf_off < sizeof(buffer->ondisk->head)) {
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

