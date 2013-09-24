/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
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
//static int hammer2_ioctl_inode_comp_set(hammer2_inode_t *ip, void *data);
//static int hammer2_ioctl_inode_comp_rec_set(hammer2_inode_t *ip, void *data);
//static int hammer2_ioctl_inode_comp_rec_set2(hammer2_inode_t *ip, void *data);

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
	/*case HAMMER2IOC_INODE_COMP_SET:
		error = hammer2_ioctl_inode_comp_set(ip, data);
		break;
	case HAMMER2IOC_INODE_COMP_REC_SET:
	 	error = hammer2_ioctl_inode_comp_rec_set(ip, data);
	 	break;
	case HAMMER2IOC_INODE_COMP_REC_SET2:
		error = hammer2_ioctl_inode_comp_rec_set2(ip, data);
		break;*/
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
	hammer2_mount_t *hmp = ip->pmp->mount_cluster->hmp;
	hammer2_ioc_version_t *version = data;

	version->version = hmp->voldata.version;
	return 0;
}

static int
hammer2_ioctl_recluster(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_recluster_t *recl = data;
	struct file *fp;

	fp = holdfp(curproc->p_fd, recl->fd, -1);
	if (fp) {
		kprintf("reconnect to cluster\n");
		hammer2_cluster_reconnect(ip->pmp, fp);
		return 0;
	} else {
		return EINVAL;
	}
}

/*
 * Retrieve information about a remote
 */
static int
hammer2_ioctl_remote_scan(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp = ip->pmp->mount_cluster->hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;

	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	remote->copy1 = hmp->voldata.copyinfo[copyid];
	hammer2_voldata_unlock(hmp, 0);

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
	hammer2_pfsmount_t *pmp = ip->pmp;
	hammer2_mount_t *hmp;
	int copyid = remote->copyid;
	int error = 0;

	if (copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hmp = pmp->mount_cluster->hmp;
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
	hammer2_modify_volume(hmp);
	remote->copy1.copyid = copyid;
	hmp->voldata.copyinfo[copyid] = remote->copy1;
	hammer2_volconf_update(pmp, copyid);
failed:
	hammer2_voldata_unlock(hmp, 1);
	return (error);
}

/*
 * Delete existing remote entry
 */
static int
hammer2_ioctl_remote_del(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_remote_t *remote = data;
	hammer2_pfsmount_t *pmp = ip->pmp;
	hammer2_mount_t *hmp;
	int copyid = remote->copyid;
	int error = 0;

	hmp = pmp->mount_cluster->hmp;
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
	hammer2_modify_volume(hmp);
	hmp->voldata.copyinfo[copyid].copyid = 0;
	hammer2_volconf_update(pmp, copyid);
failed:
	hammer2_voldata_unlock(hmp, 1);
	return (error);
}

/*
 * Replace existing remote entry
 */
static int
hammer2_ioctl_remote_rep(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_remote_t *remote = data;
	hammer2_mount_t *hmp;
	int copyid = remote->copyid;

	hmp = ip->pmp->mount_cluster->hmp;

	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	/*hammer2_volconf_update(pmp, copyid);*/
	hammer2_voldata_unlock(hmp, 1);

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
	hammer2_mount_t *hmp;
	int copyid = remote->copyid;

	hmp = ip->pmp->mount_cluster->hmp;
	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	hammer2_voldata_unlock(hmp, 0);

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
 * To retrieve the PFS associated with the file descriptor, pass
 * name_key set to (hammer2_key_t)-1.
 */
static int
hammer2_ioctl_pfs_get(hammer2_inode_t *ip, void *data)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_ioc_pfs_t *pfs;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *rchain;
	int error;

	error = 0;
	hmp = ip->pmp->mount_cluster->hmp;
	pfs = data;
	parent = hammer2_chain_lookup_init(hmp->schain, 0);
	rchain = ip->pmp->mount_cluster->rchain;

	/*
	 * Search for the first key or specific key.  Remember that keys
	 * can be returned in any order.
	 */
	if (pfs->name_key == 0) {
		chain = hammer2_chain_lookup(&parent,
					     0, (hammer2_key_t)-1, 0);
	} else if (pfs->name_key == (hammer2_key_t)-1) {
		chain = hammer2_chain_lookup(&parent,
					     rchain->data->ipdata.name_key,
					     rchain->data->ipdata.name_key,
					     0);
	} else {
		chain = hammer2_chain_lookup(&parent,
					     pfs->name_key, pfs->name_key, 0);
	}
	while (chain && chain->bref.type != HAMMER2_BREF_TYPE_INODE) {
		chain = hammer2_chain_next(&parent, chain,
					   0, (hammer2_key_t)-1, 0);
	}
	if (chain) {
		/*
		 * Load the data being returned by the ioctl.
		 */
		ipdata = &chain->data->ipdata;
		pfs->name_key = ipdata->name_key;
		pfs->pfs_type = ipdata->pfs_type;
		pfs->pfs_clid = ipdata->pfs_clid;
		pfs->pfs_fsid = ipdata->pfs_fsid;
		KKASSERT(ipdata->name_len < sizeof(pfs->name));
		bcopy(ipdata->filename, pfs->name, ipdata->name_len);
		pfs->name[ipdata->name_len] = 0;
		ipdata = NULL;	/* safety */

		/*
		 * Calculate the next field
		 */
		do {
			chain = hammer2_chain_next(&parent, chain,
						   0, (hammer2_key_t)-1, 0);
		} while (chain && chain->bref.type != HAMMER2_BREF_TYPE_INODE);
		if (chain) {
			pfs->name_next = chain->data->ipdata.name_key;
			hammer2_chain_unlock(chain);
		} else {
			pfs->name_next = (hammer2_key_t)-1;
		}
	} else {
		pfs->name_next = (hammer2_key_t)-1;
		error = ENOENT;
	}
	hammer2_chain_lookup_done(parent);

	return (error);
}

/*
 * Find a specific PFS by name
 */
static int
hammer2_ioctl_pfs_lookup(hammer2_inode_t *ip, void *data)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_ioc_pfs_t *pfs;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lhc;
	int error;
	size_t len;

	error = 0;
	hmp = ip->pmp->mount_cluster->hmp;
	pfs = data;
	parent = hammer2_chain_lookup_init(hmp->schain, HAMMER2_LOOKUP_SHARED);

	pfs->name[sizeof(pfs->name) - 1] = 0;
	len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, len);

	chain = hammer2_chain_lookup(&parent,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    len == chain->data->ipdata.name_len &&
		    bcmp(pfs->name, chain->data->ipdata.filename, len) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain,
					   lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					   HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * Load the data being returned by the ioctl.
	 */
	if (chain) {
		ipdata = &chain->data->ipdata;
		pfs->name_key = ipdata->name_key;
		pfs->pfs_type = ipdata->pfs_type;
		pfs->pfs_clid = ipdata->pfs_clid;
		pfs->pfs_fsid = ipdata->pfs_fsid;
		ipdata = NULL;

		hammer2_chain_unlock(chain);
	} else {
		error = ENOENT;
	}
	hammer2_chain_lookup_done(parent);
	return (error);
}

/*
 * Create a new PFS under the super-root
 */
static int
hammer2_ioctl_pfs_create(hammer2_inode_t *ip, void *data)
{
	hammer2_inode_data_t *nipdata;
	hammer2_mount_t *hmp;
	hammer2_ioc_pfs_t *pfs;
	hammer2_inode_t *nip;
	hammer2_chain_t *nchain;
	hammer2_trans_t trans;
	int error;

	hmp = ip->pmp->mount_cluster->hmp;
	pfs = data;
	nip = NULL;

	if (pfs->name[0] == 0)
		return(EINVAL);
	pfs->name[sizeof(pfs->name) - 1] = 0;	/* ensure 0-termination */

	hammer2_trans_init(&trans, ip->pmp, 0);
	nip = hammer2_inode_create(&trans, hmp->sroot, NULL, NULL,
				     pfs->name, strlen(pfs->name),
				     &nchain, &error);
	if (error == 0) {
		nipdata = hammer2_chain_modify_ip(&trans, nip, &nchain,
						  HAMMER2_MODIFY_ASSERTNOCOPY);
		nipdata->pfs_type = pfs->pfs_type;
		nipdata->pfs_clid = pfs->pfs_clid;
		nipdata->pfs_fsid = pfs->pfs_fsid;

		/*
		 * Do not allow compression on PFS's with the special name
		 * "boot", the boot loader can't decompress (yet).
		 */
		if (strcmp(pfs->name, "boot") == 0)
			nipdata->comp_algo = HAMMER2_COMP_AUTOZERO;
		hammer2_inode_unlock_ex(nip, nchain);
	}
	hammer2_trans_done(&trans);
	return (error);
}

/*
 * Destroy an existing PFS under the super-root
 */
static int
hammer2_ioctl_pfs_delete(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp;
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_trans_t trans;
	int error;

	hmp = ip->pmp->mount_cluster->hmp;
	hammer2_trans_init(&trans, ip->pmp, 0);
	error = hammer2_unlink_file(&trans, hmp->sroot,
				    pfs->name, strlen(pfs->name),
				    2, NULL);
	hammer2_trans_done(&trans);

	return (error);
}

static int
hammer2_ioctl_pfs_snapshot(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_trans_t trans;
	hammer2_chain_t *parent;
	int error;

	if (pfs->name[0] == 0)
		return(EINVAL);
	if (pfs->name[sizeof(pfs->name)-1] != 0)
		return(EINVAL);

	hammer2_trans_init(&trans, ip->pmp, 0);
	parent = hammer2_inode_lock_ex(ip);
	error = hammer2_chain_snapshot(&trans, ip, pfs);
	hammer2_inode_unlock_ex(ip, parent);
	hammer2_trans_done(&trans);

	return (error);
}

/*
 * Retrieve the raw inode structure
 */
static int
hammer2_ioctl_inode_get(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_inode_t *ino = data;
	hammer2_chain_t *parent;

	parent = hammer2_inode_lock_sh(ip);
	ino->ip_data = ip->chain->data->ipdata;
	ino->kdata = ip;
	hammer2_inode_unlock_sh(ip, parent);

	return (0);
}

/*
 * Set various parameters in an inode which cannot be set through
 * normal filesystem VNOPS.
 */
static int
hammer2_ioctl_inode_set(hammer2_inode_t *ip, void *data)
{
	hammer2_inode_data_t *ipdata;
	hammer2_ioc_inode_t *ino = data;
	hammer2_chain_t *chain;
	hammer2_trans_t trans;
	int error = 0;

	hammer2_trans_init(&trans, ip->pmp, 0);
	chain = hammer2_inode_lock_ex(ip);

	if (ino->ip_data.comp_algo != chain->data->ipdata.comp_algo) {
		ipdata = hammer2_chain_modify_ip(&trans, ip, &chain, 0);
		ipdata->comp_algo = ino->ip_data.comp_algo;
	}
	ino->kdata = ip;
	
	/* Ignore these flags for now...*/
	if (ino->flags & HAMMER2IOC_INODE_FLAG_IQUOTA) {
	}
	if (ino->flags & HAMMER2IOC_INODE_FLAG_DQUOTA) {
	}
	if (ino->flags & HAMMER2IOC_INODE_FLAG_COPIES) {
	}
	hammer2_trans_done(&trans);
	hammer2_inode_unlock_ex(ip, chain);

	return (error);
}
