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
 * $DragonFly: src/sbin/hammer/cmd_mirror.c,v 1.12 2008/07/31 06:01:31 dillon Exp $
 */

#include "hammer.h"

#define SERIALBUF_SIZE	(512 * 1024)

static int read_mrecords(int fd, char *buf, u_int size,
			 hammer_ioc_mrecord_head_t pickup);
static hammer_ioc_mrecord_any_t read_mrecord(int fdin, int *errorp,
			 hammer_ioc_mrecord_head_t pickup);
static void write_mrecord(int fdout, u_int32_t type,
			 hammer_ioc_mrecord_any_t mrec, int bytes);
static void generate_mrec_header(int fd, int fdout, int pfs_id,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static int validate_mrec_header(int fd, int fdin, int is_target, int pfs_id,
			 struct hammer_ioc_mrecord_head *pickup,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static void update_pfs_snapshot(int fd, hammer_tid_t snapshot_tid, int pfs_id);
static ssize_t writebw(int fd, const void *buf, size_t nbytes,
			u_int64_t *bwcount, struct timeval *tv1);
static void mirror_usage(int code);

/*
 * Generate a mirroring data stream from the specific source over the
 * entire key range, but restricted to the specified transaction range.
 *
 * The HAMMER VFS does most of the work, we add a few new mrecord
 * types to negotiate the TID ranges and verify that the entire
 * stream made it to the destination.
 */
void
hammer_cmd_mirror_read(char **av, int ac, int streaming)
{
	struct hammer_ioc_mirror_rw mirror;
	struct hammer_ioc_pseudofs_rw pfs;
	union hammer_ioc_mrecord_any mrec_tmp;
	struct hammer_ioc_mrecord_head pickup;
	hammer_ioc_mrecord_any_t mrec;
	hammer_tid_t sync_tid;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	int interrupted = 0;
	int error;
	int fd;
	int n;
	int didwork;
	int64_t total_bytes;
	time_t base_t = time(NULL);
	struct timeval bwtv;
	u_int64_t bwcount;

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];

	pickup.signature = 0;
	pickup.type = 0;

again:
	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = getpfs(&pfs, filesystem);

	if (streaming && VerboseOpt) {
		fprintf(stderr, "\nRunning");
		fflush(stderr);
	}
	total_bytes = 0;
	gettimeofday(&bwtv, NULL);
	bwcount = 0;

	/*
	 * In 2-way mode the target will send us a PFS info packet
	 * first.  Use the target's current snapshot TID as our default
	 * begin TID.
	 */
	mirror.tid_beg = 0;
	if (TwoWayPipeOpt) {
		n = validate_mrec_header(fd, 0, 0, pfs.pfs_id, &pickup,
					 NULL, &mirror.tid_beg);
		if (n < 0) {	/* got TERM record */
			relpfs(fd, &pfs);
			return;
		}
		++mirror.tid_beg;
	}

	/*
	 * Write out the PFS header, tid_beg will be updated if our PFS
	 * has a larger begin sync.  tid_end is set to the latest source
	 * TID whos flush cycle has completed.
	 */
	generate_mrec_header(fd, 1, pfs.pfs_id,
			     &mirror.tid_beg, &mirror.tid_end);

	/* XXX streaming mode support w/ cycle or command line arg */
	/*
	 * A cycle file overrides the beginning TID
	 */
	hammer_get_cycle(&mirror.key_beg, &mirror.tid_beg);

	if (ac == 2)
		mirror.tid_beg = strtoull(av[1], NULL, 0);

	if (streaming == 0 || VerboseOpt >= 2) {
		fprintf(stderr,
			"Mirror-read: Mirror from %016llx to %016llx\n",
			mirror.tid_beg, mirror.tid_end);
	}
	if (mirror.key_beg.obj_id != (int64_t)HAMMER_MIN_OBJID) {
		fprintf(stderr, "Mirror-read: Resuming at object %016llx\n",
			mirror.key_beg.obj_id);
	}

	/*
	 * Nothing to do if begin equals end.
	 */
	if (mirror.tid_beg >= mirror.tid_end) {
		if (streaming == 0 || VerboseOpt >= 2)
			fprintf(stderr, "Mirror-read: No work to do\n");
		didwork = 0;
		goto done;
	}
	didwork = 1;

	/*
	 * Write out bulk records
	 */
	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;

	do {
		mirror.count = 0;
		mirror.pfs_id = pfs.pfs_id;
		mirror.shared_uuid = pfs.ondisk->shared_uuid;
		if (ioctl(fd, HAMMERIOC_MIRROR_READ, &mirror) < 0) {
			fprintf(stderr, "Mirror-read %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
		if (mirror.head.flags & HAMMER_IOC_HEAD_ERROR) {
			fprintf(stderr,
				"Mirror-read %s fatal error %d\n",
				filesystem, mirror.head.error);
			exit(1);
		}
		if (mirror.count) {
			if (BandwidthOpt) {
				n = writebw(1, mirror.ubuf, mirror.count,
					    &bwcount, &bwtv);
			} else {
				n = write(1, mirror.ubuf, mirror.count);
			}
			if (n != mirror.count) {
				fprintf(stderr, "Mirror-read %s failed: "
						"short write\n",
				filesystem);
				exit(1);
			}
		}
		total_bytes += mirror.count;
		if (streaming && VerboseOpt) {
			fprintf(stderr, "\r%016llx %11lld",
				mirror.key_cur.obj_id,
				total_bytes);
			fflush(stderr);
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

done:
	/*
	 * Write out the termination sync record - only if not interrupted
	 */
	if (interrupted == 0) {
		if (didwork) {
			write_mrecord(1, HAMMER_MREC_TYPE_SYNC,
				      &mrec_tmp, sizeof(mrec_tmp.sync));
		} else {
			write_mrecord(1, HAMMER_MREC_TYPE_IDLE,
				      &mrec_tmp, sizeof(mrec_tmp.sync));
		}
	}

	/*
	 * If the -2 option was given (automatic when doing mirror-copy),
	 * a two-way pipe is assumed and we expect a response mrec from
	 * the target.
	 */
	if (TwoWayPipeOpt) {
		mrec = read_mrecord(0, &error, &pickup);
		if (mrec == NULL || 
		    mrec->head.type != HAMMER_MREC_TYPE_UPDATE ||
		    mrec->head.rec_size != sizeof(mrec->update)) {
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
			sync_tid = mrec->update.tid;
			if (CyclePath) {
				hammer_key_beg_init(&mirror.key_beg);
				hammer_set_cycle(&mirror.key_beg, sync_tid);
				fprintf(stderr, "Cyclefile %s updated to 0x%016llx\n",
					CyclePath, sync_tid);
			}
		}
	} else if (CyclePath) {
		/* NOTE! mirror.tid_beg cannot be updated */
		fprintf(stderr, "Warning: cycle file (-c option) cannot be "
				"fully updated unless you use mirror-copy\n");
		hammer_set_cycle(&mirror.key_beg, mirror.tid_beg);
	}
	if (streaming && interrupted == 0) {
		time_t t1 = time(NULL);
		time_t t2;

		if (VerboseOpt) {
			fprintf(stderr, " W");
			fflush(stderr);
		}
		pfs.ondisk->sync_end_tid = mirror.tid_end;
		if (ioctl(fd, HAMMERIOC_WAI_PSEUDOFS, &pfs) < 0) {
			fprintf(stderr, "Mirror-read %s: cannot stream: %s\n",
				filesystem, strerror(errno));
		} else {
			t2 = time(NULL) - t1;
			if (t2 >= 0 && t2 < DelayOpt) {
				if (VerboseOpt) {
					fprintf(stderr, "\bD");
					fflush(stderr);
				}
				sleep(DelayOpt - t2);
			}
			if (VerboseOpt) {
				fprintf(stderr, "\b ");
				fflush(stderr);
			}
			relpfs(fd, &pfs);
			goto again;
		}
	}
	write_mrecord(1, HAMMER_MREC_TYPE_TERM,
		      &mrec_tmp, sizeof(mrec_tmp.sync));
	relpfs(fd, &pfs);
	fprintf(stderr, "Mirror-read %s succeeded\n", filesystem);
}

/*
 * Pipe the mirroring data stream on stdin to the HAMMER VFS, adding
 * some additional packet types to negotiate TID ranges and to verify
 * completion.  The HAMMER VFS does most of the work.
 *
 * It is important to note that the mirror.key_{beg,end} range must
 * match the ranged used by the original.  For now both sides use
 * range the entire key space.
 *
 * It is even more important that the records in the stream conform
 * to the TID range also supplied in the stream.  The HAMMER VFS will
 * use the REC, PASS, and SKIP record types to track the portions of
 * the B-Tree being scanned in order to be able to proactively delete
 * records on the target within those active areas that are not mentioned
 * by the source.
 *
 * The mirror.key_cur field is used by the VFS to do this tracking.  It
 * must be initialized to key_beg but then is persistently updated by
 * the HAMMER VFS on each successive ioctl() call.  If you blow up this
 * field you will blow up the mirror target, possibly to the point of
 * deleting everything.  As a safety measure the HAMMER VFS simply marks
 * the records that the source has destroyed as deleted on the target,
 * and normal pruning operations will deal with their final disposition
 * at some later time.
 */
void
hammer_cmd_mirror_write(char **av, int ac)
{
	struct hammer_ioc_mirror_rw mirror;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_ioc_mrecord_head pickup;
	struct hammer_ioc_synctid synctid;
	union hammer_ioc_mrecord_any mrec_tmp;
	hammer_ioc_mrecord_any_t mrec;
	int error;
	int fd;
	int n;

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];

	pickup.signature = 0;
	pickup.type = 0;

again:
	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);
	mirror.key_end = mirror.key_beg;

	fd = getpfs(&pfs, filesystem);

	/*
	 * In two-way mode the target writes out a PFS packet first.
	 * The source uses our tid_end as its tid_beg by default,
	 * picking up where it left off.
	 */
	mirror.tid_beg = 0;
	if (TwoWayPipeOpt) {
		generate_mrec_header(fd, 1, pfs.pfs_id,
				     &mirror.tid_beg, &mirror.tid_end);
	}

	/*
	 * Read and process the PFS header.  The source informs us of
	 * the TID range the stream represents.
	 */
	n = validate_mrec_header(fd, 0, 1, pfs.pfs_id, &pickup,
				 &mirror.tid_beg, &mirror.tid_end);
	if (n < 0) {	/* got TERM record */
		relpfs(fd, &pfs);
		return;
	}

	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;

	/*
	 * Read and process bulk records (REC, PASS, and SKIP types).
	 *
	 * On your life, do NOT mess with mirror.key_cur or your mirror
	 * target may become history.
	 */
	for (;;) {
		mirror.count = 0;
		mirror.pfs_id = pfs.pfs_id;
		mirror.shared_uuid = pfs.ondisk->shared_uuid;
		mirror.size = read_mrecords(0, buf, SERIALBUF_SIZE, &pickup);
		if (mirror.size <= 0)
			break;
		if (ioctl(fd, HAMMERIOC_MIRROR_WRITE, &mirror) < 0) {
			fprintf(stderr, "Mirror-write %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
		if (mirror.head.flags & HAMMER_IOC_HEAD_ERROR) {
			fprintf(stderr,
				"Mirror-write %s fatal error %d\n",
				filesystem, mirror.head.error);
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
	}

	/*
	 * Read and process the termination sync record.
	 */
	mrec = read_mrecord(0, &error, &pickup);

	if (mrec && mrec->head.type == HAMMER_MREC_TYPE_TERM) {
		fprintf(stderr, "Mirror-write: received termination request\n");
		free(mrec);
		return;
	}

	if (mrec == NULL || 
	    (mrec->head.type != HAMMER_MREC_TYPE_SYNC &&
	     mrec->head.type != HAMMER_MREC_TYPE_IDLE) ||
	    mrec->head.rec_size != sizeof(mrec->sync)) {
		fprintf(stderr, "Mirror-write %s: Did not get termination "
				"sync record, or rec_size is wrong rt=%d\n",
				filesystem, mrec->head.type);
		exit(1);
	}

	/*
	 * Update the PFS info on the target so the user has visibility
	 * into the new snapshot, and sync the target filesystem.
	 */
	if (mrec->head.type == HAMMER_MREC_TYPE_SYNC) {
		update_pfs_snapshot(fd, mirror.tid_end, pfs.pfs_id);

		bzero(&synctid, sizeof(synctid));
		synctid.op = HAMMER_SYNCTID_SYNC2;
		ioctl(fd, HAMMERIOC_SYNCTID, &synctid);

		if (VerboseOpt >= 2) {
			fprintf(stderr, "Mirror-write %s: succeeded\n",
				filesystem);
		}
	}

	free(mrec);
	mrec = NULL;

	/*
	 * Report back to the originator.
	 */
	if (TwoWayPipeOpt) {
		mrec_tmp.update.tid = mirror.tid_end;
		write_mrecord(1, HAMMER_MREC_TYPE_UPDATE,
			      &mrec_tmp, sizeof(mrec_tmp.update));
	} else {
		printf("Source can update synctid to 0x%016llx\n",
		       mirror.tid_end);
	}
	relpfs(fd, &pfs);
	goto again;
}

void
hammer_cmd_mirror_dump(void)
{
	char *buf = malloc(SERIALBUF_SIZE);
	struct hammer_ioc_mrecord_head pickup;
	hammer_ioc_mrecord_any_t mrec;
	int error;
	int size;
	int offset;
	int bytes;

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
		offset = 0;
		while (offset < size) {
			mrec = (void *)((char *)buf + offset);
			bytes = HAMMER_HEAD_DOALIGN(mrec->head.rec_size);
			if (offset + bytes > size) {
				fprintf(stderr, "Misaligned record\n");
				exit(1);
			}

			switch(mrec->head.type) {
			case HAMMER_MREC_TYPE_REC:
				printf("Record obj=%016llx key=%016llx "
				       "rt=%02x ot=%02x\n",
					mrec->rec.leaf.base.obj_id,
					mrec->rec.leaf.base.key,
					mrec->rec.leaf.base.rec_type,
					mrec->rec.leaf.base.obj_type);
				printf("       tids %016llx:%016llx data=%d\n",
					mrec->rec.leaf.base.create_tid,
					mrec->rec.leaf.base.delete_tid,
					mrec->rec.leaf.data_len);
				break;
			case HAMMER_MREC_TYPE_PASS:
				printf("Pass   obj=%016llx key=%016llx "
				       "rt=%02x ot=%02x\n",
					mrec->rec.leaf.base.obj_id,
					mrec->rec.leaf.base.key,
					mrec->rec.leaf.base.rec_type,
					mrec->rec.leaf.base.obj_type);
				printf("       tids %016llx:%016llx data=%d\n",
					mrec->rec.leaf.base.create_tid,
					mrec->rec.leaf.base.delete_tid,
					mrec->rec.leaf.data_len);
				break;
			case HAMMER_MREC_TYPE_SKIP:
				printf("Skip   obj=%016llx key=%016llx rt=%02x to\n"
				       "       obj=%016llx key=%016llx rt=%02x\n",
				       mrec->skip.skip_beg.obj_id,
				       mrec->skip.skip_beg.key,
				       mrec->skip.skip_beg.rec_type,
				       mrec->skip.skip_end.obj_id,
				       mrec->skip.skip_end.key,
				       mrec->skip.skip_end.rec_type);
			default:
				break;
			}
			offset += bytes;
		}
	}

	/*
	 * Read and process the termination sync record.
	 */
	mrec = read_mrecord(0, &error, &pickup);
	if (mrec == NULL || 
	    (mrec->head.type != HAMMER_MREC_TYPE_SYNC &&
	     mrec->head.type != HAMMER_MREC_TYPE_IDLE)
	 ) {
		fprintf(stderr, "Mirror-dump: Did not get termination "
				"sync record\n");
	}
}

void
hammer_cmd_mirror_copy(char **av, int ac, int streaming)
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

			switch(VerboseOpt) {
			case 0:
				break;
			case 1:
				xav[xac++] = "-v";
				break;
			case 2:
				xav[xac++] = "-vv";
				break;
			default:
				xav[xac++] = "-vvv";
				break;
			}
			xav[xac++] = "-2";
			if (TimeoutOpt) {
				snprintf(tbuf, sizeof(tbuf), "%d", TimeoutOpt);
				xav[xac++] = "-t";
				xav[xac++] = tbuf;
			}
			if (streaming)
				xav[xac++] = "mirror-read-streaming";
			else
				xav[xac++] = "mirror-read";
			xav[xac++] = ptr;
			xav[xac++] = NULL;
			execv("/usr/bin/ssh", (void *)xav);
		} else {
			hammer_cmd_mirror_read(av, 1, streaming);
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

			switch(VerboseOpt) {
			case 0:
				break;
			case 1:
				xav[xac++] = "-v";
				break;
			case 2:
				xav[xac++] = "-vv";
				break;
			default:
				xav[xac++] = "-vvv";
				break;
			}

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
read_mrecords(int fd, char *buf, u_int size, hammer_ioc_mrecord_head_t pickup)
{
	hammer_ioc_mrecord_any_t mrec;
	u_int count;
	size_t n;
	size_t i;
	size_t bytes;

	count = 0;
	while (size - count >= HAMMER_MREC_HEADSIZE) {
		/*
		 * Cached the record header in case we run out of buffer
		 * space.
		 */
		fflush(stdout);
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
		}
		if (pickup->rec_size < HAMMER_MREC_HEADSIZE ||
		    pickup->rec_size > sizeof(*mrec) + HAMMER_XBUFSIZE) {
			fprintf(stderr, "read_mrecords: malformed record on pipe, illegal rec_size\n");
			exit(1);
		}

		/*
		 * Stop if we have insufficient space for the record and data.
		 */
		bytes = HAMMER_HEAD_DOALIGN(pickup->rec_size);
		if (size - count < bytes)
			break;

		/*
		 * Stop if the record type is not a REC or a SKIP (the only
		 * two types the ioctl supports.  Other types are used only
		 * by the userland protocol).
		 */
		if (pickup->type != HAMMER_MREC_TYPE_REC &&
		    pickup->type != HAMMER_MREC_TYPE_SKIP &&
		    pickup->type != HAMMER_MREC_TYPE_PASS) {
			break;
		}

		/*
		 * Read the remainder and clear the pickup signature.
		 */
		for (n = HAMMER_MREC_HEADSIZE; n < bytes; n += i) {
			i = read(fd, buf + count + n, bytes - n);
			if (i <= 0)
				break;
		}
		if (n != bytes) {
			fprintf(stderr, "read_mrecords: short read on pipe\n");
			exit(1);
		}

		bcopy(pickup, buf + count, HAMMER_MREC_HEADSIZE);
		pickup->signature = 0;
		pickup->type = 0;
		mrec = (void *)(buf + count);

		/*
		 * Validate the completed record
		 */
		if (mrec->head.rec_crc !=
		    crc32((char *)mrec + HAMMER_MREC_CRCOFF,
			  mrec->head.rec_size - HAMMER_MREC_CRCOFF)) {
			fprintf(stderr, "read_mrecords: malformed record "
					"on pipe, bad crc\n");
			exit(1);
		}

		/*
		 * If its a B-Tree record validate the data crc
		 */
		if (mrec->head.type == HAMMER_MREC_TYPE_REC) {
			if (mrec->head.rec_size <
			    sizeof(mrec->rec) + mrec->rec.leaf.data_len) {
				fprintf(stderr, 
					"read_mrecords: malformed record on "
					"pipe, illegal element data_len\n");
				exit(1);
			}
			if (mrec->rec.leaf.data_len &&
			    mrec->rec.leaf.data_offset &&
			    hammer_crc_test_leaf(&mrec->rec + 1, &mrec->rec.leaf) == 0) {
				fprintf(stderr,
					"read_mrecords: data_crc did not "
					"match data! obj=%016llx key=%016llx\n",
					mrec->rec.leaf.base.obj_id,
					mrec->rec.leaf.base.key);
				fprintf(stderr,
					"continuing, but there are problems\n");
			}
		}
		count += bytes;
	}
	return(count);
}

/*
 * Read and return a single mrecord.
 */
static
hammer_ioc_mrecord_any_t
read_mrecord(int fdin, int *errorp, hammer_ioc_mrecord_head_t pickup)
{
	hammer_ioc_mrecord_any_t mrec;
	struct hammer_ioc_mrecord_head mrechd;
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
	bytes = HAMMER_HEAD_DOALIGN(mrechd.rec_size);
	assert(bytes >= sizeof(mrechd));
	mrec = malloc(bytes);
	mrec->head = mrechd;

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
	if (mrec->head.rec_crc != 
	    crc32((char *)mrec + HAMMER_MREC_CRCOFF,
		  mrec->head.rec_size - HAMMER_MREC_CRCOFF)) {
		fprintf(stderr, "read_mrecord: bad CRC\n");
		*errorp = EINVAL;
		return(NULL);
	}
	*errorp = 0;
	return(mrec);
}

static
void
write_mrecord(int fdout, u_int32_t type, hammer_ioc_mrecord_any_t mrec,
	      int bytes)
{
	char zbuf[HAMMER_HEAD_ALIGN];
	int pad;

	pad = HAMMER_HEAD_DOALIGN(bytes) - bytes;

	assert(bytes >= (int)sizeof(mrec->head));
	bzero(&mrec->head, sizeof(mrec->head));
	mrec->head.signature = HAMMER_IOC_MIRROR_SIGNATURE;
	mrec->head.type = type;
	mrec->head.rec_size = bytes;
	mrec->head.rec_crc = crc32((char *)mrec + HAMMER_MREC_CRCOFF,
				   bytes - HAMMER_MREC_CRCOFF);
	if (write(fdout, mrec, bytes) != bytes) {
		fprintf(stderr, "write_mrecord: error %d (%s)\n",
			errno, strerror(errno));
		exit(1);
	}
	if (pad) {
		bzero(zbuf, pad);
		if (write(fdout, zbuf, pad) != pad) {
			fprintf(stderr, "write_mrecord: error %d (%s)\n",
				errno, strerror(errno));
			exit(1);
		}
	}
}

/*
 * Generate a mirroring header with the pfs information of the
 * originating filesytem.
 */
static void
generate_mrec_header(int fd, int fdout, int pfs_id,
		     hammer_tid_t *tid_begp, hammer_tid_t *tid_endp)
{
	struct hammer_ioc_pseudofs_rw pfs;
	union hammer_ioc_mrecord_any mrec_tmp;

	bzero(&pfs, sizeof(pfs));
	bzero(&mrec_tmp, sizeof(mrec_tmp));
	pfs.pfs_id = pfs_id;
	pfs.ondisk = &mrec_tmp.pfs.pfsd;
	pfs.bytes = sizeof(mrec_tmp.pfs.pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		fprintf(stderr, "Mirror-read: not a HAMMER fs/pseudofs!\n");
		exit(1);
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		fprintf(stderr, "Mirror-read: HAMMER pfs version mismatch!\n");
		exit(1);
	}

	/*
	 * sync_beg_tid - lowest TID on source after which a full history
	 *	 	  is available.
	 *
	 * sync_end_tid - highest fully synchronized TID from source.
	 */
	if (tid_begp && *tid_begp < mrec_tmp.pfs.pfsd.sync_beg_tid)
		*tid_begp = mrec_tmp.pfs.pfsd.sync_beg_tid;
	if (tid_endp)
		*tid_endp = mrec_tmp.pfs.pfsd.sync_end_tid;
	mrec_tmp.pfs.version = pfs.version;
	write_mrecord(fdout, HAMMER_MREC_TYPE_PFSD,
		      &mrec_tmp, sizeof(mrec_tmp.pfs));
}

/*
 * Validate the pfs information from the originating filesystem
 * against the target filesystem.  shared_uuid must match.
 *
 * return -1 if we got a TERM record
 */
static int
validate_mrec_header(int fd, int fdin, int is_target, int pfs_id,
		     struct hammer_ioc_mrecord_head *pickup,
		     hammer_tid_t *tid_begp, hammer_tid_t *tid_endp)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;
	hammer_ioc_mrecord_any_t mrec;
	int error;

	/*
	 * Get the PFSD info from the target filesystem.
	 */
	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.pfs_id = pfs_id;
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

	mrec = read_mrecord(fdin, &error, pickup);
	if (mrec == NULL) {
		if (error == 0)
			fprintf(stderr, "validate_mrec_header: short read\n");
		exit(1);
	}
	if (mrec->head.type == HAMMER_MREC_TYPE_TERM) {
		free(mrec);
		return(-1);
	}

	if (mrec->head.type != HAMMER_MREC_TYPE_PFSD) {
		fprintf(stderr, "validate_mrec_header: did not get expected "
				"PFSD record type\n");
		exit(1);
	}
	if (mrec->head.rec_size != sizeof(mrec->pfs)) {
		fprintf(stderr, "validate_mrec_header: unexpected payload "
				"size\n");
		exit(1);
	}
	if (mrec->pfs.version != pfs.version) {
		fprintf(stderr, "validate_mrec_header: Version mismatch\n");
		exit(1);
	}

	/*
	 * Whew.  Ok, is the read PFS info compatible with the target?
	 */
	if (bcmp(&mrec->pfs.pfsd.shared_uuid, &pfsd.shared_uuid,
		 sizeof(pfsd.shared_uuid)) != 0) {
		fprintf(stderr, 
			"mirror-write: source and target have "
			"different shared-uuid's!\n");
		exit(1);
	}
	if (is_target &&
	    (pfsd.mirror_flags & HAMMER_PFSD_SLAVE) == 0) {
		fprintf(stderr, "mirror-write: target must be in slave mode\n");
		exit(1);
	}
	if (tid_begp)
		*tid_begp = mrec->pfs.pfsd.sync_beg_tid;
	if (tid_endp)
		*tid_endp = mrec->pfs.pfsd.sync_end_tid;
	free(mrec);
	return(0);
}

static void
update_pfs_snapshot(int fd, hammer_tid_t snapshot_tid, int pfs_id)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;

	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.pfs_id = pfs_id;
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		perror("update_pfs_snapshot (read)");
		exit(1);
	}
	if (pfsd.sync_end_tid != snapshot_tid) {
		pfsd.sync_end_tid = snapshot_tid;
		if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) != 0) {
			perror("update_pfs_snapshot (rewrite)");
			exit(1);
		}
		if (VerboseOpt >= 2) {
			fprintf(stderr,
				"Mirror-write: Completed, updated snapshot "
				"to %016llx\n",
				snapshot_tid);
		}
	}
}

/*
 * Bandwidth-limited write in chunks
 */
static
ssize_t
writebw(int fd, const void *buf, size_t nbytes,
	u_int64_t *bwcount, struct timeval *tv1)
{
	struct timeval tv2;
	size_t n;
	ssize_t r;
	ssize_t a;
	int usec;

	a = 0;
	r = 0;
	while (nbytes) {
		if (*bwcount + nbytes > BandwidthOpt)
			n = BandwidthOpt - *bwcount;
		else
			n = nbytes;
		if (n)
			r = write(fd, buf, n);
		if (r >= 0) {
			a += r;
			nbytes -= r;
			buf = (const char *)buf + r;
		}
		if ((size_t)r != n)
			break;
		*bwcount += n;
		if (*bwcount >= BandwidthOpt) {
			gettimeofday(&tv2, NULL);
			usec = (int)(tv2.tv_sec - tv1->tv_sec) * 1000000 +
				(int)(tv2.tv_usec - tv1->tv_usec);
			if (usec >= 0 && usec < 1000000)
				usleep(1000000 - usec);
			gettimeofday(tv1, NULL);
			*bwcount -= BandwidthOpt;
		}
	}
	return(a ? a : r);
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
