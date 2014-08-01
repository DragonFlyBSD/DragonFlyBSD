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

#include "hammer2.h"

/*
 * The snapshot is named <PFSNAME>_<YYYYMMDD.HHMMSS.TRANSID> unless
 * overridden by a label.
 *
 * When local non-cache media is involved the media is
 * first synchronized and the snapshot is then based on
 * the media.
 *
 * If the media is remote the snapshot is created on the remote
 * end (if you have sufficient administrative rights) and a local
 * ADMIN or CACHE PFS is created with a connection to the snapshot
 * on the remote.
 *
 * If the client has snapshot rights to multiple remotes then TBD.
 */

int
cmd_pfs_snapshot(const char *sel_path, const char *path, const char *label)
{
	hammer2_ioc_pfs_t pfs;
	int ecode = 0;
	int fd;
	char filename[HAMMER2_INODE_MAXNAME];
	char *xname;
	time_t t;
	struct tm *tp;

	if (path == NULL) {
		fd = hammer2_ioctl_handle(sel_path);
		xname = strdup("");
	} else {
		fd = open(path, O_RDONLY);
		if (fd < 0)
			fprintf(stderr, "Unable to open %s\n", path);
		if (strrchr(path, '/'))
			asprintf(&xname, ".%s", strrchr(path, '/') + 1);
		else if (*path)
			asprintf(&xname, ".%s", path);
		else
			xname = strdup("");
	}
	if (fd < 0)
		return 1;

	if (label == NULL) {
		time(&t);
		tp = localtime(&t);
		bzero(&pfs, sizeof(pfs));
		pfs.name_key = (hammer2_key_t)-1;
		if (ioctl(fd, HAMMER2IOC_PFS_GET, &pfs) < 0) {
			perror("ioctl");
		}
		snprintf(filename, sizeof(filename),
			 "%s%s.%04d%02d%02d.%02d%02d%02d",
			 pfs.name,
			 xname,
			 tp->tm_year + 1900,
			 tp->tm_mon + 1,
			 tp->tm_mday,
			 tp->tm_hour,
			 tp->tm_min,
			 tp->tm_sec);
		label = filename;
	}

	bzero(&pfs, sizeof(pfs));
	snprintf(pfs.name, sizeof(pfs.name), "%s", label);

	if (ioctl(fd, HAMMER2IOC_PFS_SNAPSHOT, &pfs) < 0) {
		perror("ioctl");
		ecode = 1;
	} else {
		printf("created snapshot %s\n", label);
	}
	return ecode;
}
