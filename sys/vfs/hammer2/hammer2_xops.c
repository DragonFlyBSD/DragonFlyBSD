/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression) 
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
/*
 * Per-node backend for kernel filesystem interface.
 *
 * This executes a VOP concurrently on multiple nodes, each node via its own
 * thread, and competes to advance the original request.  The original
 * request is retired the moment all requirements are met, even if the
 * operation is still in-progress on some nodes.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/objcache.h>
#include <sys/event.h>
#include <sys/file.h>
#include <vfs/fifofs/fifo.h>

#include "hammer2.h"

/*
 * Backend for hammer2_vfs_root()
 *
 * This is called when a newly mounted PFS has not yet synchronized
 * to the inode_tid and modify_tid.
 */
void
hammer2_xop_ipcluster(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_ipcluster_t *xop = &arg->xop_ipcluster;
	hammer2_chain_t *chain;
	int error;

	chain = hammer2_inode_chain(xop->head.ip1, clindex,
				    HAMMER2_RESOLVE_ALWAYS |
				    HAMMER2_RESOLVE_SHARED);
	if (chain)
		error = chain->error;
	else
		error = EIO;
		
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain)
		hammer2_chain_drop(chain);
}

/*
 * Backend for hammer2_vop_readdir()
 */
void
hammer2_xop_readdir(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_readdir_t *xop = &arg->xop_readdir;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t lkey;
	int cache_index = -1;
	int error = 0;

	lkey = xop->lkey;
	if (hammer2_debug & 0x0020)
		kprintf("xop_readdir %p lkey=%016jx\n", xop, lkey);

	/*
	 * The inode's chain is the iterator.  If we cannot acquire it our
	 * contribution ends here.
	 */
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_readdir: NULL parent\n");
		goto done;
	}

	/*
	 * Directory scan [re]start and loop, the feed inherits the chain's
	 * lock so do not unlock it on the iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next, lkey, lkey,
				     &cache_index, HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lkey, HAMMER2_KEY_MAX,
					     &cache_index,
					     HAMMER2_LOOKUP_SHARED);
	}
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error)
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, HAMMER2_KEY_MAX,
					   &cache_index,
					   HAMMER2_LOOKUP_SHARED |
					   HAMMER2_LOOKUP_NOUNLOCK);
	}
	if (chain)
		hammer2_chain_drop(chain);
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
}

/*
 * Backend for hammer2_vop_nresolve()
 */
void
hammer2_xop_nresolve(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_nresolve_t *xop = &arg->xop_nresolve;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const hammer2_inode_data_t *ripdata;
	const char *name;
	size_t name_len;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int cache_index = -1;	/* XXX */
	int error;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = EIO;
		goto done;
	}
	name = xop->head.name1;
	name_len = xop->head.name1_len;

	/*
	 * Lookup the directory entry
	 */
	lhc = hammer2_dirhash(name, name_len);
	chain = hammer2_chain_lookup(&parent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		ripdata = &chain->data->ipdata;
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    ripdata->meta.name_len == name_len &&
		    bcmp(ripdata->filename, name, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index,
					   HAMMER2_LOOKUP_ALWAYS |
					   HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * If the entry is a hardlink pointer, resolve it.
	 */
	error = 0;
	if (chain) {
		if (chain->data->ipdata.meta.type == HAMMER2_OBJTYPE_HARDLINK) {
			error = hammer2_chain_hardlink_find(
						xop->head.ip1,
						&parent, &chain,
						HAMMER2_RESOLVE_SHARED);
		}
	}
done:
	error = hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain) {
		/* leave lock intact for feed */
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Backend for hammer2_vop_nremove(), hammer2_vop_nrmdir(), and helper
 * for hammer2_vop_nrename().
 *
 * This function locates and removes the directory entry.  If the
 * entry is a hardlink pointer, this function will also remove the
 * hardlink target if the target's nlinks is 1.
 *
 * The frontend is responsible for moving open inodes to the hidden directory
 * and for decrementing nlinks.
 */
void
hammer2_xop_unlink(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_unlink_t *xop = &arg->xop_unlink;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const hammer2_inode_data_t *ripdata;
	const char *name;
	size_t name_len;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int cache_index = -1;	/* XXX */
	int error;

	/*
	 * Requires exclusive lock
	 */
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = EIO;
		goto done;
	}
	name = xop->head.name1;
	name_len = xop->head.name1_len;

	/*
	 * Lookup the directory entry
	 */
	lhc = hammer2_dirhash(name, name_len);
	chain = hammer2_chain_lookup(&parent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS);
	while (chain) {
		ripdata = &chain->data->ipdata;
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    ripdata->meta.name_len == name_len &&
		    bcmp(ripdata->filename, name, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index,
					   HAMMER2_LOOKUP_ALWAYS);
	}

	/*
	 * If the directory entry is a HARDLINK pointer then obtain the
	 * underlying file type for the directory typing tests and delete
	 * the HARDLINK pointer chain permanently.  The frontend is left
	 * responsible for handling nlinks on and deleting the actual inode.
	 *
	 * If the directory entry is the actual inode then use its type
	 * for the directory typing tests and delete the chain, permanency
	 * depends on whether the inode is open or not.
	 *
	 * Check directory typing and delete the entry.  Note that
	 * nlinks adjustments are made on the real inode by the frontend,
	 * not here.
	 */
	error = 0;
	if (chain) {
		int dopermanent = xop->dopermanent;
		uint8_t type;

		type = chain->data->ipdata.meta.type;
		if (type == HAMMER2_OBJTYPE_HARDLINK) {
			type = chain->data->ipdata.meta.target_type;
			dopermanent |= HAMMER2_DELETE_PERMANENT;
		}

		if (type == HAMMER2_OBJTYPE_DIRECTORY &&
		    xop->isdir == 0) {
			error = ENOTDIR;
		} else 
		if (type != HAMMER2_OBJTYPE_DIRECTORY &&
		    xop->isdir >= 1) {
			error = EISDIR;
		} else {
			/*
			 * This deletes the directory entry itself, which is
			 * also the inode when nlinks == 1.  Hardlink targets
			 * are handled in the next conditional.
			 */
			hammer2_chain_delete(parent, chain,
					     xop->head.mtid, dopermanent);
		}
	}

	/*
	 * If the entry is a hardlink pointer, resolve it.  If this is the
	 * last link, delete it.  The frontend has the master copy of nlinks
	 * but we still have to make adjustments here to synchronize with it.
	 *
	 * On delete / adjust nlinks if there is no error.  But we still need
	 * to resolve the hardlink to feed the inode's real chain back to
	 * the frontend.
	 *
	 * XXX we are basically tracking the nlinks count by doing a delta
	 *     adjustment instead of having the frontend pass the absolute
	 *     value down.  We really need to have the frontend pass the
	 *     absolute value down (difficult because there might not be
	 *     an 'ip').  See also hammer2_xop_nlink().
	 */
	if (chain &&
	    chain->data->ipdata.meta.type == HAMMER2_OBJTYPE_HARDLINK) {
		int error2;

		error2 = hammer2_chain_hardlink_find(xop->head.ip1,
						     &parent, &chain, 0);
		if (chain && error == 0 && error2 == 0 &&
		    (int64_t)chain->data->ipdata.meta.nlinks <= 1) {
			hammer2_chain_delete(parent, chain,
					     xop->head.mtid,
					     xop->dopermanent);
		} else if (chain && error == 0 && error2 == 0) {
			hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
			--chain->data->ipdata.meta.nlinks;
		}
		if (error == 0)
			error = error2;
	}

	/*
	 * Chains passed to feed are expected to be locked shared.
	 */
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
					  HAMMER2_RESOLVE_SHARED);
	}

	/*
	 * We always return the hardlink target (the real inode) for
	 * further action.
	 */
done:
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain)
		hammer2_chain_drop(chain);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Backend for hammer2_vop_nlink() and hammer2_vop_nrename()
 *
 * ip1 - fdip
 * ip2 - ip
 * ip3 - cdip
 *
 * If a hardlink pointer:
 *	The existing hardlink target {fdip,ip} must be moved to another
 *	directory {cdip,ip}
 *
 * If not a hardlink pointer:
 *	Convert the target {fdip,ip} to a hardlink target {cdip,ip} and
 *	replace the original namespace {fdip,name} with a hardlink pointer.
 */
void
hammer2_xop_nlink(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_nlink_t *xop = &arg->xop_nlink;
	hammer2_pfs_t *pmp;
	hammer2_inode_data_t *wipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *tmp;
	hammer2_inode_t *ip;
	hammer2_key_t key_dummy;
	int cache_index = -1;
	int error;
	int did_delete = 0;

	/*
	 * We need the precise parent chain to issue the deletion.
	 */
	ip = xop->head.ip2;
	pmp = ip->pmp;
	parent = hammer2_inode_chain(ip, clindex, HAMMER2_RESOLVE_ALWAYS);
	if (parent)
		hammer2_chain_getparent(&parent, HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		chain = NULL;
		error = EIO;
		goto done;
	}
	chain = hammer2_inode_chain(ip, clindex, HAMMER2_RESOLVE_ALWAYS);
	if (chain == NULL) {
		error = EIO;
		goto done;
	}
	KKASSERT(chain->parent == parent);

	if (chain->data->ipdata.meta.name_key & HAMMER2_DIRHASH_VISIBLE) {
		/*
		 * Delete the original chain and hold onto it for the move
		 * to cdir.
		 *
		 * Replace the namespace with a hardlink pointer if the
		 * chain being moved is not already a hardlink target.
		 */
		hammer2_chain_delete(parent, chain, xop->head.mtid, 0);
		did_delete = 1;

		tmp = NULL;
		error = hammer2_chain_create(&parent, &tmp, pmp,
					     chain->bref.key, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES,
					     xop->head.mtid, 0, 0);
		if (error)
			goto done;
		hammer2_chain_modify(tmp, xop->head.mtid, 0, 0);
		wipdata = &tmp->data->ipdata;
		bzero(wipdata, sizeof(*wipdata));
		wipdata->meta.name_key = chain->data->ipdata.meta.name_key;
		wipdata->meta.name_len = chain->data->ipdata.meta.name_len;
		bcopy(chain->data->ipdata.filename, wipdata->filename,
		      chain->data->ipdata.meta.name_len);
		wipdata->meta.target_type = chain->data->ipdata.meta.type;
		wipdata->meta.type = HAMMER2_OBJTYPE_HARDLINK;
		wipdata->meta.inum = ip->meta.inum;
		wipdata->meta.version = HAMMER2_INODE_VERSION_ONE;
		wipdata->meta.nlinks = 1;
		wipdata->meta.op_flags = HAMMER2_OPFLAG_DIRECTDATA;

		hammer2_chain_unlock(tmp);
		hammer2_chain_drop(tmp);
	} else if (xop->head.ip1 != xop->head.ip3) {
		/*
		 * Delete the hardlink target so it can be moved
		 * to cdir.
		 */
		hammer2_chain_delete(parent, chain, xop->head.mtid, 0);
		did_delete = 1;
	} else {
		/*
		 * Deletion not necessary (just a nlinks update).
		 */
		did_delete = 0;
	}

	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
	parent = NULL;

	/*
	 * Ok, back to the deleted chain.  We must reconnect this chain
	 * as a hardlink target to cdir (ip3).
	 *
	 * WARNING! Frontend assumes filename length is 18 bytes.
	 */
	if (did_delete) {
		hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		wipdata = &chain->data->ipdata;
		ksnprintf(wipdata->filename, sizeof(wipdata->filename),
			  "0x%016jx", (intmax_t)ip->meta.inum);
		wipdata->meta.name_len = strlen(wipdata->filename);
		wipdata->meta.name_key = ip->meta.inum;

		/*
		 * We must seek parent properly for the create to reattach
		 * chain.  XXX just use chain->parent or
		 * inode_chain_and_parent() ?
		 */
		parent = hammer2_inode_chain(xop->head.ip3, clindex,
					     HAMMER2_RESOLVE_ALWAYS);
		if (parent == NULL) {
			error = EIO;
			goto done;
		}
		tmp = hammer2_chain_lookup(&parent, &key_dummy,
					   ip->meta.inum, ip->meta.inum,
					   &cache_index, 0);
		if (tmp) {
			hammer2_chain_unlock(tmp);
			hammer2_chain_drop(tmp);
			error = EEXIST;
			goto done;
		}
		error = hammer2_chain_create(&parent, &chain, pmp,
					     wipdata->meta.name_key, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES,
					     xop->head.mtid, 0, 0);
	} else {
		error = 0;
	}

	/*
	 * Bump nlinks to synchronize with frontend.
	 */
	if (xop->nlinks_delta) {
		hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		chain->data->ipdata.meta.nlinks += xop->nlinks_delta;
	}

	/*
	 * To avoid having to scan the collision space we can simply
	 * reuse the inode's original name_key.  But ip->meta.name_key
	 * may have already been updated by the front-end, so use xop->lhc.
	 *
	 * (frontend is responsible for fixing up ip->pip).
	 */
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Backend for hammer2_vop_nrename()
 *
 * This handles the final step of renaming, either renaming the
 * actual inode or renaming the hardlink pointer.
 */
void
hammer2_xop_nrename(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_nrename_t *xop = &arg->xop_nrename;
	hammer2_pfs_t *pmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *tmp;
	hammer2_inode_t *ip;
	hammer2_key_t key_dummy;
	int cache_index = -1;
	int error;

	/*
	 * We need the precise parent chain to issue the deletion.
	 *
	 * If this is not a hardlink target we can act on the inode,
	 * otherwise we have to locate the hardlink pointer.
	 */
	ip = xop->head.ip2;
	pmp = ip->pmp;
	chain = NULL;

	if (xop->ip_key & HAMMER2_DIRHASH_VISIBLE) {
		/*
		 * Find ip's direct parent chain.
		 */
		parent = hammer2_inode_chain(ip, clindex,
					     HAMMER2_RESOLVE_ALWAYS);
		if (parent)
			hammer2_chain_getparent(&parent,
						HAMMER2_RESOLVE_ALWAYS);
		if (parent == NULL) {
			error = EIO;
			goto done;
		}
		chain = hammer2_inode_chain(ip, clindex,
					    HAMMER2_RESOLVE_ALWAYS);
		if (chain == NULL) {
			error = EIO;
			goto done;
		}
	} else {
		/*
		 * The hardlink pointer for the head.ip1 hardlink target
		 * is in fdip, do a namespace search.
		 */
		const hammer2_inode_data_t *ripdata;
		hammer2_key_t lhc;
		hammer2_key_t key_next;
		const char *name;
		size_t name_len;

		parent = hammer2_inode_chain(xop->head.ip1, clindex,
					     HAMMER2_RESOLVE_ALWAYS);
		if (parent == NULL) {
			kprintf("xop_nrename: NULL parent\n");
			error = EIO;
			goto done;
		}
		name = xop->head.name1;
		name_len = xop->head.name1_len;

		/*
		 * Lookup the directory entry
		 */
		lhc = hammer2_dirhash(name, name_len);
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					     &cache_index,
					     HAMMER2_LOOKUP_ALWAYS);
		while (chain) {
			ripdata = &chain->data->ipdata;
			if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
			    ripdata->meta.name_len == name_len &&
			    bcmp(ripdata->filename, name, name_len) == 0) {
				break;
			}
			chain = hammer2_chain_next(&parent, chain, &key_next,
						   key_next,
						   lhc + HAMMER2_DIRHASH_LOMASK,
						   &cache_index,
						   HAMMER2_LOOKUP_ALWAYS);
		}
	}

	if (chain == NULL) {
		/* XXX shouldn't happen, but does under fsstress */
		kprintf("hammer2_xop_rename: \"%s\" -> \"%s\"  ENOENT\n",
			xop->head.name1,
			xop->head.name2);
		error = ENOENT;
		goto done;
	}

	/*
	 * Delete it, then create it in the new namespace.
	 */
	hammer2_chain_delete(parent, chain, xop->head.mtid, 0);
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
	parent = NULL;		/* safety */

	/*
	 * Ok, back to the deleted chain.  We must reconnect this chain
	 * to tdir (ip3).  The chain (a real inode or a hardlink pointer)
	 * is not otherwise modified.
	 *
	 * Frontend is expected to replicate the same inode meta data
	 * modifications.
	 *
	 * NOTE!  This chain may not represent the actual inode, it
	 *	  can be a hardlink pointer.
	 *
	 * XXX in-inode parent directory specification?
	 */
	if (chain->data->ipdata.meta.name_key != xop->lhc ||
	    xop->head.name1_len != xop->head.name2_len ||
	    bcmp(xop->head.name1, xop->head.name2, xop->head.name1_len) != 0) {
		hammer2_inode_data_t *wipdata;

		hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		wipdata = &chain->data->ipdata;

		bzero(wipdata->filename, sizeof(wipdata->filename));
		bcopy(xop->head.name2, wipdata->filename, xop->head.name2_len);
		wipdata->meta.name_key = xop->lhc;
		wipdata->meta.name_len = xop->head.name2_len;
	}

	/*
	 * We must seek parent properly for the create.
	 */
	parent = hammer2_inode_chain(xop->head.ip3, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = EIO;
		goto done;
	}
	tmp = hammer2_chain_lookup(&parent, &key_dummy,
				   xop->lhc, xop->lhc,
				   &cache_index, 0);
	if (tmp) {
		hammer2_chain_unlock(tmp);
		hammer2_chain_drop(tmp);
		error = EEXIST;
		goto done;
	}

	error = hammer2_chain_create(&parent, &chain, pmp,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->head.mtid, 0, 0);
	/*
	 * (frontend is responsible for fixing up ip->pip).
	 */
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Directory collision resolver scan helper (backend, threaded).
 *
 * Used by the inode create code to locate an unused lhc.
 */
void
hammer2_xop_scanlhc(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;	/* XXX */
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = EIO;
		goto done;
	}

	/*
	 * Lookup all possibly conflicting directory entries, the feed
	 * inherits the chain's lock so do not unlock it on the iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc,
				     xop->lhc + HAMMER2_DIRHASH_LOMASK,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex,
					 chain->error);
		if (error) {
			hammer2_chain_drop(chain);
			chain = NULL;	/* safety */
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   xop->lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index,
					   HAMMER2_LOOKUP_ALWAYS |
					   HAMMER2_LOOKUP_SHARED |
					   HAMMER2_LOOKUP_NOUNLOCK);
	}
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Generic lookup of a specific key.
 *
 * Used by the inode hidden directory code to find the hidden directory.
 */
void
hammer2_xop_lookup(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;	/* XXX */
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	chain = NULL;
	if (parent == NULL) {
		error = EIO;
		goto done;
	}

	/*
	 * Lookup all possibly conflicting directory entries, the feed
	 * inherits the chain's lock so do not unlock it on the iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	if (chain)
		hammer2_xop_feed(&xop->head, chain, clindex, chain->error);
	else
		hammer2_xop_feed(&xop->head, NULL, clindex, ENOENT);

done:
	if (chain) {
		/* leave lock intact for feed */
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Generic scan
 */
void
hammer2_xop_scanall(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_scanall_t *xop = &arg->xop_scanall;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;
	int error = 0;

	/*
	 * The inode's chain is the iterator.  If we cannot acquire it our
	 * contribution ends here.
	 */
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_readdir: NULL parent\n");
		goto done;
	}

	/*
	 * Generic scan of exact records.  Note that indirect blocks are
	 * automatically recursed and will not be returned.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->key_beg, xop->key_end,
				     &cache_index, HAMMER2_LOOKUP_SHARED |
						   HAMMER2_LOOKUP_NODIRECT);
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error)
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, xop->key_end,
					   &cache_index,
					   HAMMER2_LOOKUP_SHARED |
					   HAMMER2_LOOKUP_NODIRECT |
					   HAMMER2_LOOKUP_NOUNLOCK);
	}
	if (chain)
		hammer2_chain_drop(chain);
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
}
