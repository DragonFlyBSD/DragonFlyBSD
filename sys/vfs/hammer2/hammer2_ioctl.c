/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * Ioctl Functions.
 *
 * WARNING! The ioctl functions which manipulate the connection state need
 *	    to be able to run without deadlock on the volume's chain lock.
 *	    Most of these functions use a separate lock.
 */

#include "hammer2.h"

static int hammer2_ioctl_version_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_recluster(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_scan(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_add(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_del(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_rep(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_socket_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_socket_set(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_lookup(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_create(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_snapshot(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_delete(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_inode_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_inode_set(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_debug_dump(hammer2_inode_t *ip);
//static int hammer2_ioctl_inode_comp_set(hammer2_inode_t *ip, void *data);
//static int hammer2_ioctl_inode_comp_rec_set(hammer2_inode_t *ip, void *data);
//static int hammer2_ioctl_inode_comp_rec_set2(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_bulkfree_scan(hammer2_inode_t *ip, void *data);

int
hammer2_ioctl(hammer2_inode_t *ip, u_long com, void *data, int fflag,
	      struct ucred *cred)
{
	int error;

	/*
	 * Standard root cred checks, will be selectively ignored below
	 * for ioctls that do not require root creds.
	 */
	error = priv_check_cred(cred, PRIV_HAMMER_IOCTL, 0);

	switch(com) {
	case HAMMER2IOC_VERSION_GET:
		error = hammer2_ioctl_version_get(ip, data);
		break;
	case HAMMER2IOC_RECLUSTER:
		if (error == 0)
			error = hammer2_ioctl_recluster(ip, data);
		break;
	case HAMMER2IOC_REMOTE_SCAN:
		if (error == 0)
			error = hammer2_ioctl_remote_scan(ip, data);
		break;
	case HAMMER2IOC_REMOTE_ADD:
		if (error == 0)
			error = hammer2_ioctl_remote_add(ip, data);
		break;
	case HAMMER2IOC_REMOTE_DEL:
		if (error == 0)
			error = hammer2_ioctl_remote_del(ip, data);
		break;
	case HAMMER2IOC_REMOTE_REP:
		if (error == 0)
			error = hammer2_ioctl_remote_rep(ip, data);
		break;
	case HAMMER2IOC_SOCKET_GET:
		if (error == 0)
			error = hammer2_ioctl_socket_get(ip, data);
		break;
	case HAMMER2IOC_SOCKET_SET:
		if (error == 0)
			error = hammer2_ioctl_socket_set(ip, data);
		break;
	case HAMMER2IOC_PFS_GET:
		if (error == 0)
			error = hammer2_ioctl_pfs_get(ip, data);
		break;
	case HAMMER2IOC_PFS_LOOKUP:
		if (error == 0)
			error = hammer2_ioctl_pfs_lookup(ip, data);
		break;
	case HAMMER2IOC_PFS_CREATE:
		if (error == 0)
			error = hammer2_ioctl_pfs_create(ip, data);
		break;
	case HAMMER2IOC_PFS_DELETE:
		if (error == 0)
			error = hammer2_ioctl_pfs_delete(ip, data);
		break;
	case HAMMER2IOC_PFS_SNAPSHOT:
		if (error == 0)
			error = hammer2_ioctl_pfs_snapshot(ip, data);
		break;
	case HAMMER2IOC_INODE_GET:
		error = hammer2_ioctl_inode_get(ip, data);
		break;
	case HAMMER2IOC_INODE_SET:
		if (error == 0)
			error = hammer2_ioctl_inode_set(ip, data);
		break;
	case HAMMER2IOC_BULKFREE_SCAN:
		error = hammer2_ioctl_bulkfree_scan(ip, data);
		break;
	/*case HAMMER2IOC_INODE_COMP_SET:
		error = hammer2_ioctl_inode_comp_set(ip, data);
		break;
	case HAMMER2IOC_INODE_COMP_REC_SET:
	 	error = hammer2_ioctl_inode_comp_rec_set(ip, data);
	 	break;
	case HAMMER2IOC_INODE_COMP_REC_SET2:
		error = hammer2_ioctl_inode_comp_rec_set2(ip, data);
		break;*/
	case HAMMER2IOC_DEBUG_DUMP:
		error = hammer2_ioctl_debug_dump(ip);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

/*
 * Retrieve version and basic info
 */
static int
hammer2_ioctl_version_get(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_version_t *version = data;
	hammer2_dev_t *hmp;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp)
		version->version = hmp->voldata.version;
	else
		version->version = -1;
	return 0;
}

static int
hammer2_ioctl_recluster(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_recluster_t *recl = data;
	struct file *fp;
	hammer2_cluster_t *cluster;
	int error;

	fp = holdfp(curproc->p_fd, recl->fd, -1);
	if (fp) {
		kprintf("reconnect to cluster: XXX ");
		cluster = &ip->pmp->iroot->cluster;
		if (cluster->nchains != 1 || cluster->focus == NULL) {
			kprintf("not a local device mount\n");
			error = EINVAL;
		} else {
			hammer2_cluster_reconnect(cluster->focus->hmp, fp);
			kprintf("ok\n");
			error = 0;
		}
	} else {
		error = EINVAL;
	}
	return error;
}

/*
 * Retrieve information about a remote
 */
static int
hammer2_ioctl_remote_scan(hammer2_inode_t *ip, void *data)
{
	hammer2_dev_t *hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);

	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	remote->copy1 = hmp->voldata.copyinfo[copyid];
	hammer2_voldata_unlock(hmp);

	/*
	 * Adjust nextid (GET only)
	 */
	while (++copyid < HAMMER2_COPYID_COUNT &&
	       hmp->voldata.copyinfo[copyid].copyid == 0) {
		;
	}
	if (copyid == HAMMER2_COPYID_COUNT)
		remote->nextid = -1;
	else
		remote->nextid = copyid;

	return(0);
}

/*
 * Add new remote entry
 */
static int
hammer2_ioctl_remote_add(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_remote_t *remote = data;
	hammer2_pfs_t *pmp = ip->pmp;
	hammer2_dev_t *hmp;
	int copyid = remote->copyid;
	int error = 0;

	hmp = pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);
	if (copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	if (copyid < 0) {
		for (copyid = 1; copyid < HAMMER2_COPYID_COUNT; ++copyid) {
			if (hmp->voldata.copyinfo[copyid].copyid == 0)
				break;
		}
		if (copyid == HAMMER2_COPYID_COUNT) {
			error = ENOSPC;
			goto failed;
		}
	}
	hammer2_voldata_modify(hmp);
	remote->copy1.copyid = copyid;
	hmp->voldata.copyinfo[copyid] = remote->copy1;
	hammer2_volconf_update(hmp, copyid);
failed:
	hammer2_voldata_unlock(hmp);
	return (error);
}

/*
 * Delete existing remote entry
 */
static int
hammer2_ioctl_remote_del(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_remote_t *remote = data;
	hammer2_pfs_t *pmp = ip->pmp;
	hammer2_dev_t *hmp;
	int copyid = remote->copyid;
	int error = 0;

	hmp = pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);
	if (copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);
	remote->copy1.path[sizeof(remote->copy1.path) - 1] = 0;
	hammer2_voldata_lock(hmp);
	if (copyid < 0) {
		for (copyid = 1; copyid < HAMMER2_COPYID_COUNT; ++copyid) {
			if (hmp->voldata.copyinfo[copyid].copyid == 0)
				continue;
			if (strcmp(remote->copy1.path,
			    hmp->voldata.copyinfo[copyid].path) == 0) {
				break;
			}
		}
		if (copyid == HAMMER2_COPYID_COUNT) {
			error = ENOENT;
			goto failed;
		}
	}
	hammer2_voldata_modify(hmp);
	hmp->voldata.copyinfo[copyid].copyid = 0;
	hammer2_volconf_update(hmp, copyid);
failed:
	hammer2_voldata_unlock(hmp);
	return (error);
}

/*
 * Replace existing remote entry
 */
static int
hammer2_ioctl_remote_rep(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_remote_t *remote = data;
	hammer2_dev_t *hmp;
	int copyid = remote->copyid;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);
	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	hammer2_voldata_modify(hmp);
	/*hammer2_volconf_update(hmp, copyid);*/
	hammer2_voldata_unlock(hmp);

	return(0);
}

/*
 * Retrieve communications socket
 */
static int
hammer2_ioctl_socket_get(hammer2_inode_t *ip, void *data)
{
	return (EOPNOTSUPP);
}

/*
 * Set communications socket for connection
 */
static int
hammer2_ioctl_socket_set(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_remote_t *remote = data;
	hammer2_dev_t *hmp;
	int copyid = remote->copyid;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);
	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	hammer2_voldata_unlock(hmp);

	return(0);
}

/*
 * Used to scan and retrieve PFS information.  PFS's are directories under
 * the super-root.
 *
 * To scan PFSs pass name_key=0.  The function will scan for the next
 * PFS and set all fields, as well as set name_next to the next key.
 * When no PFSs remain, name_next is set to (hammer2_key_t)-1.
 *
 * To retrieve a particular PFS by key, specify the key but note that
 * the ioctl will return the lowest key >= specified_key, so the caller
 * must verify the key.
 *
 * To retrieve the PFS associated with the file descriptor, pass
 * name_key set to (hammer2_key_t)-1.
 */
static int
hammer2_ioctl_pfs_get(hammer2_inode_t *ip, void *data)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_dev_t *hmp;
	hammer2_ioc_pfs_t *pfs;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t save_key;
	int cache_index = -1;
	int error;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);

	pfs = data;
	save_key = pfs->name_key;
	error = 0;

	/*
	 * Setup
	 */
	if (save_key == (hammer2_key_t)-1) {
		hammer2_inode_lock(ip->pmp->iroot, 0);
		parent = NULL;
		chain = hammer2_inode_chain(hmp->spmp->iroot, 0,
					    HAMMER2_RESOLVE_ALWAYS |
					    HAMMER2_RESOLVE_SHARED);
	} else {
		hammer2_inode_lock(hmp->spmp->iroot, 0);
		parent = hammer2_inode_chain(hmp->spmp->iroot, 0,
					    HAMMER2_RESOLVE_ALWAYS |
					    HAMMER2_RESOLVE_SHARED);
		chain = hammer2_chain_lookup(&parent, &key_next,
					    pfs->name_key, HAMMER2_KEY_MAX,
					    &cache_index,
					    HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * Locate next PFS
	 */
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
			break;
		if (parent == NULL) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
			chain = NULL;
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					    key_next, HAMMER2_KEY_MAX,
					    &cache_index,
					    HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * Load the data being returned by the ioctl.
	 */
	if (chain) {
		ripdata = &chain->data->ipdata;
		pfs->name_key = ripdata->meta.name_key;
		pfs->pfs_type = ripdata->meta.pfs_type;
		pfs->pfs_subtype = ripdata->meta.pfs_subtype;
		pfs->pfs_clid = ripdata->meta.pfs_clid;
		pfs->pfs_fsid = ripdata->meta.pfs_fsid;
		KKASSERT(ripdata->meta.name_len < sizeof(pfs->name));
		bcopy(ripdata->filename, pfs->name, ripdata->meta.name_len);
		pfs->name[ripdata->meta.name_len] = 0;
		ripdata = NULL;	/* safety */

		/*
		 * Calculate name_next, if any.
		 */
		if (parent == NULL) {
			pfs->name_next = (hammer2_key_t)-1;
		} else {
			chain = hammer2_chain_next(&parent, chain, &key_next,
						    key_next, HAMMER2_KEY_MAX,
						    &cache_index,
						    HAMMER2_LOOKUP_SHARED);
			if (chain)
				pfs->name_next = chain->bref.key;
			else
				pfs->name_next = (hammer2_key_t)-1;
		}
	} else {
		pfs->name_next = (hammer2_key_t)-1;
		error = ENOENT;
	}

	/*
	 * Cleanup
	 */
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	if (save_key == (hammer2_key_t)-1) {
		hammer2_inode_unlock(ip->pmp->iroot);
	} else {
		hammer2_inode_unlock(hmp->spmp->iroot);
	}

	return (error);
}

/*
 * Find a specific PFS by name
 */
static int
hammer2_ioctl_pfs_lookup(hammer2_inode_t *ip, void *data)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_dev_t *hmp;
	hammer2_ioc_pfs_t *pfs;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int cache_index = -1;
	int error;
	size_t len;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);

	pfs = data;
	error = 0;

	hammer2_inode_lock(hmp->spmp->iroot, HAMMER2_RESOLVE_SHARED);
	parent = hammer2_inode_chain(hmp->spmp->iroot, 0,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);

	pfs->name[sizeof(pfs->name) - 1] = 0;
	len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, len);

	chain = hammer2_chain_lookup(&parent, &key_next,
					 lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					 &cache_index,
					 HAMMER2_LOOKUP_SHARED);
	while (chain) {
		if (hammer2_chain_dirent_test(chain, pfs->name, len))
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index,
					   HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * Load the data being returned by the ioctl.
	 */
	if (chain) {
		KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);
		ripdata = &chain->data->ipdata;
		pfs->name_key = ripdata->meta.name_key;
		pfs->pfs_type = ripdata->meta.pfs_type;
		pfs->pfs_subtype = ripdata->meta.pfs_subtype;
		pfs->pfs_clid = ripdata->meta.pfs_clid;
		pfs->pfs_fsid = ripdata->meta.pfs_fsid;
		ripdata = NULL;

		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	} else {
		error = ENOENT;
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_inode_unlock(hmp->spmp->iroot);

	return (error);
}

/*
 * Create a new PFS under the super-root
 */
static int
hammer2_ioctl_pfs_create(hammer2_inode_t *ip, void *data)
{
	hammer2_inode_data_t *nipdata;
	hammer2_chain_t *nchain;
	hammer2_dev_t *hmp;
	hammer2_dev_t *force_local;
	hammer2_ioc_pfs_t *pfs;
	hammer2_inode_t *nip;
	hammer2_tid_t mtid;
	int error;

	hmp = ip->pmp->pfs_hmps[0];	/* XXX */
	if (hmp == NULL)
		return (EINVAL);

	pfs = data;
	nip = NULL;

	if (pfs->name[0] == 0)
		return(EINVAL);
	pfs->name[sizeof(pfs->name) - 1] = 0;	/* ensure 0-termination */

	if (hammer2_ioctl_pfs_lookup(ip, pfs) == 0)
		return(EEXIST);

	hammer2_trans_init(hmp->spmp, 0);
	mtid = hammer2_trans_sub(hmp->spmp);
	nip = hammer2_inode_create(hmp->spmp->iroot, hmp->spmp->iroot,
				   NULL, NULL,
				   pfs->name, strlen(pfs->name), 0,
				   1, HAMMER2_OBJTYPE_DIRECTORY, 0,
				   HAMMER2_INSERT_PFSROOT, &error);
	if (error == 0) {
		hammer2_inode_modify(nip);
		nchain = hammer2_inode_chain(nip, 0, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_modify(nchain, mtid, 0, 0);
		nipdata = &nchain->data->ipdata;

		nip->meta.pfs_type = pfs->pfs_type;
		nip->meta.pfs_subtype = pfs->pfs_subtype;
		nip->meta.pfs_clid = pfs->pfs_clid;
		nip->meta.pfs_fsid = pfs->pfs_fsid;
		nip->meta.op_flags |= HAMMER2_OPFLAG_PFSROOT;

		/*
		 * Set default compression and check algorithm.  This
		 * can be changed later.
		 *
		 * Do not allow compression on PFS's with the special name
		 * "boot", the boot loader can't decompress (yet).
		 */
		nip->meta.comp_algo =
			HAMMER2_ENC_ALGO(HAMMER2_COMP_NEWFS_DEFAULT);
		nip->meta.check_algo =
			HAMMER2_ENC_ALGO( HAMMER2_CHECK_XXHASH64);

		if (strcasecmp(pfs->name, "boot") == 0) {
			nip->meta.comp_algo =
				HAMMER2_ENC_ALGO(HAMMER2_COMP_AUTOZERO);
		}

		/*
		 * Super-root isn't mounted, fsync it
		 */
		hammer2_chain_unlock(nchain);
		hammer2_inode_ref(nip);
		hammer2_inode_unlock(nip);
		hammer2_inode_chain_sync(nip);
		hammer2_inode_drop(nip);

		/* 
		 * We still have a ref on the chain, relock and associate
		 * with an appropriate PFS.
		 */
		force_local = (hmp->hflags & HMNT2_LOCAL) ? hmp : NULL;

		hammer2_chain_lock(nchain, HAMMER2_RESOLVE_ALWAYS);
		nipdata = &nchain->data->ipdata;
		kprintf("ADD LOCAL PFS (IOCTL): %s\n", nipdata->filename);
		hammer2_pfsalloc(nchain, nipdata,
				 nchain->bref.modify_tid, force_local);

		hammer2_chain_unlock(nchain);
		hammer2_chain_drop(nchain);

	}
	hammer2_trans_done(hmp->spmp);

	return (error);
}

/*
 * Destroy an existing PFS under the super-root
 */
static int
hammer2_ioctl_pfs_delete(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_dev_t	*hmp;
	hammer2_pfs_t	*spmp;
	hammer2_pfs_t	*pmp;
	hammer2_xop_unlink_t *xop;
	hammer2_inode_t *dip;
	hammer2_inode_t *iroot;
	int error;
	int i;

	/*
	 * The PFS should be probed, so we should be able to
	 * locate it.  We only delete the PFS from the
	 * specific H2 block device (hmp), not all of
	 * them.  We must remove the PFS from the cluster
	 * before we can destroy it.
	 */
	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);

	pfs->name[sizeof(pfs->name) - 1] = 0;	/* ensure termination */

	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);

	TAILQ_FOREACH(pmp, &hammer2_pfslist, mntentry) {
		for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
			if (pmp->pfs_hmps[i] != hmp)
				continue;
			if (pmp->pfs_names[i] &&
			    strcmp(pmp->pfs_names[i], pfs->name) == 0) {
				break;
			}
		}
		if (i != HAMMER2_MAXCLUSTER)
			break;
	}

	if (pmp == NULL) {
		lockmgr(&hammer2_mntlk, LK_RELEASE);
		return ENOENT;
	}

	/*
	 * Ok, we found the pmp and we have the index.  Permanently remove
	 * the PFS from the cluster
	 */
	iroot = pmp->iroot;
	kprintf("FOUND PFS %s CLINDEX %d\n", pfs->name, i);
	hammer2_pfsdealloc(pmp, i, 1);

	lockmgr(&hammer2_mntlk, LK_RELEASE);

	/*
	 * Now destroy the PFS under its device using the per-device
	 * super-root.
	 */
	spmp = hmp->spmp;
	dip = spmp->iroot;
	hammer2_trans_init(spmp, 0);
	hammer2_inode_lock(dip, 0);

	xop = hammer2_xop_alloc(dip, HAMMER2_XOP_MODIFYING);
	hammer2_xop_setname(&xop->head, pfs->name, strlen(pfs->name));
	xop->isdir = 2;
	xop->dopermanent = 2 | 1;	/* FORCE | PERMANENT */
	hammer2_xop_start(&xop->head, hammer2_xop_unlink);

	error = hammer2_xop_collect(&xop->head, 0);

	hammer2_inode_unlock(dip);

#if 0
        if (error == 0) {
                ip = hammer2_inode_get(dip->pmp, dip, &xop->head.cluster, -1);
                hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
                if (ip) {
                        hammer2_inode_unlink_finisher(ip, 0);
                        hammer2_inode_unlock(ip);
                }
        } else {
                hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
        }
#endif
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);

	hammer2_trans_done(spmp);

	return (error);
}

static int
hammer2_ioctl_pfs_snapshot(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_dev_t	*hmp;
	hammer2_pfs_t	*pmp;
	hammer2_chain_t	*chain;
	hammer2_tid_t	mtid;
	int error;

	if (pfs->name[0] == 0)
		return(EINVAL);
	if (pfs->name[sizeof(pfs->name)-1] != 0)
		return(EINVAL);

	pmp = ip->pmp;
	ip = pmp->iroot;

	hmp = pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);

	hammer2_vfs_sync(pmp->mp, MNT_WAIT);

	hammer2_trans_init(pmp, HAMMER2_TRANS_ISFLUSH);
	mtid = hammer2_trans_sub(pmp);
	hammer2_inode_lock(ip, 0);
	hammer2_inode_modify(ip);
	ip->meta.pfs_lsnap_tid = mtid;

	/* XXX cluster it! */
	chain = hammer2_inode_chain(ip, 0, HAMMER2_RESOLVE_ALWAYS);
	error = hammer2_chain_snapshot(chain, pfs, mtid);
	hammer2_chain_unlock(chain);
	hammer2_chain_drop(chain);

	hammer2_inode_unlock(ip);
	hammer2_trans_done(pmp);

	return (error);
}

/*
 * Retrieve the raw inode structure, non-inclusive of node-specific data.
 */
static int
hammer2_ioctl_inode_get(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_inode_t *ino;
	hammer2_chain_t *chain;
	int error;
	int i;

	ino = data;
	error = 0;

	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
	ino->data_count = 0;
	ino->inode_count = 0;
	for (i = 0; i < ip->cluster.nchains; ++i) {
		if ((chain = ip->cluster.array[i].chain) != NULL) {
			if (ino->data_count <
			    chain->bref.embed.stats.data_count) {
				ino->data_count =
					chain->bref.embed.stats.data_count;
			}
			if (ino->inode_count <
			    chain->bref.embed.stats.inode_count) {
				ino->inode_count =
					chain->bref.embed.stats.inode_count;
			}
		}
	}
	bzero(&ino->ip_data, sizeof(ino->ip_data));
	ino->ip_data.meta = ip->meta;
	ino->kdata = ip;
	hammer2_inode_unlock(ip);

	return error;
}

/*
 * Set various parameters in an inode which cannot be set through
 * normal filesystem VNOPS.
 */
static int
hammer2_ioctl_inode_set(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_inode_t *ino = data;
	int error = 0;

	hammer2_trans_init(ip->pmp, 0);
	hammer2_inode_lock(ip, 0);

	if ((ino->flags & HAMMER2IOC_INODE_FLAG_CHECK) &&
	    ip->meta.check_algo != ino->ip_data.meta.check_algo) {
		hammer2_inode_modify(ip);
		ip->meta.check_algo = ino->ip_data.meta.check_algo;
	}
	if ((ino->flags & HAMMER2IOC_INODE_FLAG_COMP) &&
	    ip->meta.comp_algo != ino->ip_data.meta.comp_algo) {
		hammer2_inode_modify(ip);
		ip->meta.comp_algo = ino->ip_data.meta.comp_algo;
	}
	ino->kdata = ip;
	
	/* Ignore these flags for now...*/
	if ((ino->flags & HAMMER2IOC_INODE_FLAG_IQUOTA) &&
	    ip->meta.inode_quota != ino->ip_data.meta.inode_quota) {
		hammer2_inode_modify(ip);
		ip->meta.inode_quota = ino->ip_data.meta.inode_quota;
	}
	if ((ino->flags & HAMMER2IOC_INODE_FLAG_DQUOTA) &&
	    ip->meta.data_quota != ino->ip_data.meta.data_quota) {
		hammer2_inode_modify(ip);
		ip->meta.data_quota = ino->ip_data.meta.data_quota;
	}
	if ((ino->flags & HAMMER2IOC_INODE_FLAG_COPIES) &&
	    ip->meta.ncopies != ino->ip_data.meta.ncopies) {
		hammer2_inode_modify(ip);
		ip->meta.ncopies = ino->ip_data.meta.ncopies;
	}
	hammer2_inode_unlock(ip);
	hammer2_trans_done(ip->pmp);

	return (error);
}

static
int
hammer2_ioctl_debug_dump(hammer2_inode_t *ip)
{
	hammer2_chain_t *chain;
	int count = 1000;
	int i;

	for (i = 0; i < ip->cluster.nchains; ++i) {
		chain = ip->cluster.array[i].chain;
		if (chain == NULL)
			continue;
		hammer2_dump_chain(chain, 0, &count, 'i');
	}
	return 0;
}

/*
 * Executes one flush/free pass per call.  If trying to recover
 * data we just freed up a moment ago it can take up to six passes
 * to fully free the blocks.  Note that passes occur automatically based
 * on free space as the storage fills up, but manual passes may be needed
 * if storage becomes almost completely full.
 */
static
int
hammer2_ioctl_bulkfree_scan(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_bulkfree_t *bfi = data;
	hammer2_dev_t *hmp;
	int error;

	hmp = ip->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return (EINVAL);

	error = hammer2_bulkfree_pass(hmp, bfi);

	return error;
}
