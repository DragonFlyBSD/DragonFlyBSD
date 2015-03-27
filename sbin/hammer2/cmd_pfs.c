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

#include "hammer2.h"

int
cmd_pfs_list(const char *sel_path)
{
	hammer2_ioc_pfs_t pfs;
	int ecode = 0;
	int count = 0;
	int fd;
	uint32_t status;
	char *pfs_id_str = NULL;

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&pfs, sizeof(pfs));

	while ((pfs.name_key = pfs.name_next) != (hammer2_key_t)-1) {
		if (ioctl(fd, HAMMER2IOC_PFS_GET, &pfs) < 0) {
			perror("ioctl");
			ecode = 1;
			break;
		}
		if (count == 0) {
			printf("Type        "
			       "ClusterId (pfs_clid)                 "
			       "Label\n");
		}
		switch(pfs.pfs_type) {
		case HAMMER2_PFSTYPE_NONE:
			printf("NONE        ");
			break;
		case HAMMER2_PFSTYPE_CACHE:
			printf("CACHE       ");
			break;
		case HAMMER2_PFSTYPE_COPY:
			printf("COPY        ");
			break;
		case HAMMER2_PFSTYPE_SLAVE:
			printf("SLAVE       ");
			break;
		case HAMMER2_PFSTYPE_SOFT_SLAVE:
			printf("SOFT_SLAVE  ");
			break;
		case HAMMER2_PFSTYPE_SOFT_MASTER:
			printf("SOFT_MASTER ");
			break;
		case HAMMER2_PFSTYPE_MASTER:
			printf("MASTER      ");
			break;
		case HAMMER2_PFSTYPE_SNAPSHOT:
			printf("SNAPSHOT    ");
			break;
		default:
			printf("%02x          ", pfs.pfs_type);
			break;
		}
		uuid_to_string(&pfs.pfs_clid, &pfs_id_str, &status);
		printf("%s ", pfs_id_str);
		free(pfs_id_str);
		pfs_id_str = NULL;
		printf("%s\n", pfs.name);
		++count;
	}
	close(fd);

	return (ecode);
}

int
cmd_pfs_getid(const char *sel_path, const char *name, int privateid)
{
	hammer2_ioc_pfs_t pfs;
	int ecode = 0;
	int fd;
	uint32_t status;
	char *pfs_id_str = NULL;

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&pfs, sizeof(pfs));

	snprintf(pfs.name, sizeof(pfs.name), "%s", name);
	if (ioctl(fd, HAMMER2IOC_PFS_LOOKUP, &pfs) < 0) {
		perror("ioctl");
		ecode = 1;
	} else {
		if (privateid)
			uuid_to_string(&pfs.pfs_fsid, &pfs_id_str, &status);
		else
			uuid_to_string(&pfs.pfs_clid, &pfs_id_str, &status);
		printf("%s\n", pfs_id_str);
		free(pfs_id_str);
		pfs_id_str = NULL;
	}
	close(fd);
	return (ecode);
}


int
cmd_pfs_create(const char *sel_path, const char *name,
	       uint8_t pfs_type, const char *uuid_str)
{
	hammer2_ioc_pfs_t pfs;
	int ecode = 0;
	int fd;
	uint32_t status;

	/*
	 * Default to MASTER if no uuid was specified.
	 * Default to SLAVE if a uuid was specified.
	 *
	 * When adding masters to a cluster, the new PFS must be added as
	 * a slave and then upgraded to ensure proper synchronization.
	 */
	if (pfs_type == HAMMER2_PFSTYPE_NONE) {
		if (uuid_str)
			pfs_type = HAMMER2_PFSTYPE_SLAVE;
		else
			pfs_type = HAMMER2_PFSTYPE_MASTER;
	}

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&pfs, sizeof(pfs));
	snprintf(pfs.name, sizeof(pfs.name), "%s", name);
	pfs.pfs_type = pfs_type;
	if (uuid_str) {
		uuid_from_string(uuid_str, &pfs.pfs_clid, &status);
	} else {
		uuid_create(&pfs.pfs_clid, &status);
	}
	if (status == uuid_s_ok)
		uuid_create(&pfs.pfs_fsid, &status);
	if (status == uuid_s_ok) {
		if (ioctl(fd, HAMMER2IOC_PFS_CREATE, &pfs) < 0) {
			if (errno == EEXIST) {
				fprintf(stderr,
					"NOTE: Typically the same name is "
					"used for cluster elements on "
					"different mounts,\n"
					"      but cluster elements on the "
					"same mount require unique names.\n"
					"pfs-create %s: already present\n",
					name);
			} else {
				perror("ioctl");
			}
			ecode = 1;
		}
	} else {
		fprintf(stderr, "hammer2: pfs_create: badly formed uuid\n");
		ecode = 1;
	}
	close(fd);
	return (ecode);
}

int
cmd_pfs_delete(const char *sel_path, const char *name)
{
	hammer2_ioc_pfs_t pfs;
	int ecode = 0;
	int fd;

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&pfs, sizeof(pfs));
	snprintf(pfs.name, sizeof(pfs.name), "%s", name);

	if (ioctl(fd, HAMMER2IOC_PFS_DELETE, &pfs) < 0) {
		fprintf(stderr, "hammer2: pfs_delete(%s): %s\n",
			name, strerror(errno));
		ecode = 1;
	}
	close(fd);

	return (ecode);
}
