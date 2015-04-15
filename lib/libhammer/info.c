/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete <tuxillo@quantumachine.net>
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

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libhammer.h"

libhammer_fsinfo_t
libhammer_get_fsinfo(const char *path)
{
	struct hammer_pseudofs_data *pfs_od;
	struct hammer_ioc_pfs_iterate pi;
	struct hammer_ioc_info info;
	libhammer_pfsinfo_t pip;
	libhammer_fsinfo_t fip;
	int error = 0;
	int fd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return NULL;

	fip = _libhammer_malloc(sizeof(*fip));
	TAILQ_INIT(&fip->list_pseudo);
	if ((ioctl(fd, HAMMERIOC_GET_INFO, &info)) < 0) {
		libhammer_free_fsinfo(fip);
		close(fd);
		return NULL;
	}

	/* Fill filesystem information */
	snprintf(fip->vol_name, TXTLEN, "%s", info.vol_name);
	fip->vol_fsid = info.vol_fsid;
	fip->version = info.version;
	fip->nvolumes = info.nvolumes;
	fip->inodes = info.inodes;
	fip->bigblocks = info.bigblocks;
	fip->freebigblocks = info.freebigblocks;
	fip->rsvbigblocks = info.rsvbigblocks;

	bzero(&pi, sizeof(pi));
	pi.ondisk = _libhammer_malloc(sizeof(*pfs_od));
	while(error == 0) {
		error = ioctl(fd, HAMMERIOC_PFS_ITERATE, &pi);
		if (error == 0 &&
		    (pi.ondisk->mirror_flags & HAMMER_PFSD_DELETED) == 0) {
			pip = _libhammer_malloc(sizeof(*pip));
			pfs_od = pi.ondisk;
			pip->ismaster =
			    (pfs_od->mirror_flags & HAMMER_PFSD_SLAVE) ? 0 : 1;

			/*
			 * Fill in structs used in the library. We don't rely on
			 * HAMMER structs but we do fill our own.
			 */
			pip->version = fip->version;
			pip->pfs_id = pi.pos;
			pip->mirror_flags = pfs_od->mirror_flags;
			pip->beg_tid = pfs_od->sync_beg_tid;
			pip->end_tid = pfs_od->sync_end_tid;
			pip->mountedon =
			    libhammer_find_pfs_mount(&pfs_od->unique_uuid);
			if (fip->version < 3) {
				libhammer_compat_old_snapcount(pip);
			} else {
				TAILQ_INIT(&pip->list_snap);
				if (libhammer_pfs_get_snapshots(fip, pip) < 0)
					pip->snapcount = 0;
			}

			/*
			 * PFS retrieval goes 0..n so inserting in the tail
			 * leaves the TAILQ sorted by pfs_id.
			 * This is important since quickly getting PFS #0 is
			 * required for some functions so *DO NOT CHANGE IT*.
			 */
			TAILQ_INSERT_TAIL(&fip->list_pseudo, pip, entries);

		}
		pi.pos++;
	}
	free(pi.ondisk);

	close (fd);

	return (fip);
}

void
libhammer_free_fsinfo(libhammer_fsinfo_t fip)
{
	struct libhammer_pfsinfo *pfstmp;

	while(!TAILQ_EMPTY(&fip->list_pseudo)) {
		pfstmp = TAILQ_FIRST(&fip->list_pseudo);
		libhammer_pfs_free_snapshots(pfstmp);
		if (pfstmp->mountedon)
			free(pfstmp->mountedon);
		TAILQ_REMOVE(&fip->list_pseudo, pfstmp, entries);
		free(pfstmp);
	}
	free(fip);
}
