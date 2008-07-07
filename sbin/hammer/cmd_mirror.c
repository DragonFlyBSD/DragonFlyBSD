/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
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
 * 
 * $DragonFly: src/sbin/hammer/cmd_mirror.c,v 1.4 2008/07/07 00:27:22 dillon Exp $
 */

#include "hammer.h"

#define SERIALBUF_SIZE	(512 * 1024)

struct hammer_pfs_head {
	u_int32_t version;
	struct hammer_pseudofs_data pfsd;
};

static int read_mrecords(int fd, char *buf, u_int size,
			 hammer_ioc_mrecord_t pickup);
static struct hammer_ioc_mrecord *read_mrecord(int fdin, int *errorp,
			 hammer_ioc_mrecord_t pickup);
static void write_mrecord(int fdout, u_int32_t type, void *payload, int bytes);
static void generate_mrec_header(int fd, int fdout,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static void validate_mrec_header(int fd, int fdin,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static void update_pfs_snapshot(int fd, hammer_tid_t snapshot_tid);
static void mirror_usage(int code);

void
hammer_cmd_mirror_read(char **av, int ac)
{
	struct hammer_ioc_mirror_rw mirror;
	hammer_ioc_mrecord_t mrec;
	hammer_tid_t sync_tid;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	int interrupted = 0;
	int error;
	int fd;
	int n;
	time_t base_t = time(NULL);

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];

	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);

	/*
	 * Write out the PFS header
	 */
	generate_mrec_header(fd, 1, &mirror.tid_beg, &mirror.tid_end);
	hammer_get_cycle(&mirror.key_beg, &mirror.tid_beg);

	fprintf(stderr, "mirror-read: Mirror from %016llx to %016llx\n",
		mirror.tid_beg, mirror.tid_end);
	if (mirror.key_beg.obj_id != (int64_t)HAMMER_MIN_OBJID) {
		fprintf(stderr, "mirror-read: Resuming at object %016llx\n",
			mirror.key_beg.obj_id);
	}

	/*
	 * Write out bulk records
	 */
	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;
	if (ac == 2)
		mirror.tid_beg = strtoull(av[1], NULL, 0);

	do {
		mirror.count = 0;
		if (ioctl(fd, HAMMERIOC_MIRROR_READ, &mirror) < 0) {
			fprintf(stderr, "Mirror-read %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
		if (mirror.count) {
			n = write(1, mirror.ubuf, mirror.count);
			if (n != mirror.count) {
				fprintf(stderr, "Mirror-read %s failed: "
						"short write\n",
				filesystem);
				exit(1);
			}
		}
		mirror.key_beg = mirror.key_cur;
		if (TimeoutOpt &&
		    (unsigned)(time(NULL) - base_t) > (unsigned)TimeoutOpt) {
			fprintf(stderr,
				"Mirror-read %s interrupted by timer at"
				" %016llx\n",
				filesystem,
				mirror.key_cur.obj_id);
			interrupted = 1;
			break;
		}
	} while (mirror.count != 0);

	/*
	 * Write out the termination sync record
	 */
	write_mrecord(1, HAMMER_MREC_TYPE_SYNC, NULL, 0);

	/*
	 * If the -2 option was given (automatic when doing mirror-copy),
	 * a two-way pipe is assumed and we expect a response mrec from
	 * the target.
	 */
	if (TwoWayPipeOpt) {
		mrec = read_mrecord(0, &error, NULL);
		if (mrec == NULL || mrec->type != HAMMER_MREC_TYPE_UPDATE) {
			fprintf(stderr, "mirror_read: Did not get final "
					"acknowledgement packet from target\n");
			exit(1);
		}
		if (interrupted) {
			if (CyclePath) {
				hammer_set_cycle(&mirror.key_cur, mirror.tid_beg);
				fprintf(stderr, "Cyclefile %s updated for continuation\n", CyclePath);
			}
		} else {
			sync_tid = *(hammer_tid_t *)(mrec + 1);
			if (CyclePath) {
				hammer_key_beg_init(&mirror.key_beg);
				hammer_set_cycle(&mirror.key_beg, sync_tid);
				fprintf(stderr, "Cyclefile %s updated to 0x%016llx\n",
					CyclePath, sync_tid);
			} else {
				fprintf(stderr, "Source can update synctid "
						"to 0x%016llx\n",
					sync_tid);
			}
		}
	} else if (CyclePath) {
		/* NOTE! mirror.tid_beg cannot be updated */
		fprintf(stderr, "Warning: cycle file (-c option) cannot be "
				"fully updated unless you use mirror-copy\n");
		hammer_set_cycle(&mirror.key_beg, mirror.tid_beg);
	}
	fprintf(stderr, "Mirror-read %s succeeded\n", filesystem);
}

void
hammer_cmd_mirror_write(char **av, int ac)
{
	struct hammer_ioc_mirror_rw mirror;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	struct hammer_ioc_mrecord pickup;
	struct hammer_ioc_synctid synctid;
	hammer_ioc_mrecord_t mrec;
	int error;
	int fd;

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];

	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);

	/*
	 * Read and process the PFS header 
	 */
	validate_mrec_header(fd, 0, &mirror.tid_beg, &mirror.tid_end);

	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;

	pickup.signature = 0;
	pickup.type = 0;

	/*
	 * Read and process bulk records
	 */
	for (;;) {
		mirror.count = 0;
		mirror.size = read_mrecords(0, buf, SERIALBUF_SIZE, &pickup);
		if (mirror.size <= 0)
			break;
		if (ioctl(fd, HAMMERIOC_MIRROR_WRITE, &mirror) < 0) {
			fprintf(stderr, "Mirror-write %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
#if 0
		if (mirror.head.flags & HAMMER_IOC_HEAD_INTR) {
			fprintf(stderr,
				"Mirror-write %s interrupted by timer at"
				" %016llx\n",
				filesystem,
				mirror.key_cur.obj_id);
			exit(0);
		}
#endif
		mirror.key_beg = mirror.key_cur;
	}

	/*
	 * Read and process the termination sync record.
	 */
	mrec = read_mrecord(0, &error, &pickup);
	if (mrec == NULL || mrec->type != HAMMER_MREC_TYPE_SYNC) {
		fprintf(stderr, "Mirror-write %s: Did not get termination "
				"sync record\n",
				filesystem);
	}

	/*
	 * Update the PFS info on the target so the user has visibility
	 * into the new snapshot.
	 */
	update_pfs_snapshot(fd, mirror.tid_end);

	/*
	 * Sync the target filesystem
	 */
	bzero(&synctid, sizeof(synctid));
	synctid.op = HAMMER_SYNCTID_SYNC2;
	ioctl(fd, HAMMERIOC_SYNCTID, &synctid);

	fprintf(stderr, "Mirror-write %s: succeeded\n", filesystem);

	/*
	 * Report back to the originator.
	 */
	if (TwoWayPipeOpt) {
		write_mrecord(1, HAMMER_MREC_TYPE_UPDATE,
			      &mirror.tid_end, sizeof(mirror.tid_end));
	} else {
		printf("Source can update synctid to 0x%016llx\n",
		       mirror.tid_end);
	}
}

void
hammer_cmd_mirror_dump(void)
{
	char *buf = malloc(SERIALBUF_SIZE);
	struct hammer_ioc_mrecord pickup;
	hammer_ioc_mrecord_t mrec;
	int error;
	int size;

	/*
	 * Read and process the PFS header 
	 */
	pickup.signature = 0;
	pickup.type = 0;

	mrec = read_mrecord(0, &error, &pickup);

	/*
	 * Read and process bulk records
	 */
	for (;;) {
		size = read_mrecords(0, buf, SERIALBUF_SIZE, &pickup);
		if (size <= 0)
			break;
		mrec = (void *)buf;
		while (mrec < (hammer_ioc_mrecord_t)((char *)buf + size)) {
			printf("Record obj=%016llx key=%016llx "
			       "rt=%02x ot=%02x\n",
				mrec->leaf.base.obj_id,
				mrec->leaf.base.key,
				mrec->leaf.base.rec_type,
				mrec->leaf.base.obj_type);
			printf("       tids %016llx:%016llx data=%d\n",
				mrec->leaf.base.create_tid,
				mrec->leaf.base.delete_tid,
				mrec->leaf.data_len);
			mrec = (void *)((char *)mrec + mrec->rec_size);
		}
	}

	/*
	 * Read and process the termination sync record.
	 */
	mrec = read_mrecord(0, &error, &pickup);
	if (mrec == NULL || mrec->type != HAMMER_MREC_TYPE_SYNC) {
		fprintf(stderr, "Mirror-dump: Did not get termination "
				"sync record\n");
	}
}

void
hammer_cmd_mirror_copy(char **av, int ac)
{
	pid_t pid1;
	pid_t pid2;
	int fds[2];
	const char *xav[16];
	char tbuf[16];
	char *ptr;
	int xac;

	if (ac != 2)
		mirror_usage(1);

	if (pipe(fds) < 0) {
		perror("pipe");
		exit(1);
	}

	TwoWayPipeOpt = 1;

	/*
	 * Source
	 */
	if ((pid1 = fork()) == 0) {
		dup2(fds[0], 0);
		dup2(fds[0], 1);
		close(fds[0]);
		close(fds[1]);
		if ((ptr = strchr(av[0], ':')) != NULL) {
			*ptr++ = 0;
			xac = 0;
			xav[xac++] = "ssh";
			xav[xac++] = av[0];
			xav[xac++] = "hammer";
			if (VerboseOpt)
				xav[xac++] = "-v";
			xav[xac++] = "-2";
			if (TimeoutOpt) {
				snprintf(tbuf, sizeof(tbuf), "%d", TimeoutOpt);
				xav[xac++] = "-t";
				xav[xac++] = tbuf;
			}
			xav[xac++] = "mirror-read";
			xav[xac++] = ptr;
			xav[xac++] = NULL;
			execv("/usr/bin/ssh", (void *)xav);
		} else {
			hammer_cmd_mirror_read(av, 1);
			fflush(stdout);
			fflush(stderr);
		}
		_exit(1);
	}

	/*
	 * Target
	 */
	if ((pid2 = fork()) == 0) {
		dup2(fds[1], 0);
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
		if ((ptr = strchr(av[1], ':')) != NULL) {
			*ptr++ = 0;
			xac = 0;
			xav[xac++] = "ssh";
			xav[xac++] = av[1];
			xav[xac++] = "hammer";
			if (VerboseOpt)
				xav[xac++] = "-v";
			xav[xac++] = "-2";
			xav[xac++] = "mirror-write";
			xav[xac++] = ptr;
			xav[xac++] = NULL;
			execv("/usr/bin/ssh", (void *)xav);
		} else {
			hammer_cmd_mirror_write(av + 1, 1);
			fflush(stdout);
			fflush(stderr);
		}
		_exit(1);
	}
	close(fds[0]);
	close(fds[1]);

	while (waitpid(pid1, NULL, 0) <= 0)
		;
	while (waitpid(pid2, NULL, 0) <= 0)
		;
}

/*
 * Read and return multiple mrecords
 */
static int
read_mrecords(int fd, char *buf, u_int size, hammer_ioc_mrecord_t pickup)
{
	u_int count;
	size_t n;
	size_t i;

	count = 0;
	while (size - count >= HAMMER_MREC_HEADSIZE) {
		/*
		 * Cached the record header in case we run out of buffer
		 * space.
		 */
		if (pickup->signature == 0) {
			for (n = 0; n < HAMMER_MREC_HEADSIZE; n += i) {
				i = read(fd, (char *)pickup + n,
					 HAMMER_MREC_HEADSIZE - n);
				if (i <= 0)
					break;
			}
			if (n == 0)
				break;
			if (n != HAMMER_MREC_HEADSIZE) {
				fprintf(stderr, "read_mrecords: short read on pipe\n");
				exit(1);
			}

			if (pickup->signature != HAMMER_IOC_MIRROR_SIGNATURE) {
				fprintf(stderr, "read_mrecords: malformed record on pipe, bad signature\n");
				exit(1);
			}
			if (pickup->rec_crc != crc32((char *)pickup + HAMMER_MREC_CRCOFF, HAMMER_MREC_HEADSIZE - HAMMER_MREC_CRCOFF)) {
				fprintf(stderr, "read_mrecords: malformed record on pipe, bad crc\n");
				exit(1);
			}
		}
		if (pickup->rec_size < HAMMER_MREC_HEADSIZE ||
		    pickup->rec_size > HAMMER_MREC_HEADSIZE + HAMMER_XBUFSIZE) {
			fprintf(stderr, "read_mrecords: malformed record on pipe, illegal rec_size\n");
			exit(1);
		}
		if (HAMMER_MREC_HEADSIZE + pickup->leaf.data_len > pickup->rec_size) {
			fprintf(stderr, "read_mrecords: malformed record on pipe, illegal element data_len\n");
			exit(1);
		}

		/*
		 * Stop if we have insufficient space for the record and data.
		 */
		if (size - count < pickup->rec_size)
			break;

		/*
		 * Stop if the record type is not HAMMER_MREC_TYPE_REC
		 */
		if (pickup->type != HAMMER_MREC_TYPE_REC)
			break;

		/*
		 * Read the remainder and clear the pickup signature.
		 */
		bcopy(pickup, buf + count, HAMMER_MREC_HEADSIZE);
		pickup->signature = 0;
		pickup->type = 0;
		for (n = HAMMER_MREC_HEADSIZE; n < pickup->rec_size; n += i) {
			i = read(fd, buf + count + n, pickup->rec_size - n);
			if (i <= 0)
				break;
		}
		if (n != pickup->rec_size) {
			fprintf(stderr, "read_mrecords: short read on pipe\n");
			exit(1);
		}
		if (pickup->leaf.data_len && pickup->leaf.data_offset) {
			if (hammer_crc_test_leaf(buf + count + HAMMER_MREC_HEADSIZE, &pickup->leaf) == 0) {
				fprintf(stderr, "read_mrecords: data_crc did not match data! obj=%016llx key=%016llx\n", pickup->leaf.base.obj_id, pickup->leaf.base.key);
				fprintf(stderr, "continuing, but there are problems\n");
			}
		}

		count += pickup->rec_size;
	}
	return(count);
}

/*
 * Read and return a single mrecord.  The returned mrec->rec_size will be
 * adjusted to be the size of the payload.
 */
static
struct hammer_ioc_mrecord *
read_mrecord(int fdin, int *errorp, hammer_ioc_mrecord_t pickup)
{
	hammer_ioc_mrecord_t mrec;
	struct hammer_ioc_mrecord mrechd;
	size_t bytes;
	size_t n;
	size_t i;

	if (pickup && pickup->type != 0) {
		mrechd = *pickup;
		pickup->signature = 0;
		pickup->type = 0;
		n = HAMMER_MREC_HEADSIZE;
	} else {
		/*
		 * Read in the PFSD header from the sender.
		 */
		for (n = 0; n < HAMMER_MREC_HEADSIZE; n += i) {
			i = read(fdin, (char *)&mrechd + n, HAMMER_MREC_HEADSIZE - n);
			if (i <= 0)
				break;
		}
		if (n == 0) {
			*errorp = 0;	/* EOF */
			return(NULL);
		}
		if (n != HAMMER_MREC_HEADSIZE) {
			fprintf(stderr, "short read of mrecord header\n");
			*errorp = EPIPE;
			return(NULL);
		}
	}
	if (mrechd.signature != HAMMER_IOC_MIRROR_SIGNATURE) {
		fprintf(stderr, "read_mrecord: bad signature\n");
		*errorp = EINVAL;
		return(NULL);
	}
	bytes = mrechd.rec_size;
	if (bytes < HAMMER_MREC_HEADSIZE)
		bytes = (int)HAMMER_MREC_HEADSIZE;
	mrec = malloc(bytes);
	*mrec = mrechd;
	while (n < bytes) {
		i = read(fdin, (char *)mrec + n, bytes - n);
		if (i <= 0)
			break;
		n += i;
	}
	if (n != bytes) {
		fprintf(stderr, "read_mrecord: short read on payload\n");
		*errorp = EPIPE;
		return(NULL);
	}
	if (mrec->rec_crc != crc32((char *)mrec + HAMMER_MREC_CRCOFF,
				   bytes - HAMMER_MREC_CRCOFF)) {
		fprintf(stderr, "read_mrecord: bad CRC\n");
		*errorp = EINVAL;
		return(NULL);
	}
	mrec->rec_size -= HAMMER_MREC_HEADSIZE;
	*errorp = 0;
	return(mrec);
}

static
void
write_mrecord(int fdout, u_int32_t type, void *payload, int bytes)
{
	hammer_ioc_mrecord_t mrec;

	mrec = malloc(HAMMER_MREC_HEADSIZE + bytes);
	bzero(mrec, sizeof(*mrec));
	mrec->signature = HAMMER_IOC_MIRROR_SIGNATURE;
	mrec->type = type;
	mrec->rec_size = HAMMER_MREC_HEADSIZE + bytes;
	bcopy(payload, mrec + 1, bytes);
	mrec->rec_crc = crc32((char *)mrec + HAMMER_MREC_CRCOFF,
				   mrec->rec_size - HAMMER_MREC_CRCOFF);
	if (write(fdout, mrec, mrec->rec_size) != (int)mrec->rec_size) {
		fprintf(stderr, "write_mrecord: error %d (%s)\n",
			errno, strerror(errno));
		exit(1);
	}
	free(mrec);
}

/*
 * Generate a mirroring header with the pfs information of the
 * originating filesytem.
 */
static void
generate_mrec_header(int fd, int fdout,
		     hammer_tid_t *tid_begp, hammer_tid_t *tid_endp)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pfs_head pfs_head;

	bzero(&pfs, sizeof(pfs));
	bzero(&pfs_head, sizeof(pfs_head));
	pfs.ondisk = &pfs_head.pfsd;
	pfs.bytes = sizeof(pfs_head.pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		fprintf(stderr, "mirror-read: not a HAMMER fs/pseudofs!\n");
		exit(1);
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		fprintf(stderr, "mirror-read: HAMMER pfs version mismatch!\n");
		exit(1);
	}

	/*
	 * sync_beg_tid - lowest TID on source after which a full history
	 *	 	  is available.
	 *
	 * sync_end_tid - highest fully synchronized TID from source.
	 */
	*tid_begp = pfs_head.pfsd.sync_beg_tid;
	*tid_endp = pfs_head.pfsd.sync_end_tid;

	pfs_head.version = pfs.version;
	write_mrecord(fdout, HAMMER_MREC_TYPE_PFSD,
		      &pfs_head, sizeof(pfs_head));
}

/*
 * Validate the pfs information from the originating filesystem
 * against the target filesystem.  shared_uuid must match.
 */
static void
validate_mrec_header(int fd, int fdin,
		     hammer_tid_t *tid_begp, hammer_tid_t *tid_endp)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pfs_head *pfs_head;
	struct hammer_pseudofs_data pfsd;
	hammer_ioc_mrecord_t mrec;
	size_t bytes;
	int error;

	/*
	 * Get the PFSD info from the target filesystem.
	 */
	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		fprintf(stderr, "mirror-write: not a HAMMER fs/pseudofs!\n");
		exit(1);
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		fprintf(stderr, "mirror-write: HAMMER pfs version mismatch!\n");
		exit(1);
	}

	mrec = read_mrecord(fdin, &error, NULL);
	if (mrec == NULL) {
		if (error == 0)
			fprintf(stderr, "validate_mrec_header: short read\n");
		exit(1);
	}
	if (mrec->type != HAMMER_MREC_TYPE_PFSD) {
		fprintf(stderr, "validate_mrec_header: did not get expected "
				"PFSD record type\n");
		exit(1);
	}
	pfs_head = (void *)(mrec + 1);
	bytes = mrec->rec_size;	/* post-adjusted for payload */
	if (bytes != sizeof(*pfs_head)) {
		fprintf(stderr, "validate_mrec_header: unexpected payload "
				"size\n");
		exit(1);
	}
	if (pfs_head->version != pfs.version) {
		fprintf(stderr, "validate_mrec_header: Version mismatch\n");
		exit(1);
	}

	/*
	 * Whew.  Ok, is the read PFS info compatible with the target?
	 */
	if (bcmp(&pfs_head->pfsd.shared_uuid, &pfsd.shared_uuid, sizeof(pfsd.shared_uuid)) != 0) {
		fprintf(stderr, "mirror-write: source and target have different shared_uuid's!\n");
		exit(1);
	}
	if ((pfsd.mirror_flags & HAMMER_PFSD_SLAVE) == 0) {
		fprintf(stderr, "mirror-write: target must be in slave mode\n");
		exit(1);
	}
	*tid_begp = pfs_head->pfsd.sync_beg_tid;
	*tid_endp = pfs_head->pfsd.sync_end_tid;
	free(mrec);
}

static void
update_pfs_snapshot(int fd, hammer_tid_t snapshot_tid)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;

	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		perror("update_pfs_snapshot (read)");
		exit(1);
	}
	pfsd.sync_beg_tid = snapshot_tid;
	if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) != 0) {
		perror("update_pfs_snapshot (rewrite)");
		exit(1);
	}
}


static void
mirror_usage(int code)
{
	fprintf(stderr, 
		"hammer mirror-read <filesystem>\n"
		"hammer mirror-write <filesystem>\n"
		"hammer mirror-dump\n"
		"hammer mirror-copy [[user@]host:]fs [[user@]host:]fs\n"
	);
	exit(code);
}
