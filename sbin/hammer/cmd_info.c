/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 *
 */

#include <libutil.h>
#include <libhammer.h>

#include "hammer.h"

void show_info(char *path);
static double percent(int64_t value, int64_t total);

void
hammer_cmd_info(char **av, int ac)
{
	struct statfs *stfsbuf;
	int mntsize, i, first = 1;
	char *fstype, *path;

	tzset();

	if (ac > 0) {
                while (ac) {
                        show_info(*av);
                        --ac;
                        ++av;
			if (ac)
				printf("\n");
                }
	} else {
		mntsize = getmntinfo(&stfsbuf, MNT_NOWAIT);
		if (mntsize > 0) {
			for (i = 0; i < mntsize; i++) {
				fstype = stfsbuf[i].f_fstypename;
				path = stfsbuf[i].f_mntonname;
				if ((strcmp(fstype, "hammer")) == 0) {
					if (first)
						first = 0;
					else
						printf("\n");
					show_info(path);
				}
			}
			if (first)
				printf("No mounted HAMMER filesystems found\n");
		} else {
			printf("No mounted filesystems found\n");
		}
	}
}

void
show_info(char *path)
{
	libhammer_fsinfo_t fip;
	libhammer_pfsinfo_t pi, pi_first;
	struct hammer_ioc_volume_list ioc;
	int64_t	    usedbigblocks;
	int64_t	    usedbytes, rsvbytes;
	int64_t	    totalbytes, freebytes;
	char	    *fsid;
	char	    buf[6];
	char	    rootvol[MAXPATHLEN];
	int i;

	fsid = NULL;
	usedbigblocks = 0;

	usedbytes = totalbytes = rsvbytes = freebytes = 0;

	fip = libhammer_get_fsinfo(path);
	if (fip == NULL) {
		perror("libhammer_get_fsinfo");
		exit(EXIT_FAILURE);
	}

	/* Find out the UUID strings */
	uuid_to_string(&fip->vol_fsid, &fsid, NULL);

	/* Get the volume paths */
	if (hammer_fs_to_vol(path, &ioc) == -1) {
		fprintf(stderr, "Failed to get volume paths\n");
		exit(1);
	}

	/* Get the root volume path */
	if (hammer_fs_to_rootvol(path, rootvol, sizeof(rootvol)) == -1) {
		fprintf(stderr, "Failed to get root volume path\n");
		exit(1);
	}

	/* Volume information */
	printf("Volume identification\n");
	printf("\tLabel               %s\n", fip->vol_name);
	printf("\tNo. Volumes         %d\n", fip->nvolumes);
	printf("\tHAMMER Volumes      ");
	for (i = 0; i < ioc.nvols; i++) {
		printf("%s", ioc.vols[i].device_name);
		if (i != ioc.nvols - 1)
			printf(":");
	}
	printf("\n");
	printf("\tRoot Volume         %s\n", rootvol);
	printf("\tFSID                %s\n", fsid);
	printf("\tHAMMER Version      %d\n", fip->version);

	/* Big-blocks information */
	usedbigblocks = fip->bigblocks - fip->freebigblocks;

	printf("Big-block information\n");
	printf("\tTotal      %10jd\n", (intmax_t)fip->bigblocks);
	printf("\tUsed       %10jd (%.2lf%%)\n"
	       "\tReserved   %10jd (%.2lf%%)\n"
	       "\tFree       %10jd (%.2lf%%)\n",
		(intmax_t)usedbigblocks,
		percent(usedbigblocks, fip->bigblocks),
		(intmax_t)fip->rsvbigblocks,
		percent(fip->rsvbigblocks, fip->bigblocks),
		(intmax_t)(fip->freebigblocks - fip->rsvbigblocks),
		percent(fip->freebigblocks - fip->rsvbigblocks, fip->bigblocks));
	printf("Space information\n");

	/* Space information */
	totalbytes = (fip->bigblocks << HAMMER_BIGBLOCK_BITS);
	usedbytes = (usedbigblocks << HAMMER_BIGBLOCK_BITS);
	rsvbytes = (fip->rsvbigblocks << HAMMER_BIGBLOCK_BITS);
	freebytes = ((fip->freebigblocks - fip->rsvbigblocks)
	    << HAMMER_BIGBLOCK_BITS);

	printf("\tNo. Inodes %10jd\n", (intmax_t)fip->inodes);
	humanize_number(buf, sizeof(buf)  - (totalbytes < 0 ? 0 : 1),
	    totalbytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	printf("\tTotal size     %6s (%jd bytes)\n",
	    buf, (intmax_t)totalbytes);

	humanize_number(buf, sizeof(buf)  - (usedbytes < 0 ? 0 : 1),
	    usedbytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	printf("\tUsed           %6s (%.2lf%%)\n", buf,
	    percent(usedbytes, totalbytes));

	humanize_number(buf, sizeof(buf)  - (rsvbytes < 0 ? 0 : 1),
	    rsvbytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	printf("\tReserved       %6s (%.2lf%%)\n", buf,
	    percent(rsvbytes, totalbytes));

	humanize_number(buf, sizeof(buf)  - (freebytes < 0 ? 0 : 1),
	    freebytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	printf("\tFree           %6s (%.2lf%%)\n", buf,
	    percent(freebytes, totalbytes));

	/* Pseudo-filesystem information */
	printf("PFS information\n");
	printf("\tPFS ID  Mode    Snaps\n");

	/* Iterate all the PFSs found */
	pi_first = libhammer_get_first_pfs(fip);
	for (pi = pi_first; pi != NULL; pi = libhammer_get_next_pfs(pi)) {
		printf("\t%6d  %-6s",
		    pi->pfs_id, (pi->ismaster ? "MASTER" : "SLAVE"));

		snprintf(buf, 6, "%d", pi->snapcount);
		printf(" %6s\n", (pi->head.error && pi->snapcount == 0) ? "-" : buf);
	}

	free(fsid);

	libhammer_free_fsinfo(fip);

}

static double
percent(int64_t value, int64_t total)
{
	/* Avoid divide-by-zero */
	if (total == 0)
		return 100.0;

	return ((value * 100.0) / (double)total);
}
