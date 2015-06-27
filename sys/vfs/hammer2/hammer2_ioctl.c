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
	hammer2_dev_t *hmp = ip->pmp->iroot->cluster.focus->hmp;
	hammer2_ioc_version_t *version = data;

	version->version = hmp->voldata.version;
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
	hammer2_dev_t *hmp = ip->pmp->iroot->cluster.focus->hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;

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

	if (copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hmp = pmp->iroot->cluster.focus->hmp; /* XXX */
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

	hmp = pmp->iroot->cluster.focus->hmp; /* XXX */
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

	hmp = ip->pmp->iroot->cluster.focus->hmp; /* XXX */

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

	hmp = ip->pmp->iroot->cluster.focus->hmp; /* XXX */
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

	error = 0;
	hmp = ip->pmp->iroot->cluster.focus->hmp; /* XXX */
	pfs = data;

	save_key = pfs->name_key;

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
		 * Calculate name_next.
		 */
		chain = hammer2_chain_next(&parent, chain, &key_next,
					    key_next, HAMMER2_KEY_MAX,
					    &cache_index,
					    HAMMER2_LOOKUP_SHARED);
		if (chain)
			pfs->name_next = chain->bref.key;
		else
			pfs->name_next = (hammer2_key_t)-1;
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

	error = 0;
	hmp = ip->pmp->iroot->cluster.focus->hmp; /* XXX */
	pfs = data;
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
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			ripdata = &chain->data->ipdata;
			if (ripdata->meta.name_len == len &&
			    bcmp(ripdata->filename, pfs->name, len) == 0) {
				break;
			}
			ripdata = NULL;	/* safety */
		}
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
	hammer2_ioc_pfs_t *pfs;
	hammer2_inode_t *nip;
	int error;

	hmp = ip->pmp->iroot->cluster.focus->hmp; /* XXX */
	pfs = data;
	nip = NULL;

	if (pfs->name[0] == 0)
		return(EINVAL);
	pfs->name[sizeof(pfs->name) - 1] = 0;	/* ensure 0-termination */

	if (hammer2_ioctl_pfs_lookup(ip, pfs) == 0)
		return(EEXIST);

	hammer2_trans_init(hmp->spmp, 0);
	nip = hammer2_inode_create(hmp->spmp->iroot, NULL, NULL,
				   pfs->name, strlen(pfs->name), 0,
				   1, HAMMER2_OBJTYPE_DIRECTORY, 0,
				   HAMMER2_INSERT_PFSROOT, &error);
	if (error == 0) {
		hammer2_inode_modify(nip);
		nchain = hammer2_inode_chain(nip, 0, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_modify(nchain, 0);
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
			HAMMER2_ENC_ALGO( HAMMER2_CHECK_ISCSI32);

		if (strcasecmp(pfs->name, "boot") == 0) {
			nip->meta.comp_algo =
				HAMMER2_ENC_ALGO(HAMMER2_COMP_AUTOZERO);
		}
#if 0
		hammer2_blockref_t bref;
		/* XXX new PFS needs to be rescanned / added */
		bref = nchain->bref;
		kprintf("ADD LOCAL PFS (IOCTL): %s\n", nipdata->filename);
		hammer2_pfsalloc(nchain, nipdata, bref.modify_tid);
#endif
		/* XXX rescan */
		hammer2_chain_unlock(nchain);
		hammer2_chain_drop(nchain);

		hammer2_inode_unlock(nip);
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
	hammer2_pfs_t *spmp;
	hammer2_xop_unlink_t *xop;
	hammer2_inode_t *dip;
	int error;

	pfs->name[sizeof(pfs->name) - 1] = 0;	/* ensure termination */

	/* XXX */
	spmp = ip->pmp->iroot->cluster.focus->hmp->spmp;
	dip = spmp->iroot;
	hammer2_trans_init(spmp, 0);
	hammer2_inode_lock(dip, 0);

	xop = &hammer2_xop_alloc(dip)->xop_unlink;
	hammer2_xop_setname(&xop->head, pfs->name, strlen(pfs->name));
	xop->isdir = 2;
	xop->dopermanent = 1;
	hammer2_xop_start(&xop->head, hammer2_xop_unlink);

	error = hammer2_xop_collect(&xop->head, 0);

	hammer2_inode_unlock(dip);
	hammer2_trans_done(spmp);

	return (error);
}

static int
hammer2_ioctl_pfs_snapshot(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_cluster_t *cparent;
	int error;

	if (pfs->name[0] == 0)
		return(EINVAL);
	if (pfs->name[sizeof(pfs->name)-1] != 0)
		return(EINVAL);

	hammer2_vfs_sync(ip->pmp->mp, MNT_WAIT);

	hammer2_trans_init(ip->pmp, HAMMER2_TRANS_ISFLUSH);
	hammer2_inode_lock(ip, 0);
	cparent = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);
	error = hammer2_cluster_snapshot(cparent, pfs);
	hammer2_inode_unlock(ip);
	hammer2_cluster_unlock(cparent);
	hammer2_cluster_drop(cparent);
	hammer2_trans_done(ip->pmp);

	return (error);
}

/*
 * Retrieve the raw inode structure, non-inclusive of node-specific data.
 */
static int
hammer2_ioctl_inode_get(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_inode_t *ino;
	int error;

	ino = data;

	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
	if (ip->cluster.focus) {
		/* XXX */
		ino->data_count = ip->cluster.focus->bref.data_count;
		ino->inode_count = ip->cluster.focus->bref.inode_count;
		error = 0;
	} else {
		ino->data_count = -1;
		ino->inode_count = -1;
		error = EIO;
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

static
int
hammer2_ioctl_bulkfree_scan(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_bulkfree_t *bfi = data;
	hammer2_dev_t *hmp = ip->pmp->iroot->cluster.focus->hmp;
	int error;

	/* XXX run local cluster targets only */
	error = hammer2_bulkfree_pass(hmp, bfi);

	return error;
}
