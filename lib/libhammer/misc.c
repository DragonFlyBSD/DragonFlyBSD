/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#include <assert.h>
#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid.h>

#include "libhammer.h"

char *
libhammer_find_pfs_mount(uuid_t *unique_uuid)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;
	struct statfs *mntbuf;
	int mntsize;
	int curmount;
	int fd;
	size_t	mntbufsize;
	uuid_t uuid;
	char *retval;

	retval = NULL;

	/* Do not continue if there are no mounted filesystems */
	mntsize = getfsstat(NULL, 0, MNT_NOWAIT);
	if (mntsize <= 0)
		return retval;

	mntbufsize = mntsize * sizeof(struct statfs);
	mntbuf = _libhammer_malloc(mntbufsize);

	mntsize = getfsstat(mntbuf, (long)mntbufsize, MNT_NOWAIT);
	curmount = mntsize - 1;

	/*
	 * Iterate all the mounted points looking for the PFS passed to
	 * this function.
	 */
	while(curmount >= 0) {
		struct statfs *mnt = &mntbuf[curmount];
		/*
		 * Discard any non null(5) or hammer(5) filesystems as synthetic
		 * filesystems like procfs(5) could accept ioctl calls and thus
		 * produce bogus results.
		 */
		if ((strcmp("hammer", mnt->f_fstypename) != 0) &&
		    (strcmp("null", mnt->f_fstypename) != 0)) {
			curmount--;
			continue;
		}
		bzero(&pfs, sizeof(pfs));
		bzero(&pfsd, sizeof(pfsd));
		pfs.pfs_id = -1;
		pfs.ondisk = &pfsd;
		pfs.bytes = sizeof(struct hammer_pseudofs_data);
		fd = open(mnt->f_mntonname, O_RDONLY);
		if (fd < 0 || (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0)) {
			close(fd);
			curmount--;
			continue;
		}

		memcpy(&uuid, &pfs.ondisk->unique_uuid, sizeof(uuid));
		if (uuid_compare(unique_uuid, &uuid, NULL) == 0) {
			retval = strdup(mnt->f_mntonname);
			close(fd);
			break;
		}

		curmount--;
		close(fd);
	}
	free(mntbuf);

	return retval;
}

/*
 * Find out the path that can be used to open(2) a PFS
 * when it is not mounted. It allocates *path so the
 * caller is in charge of freeing it up.
 */
void
libhammer_pfs_canonical_path(char * mtpt, libhammer_pfsinfo_t pip, char **path)
{
	struct statfs st;

	assert(pip != NULL);
	assert(mtpt != NULL);

	if ((statfs(mtpt, &st) < 0) ||
	    ((strcmp("hammer", st.f_fstypename) != 0) &&
	    (strcmp("null", st.f_fstypename) != 0))) {
		*path = NULL;
		return;
	}

	if (pip->ismaster)
		asprintf(path, "%s/@@-1:%.5d", mtpt,
		    pip->pfs_id);
	else
		asprintf(path, "%s/@@0x%016jx:%.5d", mtpt,
		    pip->end_tid, pip->pfs_id);
}


/*
 * Allocate len bytes of memory and return the pointer.
 * It'll exit in the case no memory could be allocated.
 *
 * To be used only by the library itself.
 */
void *
_libhammer_malloc(size_t len)
{
	void *m;

	m = calloc(len, sizeof(char));
	if (m == NULL)
		errx(1, "Failed to allocate %zd bytes", len);

	return (m);
}
