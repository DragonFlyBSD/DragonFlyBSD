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
char	*find_pfs_mount(int pfsid, uuid_t parentuuid, int ismaster);
double	percent(int64_t value, int64_t total);
u_int32_t count_snapshots (u_int32_t version,
    char *pfs_snapshots, char *mountedon, int *errorp);

void
hammer_cmd_info(void)
{
	struct statfs *stfsbuf;
	int mntsize, i, first = 1;
	char *fstype, *path;

	tzset();
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

void
show_info(char *path)
{
	struct	    hammer_pseudofs_data pfs_od;
	struct	    hammer_ioc_pseudofs_rw pfs;
	int64_t	    usedbigblocks;
	int64_t	    usedbytes, rsvbytes;
	int64_t	    totalbytes, freebytes;
	struct	    hammer_ioc_info info;
	int         fd, pfs_id, ismaster, error;
	char	    *fsid;
	char	    *mountedon;
	char	    buf[6];
	u_int32_t   sc;

	fsid = mountedon = NULL;
	usedbigblocks = 0;
	pfs_id = 0;	      /* Include PFS#0 */
	usedbytes = totalbytes = rsvbytes = freebytes = 0;
	sc = error = 0;

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
	fprintf(stdout, "\tLabel               %s\n", info.vol_name);
	fprintf(stdout, "\tNo. Volumes         %d\n", info.nvolumes);
	fprintf(stdout, "\tFSID                %s\n", fsid);
	fprintf(stdout, "\tHAMMER Version      %d\n", info.version);

	/* Big blocks information */
	usedbigblocks = info.bigblocks - info.freebigblocks;

	fprintf(stdout, "Big block information\n");
	fprintf(stdout, "\tTotal      %10jd\n", (intmax_t)info.bigblocks);
	fprintf(stdout, "\tUsed       %10jd (%.2lf%%)\n"
			"\tReserved   %10jd (%.2lf%%)\n"
			"\tFree       %10jd (%.2lf%%)\n",
			(intmax_t)usedbigblocks,
			percent(usedbigblocks, info.bigblocks),
			(intmax_t)info.rsvbigblocks,
			percent(info.rsvbigblocks, info.bigblocks),
			(intmax_t)(info.freebigblocks - info.rsvbigblocks),
			percent(info.freebigblocks - info.rsvbigblocks,
				info.bigblocks));
	fprintf(stdout, "Space information\n");

	/* Space information */
	totalbytes = (info.bigblocks << HAMMER_LARGEBLOCK_BITS);
	usedbytes = (usedbigblocks << HAMMER_LARGEBLOCK_BITS);
	rsvbytes = (info.rsvbigblocks << HAMMER_LARGEBLOCK_BITS);
	freebytes = ((info.freebigblocks - info.rsvbigblocks)
	    << HAMMER_LARGEBLOCK_BITS);

	fprintf(stdout, "\tNo. Inodes %10jd\n", (intmax_t)info.inodes);
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

	while(pfs_id < HAMMER_MAX_PFS) {
		bzero(&pfs, sizeof(pfs));
		bzero(&pfs_od, sizeof(pfs_od));
		pfs.pfs_id = pfs_id;
		pfs.ondisk = &pfs_od;
		pfs.bytes = sizeof(pfs_od);
		pfs.version = HAMMER_IOC_PSEUDOFS_VERSION;
		if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) >= 0) {
			ismaster = (pfs_od.mirror_flags & HAMMER_PFSD_SLAVE)
			    ? 0 : 1;
			if (pfs_id == 0)
				mountedon = strdup(path);
			else
				mountedon = find_pfs_mount(pfs_id,
				    info.vol_fsid, ismaster);

			sc = count_snapshots(info.version, pfs_od.snapshots,
			    mountedon, &error);

			fprintf(stdout, "\t%6d  %-6s",
			    pfs_id, (ismaster ? "MASTER" : "SLAVE"));

			snprintf(buf, 6, "%d", sc);
			fprintf(stdout, " %6s  ", (error && sc == 0) ? "-" : buf);

			if (mountedon)
				fprintf(stdout, "%s", mountedon);
			else
				fprintf(stdout, "not mounted");
			fprintf(stdout, "\n");
		}
		pfs_id++;
	}

	free(fsid);
	free(mountedon);
}

char *
find_pfs_mount(int pfsid, uuid_t parentuuid, int ismaster)
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
	mntbuf = malloc(mntbufsize);
	if (mntbuf == NULL) {
		perror("show_info");
		exit(EXIT_FAILURE);
	}

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
	}
	free(trailstr);
	return retval;
}

double
percent(int64_t value, int64_t total)
{
	/* Avoid divide-by-zero */
	if (total == 0)
		return 100.0;

	return ((value * 100.0) / (double)total);
}

u_int32_t
count_snapshots(u_int32_t version, char *pfs_snapshots, char *mountedon, int *errorp)
{
	struct hammer_ioc_snapshot snapinfo;
	char *snapshots_path, *fpath;
	struct dirent *den;
	struct stat st;
	DIR *dir;
	u_int32_t snapshot_count = 0;
	int fd;

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
				return 0;
			}
			snapshot_count += snapinfo.count;
		} while (snapinfo.head.error == 0 && snapinfo.count);
	}
	return snapshot_count;
}
