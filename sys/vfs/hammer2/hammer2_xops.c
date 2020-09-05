/*
 * Copyright (c) 2011-2018 The DragonFly Project.  All rights reserved.
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
 * Determine if the specified directory is empty.
 *
 *	Returns 0 on success.
 *
 *	Returns HAMMER_ERROR_EAGAIN if caller must re-lookup the entry and
 *	retry. (occurs if we race a ripup on oparent or ochain).
 *
 *	Or returns a permanent HAMMER2_ERROR_* error mask.
 *
 * The caller must pass in an exclusively locked oparent and ochain.  This
 * function will handle the case where the chain is a directory entry or
 * the inode itself.  The original oparent,ochain will be locked upon return.
 *
 * This function will unlock the underlying oparent,ochain temporarily when
 * doing an inode lookup to avoid deadlocks.  The caller MUST handle the EAGAIN
 * result as this means that oparent is no longer the parent of ochain, or
 * that ochain was destroyed while it was unlocked.
 */
static
int
checkdirempty(hammer2_chain_t *oparent, hammer2_chain_t *ochain, int clindex)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t inum;
	int error;
	int didunlock;

	error = 0;
	didunlock = 0;

	/*
	 * Find the inode, set it up as a locked 'chain'.  ochain can be the
	 * inode itself, or it can be a directory entry.
	 */
	if (ochain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
		inum = ochain->bref.embed.dirent.inum;
		hammer2_chain_unlock(ochain);
		hammer2_chain_unlock(oparent);

		parent = NULL;
		chain = NULL;
		error = hammer2_chain_inode_find(ochain->pmp, inum,
						 clindex, 0,
						 &parent, &chain);
		if (parent) {
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
		}
		didunlock = 1;
	} else {
		/*
		 * The directory entry *is* the directory inode
		 */
		chain = hammer2_chain_lookup_init(ochain, 0);
	}

	/*
	 * Determine if the directory is empty or not by checking its
	 * visible namespace (the area which contains directory entries).
	 */
	if (error == 0) {
		parent = chain;
		chain = NULL;
		if (parent) {
			chain = hammer2_chain_lookup(&parent, &key_next,
						     HAMMER2_DIRHASH_VISIBLE,
						     HAMMER2_KEY_MAX,
						     &error, 0);
		}
		if (chain) {
			error = HAMMER2_ERROR_ENOTEMPTY;
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
		}
		hammer2_chain_lookup_done(parent);
	}

	if (didunlock) {
		hammer2_chain_lock(oparent, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_lock(ochain, HAMMER2_RESOLVE_ALWAYS);
		if ((ochain->flags & HAMMER2_CHAIN_DELETED) ||
		    (oparent->flags & HAMMER2_CHAIN_DELETED) ||
		    ochain->parent != oparent) {
			kprintf("hammer2: debug: CHECKDIR inum %jd RETRY\n",
				inum);
			error = HAMMER2_ERROR_EAGAIN;
		}
	}
	return error;
}

/*
 * Backend for hammer2_vfs_root()
 *
 * This is called when a newly mounted PFS has not yet synchronized
 * to the inode_tid and modify_tid.
 */
void
hammer2_xop_ipcluster(hammer2_xop_t *arg, void *scratch, int clindex)
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
		error = HAMMER2_ERROR_EIO;
		
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Backend for hammer2_vop_readdir()
 */
void
hammer2_xop_readdir(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_readdir_t *xop = &arg->xop_readdir;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t lkey;
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
				     &error, HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lkey, HAMMER2_KEY_MAX,
					     &error, HAMMER2_LOOKUP_SHARED);
	}
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error)
			goto break2;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, HAMMER2_KEY_MAX,
					   &error, HAMMER2_LOOKUP_SHARED);
	}
break2:
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
}

/*
 * Backend for hammer2_vop_nresolve()
 */
void
hammer2_xop_nresolve(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_nresolve_t *xop = &arg->xop_nresolve;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const char *name;
	size_t name_len;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int error;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = HAMMER2_ERROR_EIO;
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
				     &error,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		if (hammer2_chain_dirent_test(chain, name, name_len))
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &error,
					   HAMMER2_LOOKUP_ALWAYS |
					   HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * Locate the target inode for a directory entry
	 */
	if (chain && chain->error == 0) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
			lhc = chain->bref.embed.dirent.inum;
			error = hammer2_chain_inode_find(chain->pmp,
							 lhc,
							 clindex,
							 HAMMER2_LOOKUP_SHARED,
							 &parent,
							 &chain);
		}
	} else if (chain && error == 0) {
		error = chain->error;
	}
done:
	error = hammer2_xop_feed(&xop->head, chain, clindex, error);
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
 * Backend for hammer2_vop_nremove(), hammer2_vop_nrmdir(), and
 * backend for pfs_delete.
 *
 * This function locates and removes a directory entry, and will lookup
 * and return the underlying inode.  For directory entries the underlying
 * inode is not removed.  If the directory entry is the actual inode itself,
 * it may be conditonally removed and returned.
 *
 * WARNING!  Any target inode's nlinks may not be synchronized to the
 *	     in-memory inode.  The frontend's hammer2_inode_unlink_finisher()
 *	     is responsible for the final disposition of the actual inode.
 */
void
hammer2_xop_unlink(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_unlink_t *xop = &arg->xop_unlink;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const char *name;
	size_t name_len;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int error;

again:
	/*
	 * Requires exclusive lock
	 */
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	chain = NULL;
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		error = HAMMER2_ERROR_EIO;
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
				     &error, HAMMER2_LOOKUP_ALWAYS);
	while (chain) {
		if (hammer2_chain_dirent_test(chain, name, name_len))
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &error, HAMMER2_LOOKUP_ALWAYS);
	}

	/*
	 * The directory entry will either be a BREF_TYPE_DIRENT or a
	 * BREF_TYPE_INODE.  We always permanently delete DIRENTs, but
	 * must go by xop->dopermanent for BREF_TYPE_INODE.
	 *
	 * Note that the target chain's nlinks may not be synchronized with
	 * the in-memory hammer2_inode_t structure, so we don't try to do
	 * anything fancy here.  The frontend deals with nlinks
	 * synchronization.
	 */
	if (chain && chain->error == 0) {
		int dopermanent = xop->dopermanent & H2DOPERM_PERMANENT;
		int doforce = xop->dopermanent & H2DOPERM_FORCE;
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
			 * the directory is not empty or errored.  We
			 * ignore chain->error here, allowing an errored
			 * chain (aka directory entry) to still be deleted.
			 */
			error = hammer2_chain_delete(parent, chain,
					     xop->head.mtid, dopermanent);
		} else if (type == HAMMER2_OBJTYPE_DIRECTORY &&
		    xop->isdir == 0) {
			error = HAMMER2_ERROR_EISDIR;
		} else if (type == HAMMER2_OBJTYPE_DIRECTORY &&
			   (error = checkdirempty(parent, chain, clindex)) != 0) {
			/*
			 * error may be EAGAIN or ENOTEMPTY
			 */
			if (error == HAMMER2_ERROR_EAGAIN) {
				hammer2_chain_unlock(chain);
				hammer2_chain_drop(chain);
				hammer2_chain_unlock(parent);
				hammer2_chain_drop(parent);
				goto again;
			}
		} else if (type != HAMMER2_OBJTYPE_DIRECTORY &&
			   xop->isdir >= 1) {
			error = HAMMER2_ERROR_ENOTDIR;
		} else {
			/*
			 * Delete the directory entry.  chain might also
			 * be a directly-embedded inode.
			 *
			 * Allow the deletion to proceed even if the chain
			 * is errored.  Give priority to error-on-delete over
			 * chain->error.
			 */
			error = hammer2_chain_delete(parent, chain,
						     xop->head.mtid,
						     dopermanent);
			if (error == 0)
				error = chain->error;
		}
	} else {
		if (chain && error == 0)
			error = chain->error;
	}

	/*
	 * If chain is a directory entry we must resolve it.  We do not try
	 * to manipulate the contents as it might not be synchronized with
	 * the frontend hammer2_inode_t, nor do we try to lookup the
	 * frontend hammer2_inode_t here (we are the backend!).
	 */
	if (chain && chain->bref.type == HAMMER2_BREF_TYPE_DIRENT &&
	    (xop->dopermanent & H2DOPERM_IGNINO) == 0) {
		int error2;

		lhc = chain->bref.embed.dirent.inum;

		error2 = hammer2_chain_inode_find(chain->pmp, lhc,
						  clindex, 0,
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
	hammer2_xop_feed(&xop->head, chain, clindex, error);
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
 * This handles the backend rename operation.  Typically this renames
 * directory entries but can also be used to rename embedded inodes.
 *
 * NOTE! The frontend is responsible for updating the inode meta-data in
 *	 the file being renamed and for decrementing the target-replaced
 *	 inode's nlinks, if present.
 */
void
hammer2_xop_nrename(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_nrename_t *xop = &arg->xop_nrename;
	hammer2_pfs_t *pmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *tmp;
	hammer2_inode_t *ip;
	hammer2_key_t key_next;
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
	error = 0;

	if (xop->ip_key & HAMMER2_DIRHASH_VISIBLE) {
		/*
		 * Find ip's direct parent chain.
		 */
		chain = hammer2_inode_chain(ip, clindex,
					    HAMMER2_RESOLVE_ALWAYS);
		if (chain == NULL) {
			error = HAMMER2_ERROR_EIO;
			parent = NULL;
			goto done;
		}
		if (ip->flags & HAMMER2_INODE_CREATING) {
			parent = NULL;
		} else {
			parent = hammer2_chain_getparent(chain,
						    HAMMER2_RESOLVE_ALWAYS);
			if (parent == NULL) {
				error = HAMMER2_ERROR_EIO;
				goto done;
			}
		}
	} else {
		/*
		 * The directory entry for the head.ip1 inode
		 * is in fdip, do a namespace search.
		 */
		hammer2_key_t lhc;
		const char *name;
		size_t name_len;

		parent = hammer2_inode_chain(xop->head.ip1, clindex,
					     HAMMER2_RESOLVE_ALWAYS);
		if (parent == NULL) {
			kprintf("xop_nrename: NULL parent\n");
			error = HAMMER2_ERROR_EIO;
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
					     &error, HAMMER2_LOOKUP_ALWAYS);
		while (chain) {
			if (hammer2_chain_dirent_test(chain, name, name_len))
				break;
			chain = hammer2_chain_next(&parent, chain, &key_next,
						   key_next,
						   lhc + HAMMER2_DIRHASH_LOMASK,
						   &error,
						   HAMMER2_LOOKUP_ALWAYS);
		}
	}

	if (chain == NULL) {
		/* XXX shouldn't happen, but does under fsstress */
		kprintf("hammer2_xop_nrename: \"%s\" -> \"%s\"  ENOENT\n",
			xop->head.name1,
			xop->head.name2);
		if (error == 0)
			error = HAMMER2_ERROR_ENOENT;
		goto done;
	}

	if (chain->error) {
		error = chain->error;
		goto done;
	}

	/*
	 * Delete it, then create it in the new namespace.
	 *
	 * An error can occur if the chain being deleted requires
	 * modification and the media is full.
	 */
	error = hammer2_chain_delete(parent, chain, xop->head.mtid, 0);
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
	parent = NULL;		/* safety */
	if (error)
		goto done;

	/*
	 * Adjust fields in the deleted chain appropriate for the rename
	 * operation.
	 *
	 * NOTE! For embedded inodes, the frontend will officially replicate
	 *	 the field adjustments, but we also do it here to maintain
	 *	 consistency in case of a crash.
	 */
	if (chain->bref.key != xop->lhc ||
	    xop->head.name1_len != xop->head.name2_len ||
	    bcmp(xop->head.name1, xop->head.name2, xop->head.name1_len) != 0) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			hammer2_inode_data_t *wipdata;

			error = hammer2_chain_modify(chain, xop->head.mtid,
						     0, 0);
			if (error == 0) {
				wipdata = &chain->data->ipdata;

				bzero(wipdata->filename,
				      sizeof(wipdata->filename));
				bcopy(xop->head.name2,
				      wipdata->filename,
				      xop->head.name2_len);
				wipdata->meta.name_key = xop->lhc;
				wipdata->meta.name_len = xop->head.name2_len;
			}
		}
		if (chain->bref.type == HAMMER2_BREF_TYPE_DIRENT) {
			if (xop->head.name2_len <=
			    sizeof(chain->bref.check.buf)) {
				/*
				 * Remove any related data buffer, we can
				 * embed the filename in the bref itself.
				 */
				error = hammer2_chain_resize(
						chain, xop->head.mtid, 0, 0, 0);
				if (error == 0) {
					error = hammer2_chain_modify(
							chain, xop->head.mtid,
							0, 0);
				}
				if (error == 0) {
					bzero(chain->bref.check.buf,
					      sizeof(chain->bref.check.buf));
					bcopy(xop->head.name2,
					      chain->bref.check.buf,
					      xop->head.name2_len);
				}
			} else {
				/*
				 * Associate a data buffer with the bref.
				 * Zero it for consistency.  Note that the
				 * data buffer is not 64KB so use chain->bytes
				 * instead of sizeof().
				 */
				error = hammer2_chain_resize(
					chain, xop->head.mtid, 0,
					hammer2_getradix(HAMMER2_ALLOC_MIN), 0);
				if (error == 0) {
					error = hammer2_chain_modify(
						    chain, xop->head.mtid,
						    0, 0);
				}
				if (error == 0) {
					bzero(chain->data->buf, chain->bytes);
					bcopy(xop->head.name2,
					      chain->data->buf,
					      xop->head.name2_len);
				}
			}
			if (error == 0) {
				chain->bref.embed.dirent.namlen =
					xop->head.name2_len;
			}
		}
	}

	/*
	 * The frontend will replicate this operation and is the real final
	 * authority, but adjust the inode's iparent field too if the inode
	 * is embedded in the directory.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
	    chain->data->ipdata.meta.iparent != xop->head.ip3->meta.inum) {
		hammer2_inode_data_t *wipdata;

		error = hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		if (error == 0) {
			wipdata = &chain->data->ipdata;
			wipdata->meta.iparent = xop->head.ip3->meta.inum;
		}
	}

	/*
	 * Destroy any matching target(s) before creating the new entry.
	 * This will result in some ping-ponging of the directory key
	 * iterator but that is ok.
	 */
	parent = hammer2_inode_chain(xop->head.ip3, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		goto done;
	}

	/*
	 * Delete all matching directory entries.  That is, get rid of
	 * multiple duplicates if present, as a self-healing mechanism.
	 */
	if (error == 0) {
		tmp = hammer2_chain_lookup(&parent, &key_next,
					   xop->lhc & ~HAMMER2_DIRHASH_LOMASK,
					   xop->lhc | HAMMER2_DIRHASH_LOMASK,
					   &error,
					   HAMMER2_LOOKUP_ALWAYS);
		while (tmp) {
			int e2;
			if (hammer2_chain_dirent_test(tmp, xop->head.name2,
						      xop->head.name2_len)) {
				e2 = hammer2_chain_delete(parent, tmp,
							  xop->head.mtid, 0);
				if (error == 0 && e2)
					error = e2;
			}
			tmp = hammer2_chain_next(&parent, tmp, &key_next,
						 key_next,
						 xop->lhc |
						  HAMMER2_DIRHASH_LOMASK,
						 &error,
						 HAMMER2_LOOKUP_ALWAYS);
		}
	}
	if (error == 0) {
		/*
		 * A relookup is required before the create to properly
		 * position the parent chain.
		 */
		tmp = hammer2_chain_lookup(&parent, &key_next,
					   xop->lhc, xop->lhc,
					   &error, 0);
		KKASSERT(tmp == NULL);
		error = hammer2_chain_create(&parent, &chain, NULL, pmp,
					     HAMMER2_METH_DEFAULT,
					     xop->lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES,
					     xop->head.mtid, 0, 0);
	}
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
hammer2_xop_scanlhc(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = HAMMER2_ERROR_EIO;
		goto done;
	}

	/*
	 * Lookup all possibly conflicting directory entries, the feed
	 * inherits the chain's lock so do not unlock it on the iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc,
				     xop->lhc + HAMMER2_DIRHASH_LOMASK,
				     &error,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
			chain = NULL;	/* safety */
			goto done;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   xop->lhc + HAMMER2_DIRHASH_LOMASK,
					   &error,
					   HAMMER2_LOOKUP_ALWAYS |
					   HAMMER2_LOOKUP_SHARED);
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
 */
void
hammer2_xop_lookup(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	chain = NULL;
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		goto done;
	}

	/*
	 * Lookup all possibly conflicting directory entries, the feed
	 * inherits the chain's lock so do not unlock it on the iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &error,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	if (error == 0) {
		if (chain)
			error = chain->error;
		else
			error = HAMMER2_ERROR_ENOENT;
	}
	hammer2_xop_feed(&xop->head, chain, clindex, error);

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

void
hammer2_xop_delete(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	chain = NULL;
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		goto done;
	}

	/*
	 * Lookup all possibly conflicting directory entries, the feed
	 * inherits the chain's lock so do not unlock it on the iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &error,
				     HAMMER2_LOOKUP_NODATA);
	if (error == 0) {
		if (chain)
			error = chain->error;
		else
			error = HAMMER2_ERROR_ENOENT;
	}
	if (chain) {
		error = hammer2_chain_delete(parent, chain, xop->head.mtid,
					     HAMMER2_DELETE_PERMANENT);
	}
	hammer2_xop_feed(&xop->head, NULL, clindex, error);

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
hammer2_xop_scanall(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_scanall_t *xop = &arg->xop_scanall;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
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
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
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
				     &error, xop->lookup_flags);
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error)
			goto break2;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, xop->key_end,
					   &error, xop->lookup_flags);
	}
break2:
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
}

/************************************************************************
 *			    INODE LAYER XOPS				*
 ************************************************************************
 *
 */
/*
 * Helper to create a directory entry.
 */
void
hammer2_xop_inode_mkdirent(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_mkdirent_t *xop = &arg->xop_mkdirent;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	size_t data_len;
	int error;

	if (hammer2_debug & 0x0001)
		kprintf("dirent_create lhc %016jx clindex %d\n",
			xop->lhc, clindex);

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		chain = NULL;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &error, 0);
	if (chain) {
		error = HAMMER2_ERROR_EEXIST;
		goto fail;
	}

	/*
	 * We may be able to embed the directory entry directly in the
	 * blockref.
	 */
	if (xop->dirent.namlen <= sizeof(chain->bref.check.buf))
		data_len = 0;
	else
		data_len = HAMMER2_ALLOC_MIN;

	error = hammer2_chain_create(&parent, &chain, NULL, xop->head.ip1->pmp,
				     HAMMER2_METH_DEFAULT,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_DIRENT,
				     data_len,
				     xop->head.mtid, 0, 0);
	if (error == 0) {
		/*
		 * WARNING: chain->data->buf is sized to chain->bytes,
		 *	    do not use sizeof(chain->data->buf), which
		 *	    will be much larger.
		 */
		error = hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		if (error == 0) {
			chain->bref.embed.dirent = xop->dirent;
			if (xop->dirent.namlen <= sizeof(chain->bref.check.buf))
				bcopy(xop->head.name1, chain->bref.check.buf,
				      xop->dirent.namlen);
			else
				bcopy(xop->head.name1, chain->data->buf,
				      xop->dirent.namlen);
		}
	}
fail:
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Inode create helper (threaded, backend)
 *
 * Used by ncreate, nmknod, nsymlink, nmkdir.
 * Used by nlink and rename to create HARDLINK pointers.
 *
 * Frontend holds the parent directory ip locked exclusively.  We
 * create the inode and feed the exclusively locked chain to the
 * frontend.
 */
void
hammer2_xop_inode_create(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_create_t *xop = &arg->xop_create;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error;

	if (hammer2_debug & 0x0001)
		kprintf("inode_create lhc %016jx clindex %d\n",
			xop->lhc, clindex);

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		chain = NULL;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &error, 0);
	if (chain) {
		error = HAMMER2_ERROR_EEXIST;
		goto fail;
	}

	error = hammer2_chain_create(&parent, &chain, NULL, xop->head.ip1->pmp,
				     HAMMER2_METH_DEFAULT,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->head.mtid, 0, xop->flags);
	if (error == 0) {
		error = hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		if (error == 0) {
			chain->data->ipdata.meta = xop->meta;
			if (xop->head.name1) {
				bcopy(xop->head.name1,
				      chain->data->ipdata.filename,
				      xop->head.name1_len);
				chain->data->ipdata.meta.name_len =
					xop->head.name1_len;
			}
			chain->data->ipdata.meta.name_key = xop->lhc;
		}
	}
fail:
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Create inode as above but leave it detached from the hierarchy.
 */
void
hammer2_xop_inode_create_det(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_create_t *xop = &arg->xop_create;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *null_parent;
	hammer2_key_t key_next;
	hammer2_inode_t *pip;
	hammer2_inode_t *iroot;
	int error;

	if (hammer2_debug & 0x0001)
		kprintf("inode_create_det lhc %016jx clindex %d\n",
			xop->lhc, clindex);

	pip = xop->head.ip1;
	iroot = pip->pmp->iroot;

	parent = hammer2_inode_chain(iroot, clindex, HAMMER2_RESOLVE_ALWAYS);

	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		chain = NULL;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &error, 0);
	if (chain) {
		error = HAMMER2_ERROR_EEXIST;
		goto fail;
	}

	/*
	 * Create as a detached chain with no parent.  We must specify
	 * methods
	 */
	null_parent = NULL;
	error = hammer2_chain_create(&null_parent, &chain,
				     parent->hmp, pip->pmp,
				     HAMMER2_ENC_COMP(pip->meta.comp_algo) +
				     HAMMER2_ENC_CHECK(pip->meta.check_algo),
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->head.mtid, 0, xop->flags);
	if (error == 0) {
		error = hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		if (error == 0) {
			chain->data->ipdata.meta = xop->meta;
			if (xop->head.name1) {
				bcopy(xop->head.name1,
				      chain->data->ipdata.filename,
				      xop->head.name1_len);
				chain->data->ipdata.meta.name_len =
					xop->head.name1_len;
			}
			chain->data->ipdata.meta.name_key = xop->lhc;
		}
	}
fail:
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Take a detached chain and insert it into the topology
 */
void
hammer2_xop_inode_create_ins(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_create_t *xop = &arg->xop_create;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error;

	if (hammer2_debug & 0x0001)
		kprintf("inode_create_ins lhc %016jx clindex %d\n",
			xop->lhc, clindex);

	/*
	 * (parent) will be the insertion point for inode under iroot
	 */
	parent = hammer2_inode_chain(xop->head.ip1->pmp->iroot, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		chain = NULL;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &error, 0);
	if (chain) {
		error = HAMMER2_ERROR_EEXIST;
		goto fail;
	}

	/*
	 * (chain) is the detached inode that is being inserted
	 */
	chain = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (chain == NULL) {
		error = HAMMER2_ERROR_EIO;
		chain = NULL;
		goto fail;
	}

	/*
	 * This create call will insert the non-NULL chain into parent.
	 * Most of the auxillary fields are ignored since the chain already
	 * exists.
	 */
	error = hammer2_chain_create(&parent, &chain, NULL, xop->head.ip1->pmp,
				     HAMMER2_METH_DEFAULT,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->head.mtid, 0, xop->flags);
#if 0
	if (error == 0) {
		error = hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
		if (error == 0) {
			chain->data->ipdata.meta = xop->meta;
			if (xop->head.name1) {
				bcopy(xop->head.name1,
				      chain->data->ipdata.filename,
				      xop->head.name1_len);
				chain->data->ipdata.meta.name_len =
					xop->head.name1_len;
			}
			chain->data->ipdata.meta.name_key = xop->lhc;
		}
	}
#endif
fail:
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

/*
 * Inode delete helper (backend, threaded)
 *
 * Generally used by hammer2_run_sideq()
 */
void
hammer2_xop_inode_destroy(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_destroy_t *xop = &arg->xop_destroy;
	hammer2_pfs_t *pmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_inode_t *ip;
	int error;

	/*
	 * We need the precise parent chain to issue the deletion.
	 */
	ip = xop->head.ip1;
	pmp = ip->pmp;

	chain = hammer2_inode_chain(ip, clindex, HAMMER2_RESOLVE_ALWAYS);
	if (chain == NULL) {
		parent = NULL;
		error = HAMMER2_ERROR_EIO;
		goto done;
	}

	if (ip->flags & HAMMER2_INODE_CREATING) {
		/*
		 * Inode's chains are not linked into the media topology
		 * because it is a new inode (which is now being destroyed).
		 */
		parent = NULL;
	} else {
		/*
		 * Inode's chains are linked into the media topology
		 */
		parent = hammer2_chain_getparent(chain, HAMMER2_RESOLVE_ALWAYS);
		if (parent == NULL) {
			error = HAMMER2_ERROR_EIO;
			goto done;
		}
	}
	KKASSERT(chain->parent == parent);

	/*
	 * We have the correct parent, we can issue the deletion.
	 */
	hammer2_chain_delete(parent, chain, xop->head.mtid, 0);
	error = 0;
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

void
hammer2_xop_inode_unlinkall(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_unlinkall_t *xop = &arg->xop_unlinkall;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error;

	/*
	 * We need the precise parent chain to issue the deletion.
	 */
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	chain = NULL;
	if (parent == NULL) {
		error = 0;
		goto done;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->key_beg, xop->key_end,
				     &error, HAMMER2_LOOKUP_ALWAYS);
	while (chain) {
		hammer2_chain_delete(parent, chain,
				     xop->head.mtid, HAMMER2_DELETE_PERMANENT);
		hammer2_xop_feed(&xop->head, chain, clindex, chain->error);
		/* depend on function to unlock the shared lock */
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, xop->key_end,
					   &error,
					   HAMMER2_LOOKUP_ALWAYS);
	}
done:
	if (error == 0)
		error = HAMMER2_ERROR_ENOENT;
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

void
hammer2_xop_inode_connect(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_connect_t *xop = &arg->xop_connect;
	hammer2_inode_data_t *wipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_pfs_t *pmp;
	hammer2_key_t key_dummy;
	int error;

	/*
	 * Get directory, then issue a lookup to prime the parent chain
	 * for the create.  The lookup is expected to fail.
	 */
	pmp = xop->head.ip1->pmp;
	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		chain = NULL;
		error = HAMMER2_ERROR_EIO;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_dummy,
				     xop->lhc, xop->lhc,
				     &error, 0);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		chain = NULL;
		error = HAMMER2_ERROR_EEXIST;
		goto fail;
	}
	if (error)
		goto fail;

	/*
	 * Adjust the filename in the inode, set the name key.
	 *
	 * NOTE: Frontend must also adjust ip2->meta on success, we can't
	 *	 do it here.
	 */
	chain = hammer2_inode_chain(xop->head.ip2, clindex,
				    HAMMER2_RESOLVE_ALWAYS);
	error = hammer2_chain_modify(chain, xop->head.mtid, 0, 0);
	if (error)
		goto fail;

	wipdata = &chain->data->ipdata;

	hammer2_inode_modify(xop->head.ip2);
	if (xop->head.name1) {
		bzero(wipdata->filename, sizeof(wipdata->filename));
		bcopy(xop->head.name1, wipdata->filename, xop->head.name1_len);
		wipdata->meta.name_len = xop->head.name1_len;
	}
	wipdata->meta.name_key = xop->lhc;

	/*
	 * Reconnect the chain to the new parent directory
	 */
	error = hammer2_chain_create(&parent, &chain, NULL, pmp,
				     HAMMER2_METH_DEFAULT,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->head.mtid, 0, 0);

	/*
	 * Feed result back.
	 */
fail:
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
 * Synchronize the in-memory inode with the chain.  This does not flush
 * the chain to disk.  Instead, it makes front-end inode changes visible
 * in the chain topology, thus visible to the backend.  This is done in an
 * ad-hoc manner outside of the filesystem vfs_sync, and in a controlled
 * manner inside the vfs_sync.
 */
void
hammer2_xop_inode_chain_sync(hammer2_xop_t *arg, void *scratch, int clindex)
{
	hammer2_xop_fsync_t *xop = &arg->xop_fsync;
	hammer2_chain_t	*parent;
	hammer2_chain_t	*chain;
	int error;

	parent = hammer2_inode_chain(xop->head.ip1, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	chain = NULL;
	if (parent == NULL) {
		error = HAMMER2_ERROR_EIO;
		goto done;
	}
	if (parent->error) {
		error = parent->error;
		goto done;
	}

	error = 0;

	if ((xop->ipflags & HAMMER2_INODE_RESIZED) == 0) {
		/* osize must be ignored */
	} else if (xop->meta.size < xop->osize) {
		/*
		 * We must delete any chains beyond the EOF.  The chain
		 * straddling the EOF will be pending in the bioq.
		 */
		hammer2_key_t lbase;
		hammer2_key_t key_next;

		lbase = (xop->meta.size + HAMMER2_PBUFMASK64) &
			~HAMMER2_PBUFMASK64;
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lbase, HAMMER2_KEY_MAX,
					     &error,
					     HAMMER2_LOOKUP_NODATA |
					     HAMMER2_LOOKUP_NODIRECT);
		while (chain) {
			/*
			 * Degenerate embedded case, nothing to loop on
			 */
			switch (chain->bref.type) {
			case HAMMER2_BREF_TYPE_DIRENT:
			case HAMMER2_BREF_TYPE_INODE:
				KKASSERT(0);
				break;
			case HAMMER2_BREF_TYPE_DATA:
				hammer2_chain_delete(parent, chain,
						     xop->head.mtid,
						     HAMMER2_DELETE_PERMANENT);
				break;
			}
			chain = hammer2_chain_next(&parent, chain, &key_next,
						   key_next, HAMMER2_KEY_MAX,
						   &error,
						   HAMMER2_LOOKUP_NODATA |
						   HAMMER2_LOOKUP_NODIRECT);
		}

		/*
		 * Reset to point at inode for following code, if necessary.
		 */
		if (parent->bref.type != HAMMER2_BREF_TYPE_INODE) {
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
			parent = hammer2_inode_chain(xop->head.ip1,
						     clindex,
						     HAMMER2_RESOLVE_ALWAYS);
			kprintf("hammer2: TRUNCATE RESET on '%s'\n",
				parent->data->ipdata.filename);
		}
	}

	/*
	 * Sync the inode meta-data, potentially clear the blockset area
	 * of direct data so it can be used for blockrefs.
	 */
	if (error == 0) {
		error = hammer2_chain_modify(parent, xop->head.mtid, 0, 0);
		if (error == 0) {
			parent->data->ipdata.meta = xop->meta;
			if (xop->clear_directdata) {
				bzero(&parent->data->ipdata.u.blockset,
				      sizeof(parent->data->ipdata.u.blockset));
			}
		}
	}
done:
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
}
