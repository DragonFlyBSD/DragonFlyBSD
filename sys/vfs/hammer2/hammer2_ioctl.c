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

static int hammer2_ioctl_get_version(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_get_remote(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_add_remote(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_del_remote(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_rep_remote(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_get_socket(hammer2_inode_t *ip, void *data);
static int hammer2_ioctl_set_socket(hammer2_inode_t *ip, void *data);

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
	case HAMMER2IOC_GET_VERSION:
		/*
		 * Retrieve version and basic status
		 */
		error = hammer2_ioctl_get_version(ip, data);
		break;
	case HAMMER2IOC_GET_REMOTE:
		/*
		 * Retrieve information about a remote
		 */
		if (error == 0)
			error = hammer2_ioctl_get_remote(ip, data);
		break;
	case HAMMER2IOC_ADD_REMOTE:
		/*
		 * Add new remote entry.
		 */
		if (error == 0)
			error = hammer2_ioctl_add_remote(ip, data);
		break;
	case HAMMER2IOC_DEL_REMOTE:
		/*
		 * Delete existing remote entry
		 */
		if (error == 0)
			error = hammer2_ioctl_del_remote(ip, data);
		break;
	case HAMMER2IOC_REP_REMOTE:
		/*
		 * Replace existing remote entry
		 */
		if (error == 0)
			error = hammer2_ioctl_rep_remote(ip, data);
		break;
	case HAMMER2IOC_GET_SOCKET:
		/*
		 * Retrieve communications socket
		 */
		if (error == 0)
			error = hammer2_ioctl_get_socket(ip, data);
		break;
	case HAMMER2IOC_SET_SOCKET:
		/*
		 * Set communications socket for connection
		 */
		if (error == 0)
			error = hammer2_ioctl_set_socket(ip, data);
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
hammer2_ioctl_get_version(hammer2_inode_t *ip, void *data)
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
hammer2_ioctl_get_remote(hammer2_inode_t *ip, void *data)
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
hammer2_ioctl_add_remote(hammer2_inode_t *ip, void *data)
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
hammer2_ioctl_del_remote(hammer2_inode_t *ip, void *data)
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
hammer2_ioctl_rep_remote(hammer2_inode_t *ip, void *data)
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
hammer2_ioctl_get_socket(hammer2_inode_t *ip, void *data)
{
	return (EOPNOTSUPP);
}

/*
 * Set communications socket for connection
 */
static int
hammer2_ioctl_set_socket(hammer2_inode_t *ip, void *data)
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
