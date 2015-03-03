/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/disk.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/gpt.h>

static void gpt_setslice(const char *sname, struct disk_info *info,
			 struct diskslice *sp, struct gpt_ent *sent);

/*
 * Handle GPT on raw disk.  Note that GPTs are not recursive.  The MBR is
 * ignored once a GPT has been detected.
 *
 * GPTs always start at block #1, regardless of how the MBR has been set up.
 * In fact, the MBR's starting block might be pointing to the boot partition
 * in the GPT rather then to the start of the GPT.
 *
 * This routine is called from mbrinit() when a GPT has been detected.
 */
int
gptinit(cdev_t dev, struct disk_info *info, struct diskslices **sspp)
{
	struct buf *bp1 = NULL;
	struct buf *bp2 = NULL;
	struct gpt_hdr *gpt;
	struct gpt_ent *ent;
	struct diskslice *sp;
	struct diskslices *ssp;
	cdev_t wdev;
	int error;
	uint32_t len;
	uint32_t entries;
	uint32_t entsz;
	uint32_t crc;
	uint32_t table_lba;
	uint32_t table_blocks;
	int i = 0, j;
	const char *dname;

	/*
	 * The GPT starts in sector 1.
	 */
	wdev = dev;
	dname = dev_dname(wdev);
	bp1 = geteblk((int)info->d_media_blksize);
	bp1->b_bio1.bio_offset = info->d_media_blksize;
	bp1->b_bio1.bio_done = biodone_sync;
	bp1->b_bio1.bio_flags |= BIO_SYNC;
	bp1->b_bcount = info->d_media_blksize;
	bp1->b_cmd = BUF_CMD_READ;
	bp1->b_flags |= B_FAILONDIS;
	dev_dstrategy(wdev, &bp1->b_bio1);
	if (biowait(&bp1->b_bio1, "gptrd") != 0) {
		kprintf("%s: reading GPT @ block 1: error %d\n",
			dname, bp1->b_error);
		error = EIO;
		goto done;
	}

	/*
	 * Header sanity check
	 */
	gpt = (void *)bp1->b_data;
	len = le32toh(gpt->hdr_size);
	if (len < GPT_MIN_HDR_SIZE || len > info->d_media_blksize) {
		kprintf("%s: Illegal GPT header size %d\n", dname, len);
		error = EINVAL;
		goto done;
	}

	crc = le32toh(gpt->hdr_crc_self);
	gpt->hdr_crc_self = 0;
	if (crc32(gpt, len) != crc) {
		kprintf("%s: GPT CRC32 did not match\n", dname);
		error = EINVAL;
		goto done;
	}

	/*
	 * Validate the partition table and its location, then read it
	 * into a buffer.
	 */
	entries = le32toh(gpt->hdr_entries);
	entsz = le32toh(gpt->hdr_entsz);
	table_lba = le32toh(gpt->hdr_lba_table);
	table_blocks = (entries * entsz + info->d_media_blksize - 1) /
		       info->d_media_blksize;
	if (entries < 1 || entries > 128 ||
	    entsz < 128 || (entsz & 7) || entsz > MAXBSIZE / entries ||
	    table_lba < 2 || table_lba + table_blocks > info->d_media_blocks) {
		kprintf("%s: GPT partition table is out of bounds\n", dname);
		error = EINVAL;
		goto done;
	}

	/*
	 * XXX subject to device dma size limitations
	 */
	bp2 = geteblk((int)(table_blocks * info->d_media_blksize));
	bp2->b_bio1.bio_offset = (off_t)table_lba * info->d_media_blksize;
	bp2->b_bio1.bio_done = biodone_sync;
	bp2->b_bio1.bio_flags |= BIO_SYNC;
	bp2->b_bcount = table_blocks * info->d_media_blksize;
	bp2->b_cmd = BUF_CMD_READ;
	bp2->b_flags |= B_FAILONDIS;
	dev_dstrategy(wdev, &bp2->b_bio1);
	if (biowait(&bp2->b_bio1, "gptrd") != 0) {
		kprintf("%s: reading GPT partition table @ %lld: error %d\n",
			dname,
			(long long)bp2->b_bio1.bio_offset,
			bp2->b_error);
		error = EIO;
		goto done;
	}

	/*
	 * We are passed a pointer to a minimal slices struct.  Replace
	 * it with a maximal one (128 slices + special slices).  Well,
	 * really there is only one special slice (the WHOLE_DISK_SLICE)
	 * since we use the compatibility slice for s0, but don't quibble.
	 * 
	 */
	kfree(*sspp, M_DEVBUF);
	ssp = *sspp = dsmakeslicestruct(BASE_SLICE+128, info);

	/*
	 * Create a slice for each partition.
	 */
	for (i = 0; i < (int)entries && i < 128; ++i) {
		struct gpt_ent sent;
		char partname[2];
		char *sname;

		ent = (void *)((char *)bp2->b_data + i * entsz);
		le_uuid_dec(&ent->ent_type, &sent.ent_type);
		le_uuid_dec(&ent->ent_uuid, &sent.ent_uuid);
		sent.ent_lba_start = le64toh(ent->ent_lba_start);
		sent.ent_lba_end = le64toh(ent->ent_lba_end);
		sent.ent_attr = le64toh(ent->ent_attr);

		for (j = 0; j < NELEM(ent->ent_name); ++j)
			sent.ent_name[j] = le16toh(ent->ent_name[j]);

		/*
		 * The COMPATIBILITY_SLICE is actually slice 0 (s0).  This
		 * is a bit weird becaue the whole-disk slice is #1, so
		 * slice 1 (s1) starts at BASE_SLICE.
		 */
		if (i == 0)
			sp = &ssp->dss_slices[COMPATIBILITY_SLICE];
		else
			sp = &ssp->dss_slices[BASE_SLICE+i-1];
		sname = dsname(dev, dkunit(dev), WHOLE_DISK_SLICE,
			       WHOLE_SLICE_PART, partname);

		if (kuuid_is_nil(&sent.ent_type))
			continue;

		if (sent.ent_lba_start < table_lba + table_blocks ||
		    sent.ent_lba_end >= info->d_media_blocks ||
		    sent.ent_lba_start >= sent.ent_lba_end) {
			kprintf("%s part %d: unavailable, bad start or "
				"ending lba\n",
				sname, i);
		} else {
			gpt_setslice(sname, info, sp, &sent);
		}
	}
	ssp->dss_nslices = BASE_SLICE + i;

	error = 0;
done:
	if (bp1) {
		bp1->b_flags |= B_INVAL | B_AGE;
		brelse(bp1);
	}
	if (bp2) {
		bp2->b_flags |= B_INVAL | B_AGE;
		brelse(bp2);
	}
	if (error == EINVAL)
		error = 0;
	return (error);
}

static
void
gpt_setslice(const char *sname, struct disk_info *info, struct diskslice *sp,
	     struct gpt_ent *sent)
{
	sp->ds_offset = sent->ent_lba_start;
	sp->ds_size   = sent->ent_lba_end + 1 - sent->ent_lba_start;
	sp->ds_type   = 1;	/* XXX */
	sp->ds_type_uuid = sent->ent_type;
	sp->ds_stor_uuid = sent->ent_uuid;
	sp->ds_reserved = 0;	/* no reserved sectors */
}

