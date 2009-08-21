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
#include "hammer.h"
#include <libutil.h>

void	show_info(char *path);
double	percent(int64_t value, int64_t total);

void
hammer_cmd_info(void)
{
	struct statfs *stfsbuf;
	int mntsize, i;
	char *fstype, *path;

	tzset();
	mntsize = getmntinfo(&stfsbuf, MNT_NOWAIT);
	if (mntsize > 0) {
		for (i=0; i < mntsize; i++) {
			fstype = stfsbuf[i].f_fstypename;
			path = stfsbuf[i].f_mntonname;
			if ((strcmp(fstype, "hammer")) == 0)
				show_info(path);
		}
	}
	else
		fprintf(stdout, "No mounted filesystems found\n");

}

void show_info(char *path) {

	int64_t	usedbigblocks, bytes;
	struct	hammer_ioc_info info;
	char	buf[6];
	int 	fd;
	char 	*fsid, *fstype;

	fsid = fstype = NULL;
	usedbigblocks = 0;
	bytes = 0;

	bzero(&info, sizeof(struct hammer_ioc_info));

	/* Try to get a file descriptor based on the path given */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("show_info");
		exit(EXIT_FAILURE);
	}

	if ((ioctl(fd, HAMMERIOC_GET_INFO, &info)) < 0) {
		perror("show_info");
		exit(EXIT_FAILURE);
	}

	/* Find out the UUID strings */
	uuid_to_string(&info.vol_fsid, &fsid, NULL);

	/* Volume information */
	fprintf(stdout, "Volume identification\n");
	fprintf(stdout, "\tLabel          %s\n", info.vol_name);
	fprintf(stdout, "\tNo. Volumes    %d\n", info.nvolumes);
	fprintf(stdout, "\tFSID           %s\n", fsid);

	/* Big blocks information */
	usedbigblocks = info.bigblocks - info.freebigblocks;

	fprintf(stdout, "Big block information\n");
	fprintf(stdout, "\tTotal\t       %jd\n", (intmax_t)info.bigblocks);
	fprintf(stdout, "\tUsed\t       %jd (%.2lf%%)\n\tReserved       "
				       "%jd (%.2lf%%)\n\tFree\t       "
				       "%jd (%.2lf%%)\n",
			(intmax_t)usedbigblocks,
			percent(usedbigblocks, info.bigblocks),
			(intmax_t)info.rsvbigblocks,
			percent(info.rsvbigblocks, info.bigblocks),
			(intmax_t)(info.freebigblocks - info.rsvbigblocks),
			percent(info.freebigblocks - info.rsvbigblocks,
				info.bigblocks));
	fprintf(stdout, "Space information\n");

	/* Space information */
	bytes = (info.bigblocks << HAMMER_LARGEBLOCK_BITS);
	humanize_number(buf, sizeof(buf)  - (bytes < 0 ? 0 : 1), bytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tTotal size     %6s (%jd bytes)\n",
		buf, (intmax_t)bytes);

	bytes = (usedbigblocks << HAMMER_LARGEBLOCK_BITS);
	humanize_number(buf, sizeof(buf)  - (bytes < 0 ? 0 : 1), bytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tUsed space     %6s\n", buf);

	bytes = (info.rsvbigblocks << HAMMER_LARGEBLOCK_BITS);
	humanize_number(buf, sizeof(buf)  - (bytes < 0 ? 0 : 1), bytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tReserved space %6s\n", buf);

	bytes = ((info.freebigblocks - info.rsvbigblocks) << HAMMER_LARGEBLOCK_BITS);
	humanize_number(buf, sizeof(buf)  - (bytes < 0 ? 0 : 1), bytes, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE | HN_B);
	fprintf(stdout, "\tFree space     %6s\n\n", buf);
}

double percent(int64_t value, int64_t total) {

	/* Avoid divide-by-zero */
	if (value == 0)
		value = 1;

	return ((value * 100.0) / (double)total);
}
