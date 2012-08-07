/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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
static int hammer2_ioctl_remote_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_add(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_del(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_remote_rep(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_socket_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_socket_set(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_lookup(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_create(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_pfs_delete(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_inode_get(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_inode_set(hammer2_inode_t *ip, void *data);

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
	case HAMMER2IOC_REMOTE_GET:
		if (error == 0)
			error = hammer2_ioctl_remote_get(ip, data);
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
	case HAMMER2IOC_INODE_GET:
		error = hammer2_ioctl_inode_get(ip, data);
		break;
	case HAMMER2IOC_INODE_SET:
		if (error == 0)
			error = hammer2_ioctl_inode_set(ip, data);
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
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_version_t *version = data;

	version->version = hmp->voldata.version;
	return 0;
}

/*
 * Retrieve information about a remote
 */
static int
hammer2_ioctl_remote_get(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp = ip->hmp;
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
		++copyid;
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
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;
	int error = 0;

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
	hammer2_modify_volume(hmp);
	kprintf("copyid %d\n", copyid);
	remote->copy1.copyid = copyid;
	hmp->voldata.copyinfo[copyid] = remote->copy1;
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
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;
	int error = 0;

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
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;

	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
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
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_remote_t *remote = data;
	int copyid = remote->copyid;

	if (copyid < 0 || copyid >= HAMMER2_COPYID_COUNT)
		return (EINVAL);

	hammer2_voldata_lock(hmp);
	hammer2_voldata_unlock(hmp);

	return(0);
}

/*
 * Used to scan PFSs, which are directories under the super-root.
 */
static int
hammer2_ioctl_pfs_get(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_inode_t *xip;
	int error = 0;

	parent = hmp->schain;
	error = hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
	if (error)
		goto done;

	/*
	 * Search for the first key or specific key.  Remember that keys
	 * can be returned in any order.
	 */
	if (pfs->name_key == 0) {
		chain = hammer2_chain_lookup(hmp, &parent,
					     0, (hammer2_key_t)-1, 0);
	} else {
		chain = hammer2_chain_lookup(hmp, &parent,
					     pfs->name_key, pfs->name_key, 0);
	}
	while (chain && chain->bref.type != HAMMER2_BREF_TYPE_INODE) {
		chain = hammer2_chain_next(hmp, &parent, chain,
				     0, (hammer2_key_t)-1, 0);
	}
	if (chain) {
		/*
		 * Load the data being returned by the ioctl.
		 */
		xip = chain->u.ip;
		pfs->name_key = xip->ip_data.name_key;
		pfs->pfs_type = xip->ip_data.pfs_type;
		pfs->pfs_clid = xip->ip_data.pfs_clid;
		pfs->pfs_fsid = xip->ip_data.pfs_fsid;
		KKASSERT(xip->ip_data.name_len < sizeof(pfs->name));
		bcopy(xip->ip_data.filename, pfs->name,
		      xip->ip_data.name_len);
		pfs->name[xip->ip_data.name_len] = 0;

		/*
		 * Calculate the next field
		 */
		do {
			chain = hammer2_chain_next(hmp, &parent, chain,
					     0, (hammer2_key_t)-1, 0);
		} while (chain && chain->bref.type != HAMMER2_BREF_TYPE_INODE);
		if (chain) {
			pfs->name_next = chain->u.ip->ip_data.name_key;
			hammer2_chain_unlock(hmp, chain);
		} else {
			pfs->name_next = (hammer2_key_t)-1;
		}
	} else {
		pfs->name_next = (hammer2_key_t)-1;
		error = ENOENT;
	}
done:
	hammer2_chain_unlock(hmp, parent);
	return (error);
}

/*
 * Find a specific PFS by name
 */
static int
hammer2_ioctl_pfs_lookup(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_inode_t *xip;
	hammer2_key_t lhc;
	int error = 0;
	size_t len;

	parent = hmp->schain;
	error = hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS |
						HAMMER2_RESOLVE_SHARED);
	if (error)
		goto done;

	pfs->name[sizeof(pfs->name) - 1] = 0;
	len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, len);

	chain = hammer2_chain_lookup(hmp, &parent,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    chain->u.ip &&
		    len == chain->data->ipdata.name_len &&
		    bcmp(pfs->name, chain->data->ipdata.filename, len) == 0) {
			break;
		}
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					   HAMMER2_LOOKUP_SHARED);
	}

	/*
	 * Load the data being returned by the ioctl.
	 */
	if (chain) {
		xip = chain->u.ip;
		pfs->name_key = xip->ip_data.name_key;
		pfs->pfs_type = xip->ip_data.pfs_type;
		pfs->pfs_clid = xip->ip_data.pfs_clid;
		pfs->pfs_fsid = xip->ip_data.pfs_fsid;

		hammer2_chain_unlock(hmp, chain);
	} else {
		error = ENOENT;
	}
done:
	hammer2_chain_unlock(hmp, parent);
	return (error);
}

/*
 * Create a new PFS under the super-root
 */
static int
hammer2_ioctl_pfs_create(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_pfs_t *pfs = data;
	hammer2_inode_t *nip = NULL;
	int error;

	pfs->name[sizeof(pfs->name) - 1] = 0;	/* ensure 0-termination */
	error = hammer2_inode_create(hmp->schain->u.ip, NULL, NULL,
				     pfs->name, strlen(pfs->name),
				     &nip);
	if (error == 0) {
		hammer2_chain_modify(hmp, &nip->chain, 0);
		nip->ip_data.pfs_type = pfs->pfs_type;
		nip->ip_data.pfs_clid = pfs->pfs_clid;
		nip->ip_data.pfs_fsid = pfs->pfs_fsid;
		hammer2_chain_unlock(hmp, &nip->chain);
	}
	return (error);
}

/*
 * Destroy an existing PFS under the super-root
 */
static int
hammer2_ioctl_pfs_delete(hammer2_inode_t *ip, void *data)
{
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_ioc_pfs_t *pfs = data;
	int error;

	error = hammer2_unlink_file(hmp->schain->u.ip,
				    pfs->name, strlen(pfs->name),
				    0, NULL);
	return (error);
}

/*
 * Retrieve the raw inode structure
 */
static int
hammer2_ioctl_inode_get(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_inode_t *ino = data;

	hammer2_inode_lock_sh(ip);
	ino->ip_data = ip->ip_data;
	ino->kdata = ip;
	hammer2_inode_unlock_sh(ip);
	return (0);
}

static int
hammer2_ioctl_inode_set(hammer2_inode_t *ip, void *data)
{
	hammer2_ioc_inode_t *ino = data;
	int error = EINVAL;

	hammer2_inode_lock_ex(ip);
	if (ino->flags & HAMMER2IOC_INODE_FLAG_IQUOTA) {
	}
	if (ino->flags & HAMMER2IOC_INODE_FLAG_DQUOTA) {
	}
	if (ino->flags & HAMMER2IOC_INODE_FLAG_COPIES) {
	}
	hammer2_inode_unlock_ex(ip);
	return (error);
}
