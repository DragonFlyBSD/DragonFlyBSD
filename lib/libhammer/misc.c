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
#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid.h>

#include "libhammer.h"

char *
libhammer_find_pfs_mount(int pfsid, uuid_t parentuuid, int ismaster)
{
	struct hammer_ioc_info hi;
	struct statfs *mntbuf;
	int mntsize;
	int curmount;
	int fd;
	size_t	mntbufsize;
	char *trailstr;
	char *retval;

	retval = NULL;

	/* Do not continue if there are no mounted filesystems */
	mntsize = getfsstat(NULL, 0, MNT_NOWAIT);
	if (mntsize <= 0)
		return retval;

	mntbufsize = (mntsize) * sizeof(struct statfs);
	mntbuf = _libhammer_malloc(mntbufsize);

	mntsize = getfsstat(mntbuf, (long)mntbufsize, MNT_NOWAIT);
	curmount = mntsize - 1;

	asprintf(&trailstr, ":%05d", pfsid);

	/*
	 * Iterate all the mounted points looking for the PFS passed to
	 * this function.
	 */
	while(curmount >= 0) {
		/*
		 * We need to avoid that PFS belonging to other HAMMER
		 * filesystems are showed as mounted, so we compare
		 * against the FSID, which is presumable to be unique.
		 */
		bzero(&hi, sizeof(hi));
		if ((fd = open(mntbuf[curmount].f_mntfromname, O_RDONLY)) < 0) {
			curmount--;
			continue;
		}

		if ((ioctl(fd, HAMMERIOC_GET_INFO, &hi)) < 0) {
			close(fd);
			curmount--;
			continue;
		}

		if (strstr(mntbuf[curmount].f_mntfromname, trailstr) != NULL &&
		    (uuid_compare(&hi.vol_fsid, &parentuuid, NULL)) == 0) {
			if (ismaster) {
				if (strstr(mntbuf[curmount].f_mntfromname,
				    "@@-1") != NULL) {
					retval =
					    strdup(mntbuf[curmount].f_mntonname);
					break;
				}
			} else {
				if (strstr(mntbuf[curmount].f_mntfromname,
				    "@@0x") != NULL ) {
					retval =
					    strdup(mntbuf[curmount].f_mntonname);
					break;
				}
			}
		}
		curmount--;
		close(fd);
	}
	free(trailstr);
	free(mntbuf);

	return retval;
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
