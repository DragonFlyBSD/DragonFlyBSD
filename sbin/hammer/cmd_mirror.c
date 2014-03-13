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
 */

#include "hammer.h"

#define LINE1	0,20
#define LINE2	20,78
#define LINE3	90,70

#define SERIALBUF_SIZE	(512 * 1024)

typedef struct histogram {
	hammer_tid_t	tid;
	u_int64_t	bytes;
} *histogram_t;

static int read_mrecords(int fd, char *buf, u_int size,
			 hammer_ioc_mrecord_head_t pickup);
static int generate_histogram(int fd, const char *filesystem,
			 histogram_t *histogram_ary,
			 struct hammer_ioc_mirror_rw *mirror_base,
			 int *repeatp);
static hammer_ioc_mrecord_any_t read_mrecord(int fdin, int *errorp,
			 hammer_ioc_mrecord_head_t pickup);
static void write_mrecord(int fdout, u_int32_t type,
			 hammer_ioc_mrecord_any_t mrec, int bytes);
static void generate_mrec_header(int fd, int pfs_id,
			 union hammer_ioc_mrecord_any *mrec_tmp);
static int validate_mrec_header(int fd, int fdin, int is_target, int pfs_id,
			 struct hammer_ioc_mrecord_head *pickup,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static void update_pfs_snapshot(int fd, hammer_tid_t snapshot_tid, int pfs_id);
static ssize_t writebw(int fd, const void *buf, size_t nbytes,
			u_int64_t *bwcount, struct timeval *tv1);
static int getyn(void);
static void mirror_usage(int code);

/*
 * Generate a mirroring data stream from the specific source over the
 * entire key range, but restricted to the specified transaction range.
 *
 * The HAMMER VFS does most of the work, we add a few new mrecord
 * types to negotiate the TID ranges and verify that the entire
 * stream made it to the destination.
 *
 * streaming will be 0 for mirror-read, 1 for mirror-stream.  The code will
 * set up a fake value of -1 when running the histogram for mirror-read.
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
	histogram_t histogram_ary;
	char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	int interrupted = 0;
	int error;
	int fd;
	int n;
	int didwork;
	int histogram;
	int histindex;
	int histmax;
	int repeat = 0;
	int sameline;
	int64_t total_bytes;
	time_t base_t = time(NULL);
	struct timeval bwtv;
	u_int64_t bwcount;
	u_int64_t estbytes;

	if (ac == 0 || ac > 2)
		mirror_usage(1);
	filesystem = av[0];
	hammer_check_restrict(filesystem);

	pickup.signature = 0;
	pickup.type = 0;
	histogram = 0;
	histindex = 0;
	histmax = 0;
	histogram_ary = NULL;
	sameline = 0;

again:
	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = getpfs(&pfs, filesystem);

	if (streaming >= 0)
		score_printf(LINE1, "Running");

	if (streaming >= 0 && VerboseOpt && VerboseOpt < 2) {
		fprintf(stderr, "%cRunning  \b\b", (sameline ? '\r' : '\n'));
		fflush(stderr);
		sameline = 1;
	}
	sameline = 1;
	total_bytes = 0;
	gettimeofday(&bwtv, NULL);
	bwcount = 0;

	/*
	 * Send initial header for the purpose of determining the
	 * shared-uuid.
	 */
	generate_mrec_header(fd, pfs.pfs_id, &mrec_tmp);
	write_mrecord(1, HAMMER_MREC_TYPE_PFSD,
		      &mrec_tmp, sizeof(mrec_tmp.pfs));

	/*
	 * In 2-way mode the target will send us a PFS info packet
	 * first.  Use the target's current snapshot TID as our default
	 * begin TID.
	 */
	if (TwoWayPipeOpt) {
		mirror.tid_beg = 0;
		n = validate_mrec_header(fd, 0, 0, pfs.pfs_id, &pickup,
					 NULL, &mirror.tid_beg);
		if (n < 0) {	/* got TERM record */
			relpfs(fd, &pfs);
			return;
		}
		++mirror.tid_beg;
	} else if (streaming && histogram) {
		mirror.tid_beg = histogram_ary[histindex].tid + 1;
	} else {
		mirror.tid_beg = 0;
	}

	/*
	 * Write out the PFS header, tid_beg will be updated if our PFS
	 * has a larger begin sync.  tid_end is set to the latest source
	 * TID whos flush cycle has completed.
	 */
	generate_mrec_header(fd, pfs.pfs_id, &mrec_tmp);
	if (mirror.tid_beg < mrec_tmp.pfs.pfsd.sync_beg_tid)
		mirror.tid_beg = mrec_tmp.pfs.pfsd.sync_beg_tid;
	mirror.tid_end = mrec_tmp.pfs.pfsd.sync_end_tid;
	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;
	mirror.pfs_id = pfs.pfs_id;
	mirror.shared_uuid = pfs.ondisk->shared_uuid;

	/*
	 * XXX If the histogram is exhausted and the TID delta is large
	 *     the stream might have been offline for a while and is
	 *     now picking it up again.  Do another histogram.
	 */
#if 0
	if (streaming && histogram && histindex == histend) {
		if (mirror.tid_end - mirror.tid_beg > BULK_MINIMUM)
			histogram = 0;
	}
#endif

	/*
	 * Initial bulk startup control, try to do some incremental
	 * mirroring in order to allow the stream to be killed and
	 * restarted without having to start over.
	 */
	if (histogram == 0 && BulkOpt == 0) {
		if (VerboseOpt && repeat == 0) {
			fprintf(stderr, "\n");
			sameline = 0;
		}
		histmax = generate_histogram(fd, filesystem,
					     &histogram_ary, &mirror,
					     &repeat);
		histindex = 0;
		histogram = 1;

		/*
		 * Just stream the histogram, then stop
		 */
		if (streaming == 0)
			streaming = -1;
	}

	if (streaming && histogram) {
		++histindex;
		mirror.tid_end = histogram_ary[histindex].tid;
		estbytes = histogram_ary[histindex-1].bytes;
		mrec_tmp.pfs.pfsd.sync_end_tid = mirror.tid_end;
	} else {
		estbytes = 0;
	}

	write_mrecord(1, HAMMER_MREC_TYPE_PFSD,
		      &mrec_tmp, sizeof(mrec_tmp.pfs));

	/*
	 * A cycle file overrides the beginning TID only if we are
	 * not operating in two-way or histogram mode.
	 */
	if (TwoWayPipeOpt == 0 && histogram == 0) {
		hammer_get_cycle(&mirror.key_beg, &mirror.tid_beg);
	}

	/*
	 * An additional argument overrides the beginning TID regardless
	 * of what mode we are in.  This is not recommending if operating
	 * in two-way mode.
	 */
	if (ac == 2)
		mirror.tid_beg = strtoull(av[1], NULL, 0);

	if (streaming == 0 || VerboseOpt >= 2) {
		fprintf(stderr,
			"Mirror-read: Mirror %016jx to %016jx",
			(uintmax_t)mirror.tid_beg, (uintmax_t)mirror.tid_end);
		if (histogram)
			fprintf(stderr, " (bulk= %ju)", (uintmax_t)estbytes);
		fprintf(stderr, "\n");
		fflush(stderr);
	}
	if (mirror.key_beg.obj_id != (int64_t)HAMMER_MIN_OBJID) {
		fprintf(stderr, "Mirror-read: Resuming at object %016jx\n",
			(uintmax_t)mirror.key_beg.obj_id);
	}

	/*
	 * Nothing to do if begin equals end.
	 */
	if (mirror.tid_beg >= mirror.tid_end) {
		if (streaming == 0 || VerboseOpt >= 2)
			fprintf(stderr, "Mirror-read: No work to do\n");
		sleep(DelayOpt);
		didwork = 0;
		histogram = 0;
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
			score_printf(LINE3, "Mirror-read %s failed: %s",
				     filesystem, strerror(errno));
			fprintf(stderr, "Mirror-read %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
		if (mirror.head.flags & HAMMER_IOC_HEAD_ERROR) {
			score_printf(LINE3, "Mirror-read %s fatal error %d",
				     filesystem, mirror.head.error);
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
				score_printf(LINE3,
					     "Mirror-read %s failed: "
					     "short write",
					     filesystem);
				fprintf(stderr,
					"Mirror-read %s failed: "
					"short write\n",
				filesystem);
				exit(1);
			}
		}
		total_bytes += mirror.count;
		if (streaming && VerboseOpt) {
			fprintf(stderr,
				"\rscan obj=%016jx tids=%016jx:%016jx %11jd",
				(uintmax_t)mirror.key_cur.obj_id,
				(uintmax_t)mirror.tid_beg,
				(uintmax_t)mirror.tid_end,
				(intmax_t)total_bytes);
			fflush(stderr);
			sameline = 0;
		} else if (streaming) {
			score_printf(LINE2,
				"obj=%016jx tids=%016jx:%016jx %11jd",
				(uintmax_t)mirror.key_cur.obj_id,
				(uintmax_t)mirror.tid_beg,
				(uintmax_t)mirror.tid_end,
				(intmax_t)total_bytes);
		}
		mirror.key_beg = mirror.key_cur;

		/*
		 * Deal with time limit option
		 */
		if (TimeoutOpt &&
		    (unsigned)(time(NULL) - base_t) > (unsigned)TimeoutOpt) {
			score_printf(LINE3,
				"Mirror-read %s interrupted by timer at"
				" %016jx",
				filesystem,
				(uintmax_t)mirror.key_cur.obj_id);
			fprintf(stderr,
				"Mirror-read %s interrupted by timer at"
				" %016jx\n",
				filesystem,
				(uintmax_t)mirror.key_cur.obj_id);
			interrupted = 1;
			break;
		}
	} while (mirror.count != 0);

done:
	if (streaming && VerboseOpt && sameline == 0) {
		fprintf(stderr, "\n");
		fflush(stderr);
		sameline = 1;
	}

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
				hammer_set_cycle(&mirror.key_cur,
						 mirror.tid_beg);
				fprintf(stderr, "Cyclefile %s updated for "
					"continuation\n", CyclePath);
			}
		} else {
			sync_tid = mrec->update.tid;
			if (CyclePath) {
				hammer_key_beg_init(&mirror.key_beg);
				hammer_set_cycle(&mirror.key_beg, sync_tid);
				fprintf(stderr,
					"Cyclefile %s updated to 0x%016jx\n",
					CyclePath, (uintmax_t)sync_tid);
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

		/*
		 * Try to break down large bulk transfers into smaller ones
		 * so it can sync the transaction id on the slave.  This
		 * way if we get interrupted a restart doesn't have to
		 * start from scratch.
		 */
		if (streaming && histogram) {
			if (histindex != histmax) {
				if (VerboseOpt && VerboseOpt < 2 &&
				    streaming >= 0) {
					fprintf(stderr, " (bulk incremental)");
				}
				relpfs(fd, &pfs);
				goto again;
			}
		}

		if (VerboseOpt && streaming >= 0) {
			fprintf(stderr, " W");
			fflush(stderr);
		} else if (streaming >= 0) {
			score_printf(LINE1, "Waiting");
		}
		pfs.ondisk->sync_end_tid = mirror.tid_end;
		if (streaming < 0) {
			/*
			 * Fake streaming mode when using a histogram to
			 * break up a mirror-read, do not wait on source.
			 */
			streaming = 0;
		} else if (ioctl(fd, HAMMERIOC_WAI_PSEUDOFS, &pfs) < 0) {
			score_printf(LINE3,
				     "Mirror-read %s: cannot stream: %s\n",
				     filesystem, strerror(errno));
			fprintf(stderr,
				"Mirror-read %s: cannot stream: %s\n",
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
 * What we are trying to do here is figure out how much data is
 * going to be sent for the TID range and to break the TID range
 * down into reasonably-sized slices (from the point of view of
 * data sent) so a lost connection can restart at a reasonable
 * place and not all the way back at the beginning.
 *
 * An entry's TID serves as the end_tid for the prior entry
 * So we have to offset the calculation by 1 so that TID falls into
 * the previous entry when populating entries.
 *
 * Because the transaction id space is bursty we need a relatively
 * large number of buckets (like a million) to do a reasonable job
 * for things like an initial bulk mirrors on a very large filesystem.
 */
#define HIST_COUNT	(1024 * 1024)

static int
generate_histogram(int fd, const char *filesystem,
		   histogram_t *histogram_ary,
		   struct hammer_ioc_mirror_rw *mirror_base,
		   int *repeatp)
{
	struct hammer_ioc_mirror_rw mirror;
	union hammer_ioc_mrecord_any *mrec;
	hammer_tid_t tid_beg;
	hammer_tid_t tid_end;
	hammer_tid_t tid;
	hammer_tid_t tidx;
	u_int64_t *tid_bytes;
	u_int64_t total;
	u_int64_t accum;
	int chunkno;
	int i;
	int res;
	int off;
	int len;

	mirror = *mirror_base;
	tid_beg = mirror.tid_beg;
	tid_end = mirror.tid_end;
	mirror.head.flags |= HAMMER_IOC_MIRROR_NODATA;

	if (*histogram_ary == NULL) {
		*histogram_ary = malloc(sizeof(struct histogram) *
					(HIST_COUNT + 2));
	}
	if (tid_beg >= tid_end)
		return(0);

	/* needs 2 extra */
	tid_bytes = malloc(sizeof(*tid_bytes) * (HIST_COUNT + 2));
	bzero(tid_bytes, sizeof(*tid_bytes) * (HIST_COUNT + 2));

	if (*repeatp == 0) {
		fprintf(stderr, "Prescan to break up bulk transfer");
		if (VerboseOpt > 1)
			fprintf(stderr, " (%juMB chunks)",
				(uintmax_t)(SplitupOpt / (1024 * 1024)));
		fprintf(stderr, "\n");
	}

	/*
	 * Note: (tid_beg,tid_end), range is inclusive of both beg & end.
	 *
	 * Note: Estimates can be off when the mirror is way behind due
	 *	 to skips.
	 */
	total = 0;
	accum = 0;
	chunkno = 0;
	for (;;) {
		mirror.count = 0;
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
		for (off = 0;
		     off < mirror.count;
		     off += HAMMER_HEAD_DOALIGN(mrec->head.rec_size)
		) {
			mrec = (void *)((char *)mirror.ubuf + off);

			/*
			 * We only care about general RECs and PASS
			 * records.  We ignore SKIPs.
			 */
			switch (mrec->head.type & HAMMER_MRECF_TYPE_LOMASK) {
			case HAMMER_MREC_TYPE_REC:
			case HAMMER_MREC_TYPE_PASS:
				break;
			default:
				continue;
			}

			/*
			 * Calculate for two indices, create_tid and
			 * delete_tid.  Record data only applies to
			 * the create_tid.
			 *
			 * When tid is exactly on the boundary it really
			 * belongs to the previous entry because scans
			 * are inclusive of the ending entry.
			 */
			tid = mrec->rec.leaf.base.delete_tid;
			if (tid && tid >= tid_beg && tid <= tid_end) {
				len = HAMMER_HEAD_DOALIGN(mrec->head.rec_size);
				if (mrec->head.type ==
				    HAMMER_MREC_TYPE_REC) {
					len -= HAMMER_HEAD_DOALIGN(
						    mrec->rec.leaf.data_len);
					assert(len > 0);
				}
				i = (tid - tid_beg) * HIST_COUNT /
				    (tid_end - tid_beg);
				tidx = tid_beg + i * (tid_end - tid_beg) /
						 HIST_COUNT;
				if (tid == tidx && i)
					--i;
				assert(i >= 0 && i < HIST_COUNT);
				tid_bytes[i] += len;
				total += len;
				accum += len;
			}

			tid = mrec->rec.leaf.base.create_tid;
			if (tid && tid >= tid_beg && tid <= tid_end) {
				len = HAMMER_HEAD_DOALIGN(mrec->head.rec_size);
				if (mrec->head.type ==
				    HAMMER_MREC_TYPE_REC_NODATA) {
					len += HAMMER_HEAD_DOALIGN(
						    mrec->rec.leaf.data_len);
				}
				i = (tid - tid_beg) * HIST_COUNT /
				    (tid_end - tid_beg);
				tidx = tid_beg + i * (tid_end - tid_beg) /
						 HIST_COUNT;
				if (tid == tidx && i)
					--i;
				assert(i >= 0 && i < HIST_COUNT);
				tid_bytes[i] += len;
				total += len;
				accum += len;
			}
		}
		if (*repeatp == 0 && accum > SplitupOpt) {
			if (VerboseOpt > 1) {
				fprintf(stderr, ".");
				fflush(stderr);
			}
			++chunkno;
			score_printf(LINE2, "Prescan chunk %d", chunkno);
			accum = 0;
		}
		if (mirror.count == 0)
			break;
		mirror.key_beg = mirror.key_cur;
	}

	/*
	 * Reduce to SplitupOpt (default 4GB) chunks.  This code may
	 * use up to two additional elements.  Do the array in-place.
	 *
	 * Inefficient degenerate cases can occur if we do not accumulate
	 * at least the requested split amount, so error on the side of
	 * going over a bit.
	 */
	res = 0;
	(*histogram_ary)[res].tid = tid_beg;
	(*histogram_ary)[res].bytes = tid_bytes[0];
	for (i = 1; i < HIST_COUNT; ++i) {
		if ((*histogram_ary)[res].bytes >= SplitupOpt) {
			++res;
			(*histogram_ary)[res].tid = tid_beg +
					i * (tid_end - tid_beg) /
					HIST_COUNT;
			(*histogram_ary)[res].bytes = 0;

		}
		(*histogram_ary)[res].bytes += tid_bytes[i];
	}
	++res;
	(*histogram_ary)[res].tid = tid_end;
	(*histogram_ary)[res].bytes = -1;

	if (*repeatp == 0) {
		if (VerboseOpt > 1)
			fprintf(stderr, "\n");	/* newline after ... */
		score_printf(LINE3, "Prescan %d chunks, total %ju MBytes",
			res, (uintmax_t)total / (1024 * 1024));
		fprintf(stderr, "Prescan %d chunks, total %ju MBytes (",
			res, (uintmax_t)total / (1024 * 1024));
		for (i = 0; i < res && i < 3; ++i) {
			if (i)
				fprintf(stderr, ", ");
			fprintf(stderr, "%ju",
				(uintmax_t)(*histogram_ary)[i].bytes);
		}
		if (i < res)
			fprintf(stderr, ", ...");
		fprintf(stderr, ")\n");
	}
	assert(res <= HIST_COUNT);
	*repeatp = 1;

	free(tid_bytes);
	return(res);
}

static void
create_pfs(const char *filesystem, uuid_t *s_uuid)
{
	if (ForceYesOpt == 1) {
		fprintf(stderr, "PFS slave %s does not exist. "
			"Auto create new slave PFS!\n", filesystem);

	} else {
		fprintf(stderr, "PFS slave %s does not exist.\n"
			"Do you want to create a new slave PFS? (yes|no) ",
			filesystem);
		fflush(stderr);
		if (getyn() != 1) {
			fprintf(stderr, "Aborting operation\n");
			exit(1);
		}
	}

	u_int32_t status;
	char *shared_uuid = NULL;
	uuid_to_string(s_uuid, &shared_uuid, &status);

	char *cmd = NULL;
	asprintf(&cmd, "/sbin/hammer pfs-slave '%s' shared-uuid=%s 1>&2",
		 filesystem, shared_uuid);
	free(shared_uuid);

	if (cmd == NULL) {
		fprintf(stderr, "Failed to alloc memory\n");
		exit(1);
	}
	if (system(cmd) != 0) {
		fprintf(stderr, "Failed to create PFS\n");
	}
	free(cmd);
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
	char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_ioc_mrecord_head pickup;
	struct hammer_ioc_synctid synctid;
	union hammer_ioc_mrecord_any mrec_tmp;
	hammer_ioc_mrecord_any_t mrec;
	struct stat st;
	int error;
	int fd;
	int n;

	if (ac != 1)
		mirror_usage(1);
	filesystem = av[0];
	hammer_check_restrict(filesystem);

	pickup.signature = 0;
	pickup.type = 0;

again:
	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);
	mirror.key_end = mirror.key_beg;

	/*
	 * Read initial packet
	 */
	mrec = read_mrecord(0, &error, &pickup);
	if (mrec == NULL) {
		if (error == 0)
			fprintf(stderr, "validate_mrec_header: short read\n");
		exit(1);
	}
	/*
	 * Validate packet
	 */
	if (mrec->head.type == HAMMER_MREC_TYPE_TERM) {
		return;
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

	/*
	 * Create slave PFS if it doesn't yet exist
	 */
	if (lstat(filesystem, &st) != 0) {
		create_pfs(filesystem, &mrec->pfs.pfsd.shared_uuid);
	}
	free(mrec);
	mrec = NULL;

	fd = getpfs(&pfs, filesystem);

	/*
	 * In two-way mode the target writes out a PFS packet first.
	 * The source uses our tid_end as its tid_beg by default,
	 * picking up where it left off.
	 */
	mirror.tid_beg = 0;
	if (TwoWayPipeOpt) {
		generate_mrec_header(fd, pfs.pfs_id, &mrec_tmp);
		if (mirror.tid_beg < mrec_tmp.pfs.pfsd.sync_beg_tid)
			mirror.tid_beg = mrec_tmp.pfs.pfsd.sync_beg_tid;
		mirror.tid_end = mrec_tmp.pfs.pfsd.sync_end_tid;
		write_mrecord(1, HAMMER_MREC_TYPE_PFSD,
			      &mrec_tmp, sizeof(mrec_tmp.pfs));
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
				filesystem,
				(mrec ? (int)mrec->head.type : -1));
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
		printf("Source can update synctid to 0x%016jx\n",
		       (uintmax_t)mirror.tid_end);
	}
	relpfs(fd, &pfs);
	goto again;
}

void
hammer_cmd_mirror_dump(char **av, int ac)
{
	char *buf = malloc(SERIALBUF_SIZE);
	struct hammer_ioc_mrecord_head pickup;
	hammer_ioc_mrecord_any_t mrec;
	int error;
	int size;
	int offset;
	int bytes;
	int header_only = 0;

	if (ac == 1 && strcmp(*av, "header") == 0)
		header_only = 1;
	else if (ac != 0)
		mirror_usage(1);

	/*
	 * Read and process the PFS header
	 */
	pickup.signature = 0;
	pickup.type = 0;

	mrec = read_mrecord(0, &error, &pickup);

	/*
	 * Dump the PFS header. mirror-dump takes its input from the output
	 * of a mirror-read so getpfs() can't be used to get a fd to be passed
	 * to dump_pfsd().
	 */
	if (header_only && mrec != NULL) {
		dump_pfsd(&mrec->pfs.pfsd, -1);
		return;
	}

again:
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

			switch(mrec->head.type & HAMMER_MRECF_TYPE_MASK) {
			case HAMMER_MREC_TYPE_REC_BADCRC:
			case HAMMER_MREC_TYPE_REC:
				printf("Record obj=%016jx key=%016jx "
				       "rt=%02x ot=%02x",
					(uintmax_t)mrec->rec.leaf.base.obj_id,
					(uintmax_t)mrec->rec.leaf.base.key,
					mrec->rec.leaf.base.rec_type,
					mrec->rec.leaf.base.obj_type);
				if (mrec->head.type ==
				    HAMMER_MREC_TYPE_REC_BADCRC) {
					printf(" (BAD CRC)");
				}
				printf("\n");
				printf("       tids %016jx:%016jx data=%d\n",
				    (uintmax_t)mrec->rec.leaf.base.create_tid,
				    (uintmax_t)mrec->rec.leaf.base.delete_tid,
				    mrec->rec.leaf.data_len);
				break;
			case HAMMER_MREC_TYPE_PASS:
				printf("Pass   obj=%016jx key=%016jx "
				       "rt=%02x ot=%02x\n",
					(uintmax_t)mrec->rec.leaf.base.obj_id,
					(uintmax_t)mrec->rec.leaf.base.key,
					mrec->rec.leaf.base.rec_type,
					mrec->rec.leaf.base.obj_type);
				printf("       tids %016jx:%016jx data=%d\n",
				    (uintmax_t)mrec->rec.leaf.base.create_tid,
				    (uintmax_t)mrec->rec.leaf.base.delete_tid,
					mrec->rec.leaf.data_len);
				break;
			case HAMMER_MREC_TYPE_SKIP:
				printf("Skip   obj=%016jx key=%016jx rt=%02x to\n"
				       "       obj=%016jx key=%016jx rt=%02x\n",
				       (uintmax_t)mrec->skip.skip_beg.obj_id,
				       (uintmax_t)mrec->skip.skip_beg.key,
				       mrec->skip.skip_beg.rec_type,
				       (uintmax_t)mrec->skip.skip_end.obj_id,
				       (uintmax_t)mrec->skip.skip_end.key,
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

	/*
	 * Continue with more batches until EOF.
	 */
	mrec = read_mrecord(0, &error, &pickup);
	if (mrec)
		goto again;
}

void
hammer_cmd_mirror_copy(char **av, int ac, int streaming)
{
	pid_t pid1;
	pid_t pid2;
	int fds[2];
	const char *xav[32];
	char tbuf[16];
	char *sh, *user, *host, *rfs;
	int xac;

	if (ac != 2)
		mirror_usage(1);

	TwoWayPipeOpt = 1;
	signal(SIGPIPE, SIG_IGN);

again:
	if (pipe(fds) < 0) {
		perror("pipe");
		exit(1);
	}

	/*
	 * Source
	 */
	if ((pid1 = fork()) == 0) {
		signal(SIGPIPE, SIG_DFL);
		dup2(fds[0], 0);
		dup2(fds[0], 1);
		close(fds[0]);
		close(fds[1]);
		if ((rfs = strchr(av[0], ':')) != NULL) {
			xac = 0;

			if((sh = getenv("HAMMER_RSH")) == NULL)
				xav[xac++] = "ssh";
			else
				xav[xac++] = sh;

			if (CompressOpt)
				xav[xac++] = "-C";

			user = strndup(av[0], (rfs - av[0]));

			if ((host = strchr(av[0], '@')) != NULL) {
				user = strndup( av[0], (host++ - av[0]));
				host = strndup( host, (rfs++ - host));
				xav[xac++] = "-l";
				xav[xac++] = user;
				xav[xac++] = host;
			}
			else {
				host = strndup( av[0], (rfs++ - av[0]));
				user = NULL;
				xav[xac++] = host;
			}


			if (SshPort) {
				xav[xac++] = "-p";
				xav[xac++] = SshPort;
			}

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
			if (ForceYesOpt) {
				xav[xac++] = "-y";
			}
			xav[xac++] = "-2";
			if (TimeoutOpt) {
				snprintf(tbuf, sizeof(tbuf), "%d", TimeoutOpt);
				xav[xac++] = "-t";
				xav[xac++] = tbuf;
			}
			if (SplitupOptStr) {
				xav[xac++] = "-S";
				xav[xac++] = SplitupOptStr;
			}
			if (streaming)
				xav[xac++] = "mirror-read-stream";
			else
				xav[xac++] = "mirror-read";
			xav[xac++] = rfs;
			xav[xac++] = NULL;
			execvp(*xav, (void *)xav);
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
		signal(SIGPIPE, SIG_DFL);
		dup2(fds[1], 0);
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
		if ((rfs = strchr(av[1], ':')) != NULL) {
			xac = 0;

			if((sh = getenv("HAMMER_RSH")) == NULL)
				xav[xac++] = "ssh";
			else
				xav[xac++] = sh;

			if (CompressOpt)
				xav[xac++] = "-C";

			user = strndup(av[1], (rfs - av[1]));

			if ((host = strchr(av[1], '@')) != NULL) {
				user = strndup( av[1], (host++ - av[1]));
				host = strndup( host, (rfs++ - host));
				xav[xac++] = "-l";
				xav[xac++] = user;
				xav[xac++] = host;
			}
			else {
				host = strndup( av[1], (rfs++ - av[1]));
				user = NULL;
				xav[xac++] = host;
			}

			if (SshPort) {
				xav[xac++] = "-p";
				xav[xac++] = SshPort;
			}

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
			if (ForceYesOpt) {
				xav[xac++] = "-y";
			}
			xav[xac++] = "-2";
			xav[xac++] = "mirror-write";
			xav[xac++] = rfs;
			xav[xac++] = NULL;
			execvp(*xav, (void *)xav);
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

	/*
	 * If the link is lost restart
	 */
	if (streaming) {
		if (VerboseOpt) {
			fprintf(stderr, "\nLost Link\n");
			fflush(stderr);
		}
		sleep(15 + DelayOpt);
		goto again;
	}

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
	int type;

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
				fprintf(stderr, "read_mrecords: malformed record on pipe, "
					"bad signature\n");
				exit(1);
			}
		}
		if (pickup->rec_size < HAMMER_MREC_HEADSIZE ||
		    pickup->rec_size > sizeof(*mrec) + HAMMER_XBUFSIZE) {
			fprintf(stderr, "read_mrecords: malformed record on pipe, "
				"illegal rec_size\n");
			exit(1);
		}

		/*
		 * Stop if we have insufficient space for the record and data.
		 */
		bytes = HAMMER_HEAD_DOALIGN(pickup->rec_size);
		if (size - count < bytes)
			break;

		/*
		 * Stop if the record type is not a REC, SKIP, or PASS,
		 * which are the only types the ioctl supports.  Other types
		 * are used only by the userland protocol.
		 *
		 * Ignore all flags.
		 */
		type = pickup->type & HAMMER_MRECF_TYPE_LOMASK;
		if (type != HAMMER_MREC_TYPE_PFSD &&
		    type != HAMMER_MREC_TYPE_REC &&
		    type != HAMMER_MREC_TYPE_SKIP &&
		    type != HAMMER_MREC_TYPE_PASS) {
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
		 * If its a B-Tree record validate the data crc.
		 *
		 * NOTE: If the VFS passes us an explicitly errorde mrec
		 *	 we just pass it through.
		 */
		type = mrec->head.type & HAMMER_MRECF_TYPE_MASK;

		if (type == HAMMER_MREC_TYPE_REC) {
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
					"match data! obj=%016jx key=%016jx\n",
					(uintmax_t)mrec->rec.leaf.base.obj_id,
					(uintmax_t)mrec->rec.leaf.base.key);
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
generate_mrec_header(int fd, int pfs_id,
		     union hammer_ioc_mrecord_any *mrec_tmp)
{
	struct hammer_ioc_pseudofs_rw pfs;

	bzero(&pfs, sizeof(pfs));
	bzero(mrec_tmp, sizeof(*mrec_tmp));
	pfs.pfs_id = pfs_id;
	pfs.ondisk = &mrec_tmp->pfs.pfsd;
	pfs.bytes = sizeof(mrec_tmp->pfs.pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		fprintf(stderr, "Mirror-read: not a HAMMER fs/pseudofs!\n");
		exit(1);
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		fprintf(stderr, "Mirror-read: HAMMER pfs version mismatch!\n");
		exit(1);
	}
	mrec_tmp->pfs.version = pfs.version;
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
				"to %016jx\n",
				(uintmax_t)snapshot_tid);
			fflush(stderr);
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

/*
 * Get a yes or no answer from the terminal.  The program may be run as
 * part of a two-way pipe so we cannot use stdin for this operation.
 */
static int
getyn(void)
{
	char buf[256];
	FILE *fp;
	int result;

	fp = fopen("/dev/tty", "r");
	if (fp == NULL) {
		fprintf(stderr, "No terminal for response\n");
		return(-1);
	}
	result = -1;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (buf[0] == 'y' || buf[0] == 'Y') {
			result = 1;
			break;
		}
		if (buf[0] == 'n' || buf[0] == 'N') {
			result = 0;
			break;
		}
		fprintf(stderr, "Response not understood\n");
		break;
	}
	fclose(fp);
	return(result);
}

static void
mirror_usage(int code)
{
	fprintf(stderr,
		"hammer mirror-read <filesystem> [begin-tid]\n"
		"hammer mirror-read-stream <filesystem> [begin-tid]\n"
		"hammer mirror-write <filesystem>\n"
		"hammer mirror-dump [header]\n"
		"hammer mirror-copy [[user@]host:]<filesystem>"
				  " [[user@]host:]<filesystem>\n"
		"hammer mirror-stream [[user@]host:]<filesystem>"
				    " [[user@]host:]<filesystem>\n"
	);
	exit(code);
}

