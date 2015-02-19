/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete <tuxillo@quantumachine.net>
 * by Matthew Dillon <dillon@backplane.com>
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
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "libhammer.h"

int
libhammer_pfs_get_snapshots(libhammer_fsinfo_t fip, libhammer_pfsinfo_t pip)
{
	struct hammer_snapshot_data *snapdata = NULL;
	struct hammer_ioc_snapshot snap;
	libhammer_snapinfo_t sip;
	libhammer_pfsinfo_t pfs0;
	char *path = NULL;
	int ret = 0;
	int fd;
	u_int i;

	assert(pip != NULL);
	assert(fip != NULL);

	/*
	 * Still need a path to open so, when not mounted, try
	 * to figure out the PFS path access in order to open(2)
	 * Note that this will fail for slave PFS that were created
	 * with pfs-slave directive since they don't have any transaction
	 * recorded and nlookup can't find them. For those we simply
	 * return the error in the head structure of libhammer_pfsinfo_t
	 * for the caller to handle the situation.
	 */
	pfs0 = libhammer_get_first_pfs(fip);
	if (pip->mountedon == NULL)
		libhammer_pfs_canonical_path(pfs0->mountedon, pip, &path);
	else
		path = strdup(pip->mountedon);

	if (path == NULL || (fd = open(path, O_RDONLY)) < 0) {
		pip->head.error = errno;
		ret = -1;
		goto out;
	}

	bzero(&snap, sizeof(snap));

	/*
	 * Loop while there are snapshots returned from the ioctl(2) call.
	 *
	 * For more information on how the snapshots are returned
	 * to userland please check sys/vfs/hammer/hammer_ioctl.c
	 */
	do {
		if (ioctl(fd, HAMMERIOC_GET_SNAPSHOT, &snap) < 0) {
			pip->head.error = errno;
			ret = -1;
			close(fd);
			goto out;
		}
		for (i = 0; i < snap.count; i++) {
			snapdata = &snap.snaps[i];
			sip = _libhammer_malloc(sizeof(*sip));
			sip->tid = snapdata->tid;
			sip->ts = snapdata->ts;
			if (strlen(snapdata->label))
				sprintf(sip->label, "%s", snapdata->label);
			else
				sip->label[0] = '\0';
			TAILQ_INSERT_TAIL(&pip->list_snap, sip, entries);
			pip->snapcount++;
		}
	} while (snap.head.error == 0 && snap.count);
	close(fd);

out:
	if (path)
		free(path);

	return (ret);
}

void
libhammer_pfs_free_snapshots(libhammer_pfsinfo_t pip)
{
	struct libhammer_snapinfo *si;

	while(!TAILQ_EMPTY(&pip->list_snap)) {
		si = TAILQ_FIRST(&pip->list_snap);
		TAILQ_REMOVE(&pip->list_snap, si, entries);
		free(si);
	}
}
