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
 * $DragonFly: src/sys/vfs/hammer/hammer_recover.c,v 1.21 2008/06/08 18:16:26 dillon Exp $
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
	int error;

	/*
	 * Examine the UNDO FIFO.  If it is empty the filesystem is clean
	 * and no action need be taken.
	 *
	 * NOTE: hmp->blockmap has not been initialized yet so use the
	 * root volume's ondisk buffer directly.
	 */
	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	hmp->flusher_undo_start = rootmap->next_offset;

	if (rootmap->first_offset == rootmap->next_offset)
		return(0);

	if (rootmap->next_offset >= rootmap->first_offset) {
		bytes = rootmap->next_offset - rootmap->first_offset;
	} else {
		bytes = rootmap->alloc_offset - rootmap->first_offset +
			(rootmap->next_offset & HAMMER_OFF_LONG_MASK);
	}
	kprintf("HAMMER(%s) Start Recovery %016llx - %016llx "
		"(%lld bytes of UNDO)%s\n",
		root_volume->ondisk->vol_name,
		rootmap->first_offset, rootmap->next_offset,
		bytes,
		(hmp->ronly ? " (RO)" : "(RW)"));
	if (bytes > (rootmap->alloc_offset & HAMMER_OFF_LONG_MASK)) {
		kprintf("Undo size is absurd, unable to mount\n");
		return(EIO);
	}

	/*
	 * Scan the UNDOs backwards.
	 */
	scan_offset = rootmap->next_offset;
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
	}
done:
	/*
	 * Reload flusher_undo_start to kick off the UNDO sequencing.
	 */
	hmp->flusher_undo_start = rootmap->next_offset;
	if (buffer)
		hammer_rel_buffer(buffer, 0);

	/*
	 * Flush out the root volume header after all other flushes have
	 * completed.
	 */
	if (hmp->ronly == 0 && error == 0 && root_volume->io.recovered) {
		hammer_recover_flush_buffers(hmp, root_volume);
	}
	kprintf("HAMMER(%s) End Recovery\n",
		root_volume->ondisk->vol_name);
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
	if (tail->tail_size < 0 || tail->tail_size > max_bytes) {
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
		 * Multiple modifications may be made to the same buffer,
		 * improve performance by delaying the flush.  This also
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
		buffer = hammer_get_buffer(hmp, buf_offset, 0, &error);
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
 * Flush unwritten buffers from undo recovery operations on a read-only mount
 * when the mount is updated to read-write.
 */
static int hammer_recover_flush_volume_callback(hammer_volume_t, void *);
static int hammer_recover_flush_buffer_callback(hammer_buffer_t, void *);

void
hammer_recover_flush_buffers(hammer_mount_t hmp, hammer_volume_t root_volume)
{
	RB_SCAN(hammer_buf_rb_tree, &hmp->rb_bufs_root, NULL,
		hammer_recover_flush_buffer_callback, NULL);
	RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
		hammer_recover_flush_volume_callback, root_volume);
	if (root_volume->io.recovered) {
		crit_enter();
		while (hmp->io_running_count)
			tsleep(&hmp->io_running_count, 0, "hmrflx", 0);
		crit_exit();
		root_volume->io.recovered = 0;
		hammer_io_flush(&root_volume->io);
		hammer_rel_volume(root_volume, 0);
	}
}

static
int
hammer_recover_flush_volume_callback(hammer_volume_t volume, void *data)
{
	hammer_volume_t root_volume = data;

	if (volume->io.recovered && volume != root_volume) {
		volume->io.recovered = 0;
		hammer_io_flush(&volume->io);
		hammer_rel_volume(volume, 0);
	}
	return(0);
}

static
int
hammer_recover_flush_buffer_callback(hammer_buffer_t buffer, void *data)
{
	if (buffer->io.recovered) {
		buffer->io.recovered = 0;
		hammer_io_flush(&buffer->io);
		hammer_rel_buffer(buffer, 2);
	}
	return(0);
}

