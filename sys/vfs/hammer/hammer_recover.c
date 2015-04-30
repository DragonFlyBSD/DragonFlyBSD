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

/*
 * UNDO ALGORITHM:
 *
 *	The UNDO algorithm is trivial.  The nominal UNDO range in the
 *	FIFO is determined by taking the first/next offset stored in
 *	the volume header.  The next offset may not be correct since
 *	UNDO flushes are not required to flush the volume header, so
 *	the code also scans forward until it finds a discontinuous
 *	sequence number.
 *
 *	The UNDOs are then scanned and executed in reverse order.  These
 *	UNDOs are effectively just data restorations based on HAMMER offsets.
 *
 * REDO ALGORITHM:
 *
 *	REDO records are laid down in the UNDO/REDO FIFO for nominal
 *	writes, truncations, and file extension ops.  On a per-inode
 *	basis two types of REDO records are generated, REDO_WRITE
 *	and REDO_TRUNC.
 *
 *	Essentially the recovery block will contain UNDO records backing
 *	out partial operations and REDO records to regenerate those partial
 *	operations guaranteed by the filesystem during recovery.
 *
 *	REDO generation is optional, and can also be started and then
 *	later stopped due to excessive write()s inbetween fsyncs, or not
 *	started at all.  Because of this the recovery code must determine
 *	when REDOs are valid and when they are not.  Additional records are
 *	generated to help figure it out.
 *
 *	The REDO_TERM_WRITE and REDO_TERM_TRUNC records are generated
 *	during a flush cycle indicating which records the flush cycle
 *	has synched meta-data for, and HAMMER_REDO_SYNC is generated in
 *	each flush cycle to indicate how far back in the UNDO/REDO FIFO
 *	the recovery code must go to find the earliest applicable REDO
 *	record.  Applicable REDO records can be far outside the nominal
 *	UNDO recovery range, for example if a write() lays down a REDO but
 *	the related file is not flushed for several cycles.
 *
 *	The SYNC reference is to a point prior to the nominal UNDO FIFO
 *	range, creating an extended REDO range which must be scanned.
 *
 *	Any REDO_WRITE/REDO_TRUNC encountered within the extended range
 *	which have no matching REDO_TERM_WRITE/REDO_TERM_TRUNC records
 *	prior to the start of the nominal UNDO range are applicable.
 *	That is, any REDO_TERM_* records in the extended range but not in
 *	the nominal undo range will mask any redo operations for prior REDO
 *	records.  This is necessary because once the TERM is laid down
 *	followup operations may make additional changes to the related
 *	records but not necessarily record them as REDOs (because REDOs are
 *	optional).
 *
 *	REDO_TERM_WRITE/REDO_TERM_TRUNC records in the nominal UNDO range
 *	must be ignored since they represent meta-data flushes which are
 *	undone by the UNDOs in that nominal UNDO range by the recovery
 *	code.  Only REDO_TERM_* records in the extended range but not
 *	in the nominal undo range are applicable.
 *
 *	The REDO_SYNC record itself always exists in the nominal UNDO range
 *	(this is how the extended range is determined).  For recovery
 *	purposes the most recent REDO_SYNC record is always used if several
 *	are found.
 *
 * CRASHES DURING UNDO/REDO
 *
 *	A crash during the UNDO phase requires no additional effort.  The
 *	UNDOs will simply be re-run again.  The state of the UNDO/REDO fifo
 *	remains unchanged and has no re-crash issues.
 *
 *	A crash during the REDO phase is more complex because the REDOs
 *	run normal filesystem ops and generate additional UNDO/REDO records.
 *	REDO is disabled during REDO recovery and any SYNC records generated
 *	by flushes during REDO recovery must continue to reference the
 *	original extended range.
 *
 *	If multiple crashes occur and the UNDO/REDO FIFO wraps, REDO recovery
 *	may become impossible.  This is detected when the start of the
 *	extended range fails to have monotonically increasing sequence
 *	numbers leading into the nominal undo range.
 */

#include "hammer.h"

/*
 * Specify the way we want to handle stage2 errors.
 *
 * Following values are accepted:
 *
 * 0 - Run redo recovery normally and fail to mount if
 *     the operation fails (default).
 * 1 - Run redo recovery, but don't fail to mount if the
 *     operation fails.
 * 2 - Completely skip redo recovery (only for severe error
 *     conditions and/or debugging.
 */
static int hammer_skip_redo = 0;
TUNABLE_INT("vfs.hammer.skip_redo", &hammer_skip_redo);

/*
 * Each rterm entry has a list of fifo offsets indicating termination
 * points.  These are stripped as the scan progresses.
 */
typedef struct hammer_rterm_entry {
	struct hammer_rterm_entry *next;
	hammer_off_t		fifo_offset;
} *hammer_rterm_entry_t;

/*
 * rterm entries sorted in RB tree are indexed by objid, flags, and offset.
 * TRUNC entries ignore the offset.
 */
typedef struct hammer_rterm {
	RB_ENTRY(hammer_rterm)	rb_node;
	int64_t			redo_objid;
	u_int32_t		redo_localization;
	u_int32_t		redo_flags;
	hammer_off_t		redo_offset;
	hammer_rterm_entry_t	term_list;
} *hammer_rterm_t;

static int hammer_rterm_rb_cmp(hammer_rterm_t rt1, hammer_rterm_t rt2);
struct hammer_rterm_rb_tree;
RB_HEAD(hammer_rterm_rb_tree, hammer_rterm);
RB_PROTOTYPE(hammer_rterm_rb_tree, hammer_rterm, rb_node, hammer_rterm_rb_cmp);

static int hammer_check_tail_signature(hammer_fifo_tail_t tail,
			hammer_off_t end_off);
static int hammer_check_head_signature(hammer_fifo_head_t head,
			hammer_off_t beg_off);
static void hammer_recover_copy_undo(hammer_off_t undo_offset,
			char *src, char *dst, int bytes);
static hammer_fifo_any_t hammer_recover_scan_fwd(hammer_mount_t hmp,
			hammer_volume_t root_volume,
			hammer_off_t *scan_offsetp,
			int *errorp, struct hammer_buffer **bufferp);
static hammer_fifo_any_t hammer_recover_scan_rev(hammer_mount_t hmp,
			hammer_volume_t root_volume,
			hammer_off_t *scan_offsetp,
			int *errorp, struct hammer_buffer **bufferp);
#if 0
static void hammer_recover_debug_dump(int w, char *buf, int bytes);
#endif
static int hammer_recover_undo(hammer_mount_t hmp, hammer_volume_t root_volume,
			hammer_fifo_undo_t undo);
static int hammer_recover_redo_rec(hammer_mount_t hmp,
			struct hammer_rterm_rb_tree *root,
			hammer_off_t redo_fifo_offset, hammer_fifo_redo_t redo);
static int hammer_recover_redo_run(hammer_mount_t hmp,
			struct hammer_rterm_rb_tree *root,
			hammer_off_t redo_fifo_offset, hammer_fifo_redo_t redo);
static void hammer_recover_redo_exec(hammer_mount_t hmp,
			hammer_fifo_redo_t redo);

RB_GENERATE(hammer_rterm_rb_tree, hammer_rterm, rb_node, hammer_rterm_rb_cmp);

/*
 * Recover filesystem meta-data on mount.  This procedure figures out the
 * UNDO FIFO range and runs the UNDOs backwards.  The FIFO pointers are not
 * resynchronized by this procedure.
 *
 * This procedure is run near the beginning of the mount sequence, before
 * any B-Tree or high-level accesses are enabled, and is responsible for
 * restoring the meta-data to a consistent state.  High level HAMMER data
 * structures (such as the B-Tree) cannot be accessed here.
 *
 * NOTE: No information from the root volume has been cached in the
 *	 hammer_mount structure yet, so we need to access the root volume's
 *	 buffer directly.
 *
 * NOTE:
 */
int
hammer_recover_stage1(hammer_mount_t hmp, hammer_volume_t root_volume)
{
	hammer_blockmap_t rootmap;
	hammer_buffer_t buffer;
	hammer_off_t scan_offset;
	hammer_off_t scan_offset_save;
	hammer_off_t bytes;
	hammer_fifo_any_t head;
	hammer_off_t first_offset;
	hammer_off_t last_offset;
	u_int32_t seqno;
	int error;
	int degenerate_case = 0;

	/*
	 * Examine the UNDO FIFO indices in the volume header.
	 */
	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	first_offset = rootmap->first_offset;
	last_offset  = rootmap->next_offset;
	buffer = NULL;
	error = 0;

	hmp->recover_stage2_offset = 0;

	if (first_offset > rootmap->alloc_offset ||
	    last_offset > rootmap->alloc_offset) {
		kprintf("HAMMER(%s) Illegal UNDO FIFO index range "
			"%016jx, %016jx limit %016jx\n",
			root_volume->ondisk->vol_name,
			(intmax_t)first_offset,
			(intmax_t)last_offset,
			(intmax_t)rootmap->alloc_offset);
		error = EIO;
		goto done;
	}

	/*
	 * In HAMMER version 4+ filesystems the volume header does NOT
	 * contain definitive UNDO FIFO state.  In particular, the
	 * rootmap->next_offset may not be indexed completely to the
	 * end of the active UNDO FIFO.
	 */
	if (hmp->version >= HAMMER_VOL_VERSION_FOUR) {
		/*
		 * To find the definitive range we must first scan backwards
		 * from first_offset to locate the first real record and
		 * extract the sequence number from it.  This record is not
		 * part of the active undo space.
		 */
		scan_offset = first_offset;
		seqno = 0;

		for (;;) {
			head = hammer_recover_scan_rev(hmp, root_volume,
						       &scan_offset,
						       &error, &buffer);
			if (error)
				break;
			if (head->head.hdr_type != HAMMER_HEAD_TYPE_PAD) {
				seqno = head->head.hdr_seq;
				break;
			}
		}
		if (error) {
			kprintf("HAMMER(%s) recovery failure "
				"during seqno backscan\n",
				root_volume->ondisk->vol_name);
			goto done;
		}

		/*
		 * Scan forwards from first_offset and (seqno+1) looking
		 * for a sequence space discontinuity.  This denotes the
		 * end of the active FIFO area.
		 *
		 * NOTE: For the case where the FIFO is empty the very first
		 *	 record we find will be discontinuous.
		 *
		 * NOTE: Do not include trailing PADs in the scan range,
		 *	 and remember the returned scan_offset after a
		 *	 fwd iteration points to the end of the returned
		 *	 record.
		 */
		kprintf("HAMMER(%s) recovery check seqno=%08x\n",
			root_volume->ondisk->vol_name,
			seqno);

		scan_offset = first_offset;
		scan_offset_save = scan_offset;
		++seqno;
		hmp->recover_stage2_seqno = seqno;

		for (;;) {
			head = hammer_recover_scan_fwd(hmp, root_volume,
						       &scan_offset,
						       &error, &buffer);
			if (error)
				break;
			if (head->head.hdr_type != HAMMER_HEAD_TYPE_PAD) {
				if (seqno != head->head.hdr_seq) {
					scan_offset = scan_offset_save;
					break;
				}
				scan_offset_save = scan_offset;
				++seqno;
			}

#if 0
			/*
			 * If the forward scan is grossly ahead of last_offset
			 * then something is wrong.  last_offset is supposed
			 * to be flushed out
			 */
			if (last_offset >= scan_offset) {
				bytes = last_offset - scan_offset;
			} else {
				bytes = rootmap->alloc_offset - scan_offset +
					(last_offset & HAMMER_OFF_LONG_MASK);
			}
			if (bytes >
			    (rootmap->alloc_offset & HAMMER_OFF_LONG_MASK) *
			    4 / 5) {
				kprintf("HAMMER(%s) recovery forward scan is "
					"grossly beyond the last_offset in "
					"the volume header, this can't be "
					"right.\n",
					root_volume->ondisk->vol_name);
				error = EIO;
				break;
			}
#endif
		}

		/*
		 * Store the seqno.  This will be the next seqno we lay down
		 * when generating new UNDOs.
		 */
		hmp->undo_seqno = seqno;
		if (error) {
			kprintf("HAMMER(%s) recovery failure "
				"during seqno fwdscan\n",
				root_volume->ondisk->vol_name);
			goto done;
		}
		last_offset = scan_offset;
		kprintf("HAMMER(%s) recovery range %016jx-%016jx\n"
			"HAMMER(%s) recovery nexto %016jx endseqno=%08x\n",
			root_volume->ondisk->vol_name,
			(intmax_t)first_offset,
			(intmax_t)last_offset,
			root_volume->ondisk->vol_name,
			(intmax_t)rootmap->next_offset,
			seqno);
	}

	/*
	 * Calculate the size of the active portion of the FIFO.  If the
	 * FIFO is empty the filesystem is clean and no further action is
	 * needed.
	 */
	if (last_offset >= first_offset) {
		bytes = last_offset - first_offset;
	} else {
		bytes = rootmap->alloc_offset - first_offset +
			(last_offset & HAMMER_OFF_LONG_MASK);
	}
	if (bytes == 0) {
		degenerate_case = 1;
		error = 0;
		goto done;
	}

	kprintf("HAMMER(%s) recovery undo  %016jx-%016jx (%jd bytes)%s\n",
		root_volume->ondisk->vol_name,
		(intmax_t)first_offset,
		(intmax_t)last_offset,
		(intmax_t)bytes,
		(hmp->ronly ? " (RO)" : "(RW)"));
	if (bytes > (rootmap->alloc_offset & HAMMER_OFF_LONG_MASK)) {
		kprintf("Undo size is absurd, unable to mount\n");
		error = EIO;
		goto done;
	}

	/*
	 * Scan the UNDOs backwards.
	 */
	scan_offset = last_offset;

	while ((int64_t)bytes > 0) {
		KKASSERT(scan_offset != first_offset);
		head = hammer_recover_scan_rev(hmp, root_volume,
					       &scan_offset, &error, &buffer);
		if (error)
			break;

		/*
		 * Normal UNDO
		 */
		error = hammer_recover_undo(hmp, root_volume, &head->undo);
		if (error) {
			kprintf("HAMMER(%s) UNDO record at %016jx failed\n",
				root_volume->ondisk->vol_name,
				(intmax_t)scan_offset - head->head.hdr_size);
			break;
		}

		/*
		 * The first REDO_SYNC record encountered (scanning backwards)
		 * enables REDO processing.
		 */
		if (head->head.hdr_type == HAMMER_HEAD_TYPE_REDO &&
		    head->redo.redo_flags == HAMMER_REDO_SYNC) {
			if (hmp->flags & HAMMER_MOUNT_REDO_RECOVERY_REQ) {
				kprintf("HAMMER(%s) Ignoring extra REDO_SYNC "
					"records in UNDO/REDO FIFO.\n",
					root_volume->ondisk->vol_name
				);
			} else {
				hmp->flags |= HAMMER_MOUNT_REDO_RECOVERY_REQ;
				hmp->recover_stage2_offset =
					head->redo.redo_offset;
				kprintf("HAMMER(%s) Found REDO_SYNC %016jx\n",
					root_volume->ondisk->vol_name,
					(intmax_t)head->redo.redo_offset);
			}
		}

		bytes -= head->head.hdr_size;

		/*
		 * If too many dirty buffers have built up we have to flush'm
		 * out.  As long as we do not flush out the volume header
		 * a crash here should not cause any problems.
		 *
		 * buffer must be released so the flush can assert that
		 * all buffers are idle.
		 */
		if (hammer_flusher_meta_limit(hmp)) {
			if (buffer) {
				hammer_rel_buffer(buffer, 0);
				buffer = NULL;
			}
			if (hmp->ronly == 0) {
				hammer_recover_flush_buffers(hmp, root_volume,
							     0);
				kprintf("HAMMER(%s) Continuing recovery\n",
					root_volume->ondisk->vol_name);
			} else {
				kprintf("HAMMER(%s) Recovery failure: Insufficient buffer cache to hold dirty buffers on read-only mount!\n",
					root_volume->ondisk->vol_name);
				error = EIO;
				break;
			}
		}
	}
	KKASSERT(error || bytes == 0);
done:
	if (buffer) {
		hammer_rel_buffer(buffer, 0);
		buffer = NULL;
	}

	/*
	 * After completely flushing all the recovered buffers the volume
	 * header will also be flushed.
	 */
	if (root_volume->io.recovered == 0) {
		hammer_ref_volume(root_volume);
		root_volume->io.recovered = 1;
	}

	/*
	 * Finish up flushing (or discarding) recovered buffers.  FIFO
	 * indices in the volume header are updated to the actual undo
	 * range but will not be collapsed until stage 2.
	 */
	if (error == 0) {
		hammer_modify_volume_noundo(NULL, root_volume);
		rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
		rootmap->first_offset = first_offset;
		rootmap->next_offset = last_offset;
		hammer_modify_volume_done(root_volume);
		if (hmp->ronly == 0)
			hammer_recover_flush_buffers(hmp, root_volume, 1);
	} else {
		hammer_recover_flush_buffers(hmp, root_volume, -1);
	}
	if (degenerate_case == 0) {
		kprintf("HAMMER(%s) recovery complete\n",
			root_volume->ondisk->vol_name);
	} else {
		kprintf("HAMMER(%s) mounted clean, no recovery needed\n",
			root_volume->ondisk->vol_name);
	}
	return (error);
}

/*
 * Execute redo operations
 *
 * This procedure is run at the end of the mount sequence, after the hammer
 * mount structure has been completely initialized but before the filesystem
 * goes live.  It can access standard cursors, the B-Tree, flush the
 * filesystem, and so forth.
 *
 * This code may only be called for read-write mounts or when a mount
 * switches from read-only to read-write.  vnodes may or may not be present.
 *
 * The stage1 code will have already calculated the correct FIFO range
 * for the nominal UNDO FIFO and stored it in the rootmap.  The extended
 * range for REDO is stored in hmp->recover_stage2_offset.
 */
int
hammer_recover_stage2(hammer_mount_t hmp, hammer_volume_t root_volume)
{
	hammer_blockmap_t rootmap;
	hammer_buffer_t buffer;
	hammer_off_t scan_offset;
	hammer_off_t oscan_offset;
	hammer_off_t bytes;
	hammer_off_t ext_bytes;
	hammer_fifo_any_t head;
	hammer_off_t first_offset;
	hammer_off_t last_offset;
	hammer_off_t ext_offset;
	struct hammer_rterm_rb_tree rterm_root;
	u_int32_t seqno;
	int error;
	int verbose = 0;
	int dorscan;

	/*
	 * Stage 2 can only be run on a RW mount, or when the mount is
	 * switched from RO to RW.
	 */
	KKASSERT(hmp->ronly == 0);
	RB_INIT(&rterm_root);

	if (hammer_skip_redo == 1)
		kprintf("HAMMER(%s) recovery redo marked as optional\n",
		    root_volume->ondisk->vol_name);

	if (hammer_skip_redo == 2) {
		kprintf("HAMMER(%s) recovery redo skipped.\n",
		    root_volume->ondisk->vol_name);
		return (0);
	}

	/*
	 * Examine the UNDO FIFO.  If it is empty the filesystem is clean
	 * and no action need be taken.
	 */
	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	first_offset = rootmap->first_offset;
	last_offset  = rootmap->next_offset;
	if (first_offset == last_offset) {
		KKASSERT((hmp->flags & HAMMER_MOUNT_REDO_RECOVERY_REQ) == 0);
		return(0);
	}

	/*
	 * Stage2 must only be run once, and will not be run at all
	 * if Stage1 did not find a REDO_SYNC record.
	 */
	error = 0;
	buffer = NULL;

	if ((hmp->flags & HAMMER_MOUNT_REDO_RECOVERY_REQ) == 0)
		goto done;
	hmp->flags &= ~HAMMER_MOUNT_REDO_RECOVERY_REQ;
	hmp->flags |= HAMMER_MOUNT_REDO_RECOVERY_RUN;
	ext_offset = hmp->recover_stage2_offset;
	if (ext_offset == 0) {
		kprintf("HAMMER(%s) REDO stage specified but no REDO_SYNC "
			"offset, ignoring\n",
			root_volume->ondisk->vol_name);
		goto done;
	}

	/*
	 * Calculate nominal UNDO range (this is not yet the extended
	 * range).
	 */
	if (last_offset >= first_offset) {
		bytes = last_offset - first_offset;
	} else {
		bytes = rootmap->alloc_offset - first_offset +
			(last_offset & HAMMER_OFF_LONG_MASK);
	}
	kprintf("HAMMER(%s) recovery redo  %016jx-%016jx (%jd bytes)%s\n",
		root_volume->ondisk->vol_name,
		(intmax_t)first_offset,
		(intmax_t)last_offset,
		(intmax_t)bytes,
		(hmp->ronly ? " (RO)" : "(RW)"));
	verbose = 1;
	if (bytes > (rootmap->alloc_offset & HAMMER_OFF_LONG_MASK)) {
		kprintf("Undo size is absurd, unable to mount\n");
		error = EIO;
		goto fatal;
	}

	/*
	 * Scan the REDOs backwards collecting REDO_TERM_* information.
	 * This information is only collected for the extended range,
	 * non-inclusive of any TERMs in the nominal UNDO range.
	 *
	 * If the stage2 extended range is inside the nominal undo range
	 * we have nothing to scan.
	 *
	 * This must fit in memory!
	 */
	if (first_offset < last_offset) {
		/*
		 * [      first_offset........last_offset      ]
		 */
		if (ext_offset < first_offset) {
			dorscan = 1;
			ext_bytes = first_offset - ext_offset;
		} else if (ext_offset > last_offset) {
			dorscan = 1;
			ext_bytes = (rootmap->alloc_offset - ext_offset) +
				    (first_offset & HAMMER_OFF_LONG_MASK);
		} else {
			ext_bytes = -(ext_offset - first_offset);
			dorscan = 0;
		}
	} else {
		/*
		 * [......last_offset         first_offset.....]
		 */
		if (ext_offset < last_offset) {
			ext_bytes = -((rootmap->alloc_offset - first_offset) +
				    (ext_offset & HAMMER_OFF_LONG_MASK));
			dorscan = 0;
		} else if (ext_offset > first_offset) {
			ext_bytes = -(ext_offset - first_offset);
			dorscan = 0;
		} else {
			ext_bytes = first_offset - ext_offset;
			dorscan = 1;
		}
	}

	if (dorscan) {
		scan_offset = first_offset;
		kprintf("HAMMER(%s) Find extended redo  %016jx, %jd extbytes\n",
			root_volume->ondisk->vol_name,
			(intmax_t)ext_offset,
			(intmax_t)ext_bytes);
		seqno = hmp->recover_stage2_seqno - 1;
		for (;;) {
			head = hammer_recover_scan_rev(hmp, root_volume,
						       &scan_offset,
						       &error, &buffer);
			if (error)
				break;
			if (head->head.hdr_type != HAMMER_HEAD_TYPE_PAD) {
				if (head->head.hdr_seq != seqno) {
					error = ERANGE;
					break;
				}
				error = hammer_recover_redo_rec(
						hmp, &rterm_root,
						scan_offset, &head->redo);
				--seqno;
			}
			if (scan_offset == ext_offset)
				break;
		}
		if (error) {
			kprintf("HAMMER(%s) Find extended redo failed %d, "
				"unable to run REDO\n",
				root_volume->ondisk->vol_name,
				error);
			goto done;
		}
	} else {
		kprintf("HAMMER(%s) Embedded extended redo %016jx, "
			"%jd extbytes\n",
			root_volume->ondisk->vol_name,
			(intmax_t)ext_offset,
			(intmax_t)ext_bytes);
	}

	/*
	 * Scan the REDO forwards through the entire extended range.
	 * Anything with a previously recorded matching TERM is discarded.
	 */
	scan_offset = ext_offset;
	bytes += ext_bytes;

	/*
	 * NOTE: when doing a forward scan the returned scan_offset is
	 *	 for the record following the returned record, so we
	 *	 have to play a bit.
	 */
	while ((int64_t)bytes > 0) {
		KKASSERT(scan_offset != last_offset);

		oscan_offset = scan_offset;
		head = hammer_recover_scan_fwd(hmp, root_volume,
					       &scan_offset, &error, &buffer);
		if (error)
			break;

		error = hammer_recover_redo_run(hmp, &rterm_root,
						oscan_offset, &head->redo);
		if (error) {
			kprintf("HAMMER(%s) UNDO record at %016jx failed\n",
				root_volume->ondisk->vol_name,
				(intmax_t)scan_offset - head->head.hdr_size);
			break;
		}
		bytes -= head->head.hdr_size;
	}
	KKASSERT(error || bytes == 0);

done:
	if (buffer) {
		hammer_rel_buffer(buffer, 0);
		buffer = NULL;
	}

	/*
	 * Cleanup rterm tree
	 */
	{
		hammer_rterm_t rterm;
		hammer_rterm_entry_t rte;

		while ((rterm = RB_ROOT(&rterm_root)) != NULL) {
			RB_REMOVE(hammer_rterm_rb_tree, &rterm_root, rterm);
			while ((rte = rterm->term_list) != NULL) {
				rterm->term_list = rte->next;
				kfree(rte, hmp->m_misc);
			}
			kfree(rterm, hmp->m_misc);
		}
	}

	/*
	 * Finish up flushing (or discarding) recovered buffers by executing
	 * a normal flush cycle.  Setting HMNT_UNDO_DIRTY bypasses degenerate
	 * case tests and forces the flush in order to update the FIFO indices.
	 *
	 * If a crash occurs during the flush the entire undo/redo will be
	 * re-run during recovery on the next mount.
	 */
	if (error == 0) {
		if (rootmap->first_offset != rootmap->next_offset)
			hmp->hflags |= HMNT_UNDO_DIRTY;
		hammer_flusher_sync(hmp);
	}
fatal:
	hmp->flags &= ~HAMMER_MOUNT_REDO_RECOVERY_RUN;
	if (verbose) {
		kprintf("HAMMER(%s) End redo recovery\n",
			root_volume->ondisk->vol_name);
	}

	if (error && hammer_skip_redo == 1)
		kprintf("HAMMER(%s) recovery redo error %d, "
		    " skipping.\n", root_volume->ondisk->vol_name,
		    error);

	return (hammer_skip_redo ? 0 : error);
}

/*
 * Scan backwards from *scan_offsetp, return the FIFO record prior to the
 * record at *scan_offsetp or NULL if an error occured.
 *
 * On return *scan_offsetp will be the offset of the returned record.
 */
hammer_fifo_any_t
hammer_recover_scan_rev(hammer_mount_t hmp, hammer_volume_t root_volume,
			hammer_off_t *scan_offsetp,
			int *errorp, struct hammer_buffer **bufferp)
{
	hammer_off_t scan_offset;
	hammer_blockmap_t rootmap;
	hammer_fifo_any_t head;
	hammer_fifo_tail_t tail;

	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	scan_offset = *scan_offsetp;

	if (hammer_debug_general & 0x0080)
		kprintf("rev scan_offset %016jx\n", (intmax_t)scan_offset);
	if (scan_offset == HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0))
		scan_offset = rootmap->alloc_offset;
	if (scan_offset - sizeof(*tail) <
	    HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0)) {
		kprintf("HAMMER(%s) UNDO record at %016jx FIFO underflow\n",
			root_volume->ondisk->vol_name,
			(intmax_t)scan_offset);
		*errorp = EIO;
		return (NULL);
	}
	tail = hammer_bread(hmp, scan_offset - sizeof(*tail),
			    errorp, bufferp);
	if (*errorp) {
		kprintf("HAMMER(%s) Unable to read UNDO TAIL "
			"at %016jx\n",
			root_volume->ondisk->vol_name,
			(intmax_t)scan_offset - sizeof(*tail));
		return (NULL);
	}

	if (hammer_check_tail_signature(tail, scan_offset) != 0) {
		kprintf("HAMMER(%s) Illegal UNDO TAIL signature "
			"at %016jx\n",
			root_volume->ondisk->vol_name,
			(intmax_t)scan_offset - sizeof(*tail));
		*errorp = EIO;
		return (NULL);
	}
	head = (void *)((char *)tail + sizeof(*tail) - tail->tail_size);
	*scan_offsetp = scan_offset - head->head.hdr_size;

	return (head);
}

/*
 * Scan forwards from *scan_offsetp, return the FIFO record or NULL if
 * an error occured.
 *
 * On return *scan_offsetp will be the offset of the record following
 * the returned record.
 */
hammer_fifo_any_t
hammer_recover_scan_fwd(hammer_mount_t hmp, hammer_volume_t root_volume,
			hammer_off_t *scan_offsetp,
			int *errorp, struct hammer_buffer **bufferp)
{
	hammer_off_t scan_offset;
	hammer_blockmap_t rootmap;
	hammer_fifo_any_t head;

	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	scan_offset = *scan_offsetp;

	if (hammer_debug_general & 0x0080)
		kprintf("fwd scan_offset %016jx\n", (intmax_t)scan_offset);
	if (scan_offset == rootmap->alloc_offset)
		scan_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);

	head = hammer_bread(hmp, scan_offset, errorp, bufferp);
	if (*errorp) {
		kprintf("HAMMER(%s) Unable to read UNDO HEAD at %016jx\n",
			root_volume->ondisk->vol_name,
			(intmax_t)scan_offset);
		return (NULL);
	}

	if (hammer_check_head_signature(&head->head, scan_offset) != 0) {
		kprintf("HAMMER(%s) Illegal UNDO TAIL signature "
			"at %016jx\n",
			root_volume->ondisk->vol_name,
			(intmax_t)scan_offset);
		*errorp = EIO;
		return (NULL);
	}
	scan_offset += head->head.hdr_size;
	if (scan_offset == rootmap->alloc_offset)
		scan_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
	*scan_offsetp = scan_offset;

	return (head);
}

/*
 * Helper function for hammer_check_{head,tail}_signature().  Check stuff
 * once the head and tail has been established.
 *
 * This function validates the entire FIFO record wrapper.
 */
static __inline
int
_hammer_check_signature(hammer_fifo_head_t head, hammer_fifo_tail_t tail,
			hammer_off_t beg_off)
{
	hammer_off_t end_off;
	u_int32_t crc;
	int bytes;

	/*
	 * Check signatures.  The tail signature is allowed to be the
	 * head signature only for 8-byte PADs.
	 */
	if (head->hdr_signature != HAMMER_HEAD_SIGNATURE) {
		kprintf("HAMMER: FIFO record bad head signature "
			"%04x at %016jx\n",
			head->hdr_signature,
			(intmax_t)beg_off);
		return(2);
	}
	if (head->hdr_size < HAMMER_HEAD_ALIGN ||
	    (head->hdr_size & HAMMER_HEAD_ALIGN_MASK)) {
		kprintf("HAMMER: FIFO record unaligned or bad size"
			"%04x at %016jx\n",
			head->hdr_size,
			(intmax_t)beg_off);
		return(2);
	}
	end_off = beg_off + head->hdr_size;

	if (head->hdr_type != HAMMER_HEAD_TYPE_PAD ||
	    (size_t)(end_off - beg_off) != sizeof(*tail)) {
		if (head->hdr_type != tail->tail_type) {
			kprintf("HAMMER: FIFO record head/tail type mismatch "
				"%04x %04x at %016jx\n",
				head->hdr_type, tail->tail_type,
				(intmax_t)beg_off);
			return(2);
		}
		if (head->hdr_size != tail->tail_size) {
			kprintf("HAMMER: FIFO record head/tail size mismatch "
				"%04x %04x at %016jx\n",
				head->hdr_size, tail->tail_size,
				(intmax_t)beg_off);
			return(2);
		}
		if (tail->tail_signature != HAMMER_TAIL_SIGNATURE) {
			kprintf("HAMMER: FIFO record bad tail signature "
				"%04x at %016jx\n",
				tail->tail_signature,
				(intmax_t)beg_off);
			return(3);
		}
	}

	/*
	 * Non-PAD records must have a CRC and must be sized at
	 * least large enough to fit the head and tail.
	 */
	if (head->hdr_type != HAMMER_HEAD_TYPE_PAD) {
		crc = crc32(head, HAMMER_FIFO_HEAD_CRCOFF) ^
		      crc32(head + 1, head->hdr_size - sizeof(*head));
		if (head->hdr_crc != crc) {
			kprintf("HAMMER: FIFO record CRC failed %08x %08x "
				"at %016jx\n",
				head->hdr_crc, crc,
				(intmax_t)beg_off);
			return(EIO);
		}
		if (head->hdr_size < sizeof(*head) + sizeof(*tail)) {
			kprintf("HAMMER: FIFO record too small "
				"%04x at %016jx\n",
				head->hdr_size,
				(intmax_t)beg_off);
			return(EIO);
		}
	}

	/*
	 * Check the tail
	 */
	bytes = head->hdr_size;
	tail = (void *)((char *)head + bytes - sizeof(*tail));
	if (tail->tail_size != head->hdr_size) {
		kprintf("HAMMER: Bad tail size %04x vs %04x at %016jx\n",
			tail->tail_size, head->hdr_size,
			(intmax_t)beg_off);
		return(EIO);
	}
	if (tail->tail_type != head->hdr_type) {
		kprintf("HAMMER: Bad tail type %04x vs %04x at %016jx\n",
			tail->tail_type, head->hdr_type,
			(intmax_t)beg_off);
		return(EIO);
	}

	return(0);
}

/*
 * Check that the FIFO record is in-bounds given the head and the
 * hammer offset.
 *
 * Also checks that the head and tail structures agree with each other,
 * but does not check beyond the signature, type, and size.
 */
static int
hammer_check_head_signature(hammer_fifo_head_t head, hammer_off_t beg_off)
{
	hammer_fifo_tail_t tail;
	hammer_off_t end_off;

	/*
	 * head overlaps buffer boundary.  This could be a PAD so only
	 * check the minimum PAD size here.
	 */
	if (((beg_off + sizeof(*tail) - 1) ^ (beg_off)) & ~HAMMER_BUFMASK64)
		return(1);

	/*
	 * Calculate the ending offset and make sure the record does
	 * not cross a buffer boundary.
	 */
	end_off = beg_off + head->hdr_size;
	if ((beg_off ^ (end_off - 1)) & ~HAMMER_BUFMASK64)
		return(1);
	tail = (void *)((char *)head + head->hdr_size - sizeof(*tail));
	return (_hammer_check_signature(head, tail, beg_off));
}

/*
 * Check that the FIFO record is in-bounds given the tail and the
 * hammer offset.  The offset is pointing at the ending boundary of the
 * record.
 *
 * Also checks that the head and tail structures agree with each other,
 * but does not check beyond the signature, type, and size.
 */
static int
hammer_check_tail_signature(hammer_fifo_tail_t tail, hammer_off_t end_off)
{
	hammer_fifo_head_t head;
	hammer_off_t beg_off;

	/*
	 * tail overlaps buffer boundary
	 */
	if (((end_off - sizeof(*tail)) ^ (end_off - 1)) & ~HAMMER_BUFMASK64)
		return(1);

	/*
	 * Calculate the begining offset and make sure the record does
	 * not cross a buffer boundary.
	 */
	beg_off = end_off - tail->tail_size;
	if ((beg_off ^ (end_off - 1)) & ~HAMMER_BUFMASK64)
		return(1);
	head = (void *)((char *)tail + sizeof(*tail) - tail->tail_size);
	return (_hammer_check_signature(head, tail, beg_off));
}

static int
hammer_recover_undo(hammer_mount_t hmp, hammer_volume_t root_volume,
		    hammer_fifo_undo_t undo)
{
	hammer_volume_t volume;
	hammer_buffer_t buffer;
	hammer_off_t buf_offset;
	int zone;
	int error;
	int vol_no;
	int bytes;
	u_int32_t offset;

	/*
	 * Only process UNDO records.  Flag if we find other records to
	 * optimize stage2 recovery.
	 */
	if (undo->head.hdr_type != HAMMER_HEAD_TYPE_UNDO)
		return(0);

	/*
	 * Validate the UNDO record.
	 */
	bytes = undo->head.hdr_size - sizeof(*undo) -
		sizeof(struct hammer_fifo_tail);
	if (bytes < 0 || undo->undo_data_bytes < 0 ||
	    undo->undo_data_bytes > bytes) {
		kprintf("HAMMER: Corrupt UNDO record, undo_data_bytes %d/%d\n",
			undo->undo_data_bytes, bytes);
		return(EIO);
	}

	bytes = undo->undo_data_bytes;

	/*
	 * The undo offset may only be a zone-1 or zone-2 offset.
	 *
	 * Currently we only support a zone-1 offset representing the
	 * volume header.
	 */
	zone = HAMMER_ZONE_DECODE(undo->undo_offset);
	offset = undo->undo_offset & HAMMER_BUFMASK;

	if (offset + bytes > HAMMER_BUFSIZE) {
		kprintf("HAMMER: Corrupt UNDO record, bad offset\n");
		return (EIO);
	}

	switch(zone) {
	case HAMMER_ZONE_RAW_VOLUME_INDEX:
		vol_no = HAMMER_VOL_DECODE(undo->undo_offset);
		volume = hammer_get_volume(hmp, vol_no, &error);
		if (volume == NULL) {
			kprintf("HAMMER: UNDO record, "
				"cannot access volume %d\n", vol_no);
			break;
		}
		hammer_modify_volume_noundo(NULL, volume);
		hammer_recover_copy_undo(undo->undo_offset,
					 (char *)(undo + 1),
					 (char *)volume->ondisk + offset,
					 bytes);
		hammer_modify_volume_done(volume);

		/*
		 * Multiple modifications may be made to the same buffer.
		 * Also, the volume header cannot be written out until
		 * everything else has been flushed.  This also
		 * covers the read-only case by preventing the kernel from
		 * flushing the buffer.
		 */
		if (volume->io.recovered == 0)
			volume->io.recovered = 1;
		else
			hammer_rel_volume(volume, 0);
		break;
	case HAMMER_ZONE_RAW_BUFFER_INDEX:
		buf_offset = undo->undo_offset & ~HAMMER_BUFMASK64;
		buffer = hammer_get_buffer(hmp, buf_offset, HAMMER_BUFSIZE,
					   0, &error);
		if (buffer == NULL) {
			kprintf("HAMMER: UNDO record, "
				"cannot access buffer %016jx\n",
				(intmax_t)undo->undo_offset);
			break;
		}
		hammer_modify_buffer_noundo(NULL, buffer);
		hammer_recover_copy_undo(undo->undo_offset,
					 (char *)(undo + 1),
					 (char *)buffer->ondisk + offset,
					 bytes);
		hammer_modify_buffer_done(buffer);

		/*
		 * Multiple modifications may be made to the same buffer,
		 * improve performance by delaying the flush.  This also
		 * covers the read-only case by preventing the kernel from
		 * flushing the buffer.
		 */
		if (buffer->io.recovered == 0)
			buffer->io.recovered = 1;
		else
			hammer_rel_buffer(buffer, 0);
		break;
	default:
		kprintf("HAMMER: Corrupt UNDO record\n");
		error = EIO;
	}
	return (error);
}

static void
hammer_recover_copy_undo(hammer_off_t undo_offset, 
			 char *src, char *dst, int bytes)
{
	if (hammer_debug_general & 0x0080) {
		kprintf("UNDO %016jx: %d\n",
			(intmax_t)undo_offset, bytes);
	}
#if 0
	kprintf("UNDO %016jx:", (intmax_t)undo_offset);
	hammer_recover_debug_dump(22, dst, bytes);
	kprintf("%22s", "to:");
	hammer_recover_debug_dump(22, src, bytes);
#endif
	bcopy(src, dst, bytes);
}

/*
 * Record HAMMER_REDO_TERM_WRITE and HAMMER_REDO_TERM_TRUNC operations
 * during the backwards scan of the extended UNDO/REDO FIFO.  This scan
 * does not include the nominal UNDO range, just the extended range.
 */
int
hammer_recover_redo_rec(hammer_mount_t hmp, struct hammer_rterm_rb_tree *root,
			hammer_off_t scan_offset, hammer_fifo_redo_t redo)
{
	hammer_rterm_t rterm;
	hammer_rterm_t nrterm;
	hammer_rterm_entry_t rte;

	if (redo->head.hdr_type != HAMMER_HEAD_TYPE_REDO)
		return(0);
	if (redo->redo_flags != HAMMER_REDO_TERM_WRITE &&
	    redo->redo_flags != HAMMER_REDO_TERM_TRUNC) {
		return(0);
	}

	nrterm = kmalloc(sizeof(*nrterm), hmp->m_misc, M_WAITOK|M_ZERO);
	nrterm->redo_objid = redo->redo_objid;
	nrterm->redo_localization = redo->redo_localization;
	nrterm->redo_flags = redo->redo_flags;
	nrterm->redo_offset = redo->redo_offset;

	rterm = RB_INSERT(hammer_rterm_rb_tree, root, nrterm);
	if (rterm)
		kfree(nrterm, hmp->m_misc);
	else
		rterm = nrterm;

	if (bootverbose) {
		kprintf("record record %016jx objid %016jx "
			"offset %016jx flags %08x\n",
			(intmax_t)scan_offset,
			(intmax_t)redo->redo_objid,
			(intmax_t)redo->redo_offset,
			(int)redo->redo_flags);
	}

	/*
	 * Scan in reverse order, rte prepended, so the rte list will be
	 * in forward order.
	 */
	rte = kmalloc(sizeof(*rte), hmp->m_misc, M_WAITOK|M_ZERO);
	rte->fifo_offset = scan_offset;
	rte->next = rterm->term_list;
	rterm->term_list = rte;

	return(0);
}

/*
 * Execute HAMMER_REDO_WRITE and HAMMER_REDO_TRUNC operations during
 * the forwards scan of the entire extended UNDO/REDO FIFO range.
 *
 * Records matching previously recorded TERMs have already been committed
 * and are ignored.
 */
int
hammer_recover_redo_run(hammer_mount_t hmp, struct hammer_rterm_rb_tree *root,
			hammer_off_t scan_offset, hammer_fifo_redo_t redo)
{
	struct hammer_rterm rtval;
	hammer_rterm_t rterm;
	hammer_rterm_entry_t rte;

	if (redo->head.hdr_type != HAMMER_HEAD_TYPE_REDO)
		return(0);

	switch(redo->redo_flags) {
	case HAMMER_REDO_WRITE:
	case HAMMER_REDO_TRUNC:
		/*
		 * We hit a REDO request.  The REDO request is only executed
		 * if there is no matching TERM.
		 */
		bzero(&rtval, sizeof(rtval));
		rtval.redo_objid = redo->redo_objid;
		rtval.redo_localization = redo->redo_localization;
		rtval.redo_offset = redo->redo_offset;
		rtval.redo_flags = (redo->redo_flags == HAMMER_REDO_WRITE) ?
				   HAMMER_REDO_TERM_WRITE :
				   HAMMER_REDO_TERM_TRUNC;

		rterm = RB_FIND(hammer_rterm_rb_tree, root, &rtval);
		if (rterm) {
			if (bootverbose) {
				kprintf("ignore record %016jx objid %016jx "
					"offset %016jx flags %08x\n",
					(intmax_t)scan_offset,
					(intmax_t)redo->redo_objid,
					(intmax_t)redo->redo_offset,
					(int)redo->redo_flags);
			}
			break;
		}
		if (bootverbose) {
			kprintf("run    record %016jx objid %016jx "
				"offset %016jx flags %08x\n",
				(intmax_t)scan_offset,
				(intmax_t)redo->redo_objid,
				(intmax_t)redo->redo_offset,
				(int)redo->redo_flags);
		}

		/*
		 * Redo stage2 can access a live filesystem, acquire the
		 * vnode.
		 */
		hammer_recover_redo_exec(hmp, redo);
		break;
	case HAMMER_REDO_TERM_WRITE:
	case HAMMER_REDO_TERM_TRUNC:
		/*
		 * As we encounter TERMs in the forward scan we remove
		 * them.  Once the forward scan hits the nominal undo range
		 * there will be no more recorded TERMs.
		 */
		bzero(&rtval, sizeof(rtval));
		rtval.redo_objid = redo->redo_objid;
		rtval.redo_localization = redo->redo_localization;
		rtval.redo_flags = redo->redo_flags;
		rtval.redo_offset = redo->redo_offset;

		rterm = RB_FIND(hammer_rterm_rb_tree, root, &rtval);
		if (rterm) {
			if ((rte = rterm->term_list) != NULL) {
				KKASSERT(rte->fifo_offset == scan_offset);
				rterm->term_list = rte->next;
				kfree(rte, hmp->m_misc);
			}
		}
		break;
	}
	return(0);
}

static void
hammer_recover_redo_exec(hammer_mount_t hmp, hammer_fifo_redo_t redo)
{
	struct hammer_transaction trans;
	struct vattr va;
	struct hammer_inode *ip;
	struct vnode *vp = NULL;
	int error;

	hammer_start_transaction(&trans, hmp);

	ip = hammer_get_inode(&trans, NULL, redo->redo_objid,
			      HAMMER_MAX_TID, redo->redo_localization,
			      0, &error);
	if (ip == NULL) {
		kprintf("unable to find objid %016jx:%08x\n",
			(intmax_t)redo->redo_objid, redo->redo_localization);
		goto done2;
	}
	error = hammer_get_vnode(ip, &vp);
	if (error) {
		kprintf("unable to acquire vnode for %016jx:%08x\n",
			(intmax_t)redo->redo_objid, redo->redo_localization);
		goto done1;
	}

	switch(redo->redo_flags) {
	case HAMMER_REDO_WRITE:
		error = VOP_OPEN(vp, FREAD|FWRITE, proc0.p_ucred, NULL);
		if (error) {
			kprintf("vn_rdwr open %016jx:%08x returned %d\n",
				(intmax_t)redo->redo_objid,
				redo->redo_localization, error);
			break;
		}
		vn_unlock(vp);
		error = vn_rdwr(UIO_WRITE, vp, (void *)(redo + 1),
				redo->redo_data_bytes,
				redo->redo_offset, UIO_SYSSPACE,
				0, proc0.p_ucred, NULL);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		if (error) {
			kprintf("write %016jx:%08x returned %d\n",
				(intmax_t)redo->redo_objid,
				redo->redo_localization, error);
		}
		VOP_CLOSE(vp, FREAD|FWRITE, NULL);
		break;
	case HAMMER_REDO_TRUNC:
		VATTR_NULL(&va);
		va.va_size = redo->redo_offset;
		error = VOP_SETATTR(vp, &va, proc0.p_ucred);
		if (error) {
			kprintf("setattr offset %016jx error %d\n",
				(intmax_t)redo->redo_offset, error);
		}
		break;
	}
	vput(vp);
done1:
	hammer_rel_inode(ip, 0);
done2:
	hammer_done_transaction(&trans);
}

/*
 * RB tree compare function.  Note that REDO_TERM_TRUNC ops ignore
 * the offset.
 *
 * WRITE@0 TERM@0 WRITE@0 .... (no TERM@0) etc.
 */
static int
hammer_rterm_rb_cmp(hammer_rterm_t rt1, hammer_rterm_t rt2)
{
	if (rt1->redo_objid < rt2->redo_objid)
		return(-1);
	if (rt1->redo_objid > rt2->redo_objid)
		return(1);
	if (rt1->redo_localization < rt2->redo_localization)
		return(-1);
	if (rt1->redo_localization > rt2->redo_localization)
		return(1);
	if (rt1->redo_flags < rt2->redo_flags)
		return(-1);
	if (rt1->redo_flags > rt2->redo_flags)
		return(1);
	if (rt1->redo_flags != HAMMER_REDO_TERM_TRUNC) {
		if (rt1->redo_offset < rt2->redo_offset)
			return(-1);
		if (rt1->redo_offset > rt2->redo_offset)
			return(1);
	}
	return(0);
}

#if 0

static void
hammer_recover_debug_dump(int w, char *buf, int bytes)
{
	int i;

	for (i = 0; i < bytes; ++i) {
		if (i && (i & 15) == 0)
			kprintf("\n%*.*s", w, w, "");
		kprintf(" %02x", (unsigned char)buf[i]);
	}
	kprintf("\n");
}

#endif

/*
 * Flush recovered buffers from recovery operations.  The call to this
 * routine may be delayed if a read-only mount was made and then later
 * upgraded to read-write.  This routine is also called when unmounting
 * a read-only mount to clean out recovered (dirty) buffers which we
 * couldn't flush (because the mount is read-only).
 *
 * The volume header is always written last.  The UNDO FIFO will be forced
 * to zero-length by setting next_offset to first_offset.  This leaves the
 * (now stale) UNDO information used to recover the disk available for
 * forensic analysis.
 *
 * final is typically 0 or 1.  The volume header is only written if final
 * is 1.  If final is -1 the recovered buffers are discarded instead of
 * written and root_volume can also be passed as NULL in that case.
 */
static int hammer_recover_flush_volume_callback(hammer_volume_t, void *);
static int hammer_recover_flush_buffer_callback(hammer_buffer_t, void *);

void
hammer_recover_flush_buffers(hammer_mount_t hmp, hammer_volume_t root_volume,
			     int final)
{
        /*
         * Flush the buffers out asynchronously, wait for all the I/O to
	 * complete, then do it again to destroy the buffer cache buffer
	 * so it doesn't alias something later on.
         */
	RB_SCAN(hammer_buf_rb_tree, &hmp->rb_bufs_root, NULL,
		hammer_recover_flush_buffer_callback, &final);
	hammer_io_wait_all(hmp, "hmrrcw", 1);
	RB_SCAN(hammer_buf_rb_tree, &hmp->rb_bufs_root, NULL,
		hammer_recover_flush_buffer_callback, &final);

	/*
	 * Flush all volume headers except the root volume.  If final < 0
	 * we discard all volume headers including the root volume.
	 */
	if (final >= 0) {
		RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
			hammer_recover_flush_volume_callback, root_volume);
	} else {
		RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
			hammer_recover_flush_volume_callback, NULL);
	}

	/*
	 * Finalize the root volume header.
	 *
	 * No interlock is needed, volume buffers are not
	 * messed with by bioops.
	 */
	if (root_volume && root_volume->io.recovered && final > 0) {
		hammer_io_wait_all(hmp, "hmrflx", 1);
		root_volume->io.recovered = 0;
		hammer_io_flush(&root_volume->io, 0);
		hammer_rel_volume(root_volume, 0);
		hammer_io_wait_all(hmp, "hmrfly", 1);
	}
}

/*
 * Callback to flush volume headers.  If discarding data will be NULL and
 * all volume headers (including the root volume) will be discarded.
 * Otherwise data is the root_volume and we flush all volume headers
 * EXCEPT the root_volume.
 *
 * Clear any I/O error or modified condition when discarding buffers to
 * clean up the reference count, otherwise the buffer may have extra refs
 * on it.
 */
static
int
hammer_recover_flush_volume_callback(hammer_volume_t volume, void *data)
{
	hammer_volume_t root_volume = data;

	if (volume->io.recovered && volume != root_volume) {
		volume->io.recovered = 0;
		if (root_volume != NULL) {
			/*
			 * No interlock is needed, volume buffers are not
			 * messed with by bioops.
			 */
			hammer_io_flush(&volume->io, 0);
		} else {
			hammer_io_clear_error(&volume->io);
			hammer_io_clear_modify(&volume->io, 1);
		}
		hammer_rel_volume(volume, 0);
	}
	return(0);
}

/*
 * Flush or discard recovered I/O buffers.
 *
 * Clear any I/O error or modified condition when discarding buffers to
 * clean up the reference count, otherwise the buffer may have extra refs
 * on it.
 */
static
int
hammer_recover_flush_buffer_callback(hammer_buffer_t buffer, void *data)
{
	int final = *(int *)data;
	int flush;

	if (buffer->io.recovered) {
		buffer->io.recovered = 0;
		buffer->io.reclaim = 1;
		if (final < 0) {
			hammer_io_clear_error(&buffer->io);
			hammer_io_clear_modify(&buffer->io, 1);
		} else {
			hammer_io_write_interlock(&buffer->io);
			hammer_io_flush(&buffer->io, 0);
			hammer_io_done_interlock(&buffer->io);
		}
		hammer_rel_buffer(buffer, 0);
	} else {
		flush = hammer_ref_interlock(&buffer->io.lock);
		if (flush)
			atomic_add_int(&hammer_count_refedbufs, 1);

		if (final < 0) {
			hammer_io_clear_error(&buffer->io);
			hammer_io_clear_modify(&buffer->io, 1);
		}
		KKASSERT(hammer_oneref(&buffer->io.lock));
		buffer->io.reclaim = 1;
		hammer_rel_buffer(buffer, flush);
	}
	return(0);
}

