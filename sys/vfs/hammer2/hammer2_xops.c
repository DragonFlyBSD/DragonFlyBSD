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
 * Determine if the specified directory is empty.  Returns 0 on success.
 *
 * May return 0, ENOTDIR, or EAGAIN.
 */
static
int
checkdirempty(hammer2_chain_t *oparent, hammer2_chain_t *ochain, int clindex)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t inum;
	int cache_index = -1;
	int error;

	error = 0;
	chain = hammer2_chain_lookup_init(ochain, 0);

	if (chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
		if (oparent)
			hammer2_chain_unlock(oparent);
		inum = chain->bref.embed.dirent.inum;
		parent = NULL;
		error = hammer2_chain_inode_find(chain->pmp, inum,
						 clindex, 0,
						 &parent, &chain);
		if (parent) {
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
		}
		if (oparent) {
			hammer2_chain_lock(oparent, HAMMER2_RESOLVE_ALWAYS);
			if (ochain->parent != oparent) {
				if (chain) {
					hammer2_chain_unlock(chain);
					hammer2_chain_drop(chain);
				}
				kprintf("H2EAGAIN\n");

				return EAGAIN;
			}
		}
	}

	/*
	 * Determine if the directory is empty or not by checking its
	 * visible namespace (the area which contains directory entries).
	 */
	parent = chain;
	chain = NULL;
	if (parent) {
		chain = hammer2_chain_lookup(&parent, &key_next,
					     HAMMER2_DIRHASH_VISIBLE,
					     HAMMER2_KEY_MAX,
					     &cache_index, 0);
	}
	if (chain) {
		error = ENOTEMPTY;
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	} else {
		error = 0;
	}
	hammer2_chain_lookup_done(parent);

	return error;
}

/*
 * Backend for hammer2_vfs_root()
 *
 * This is called when a newly mounted PFS has not yet synchronized
 * to the inode_tid and modify_tid.
 */
void
hammer2_xop_ipcluster(hammer2_thread_t *thr, hammer2_xop_t *arg)
{
	hammer2_xop_ipcluster_t *xop = &arg->xop_ipcluster;
	hammer2_chain_t *chain;
	int error;

	chain = hammer2_inode_chain(xop->head.ip1, thr->clindex,
				    HAMMER2_RESOLVE_ALWAYS |
				    HAMMER2_RESOLVE_SHARED);
	if (chain)
		error = chain->error;
	else
		error = EIO;
		
	hammer2_xop_feed(&xop->head, chain, thr->clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Backend for hammer2_vop_readdir()
 */
void
hammer2_xop_readdir(hammer2_thread_t *thr, hammer2_xop_t *arg)
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
	parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
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
		error = hammer2_xop_feed(&xop->head, chain, thr->clindex, 0);
		if (error)
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, HAMMER2_KEY_MAX,
					   &cache_index,
					   HAMMER2_LOOKUP_SHARED);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, thr->clindex, error);
}

/*
 * Backend for hammer2_vop_nresolve()
 */
void
hammer2_xop_nresolve(hammer2_thread_t *thr, hammer2_xop_t *arg)
{
	hammer2_xop_nresolve_t *xop = &arg->xop_nresolve;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const char *name;
	size_t name_len;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int cache_index = -1;	/* XXX */
	int error;

	parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
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
		if (hammer2_chain_dirent_test(chain, name, name_len))
			break;
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
		if (chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
			lhc = chain->bref.embed.dirent.inum;
			error = hammer2_chain_inode_find(chain->pmp,
							 lhc,
							 thr->clindex,
							 HAMMER2_LOOKUP_SHARED,
							 &parent,
							 &chain);
		}
	}
done:
	error = hammer2_xop_feed(&xop->head, chain, thr->clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Backend for hammer2_vop_nremove(), hammer2_vop_nrmdir(), helper
 * for hammer2_vop_nrename(), and backend for pfs_delete.
 *
 * This function locates and removes a directory entry, and will lookup
 * and return the underlying inode.  For directory entries the underlying
 * inode is not removed.  If the directory entry is the actual inode itself,
 * it may be conditonally removed and returned.
 *
 * WARNING!  Any target inode's nlinks may not be synchronized to the
 *	     in-memory inode.  hammer2_inode_unlink_finisher() is
 *	     responsible for the final disposition of the actual inode.
 *
 * The frontend is responsible for moving open-but-deleted inodes to the
 * mount's hidden directory and for decrementing nlinks.
 */
void
hammer2_xop_unlink(hammer2_thread_t *thr, hammer2_xop_t *arg)
{
	hammer2_xop_unlink_t *xop = &arg->xop_unlink;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const char *name;
	size_t name_len;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int cache_index = -1;	/* XXX */
	int error;

again:
	/*
	 * Requires exclusive lock
	 */
	parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	chain = NULL;
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
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
		if (hammer2_chain_dirent_test(chain, name, name_len))
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index,
					   HAMMER2_LOOKUP_ALWAYS);
	}

	/*
	 * The directory entry will either be a BREF_TYPE_DIRENT or a
	 * BREF_TYPE_INODE.  We always permanently delete DIRENTs, but
	 * must go by xop->dopermanent for BREF_TYPE_INODE.
	 *
	 * Note that the target chain's nlinks may not be synchronized with
	 * the in-memory hammer2_inode_t structure, so we don't try to do
	 * anything fancy here.
	 */
	error = 0;
	if (chain) {
		int dopermanent = xop->dopermanent & 1;
		int doforce = xop->dopermanent & 2;
		uint8_t type;

		/*
		 * If the directory entry is the actual inode then use its
		 * type for the directory typing tests, otherwise if it is
		 * a directory entry, pull the type field from the entry.
		 *
		 * Directory entries are always permanently deleted
		 * (because they aren't the actual inode).
		 */
		if (chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
			type = chain->bref.embed.dirent.type;
			dopermanent |= HAMMER2_DELETE_PERMANENT;
		} else {
			type = chain->data->ipdata.meta.type;
		}

		/*
		 * Check directory typing and delete the entry.  Note that
		 * nlinks adjustments are made on the real inode by the
		 * frontend, not here.
		 *
		 * Unfortunately, checkdirempty() may have to unlock (parent).
		 * If it no longer matches chain->parent after re-locking,
		 * EAGAIN is returned.
		 */
		if (type == HAMMER2_OBJTYPE_DIRECTORY && doforce) {
			/*
			 * If doforce then execute the operation even if
			 * the directory is not empty.
			 */
			error = chain->error;
			hammer2_chain_delete(parent, chain,
					     xop->head.mtid, dopermanent);
		} else if (type == HAMMER2_OBJTYPE_DIRECTORY &&
			   (error = checkdirempty(parent, chain, thr->clindex)) != 0) {
			/*
			 * error may be EAGAIN or ENOTEMPTY
			 */
			if (error == EAGAIN) {
				hammer2_chain_unlock(chain);
				hammer2_chain_drop(chain);
				hammer2_chain_unlock(parent);
				hammer2_chain_drop(parent);
				goto again;
			}
		} else if (type == HAMMER2_OBJTYPE_DIRECTORY &&
		    xop->isdir == 0) {
			error = ENOTDIR;
		} else if (type != HAMMER2_OBJTYPE_DIRECTORY &&
			   xop->isdir >= 1) {
			error = EISDIR;
		} else {
			/*
			 * Delete the directory entry.  chain might also
			 * be a directly-embedded inode.
			 */
			error = chain->error;
			hammer2_chain_delete(parent, chain,
					     xop->head.mtid, dopermanent);
		}
	}

	/*
	 * If chain is a directory entry we must resolve it.  We do not try
	 * to manipulate the contents as it might not be synchronized with
	 * the frontend hammer2_inode_t, nor do we try to lookup the
	 * frontend hammer2_inode_t here (we are the backend!).
	 */
	if (chain && chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
		int error2;

		lhc = chain->bref.embed.dirent.inum;

		error2 = hammer2_chain_inode_find(chain->pmp, lhc,
						  thr->clindex, 0,
						  &parent, &chain);
		if (error2) {
			kprintf("inode_find: %016jx %p failed\n",
				lhc, chain);
			error2 = 0;	/* silently ignore */
		}
		if (error == 0)
			error = error2;
	}

	/*
	 * Return the inode target for further action.  Typically used by
	 * hammer2_inode_unlink_finisher().
	 */
done:
	hammer2_xop_feed(&xop->head, chain, thr->clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		chain = NULL;
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
		parent = NULL;
	}
}

/*
 * Backend for hammer2_vop_nrename()
 *
 * This handles the final step of renaming, either renaming the
 * actual inode or renaming the directory entry.
 */
void
hammer2_xop_nrename(hammer2_thread_t *thr, hammer2_xop_t *arg)
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
	 * If this is a directory entry we must locate the underlying
	 * inode.  If it is an embedded inode we can act directly on it.
	 */
	ip = xop->head.ip2;
	pmp = ip->pmp;
	chain = NULL;

	if (xop->ip_key & HAMMER2_DIRHASH_VISIBLE) {
		/*
		 * Find ip's direct parent chain.
		 */
		parent = hammer2_inode_chain(ip, thr->clindex,
					     HAMMER2_RESOLVE_ALWAYS);
		if (parent)
			hammer2_chain_getparent(&parent,
						HAMMER2_RESOLVE_ALWAYS);
		if (parent == NULL) {
			error = EIO;
			goto done;
		}
		chain = hammer2_inode_chain(ip, thr->clindex,
					    HAMMER2_RESOLVE_ALWAYS);
		if (chain == NULL) {
			error = EIO;
			goto done;
		}
	} else {
		/*
		 * The directory entry for the head.ip1 inode
		 * is in fdip, do a namespace search.
		 */
		hammer2_key_t lhc;
		hammer2_key_t key_next;
		const char *name;
		size_t name_len;

		parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
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
			if (hammer2_chain_dirent_test(chain, name, name_len))
				break;
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
	 * to tdir (ip3) and adjust the filename.  The chain (a real inode
	 * or a directory entry) is not otherwise modified.
	 *
	 * The frontend is expected to replicate the same inode meta data
	 * modifications if necessary.
	 */
	if (chain->bref.key != xop->lhc ||
	    xop->head.name1_len != xop->head.name2_len ||
	    bcmp(xop->head.name1, xop->head.name2, xop->head.name1_len) != 0) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			hammer2_inode_data_t *wipdata;

			hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
			wipdata = &chain->data->ipdata;

			bzero(wipdata->filename, sizeof(wipdata->filename));
			bcopy(xop->head.name2, wipdata->filename,
			      xop->head.name2_len);
			wipdata->meta.name_key = xop->lhc;
			wipdata->meta.name_len = xop->head.name2_len;
		}
		if (chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
			if (xop->head.name2_len <= sizeof(chain->bref.check.buf)) {
				hammer2_chain_resize(chain, xop->head.mtid, 0,
						     0, 0);
				hammer2_chain_modify(chain, xop->head.mtid,
						     0, 0);
				bzero(chain->bref.check.buf,
				      sizeof(chain->bref.check.buf));
				bcopy(xop->head.name2, chain->bref.check.buf,
				      xop->head.name2_len);
			} else {
				hammer2_chain_resize(chain, xop->head.mtid, 0,
				     hammer2_getradix(HAMMER2_ALLOC_MIN), 0);
				hammer2_chain_modify(chain, xop->head.mtid,
						     0, 0);
				bzero(chain->data->buf,
				      sizeof(chain->data->buf));
				bcopy(xop->head.name2, chain->data->buf,
				      xop->head.name2_len);
			}
			chain->bref.embed.dirent.namlen = xop->head.name2_len;
		}
	}

	/*
	 * If an embedded inode, adjust iparent directly.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
	    chain->data->ipdata.meta.iparent != xop->head.ip3->meta.inum) {
		hammer2_inode_data_t *wipdata;

		hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		wipdata = &chain->data->ipdata;

		wipdata->meta.iparent = xop->head.ip3->meta.inum;
	}

	/*
	 * We must seek parent properly for the create.
	 */
	parent = hammer2_inode_chain(xop->head.ip3, thr->clindex,
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

	error = hammer2_chain_create(&parent, &chain,
				     pmp, HAMMER2_METH_DEFAULT,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->head.mtid, 0, 0);
done:
	hammer2_xop_feed(&xop->head, NULL, thr->clindex, error);
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
hammer2_xop_scanlhc(hammer2_thread_t *thr, hammer2_xop_t *arg)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;	/* XXX */
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
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
		error = hammer2_xop_feed(&xop->head, chain, thr->clindex,
					 chain->error);
		if (error) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
			chain = NULL;	/* safety */
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   xop->lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index,
					   HAMMER2_LOOKUP_ALWAYS |
					   HAMMER2_LOOKUP_SHARED);
	}
done:
	hammer2_xop_feed(&xop->head, NULL, thr->clindex, error);
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
hammer2_xop_lookup(hammer2_thread_t *thr, hammer2_xop_t *arg)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;	/* XXX */
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
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
		hammer2_xop_feed(&xop->head, chain, thr->clindex, chain->error);
	else
		hammer2_xop_feed(&xop->head, NULL, thr->clindex, ENOENT);

done:
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Generic scan
 *
 * WARNING! Fed chains must be locked shared so ownership can be transfered
 *	    and to prevent frontend/backend stalls that would occur with an
 *	    exclusive lock.  The shared lock also allows chain->data to be
 *	    retained.
 */
void
hammer2_xop_scanall(hammer2_thread_t *thr, hammer2_xop_t *arg)
{
	hammer2_xop_scanall_t *xop = &arg->xop_scanall;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;
	int error = 0;

	/*
	 * Assert required flags.
	 */
	KKASSERT(xop->resolve_flags & HAMMER2_RESOLVE_SHARED);
	KKASSERT(xop->lookup_flags & HAMMER2_LOOKUP_SHARED);

	/*
	 * The inode's chain is the iterator.  If we cannot acquire it our
	 * contribution ends here.
	 */
	parent = hammer2_inode_chain(xop->head.ip1, thr->clindex,
				     xop->resolve_flags);
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
				     &cache_index, xop->lookup_flags);
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, thr->clindex, 0);
		if (error)
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, xop->key_end,
					   &cache_index, xop->lookup_flags);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, thr->clindex, error);
}
