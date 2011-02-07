/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
 * This file implements initial version of a mirror target
 */
#include <sys/types.h>
#include <sys/param.h>

#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/uuid.h>
#include <sys/vnode.h>

#include <dev/disk/dm/dm.h>
MALLOC_DEFINE(M_DMDMIRROR, "dm_dmirror", "Device Mapper Target DMIRROR");

/* segdesc flags */
#define MEDIA_UNSTABLE		0x0001
#define	MEDIA_READ_DEGRADED	0x0002
#define MEDIA_WRITE_DEGRADED	0x0004
#define MEDIA_MASTER		0x0008
#define UNINITIALIZED		0x0010
#define OLD_UNSTABLE		0x0020
#define OLD_MSATER		0x0040

/* dmirror disk flags */
#define DISK_ONLINE		0x0001


#define	dmirror_set_bio_disk(bio, x)	((bio)->bio_caller_info1.ptr = (x))
#define	dmirror_get_bio_disk(bio)	((bio)?((bio)->bio_caller_info1.ptr):NULL)
#define	dmirror_set_bio_seg(bio, x)	((bio)->bio_caller_info2.offset = (x))
#define	dmirror_get_bio_segno(bio)	((bio)?((bio)->bio_caller_info2.offset):0)

#define	dmirror_set_bio_retries(bio, x)	((bio)->bio_caller_info3.value = (x))
#define	dmirror_get_bio_retries(bio)	((bio)?((bio)->bio_caller_info3.value):0)

#define dmirror_set_bio_mbuf(bio, x)	((bio)->bio_caller_info3.ptr = (x))
#define dmirror_get_bio_mbuf(bio)	((bio)?((bio)->bio_caller_info3.ptr):NULL)



/* Segment descriptor for each logical segment */
typedef struct segdesc {
	uint32_t	flags;		/* Flags, including state */
	uint32_t	zf_bitmap;	/* Zero-fill bitmap */
	uint8_t		disk_no;
	uint8_t		spare1;
	uint16_t	spare2;
	uint32_t	spare3;
	/* XXX: some timestamp/serial */
} segdesc_t;

typedef struct dmirror_disk {
	uint32_t	flags;
	dm_pdev_t 	*pdev;
} dmirror_disk_t;

typedef struct target_dmirror_config {
	size_t	params_len;
	dmirror_disk_t	disks[4];
	uint8_t	ndisks;
	/* XXX: uuid stuff */
	
} dm_target_dmirror_config_t;

static
struct bio*
dmirror_clone_bio(struct bio *obio)
{
	struct bio *bio;
	struct buf *mbp;
	struct buf *bp;

	mbp = obio->bio_buf;
	bp = getpbuf(NULL);

	BUF_KERNPROC(bp);
	bp->b_vp = mbp->b_vp;
	bp->b_cmd = mbp->b_cmd;
	bp->b_data = (char *)mbp->b_data;
	bp->b_resid = bp->b_bcount = mbp->b_bcount;
	bp->b_bufsize = bp->b_bcount;

	bio = &bp->b_bio1;
	bio->bio_offset = obio->bio_offset;
	
	return (bio);
}

static void
dmirror_write_done(struct bio *bio)
{
	dmirror_disk_t disk;
	off_t segno;
	struct bio *obio, *mbio;
	int retries;

	disk = dmirror_get_bio_disk(bio);
	segno = dmirror_get_bio_segno(bio);
	mbio = dmirror_get_bio_mbuf(bio);

	if (bio->bio_buf->b_flags & B_ERROR) {
		/* write failed */
	}

	obio = pop_bio(bio);
	biodone(obio);
}

void
dmirror_issue_write(dmirror_disk_t disk, struct bio *bio)
{
	dmirror_set_bio_disk(bio, disk);
	dmirror_set_bio_segno(bio, SEGNO_FROM_OFFSET(bio->bio_offset));

	bio->bio_done = dmirror_write_done;
	vn_strategy(disk->pdev, bio);
}

void
dmirror_write(dm_target_crypt_config_t config, struct bio *bio)
{
	dmirror_disk_t disk, m_disk;
	struct bio *wbio1, *wbio2;
	segdesc_t segdesc;
	int i, masters = 0;

	for(i = 0; i < XXX config->ndisks; i++) {
		disk = &config->disks[i];
		segdesc = SEGDESC_FROM_OFFSET(disk, bio->bio_offset);
		if (segdesc->flags & MEDIA_MASTER) {
			if (++masters == 1)
				m_disk = disk;
		}
	}

	if (masters == 1) {
		dmirror_set_bio_mbuf(bio, NULL);
		dmirror_issue_write(m_disk, bio);
	} else {
		wbio1 = dmirror_clone_bio(bio);
		wbio2 = dmirror_clone_bio(bio);
		dmirror_set_bio_mbuf(wbio1, bio);
		dmirror_set_bio_mbuf(wbio2, bio);
		dmirror_issue_write(XXX disk1, wbio1);
		dmirror_issue_write(XXX disk2, wbio2);
	}
	
}

static void
segdesc_set_flag(dmirror_disk_t disk, off_t segno, int flag)
{
	/*
	 * XXX: set the flag on the in-memory descriptor and write back to disks.
	 */
	foo |= flag;
}


static void
segdesc_clear_flag(dmirror_disk_t disk, off_t segno, int flag)
{
	/*
	 * XXX: set the flag on the in-memory descriptor and write back to disks.
	 */
	foo &= ~flag;
}

static void
dmirror_read_done(struct bio *bio)
{
	dmirror_disk_t disk;
	off_t segno;
	struct bio *obio;
	int retries;

	disk = dmirror_get_bio_disk(bio);
	segno = dmirror_get_bio_segno(bio);
	retries = dmirror_get_bio_retries(bio);

	if (bio->bio_buf->b_flags & B_ERROR) {
		/* read failed, so redispatch to a different disk */
		segdesc_set_flag(disk, segno, MEDIA_READ_DEGRADED);
		/* XXX: set other disk to master, if possible */
		if (retries < disk->config->max_retries) {
			dmirror_set_bio_retries(bio, retries + 1);
			/*
			 * XXX: how do we restore the bio to health? Like this?
			 */
			bio->bio_buf->b_flags &= ~(B_ERROR | B_INVAL);
			/*
			 * XXX: something tells me that dispatching stuff from a
			 *	biodone routine is not the greatest idea
			 */
			dmirror_issue_read(next_disk, bio);
			return;
		}
	}

	obio = pop_bio(bio);
	biodone(obio);
}

void
dmirror_issue_read(dmirror_disk_t disk, struct bio *bio)
{
	dmirror_set_bio_disk(bio, disk);
	dmirror_set_bio_segno(bio, SEGNO_FROM_OFFSET(bio->bio_offset));

	bio->bio_done = dmirror_read_done;
	vn_strategy(disk->pdev, bio);
}

void
dmirror_read(dm_target_crypt_config_t config, struct bio *bio)
{
	dmirror_disk_t disk, m_disk;
	segdesc_t segdesc;
	int i, masters = 0;

	for(i = 0; i < XXX config->ndisks; i++) {
		disk = &config->disks[i];
		segdesc = SEGDESC_FROM_OFFSET(disk, bio->bio_offset);
		if (segdesc->flags & MEDIA_MASTER) {
			if (++masters == 1)
				m_disk = disk;
		}
	}

	if (masters > 1) {
		/* XXX: fail. */
		biodone(foo);
		return;
	}

	if (masters == 1) {
		segdesc = SEGDESC_FROM_OFFSET(m_disk, bio->bio_offset);
		if (segdesc->flags & UNINITIALIZED) {
			/* XXX: ... */
		}
		dmirror_issue_read(m_disk, bio);
	} else {
		/* dispatch read to any disk */
		/* but try not to send to a READ_DEGRADED drive */
		m_disk = NULL;		
		for (i = 0; i < config->ndisks; i++) {
			disk = &config->disks[i];
			segdesc = SEGDESC_FROM_OFFSET(disk, bio->bio_offset);
			if (!(segdesc->flags & MEDIA_READ_DEGRADED)) {
				m_disk = disk;
				break;	
			}
		}
		/* XXX: do the uninitialized magic here, too */
		if (m_disk) {
			/*
			 * XXX: we found some non-degraded disk. We might want to
			 * optimize performance by sending reads to different disks,
			 * not just the first one.
			 */
			dmirror_set_bio_retries(bio, 0);
			dmirror_issue_read(m_disk, bio);
		} else {
			/* XXX: all disks are read degraded, just sent to any */
			m_disk = &config->disks[i];
			dmirror_set_bio_retries(bio, 0);
			dmirror_issue_read(m_disk, bio);		
		}
	}
}

/* Strategy routine called from dm_strategy. */
/*
 * Do IO operation, called from dmstrategy routine.
 */
int
dm_target_dmirror_strategy(dm_table_entry_t * table_en, struct buf * bp)
{
	struct bio *bio, *split_bio1, *split_bio2;
	struct buf *bp;
	off_t bseg, eseg, seg_end;
	size_t fsb;
	int split_transaction = 0;

	dm_target_crypt_config_t *priv;
	priv = table_en->target_config;

	if ((bp->b_cmd == BUF_CMD_READ) || (bp->b_cmd == BUF_CMD_WRITE)) {
		/* Get rid of stuff we can't really handle */
		if (((bp->b_bcount % DEV_BSIZE) != 0) || (bp->b_bcount == 0)) {
			kprintf("dm_target_dmirror_strategy: can't really handle bp->b_bcount = %d\n", bp->b_bcount);
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR | B_INVAL;
			biodone(&bp->b_bio1);
			return 0;
		}

		bseg = SEGNO_FROM_OFFSET(bp->b_bio1.bio_offset);
		eseg = SEGNO_FROM_OFFSET(bp->b_bio1.bio_offset + bp->b_resid);
		seg_end = OFFSET_FROM_SEGNO(eseg);

		if (bseg != eseg) {
			split_transaction = 1;
			/* fsb = first segment bytes (bytes in the first segment) */
			fsb = seg_end - bp->b_bio1.bio_offset;

			nestbuf = getpbuf(NULL);
			nestiobuf_setup(&bp->b_bio1, nestbuf, 0, fsb);
			split_bio1 = push_bio(&nestbuf->b_bio1);
			split_bio1->bio_offset = bp->b_bio1.bio_offset +
			    priv->block_offset*DEV_BSIZE;

			nestbuf = getpbuf(NULL);
			nestiobuf_setup(&bp->b_bio1, nestbuf, fsb, bp->b_resid - fsb);
			split_bio2 = push_bio(&nestbuf->b_bio1);
			split_bio2->bio_offset = bp->b_bio1.bio_offset + fsb +
			    priv->block_offset*DEV_BSIZE;
		}
	}

	switch (bp->b_cmd) {
	case BUF_CMD_READ:
		if (split_transaction) {
			dmirror_read(priv, split_bio1);
			dmirror_read(priv, split_bio2);
		} else {
			bio = push_bio(&bp->b_bio1);
			bio->bio_offset = bp->b_bio1.bio_offset + priv->block_offset*DEV_BSIZE;
			dmirror_read(priv, bio);
		}
		break;

	case BUF_CMD_WRITE:
		if (split_transaction) {
			dmirror_write(priv, split_bio1);
			dmirror_write(priv, split_bio2);
		} else {
			bio = push_bio(&bp->b_bio1);
			bio->bio_offset = bp->b_bio1.bio_offset + priv->block_offset*DEV_BSIZE;
			dmirror_write(priv, bio);
		}
		break;

	default:
		/* XXX: clone... */
		vn_strategy(priv->pdev[0]->pdev_vnode, &bp->b_bio1);
		vn_strategy(priv->pdev[1]->pdev_vnode, &bp->b_bio1);
	}

	return 0;

}

/* XXX: add missing dm functions */
