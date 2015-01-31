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
#include <libhammer.h>
#include <libutil.h>

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
						fprintf(stdout, "\n");
					show_info(path);
				}
			}
		} else {
			fprintf(stdout, "No mounted filesystems found\n");
		}
	}
}

void
show_info(char *path)
{
	libhammer_volinfo_t hvi;
	libhammer_pfsinfo_t pi, pi_first;
	int64_t	    usedbigblocks;
	int64_t	    usedbytes, rsvbytes;
	int64_t	    totalbytes, freebytes;
	char	    *fsid;
	char	    buf[6];

	fsid = NULL;
	usedbigblocks = 0;

	usedbytes = totalbytes = rsvbytes = freebytes = 0;

	hvi = libhammer_get_volinfo(path);
	if (hvi == NULL) {
		perror("libhammer_get_volinfo");
		exit(EXIT_FAILURE);
	}

	/* Find out the UUID strings */
	uuid_to_string(&hvi->vol_fsid, &fsid, NULL);

	/* Volume information */
	fprintf(stdout, "Volume identification\n");
	fprintf(stdout, "\tLabel               %s\n", hvi->vol_name);
	fprintf(stdout, "\tNo. Volumes         %d\n", hvi->nvolumes);
	fprintf(stdout, "\tFSID                %s\n", fsid);
	fprintf(stdout, "\tHAMMER Version      %d\n", hvi->version);

	/* Big blocks information */
	usedbigblocks = hvi->bigblocks - hvi->freebigblocks;

	fprintf(stdout, "Big block information\n");
	fprintf(stdout, "\tTotal      %10jd\n", (intmax_t)hvi->bigblocks);
	fprintf(stdout, "\tUsed       %10jd (%.2lf%%)\n"
			"\tReserved   %10jd (%.2lf%%)\n"
			"\tFree       %10jd (%.2lf%%)\n",
			(intmax_t)usedbigblocks,
			percent(usedbigblocks, hvi->bigblocks),
			(intmax_t)hvi->rsvbigblocks,
			percent(hvi->rsvbigblocks, hvi->bigblocks),
			(intmax_t)(hvi->freebigblocks - hvi->rsvbigblocks),
			percent(hvi->freebigblocks - hvi->rsvbigblocks,
				hvi->bigblocks));
	fprintf(stdout, "Space information\n");

	/* Space information */
	totalbytes = (hvi->bigblocks << HAMMER_BIGBLOCK_BITS);
	usedbytes = (usedbigblocks << HAMMER_BIGBLOCK_BITS);
	rsvbytes = (hvi->rsvbigblocks << HAMMER_BIGBLOCK_BITS);
	freebytes = ((hvi->freebigblocks - hvi->rsvbigblocks)
	    << HAMMER_BIGBLOCK_BITS);

	fprintf(stdout, "\tNo. Inodes %10jd\n", (intmax_t)hvi->inodes);
	humanize_number(buf, sizeof(buf)  - (totalbytes < 0 ? 0 : 1),
	    totalbytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tTotal size     %6s (%jd bytes)\n",
	    buf, (intmax_t)totalbytes);

	humanize_number(buf, sizeof(buf)  - (usedbytes < 0 ? 0 : 1),
	    usedbytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tUsed           %6s (%.2lf%%)\n", buf,
	    percent(usedbytes, totalbytes));

	humanize_number(buf, sizeof(buf)  - (rsvbytes < 0 ? 0 : 1),
	    rsvbytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tReserved       %6s (%.2lf%%)\n", buf,
	    percent(rsvbytes, totalbytes));

	humanize_number(buf, sizeof(buf)  - (freebytes < 0 ? 0 : 1),
	    freebytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tFree           %6s (%.2lf%%)\n", buf,
	    percent(freebytes, totalbytes));

	/* Pseudo-filesystem information */
	fprintf(stdout, "PFS information\n");
	fprintf(stdout, "\tPFS ID  Mode    Snaps  Mounted on\n");

	/* Iterate all the PFSs found */
	pi_first = libhammer_get_first_pfs(hvi);
	for (pi = pi_first; pi != NULL; pi = libhammer_get_next_pfs(pi)) {
		fprintf(stdout, "\t%6d  %-6s",
		    pi->pfs_id, (pi->ismaster ? "MASTER" : "SLAVE"));

		snprintf(buf, 6, "%d", pi->snapcount);
		fprintf(stdout, " %6s  ", (pi->head.error && pi->snapcount == 0) ? "-" : buf);

		if (pi->mountedon)
			fprintf(stdout, "%s", pi->mountedon);
		else
			fprintf(stdout, "not mounted");

		fprintf(stdout, "\n");
	}

	free(fsid);

	libhammer_free_volinfo(hvi);

}

static double
percent(int64_t value, int64_t total)
{
	/* Avoid divide-by-zero */
	if (total == 0)
		return 100.0;

	return ((value * 100.0) / (double)total);
}
