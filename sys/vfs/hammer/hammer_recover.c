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
 * $DragonFly: src/sys/vfs/hammer/hammer_recover.c,v 1.29 2008/07/26 05:36:21 dillon Exp $
 */

#include "hammer.h"

static int hammer_check_tail_signature(hammer_fifo_tail_t tail,
			hammer_off_t end_off);
static void hammer_recover_copy_undo(hammer_off_t undo_offset,
			char *src, char *dst, int bytes);
#if 0
static void hammer_recover_debug_dump(int w, char *buf, int bytes);
#endif
static int hammer_recover_undo(hammer_mount_t hmp, hammer_volume_t root_volume,
			hammer_fifo_undo_t undo, int bytes);

/*
 * Recover a filesystem on mount
 *
 * NOTE: No information from the root volume has been cached in the
 * hammer_mount structure yet, so we need to access the root volume's
 * buffer directly.
 */
int
hammer_recover(hammer_mount_t hmp, hammer_volume_t root_volume)
{
	hammer_blockmap_t rootmap;
	hammer_buffer_t buffer;
	hammer_off_t scan_offset;
	hammer_off_t bytes;
	hammer_fifo_tail_t tail;
	hammer_fifo_undo_t undo;
	hammer_off_t first_offset;
	hammer_off_t last_offset;
	int error;

	/*
	 * Examine the UNDO FIFO.  If it is empty the filesystem is clean
	 * and no action need be taken.
	 */
	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];

	if (rootmap->first_offset == rootmap->next_offset)
		return(0);

	first_offset = rootmap->first_offset;
	last_offset  = rootmap->next_offset;

	if (last_offset >= first_offset) {
		bytes = last_offset - first_offset;
	} else {
		bytes = rootmap->alloc_offset - first_offset +
			(last_offset & HAMMER_OFF_LONG_MASK);
	}
	kprintf("HAMMER(%s) Start Recovery %016llx - %016llx "
		"(%lld bytes of UNDO)%s\n",
		root_volume->ondisk->vol_name,
		first_offset, last_offset,
		bytes,
		(hmp->ronly ? " (RO)" : "(RW)"));
	if (bytes > (rootmap->alloc_offset & HAMMER_OFF_LONG_MASK)) {
		kprintf("Undo size is absurd, unable to mount\n");
		return(EIO);
	}

	/*
	 * Scan the UNDOs backwards.
	 */
	scan_offset = last_offset;
	buffer = NULL;
	if (scan_offset > rootmap->alloc_offset) {
		kprintf("HAMMER(%s) UNDO record at %016llx FIFO overflow\n",
			root_volume->ondisk->vol_name,
			scan_offset);
		error = EIO;
		goto done;
	}

	while ((int64_t)bytes > 0) {
		if (hammer_debug_general & 0x0080)
			kprintf("scan_offset %016llx\n", scan_offset);
		if (scan_offset == HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0)) {
			scan_offset = rootmap->alloc_offset;
			continue;
		}
		if (scan_offset - sizeof(*tail) <
		    HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0)) {
			kprintf("HAMMER(%s) UNDO record at %016llx FIFO "
				"underflow\n",
				root_volume->ondisk->vol_name,
				scan_offset);
			error = EIO;
			break;
		}
		tail = hammer_bread(hmp, scan_offset - sizeof(*tail),
				    &error, &buffer);
		if (error) {
			kprintf("HAMMER(%s) Unable to read UNDO TAIL "
				"at %016llx\n",
				root_volume->ondisk->vol_name,
				scan_offset - sizeof(*tail));
			break;
		}

		if (hammer_check_tail_signature(tail, scan_offset) != 0) {
			kprintf("HAMMER(%s) Illegal UNDO TAIL signature "
				"at %016llx\n",
				root_volume->ondisk->vol_name,
				scan_offset - sizeof(*tail));
			error = EIO;
			break;
		}
		undo = (void *)((char *)tail + sizeof(*tail) - tail->tail_size);

		error = hammer_recover_undo(hmp, root_volume, undo,
				HAMMER_BUFSIZE -
				(int)((char *)undo - (char *)buffer->ondisk));
		if (error) {
			kprintf("HAMMER(%s) UNDO record at %016llx failed\n",
				root_volume->ondisk->vol_name,
				scan_offset - tail->tail_size);
			break;
		}
		scan_offset -= tail->tail_size;
		bytes -= tail->tail_size;

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
done:
	if (buffer)
		hammer_rel_buffer(buffer, 0);

	/*
	 * After completely flushing all the recovered buffers the volume
	 * header will also be flushed.  Force the UNDO FIFO to 0-length.
	 */
	if (root_volume->io.recovered == 0) {
		hammer_ref_volume(root_volume);
		root_volume->io.recovered = 1;
	}

	/*
	 * Finish up flushing (or discarding) recovered buffers
	 */
	if (error == 0) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
		rootmap->first_offset = last_offset;
		rootmap->next_offset = last_offset;
		hammer_modify_volume_done(root_volume);
		if (hmp->ronly == 0)
			hammer_recover_flush_buffers(hmp, root_volume, 1);
	} else {
		hammer_recover_flush_buffers(hmp, root_volume, -1);
	}
	kprintf("HAMMER(%s) End Recovery\n", root_volume->ondisk->vol_name);
	return (error);
}

static int
hammer_check_tail_signature(hammer_fifo_tail_t tail, hammer_off_t end_off)
{
	int max_bytes;

	max_bytes = ((end_off - sizeof(*tail)) & HAMMER_BUFMASK);
	max_bytes += sizeof(*tail);

	/*
	 * tail overlaps buffer boundary
	 */
	if (((end_off - sizeof(*tail)) ^ (end_off - 1)) & ~HAMMER_BUFMASK64) {
		return(1);
	}

	/*
	 * signature check, the tail signature is allowed to be the head
	 * signature only for 8-byte PADs.
	 */
	switch(tail->tail_signature) {
	case HAMMER_TAIL_SIGNATURE:
		break;
	case HAMMER_HEAD_SIGNATURE:
		if (tail->tail_type != HAMMER_HEAD_TYPE_PAD ||
		    tail->tail_size != sizeof(*tail)) {
			return(2);
		}
		break;
	}

	/*
	 * The undo structure must not overlap a buffer boundary.
	 */
	if (tail->tail_size < sizeof(*tail) || tail->tail_size > max_bytes) {
		return(3);
	}
	return(0);
}

static int
hammer_recover_undo(hammer_mount_t hmp, hammer_volume_t root_volume,
		    hammer_fifo_undo_t undo, int bytes)
{
	hammer_fifo_tail_t tail;
	hammer_volume_t volume;
	hammer_buffer_t buffer;
	hammer_off_t buf_offset;
	int zone;
	int error;
	int vol_no;
	int max_bytes;
	u_int32_t offset;
	u_int32_t crc;

	/*
	 * Basic sanity checks
	 */
	if (bytes < HAMMER_HEAD_ALIGN) {
		kprintf("HAMMER: Undo alignment error (%d)\n", bytes);
		return(EIO);
	}
	if (undo->head.hdr_signature != HAMMER_HEAD_SIGNATURE) {
		kprintf("HAMMER: Bad head signature %04x\n", 
			undo->head.hdr_signature);
		return(EIO);
	}
	if (undo->head.hdr_size < HAMMER_HEAD_ALIGN ||
	    undo->head.hdr_size > bytes) {
		kprintf("HAMMER: Bad size %d\n", bytes);
		return(EIO);
	}

	/*
	 * Skip PAD records.  Note that PAD records also do not require
	 * a tail and may have a truncated structure.
	 */
	if (undo->head.hdr_type == HAMMER_HEAD_TYPE_PAD)
		return(0);

	/*
	 * Check the CRC
	 */
	crc = crc32(undo, HAMMER_FIFO_HEAD_CRCOFF) ^
	      crc32(&undo->head + 1, undo->head.hdr_size - sizeof(undo->head));
	if (undo->head.hdr_crc != crc) {
		kprintf("HAMMER: Undo record CRC failed %08x %08x\n",
			undo->head.hdr_crc, crc);
		return(EIO);
	}


	/*
	 * Check the tail
	 */
	bytes = undo->head.hdr_size;
	tail = (void *)((char *)undo + bytes - sizeof(*tail));
	if (tail->tail_size != undo->head.hdr_size) {
		kprintf("HAMMER: Bad tail size %d\n", tail->tail_size);
		return(EIO);
	}
	if (tail->tail_type != undo->head.hdr_type) {
		kprintf("HAMMER: Bad tail type %d\n", tail->tail_type);
		return(EIO);
	}

	/*
	 * Only process UNDO records
	 */
	if (undo->head.hdr_type != HAMMER_HEAD_TYPE_UNDO)
		return(0);

	/*
	 * Validate the UNDO record.
	 */
	max_bytes = undo->head.hdr_size - sizeof(*undo) - sizeof(*tail);
	if (undo->undo_data_bytes < 0 || undo->undo_data_bytes > max_bytes) {
		kprintf("HAMMER: Corrupt UNDO record, undo_data_bytes %d/%d\n",
			undo->undo_data_bytes, max_bytes);
		return(EIO);
	}

	/*
	 * The undo offset may only be a zone-1 or zone-2 offset.
	 *
	 * Currently we only support a zone-1 offset representing the
	 * volume header.
	 */
	zone = HAMMER_ZONE_DECODE(undo->undo_offset);
	offset = undo->undo_offset & HAMMER_BUFMASK;

	if (offset + undo->undo_data_bytes > HAMMER_BUFSIZE) {
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
		hammer_modify_volume(NULL, volume, NULL, 0);
		hammer_recover_copy_undo(undo->undo_offset,
					 (char *)(undo + 1),
					 (char *)volume->ondisk + offset,
					 undo->undo_data_bytes);
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
				"cannot access buffer %016llx\n",
				undo->undo_offset);
			break;
		}
		hammer_modify_buffer(NULL, buffer, NULL, 0);
		hammer_recover_copy_undo(undo->undo_offset,
					 (char *)(undo + 1),
					 (char *)buffer->ondisk + offset,
					 undo->undo_data_bytes);
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
	if (hammer_debug_general & 0x0080)
		kprintf("UNDO %016llx: %d\n", undo_offset, bytes);
#if 0
	kprintf("UNDO %016llx:", undo_offset);
	hammer_recover_debug_dump(22, dst, bytes);
	kprintf("%22s", "to:");
	hammer_recover_debug_dump(22, src, bytes);
#endif
	bcopy(src, dst, bytes);
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
 * upgraded to read-write.
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
	hammer_io_wait_all(hmp, "hmrrcw");
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
	 */
	if (root_volume && root_volume->io.recovered && final > 0) {
		crit_enter();
		while (hmp->io_running_space > 0)
			tsleep(&hmp->io_running_space, 0, "hmrflx", 0);
		crit_exit();
		root_volume->io.recovered = 0;
		hammer_io_flush(&root_volume->io);
		hammer_rel_volume(root_volume, 0);
	}
}

/*
 * Callback to flush volume headers.  If discarding data will be NULL and
 * all volume headers (including the root volume) will be discarded.
 * Otherwise data is the root_volume and we flush all volume headers
 * EXCEPT the root_volume.
 */
static
int
hammer_recover_flush_volume_callback(hammer_volume_t volume, void *data)
{
	hammer_volume_t root_volume = data;

	if (volume->io.recovered && volume != root_volume) {
		volume->io.recovered = 0;
		if (root_volume != NULL)
			hammer_io_flush(&volume->io);
		else
			hammer_io_clear_modify(&volume->io, 1);
		hammer_rel_volume(volume, 0);
	}
	return(0);
}

static
int
hammer_recover_flush_buffer_callback(hammer_buffer_t buffer, void *data)
{
	int final = *(int *)data;

	if (buffer->io.recovered) {
		buffer->io.recovered = 0;
		buffer->io.reclaim = 1;
		if (final < 0)
			hammer_io_clear_modify(&buffer->io, 1);
		else
			hammer_io_flush(&buffer->io);
		hammer_rel_buffer(buffer, 0);
	} else {
		KKASSERT(buffer->io.lock.refs == 0);
		++hammer_count_refedbufs;
		hammer_ref(&buffer->io.lock);
		buffer->io.reclaim = 1;
		hammer_rel_buffer(buffer, 1);
	}
	return(0);
}

