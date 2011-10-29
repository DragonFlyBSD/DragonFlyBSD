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

#include <libhammer.h>

static u_int32_t count_snapshots(u_int32_t, char *, char *, int *);

libhammer_volinfo_t
libhammer_get_volinfo(const char *path)
{
	struct hammer_ioc_pseudofs_rw pseudofs;
	struct hammer_pseudofs_data *pfs_od;
	struct hammer_ioc_info info;
	libhammer_pfsinfo_t pfstmp;
	libhammer_volinfo_t hvi;
	int pfs_id;
	int fd;

	hvi = _libhammer_malloc(sizeof(*hvi));

	TAILQ_INIT(&hvi->list_pseudo);

	if ((fd = open(path, O_RDONLY)) < 0)
		goto error1;

	if ((ioctl(fd, HAMMERIOC_GET_INFO, &info)) < 0)
		goto error1;

	/* Fill volume information */
	snprintf(hvi->vol_name, TXTLEN, "%s", info.vol_name);
	hvi->vol_fsid = info.vol_fsid;
	hvi->version = info.version;
	hvi->nvolumes = info.nvolumes;
	hvi->inodes = info.inodes;
	hvi->bigblocks = info.bigblocks;
	hvi->freebigblocks = info.freebigblocks;
	hvi->rsvbigblocks = info.rsvbigblocks;

	/*
	 * XXX - By now we iterate the whole range of +65000 possible
	 * pseudofs to discover the ones that are in use. Of course
	 * this is suboptimal. Best way would be to extract the
	 * pseudofs information from the control records in the
	 * B-Tree, that's in kernel land.
	 */
	pfs_od = _libhammer_malloc(sizeof(*pfs_od));

	for(pfs_id = 0; pfs_id < HAMMER_MAX_PFS; pfs_id++) {
		/* Clear the struct to be passed to the ioctl. */
		bzero(&pseudofs, sizeof(pseudofs));

		pfstmp = _libhammer_malloc(sizeof(*pfstmp));

		pseudofs.pfs_id = pfs_id;
		pseudofs.ondisk = pfs_od;
		pseudofs.bytes = sizeof(struct hammer_pseudofs_data);
		pseudofs.version = HAMMER_IOC_PSEUDOFS_VERSION;
		if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pseudofs) != -1) {
			/* XXX
			 * It may be completely wrong in the case that we pass a path
			 * that is in PFS 0 but isn't the mount path of it. There's no
			 * easy way to do this.
			 */
			pfstmp->ismaster = (pfs_od->mirror_flags & HAMMER_PFSD_SLAVE)
			    ? 0 : 1;

			if (pfs_id == 0)
				pfstmp->mountedon = strdup(path);
			else
				pfstmp->mountedon = libhammer_find_pfs_mount(pfs_id,
				    hvi->vol_fsid, pfstmp->ismaster);
			/*
			 * Fill in structs used in the library. We don't rely on
			 * HAMMER own struct but we do fill our own.
			 */
			pfstmp->version = pseudofs.version;
			pfstmp->pfs_id = pseudofs.pfs_id;
			pfstmp->mirror_flags = pfs_od->mirror_flags;
			pfstmp->snapcount = count_snapshots(hvi->version,
			    pfstmp->snapshots, pfstmp->mountedon, &pfstmp->head.error);

			TAILQ_INSERT_TAIL(&hvi->list_pseudo, pfstmp, entries);
		}
	}

	goto end;

error1:
	libhammer_free_volinfo(hvi);
	if (fd != -1)
		close(fd);

end:
	return (hvi);
}

void
libhammer_free_volinfo(libhammer_volinfo_t volinfo)
{
	struct libhammer_pfsinfo *pfstmp;

	while(!TAILQ_EMPTY(&volinfo->list_pseudo)) {
		pfstmp = TAILQ_FIRST(&volinfo->list_pseudo);
		free(pfstmp->mountedon);
		TAILQ_REMOVE(&volinfo->list_pseudo, pfstmp, entries);
		free(pfstmp);
	}
	free(volinfo);
}

static u_int32_t
count_snapshots(u_int32_t version, char *pfs_snapshots, char *mountedon, int *errorp)
{
	struct hammer_ioc_snapshot snapinfo;
	char *snapshots_path, *fpath;
	struct dirent *den;
	struct stat st;
	DIR *dir;
	u_int32_t snapshot_count;
	int fd;

	snapshot_count = 0;

	bzero(&snapinfo, sizeof(struct hammer_ioc_snapshot));

	fd = open(mountedon, O_RDONLY);
	if (fd < 0) {
		*errorp = errno;
		return 0;
	}

	if (version < 3) {
		/*
		 * old style: count the number of softlinks in the snapshots dir
		 */
		if (pfs_snapshots[0])
			snapshots_path = pfs_snapshots;
		else
			asprintf(&snapshots_path, "%s/snapshots", mountedon);
		if ((dir = opendir(snapshots_path)) != NULL) {
			while ((den = readdir(dir)) != NULL) {
				if (den->d_name[0] == '.')
					continue;
				asprintf(&fpath, "%s/%s", snapshots_path,
				    den->d_name);
				if (lstat(fpath, &st) == 0 &&
				    S_ISLNK(st.st_mode))
					snapshot_count++;
				free(fpath);
			}
			closedir(dir);
		}
	} else {
		/*
		 * new style: file system meta-data
		 */
		do {
			if (ioctl(fd, HAMMERIOC_GET_SNAPSHOT, &snapinfo) < 0) {
				*errorp = errno;
				goto out;
			}

			snapshot_count += snapinfo.count;
		} while (snapinfo.head.error == 0 && snapinfo.count);
	}

out:
	if (fd != -1)
		close(fd);
	return snapshot_count;
}
