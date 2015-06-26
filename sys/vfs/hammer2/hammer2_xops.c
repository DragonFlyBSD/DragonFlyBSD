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

	lkey = xop->head.lkey;
	if (hammer2_debug & 0x0020)
		kprintf("xop_readdir %p lkey=%016jx\n", xop, lkey);

	/*
	 * The inode's chain is the iterator.  If we cannot acquire it our
	 * contribution ends here.
	 */
	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_readdir: NULL parent\n");
		goto done;
	}

	/*
	 * Directory scan [re]start and loop.
	 *
	 * We feed the share-locked chain back to the frontend and must be
	 * sure not to unlock it in our iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next, lkey, lkey,
			     &cache_index, HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lkey, (hammer2_key_t)-1,
					     &cache_index,
					     HAMMER2_LOOKUP_SHARED);
	}
	while (chain) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error)
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, (hammer2_key_t)-1,
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

void
hammer2_xop_nresolve(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_nresolve_t *xop = &arg->xop_nresolve;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	const hammer2_inode_data_t *ripdata;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int cache_index = -1;	/* XXX */
	int error;

	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = EIO;
		goto done;
	}

	/*
	 * Lookup the directory entry
	 */
	lhc = hammer2_dirhash(xop->name, xop->name_len);
	chain = hammer2_chain_lookup(&parent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		ripdata = &chain->data->ipdata;
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    ripdata->meta.name_len == xop->name_len &&
		    bcmp(ripdata->filename, xop->name, xop->name_len) == 0) {
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
			error = hammer2_chain_hardlink_find(xop->head.ip,
							    &parent,
							    &chain);
		}
	}
done:
	error = hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain)
		hammer2_chain_drop(chain);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

/*
 * Scan directory collision entries for the specified lhc.  Used by
 * the inode create code to locate an unused lhc.
 */
void
hammer2_inode_xop_scanlhc(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_scanlhc_t *xop = &arg->xop_scanlhc;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;	/* XXX */
	int error = 0;

	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_nresolve: NULL parent\n");
		chain = NULL;
		error = EIO;
		goto done;
	}

	/*
	 * Lookup the directory entry
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
 * Inode create helper.
 *
 * Frontend holds the parent directory ip locked exclusively.  We
 * create the inode and feed the exclusively locked chain to the
 * frontend.
 */
void
hammer2_inode_xop_create(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_create_t *xop = &arg->xop_create;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;
	int error;

	chain = NULL;
	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = EIO;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &cache_index, 0);
	if (chain) {
		hammer2_chain_unlock(chain);
		error = EEXIST;
		goto fail;
	}

	error = hammer2_chain_create(&parent, &chain,
				     xop->head.ip->pmp,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->flags);
	if (error == 0) {
		hammer2_chain_modify(chain, 0);
		chain->data->ipdata.meta = xop->meta;
		bcopy(xop->name, chain->data->ipdata.filename,
		      xop->name_len);
	}
	hammer2_chain_unlock(chain);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
				  HAMMER2_RESOLVE_SHARED);
fail:
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	error = hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain)
		hammer2_chain_drop(chain);
}

/*
 * Inode delete helper
 */
void
hammer2_inode_xop_destroy(hammer2_xop_t *arg, int clindex)
{
	/*hammer2_xop_inode_t *xop = &arg->xop_inode;*/
}
