/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/disklabel32.h>
#include <sys/disklabel64.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/devfs.h>
#include <sys/thread.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/stat.h>
#include <sys/uuid.h>
#include <sys/dmsg.h>

#include <sys/buf2.h>
#include <sys/mplock2.h>
#include <sys/msgport2.h>
#include <sys/thread2.h>

struct dios_open {
	int	openrd;
	int	openwr;
};

struct dios_io {
	int	count;
	int	eof;
};

static MALLOC_DEFINE(M_DMSG_DISK, "dmsg_disk", "disk dmsg");

static int disk_iocom_reconnect(struct disk *dp, struct file *fp);
static int disk_rcvdmsg(kdmsg_msg_t *msg);

static void disk_blk_open(struct disk *dp, kdmsg_msg_t *msg);
static void disk_blk_read(struct disk *dp, kdmsg_msg_t *msg);
static void disk_blk_write(struct disk *dp, kdmsg_msg_t *msg);
static void disk_blk_flush(struct disk *dp, kdmsg_msg_t *msg);
static void disk_blk_freeblks(struct disk *dp, kdmsg_msg_t *msg);
static void diskiodone(struct bio *bio);

void
disk_iocom_init(struct disk *dp)
{
	kdmsg_iocom_init(&dp->d_iocom, dp,
			 KDMSG_IOCOMF_AUTOCONN |
			 KDMSG_IOCOMF_AUTORXSPAN |
			 KDMSG_IOCOMF_AUTOTXSPAN,
			 M_DMSG_DISK, disk_rcvdmsg);
}

void
disk_iocom_update(struct disk *dp)
{
}

void
disk_iocom_uninit(struct disk *dp)
{
	kdmsg_iocom_uninit(&dp->d_iocom);
}

int
disk_iocom_ioctl(struct disk *dp, int cmd, void *data)
{
	struct file *fp;
	struct disk_ioc_recluster *recl;
	int error;

	switch(cmd) {
	case DIOCRECLUSTER:
		recl = data;
		fp = holdfp(curproc->p_fd, recl->fd, -1);
		if (fp) {
			error = disk_iocom_reconnect(dp, fp);
		} else {
			error = EINVAL;
		}
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return error;
}

static
int
disk_iocom_reconnect(struct disk *dp, struct file *fp)
{
	char devname[64];

	ksnprintf(devname, sizeof(devname), "%s%d",
		  dev_dname(dp->d_rawdev), dkunit(dp->d_rawdev));

	kdmsg_iocom_reconnect(&dp->d_iocom, fp, devname);

	dp->d_iocom.auto_lnk_conn.pfs_type = DMSG_PFSTYPE_SERVER;
	dp->d_iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	dp->d_iocom.auto_lnk_conn.peer_type = DMSG_PEER_BLOCK;
	dp->d_iocom.auto_lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;
	dp->d_iocom.auto_lnk_conn.pfs_mask = (uint64_t)-1;
	ksnprintf(dp->d_iocom.auto_lnk_conn.cl_label,
		  sizeof(dp->d_iocom.auto_lnk_conn.cl_label),
		  "%s/%s", hostname, devname);
	if (dp->d_info.d_serialno) {
		ksnprintf(dp->d_iocom.auto_lnk_conn.fs_label,
			  sizeof(dp->d_iocom.auto_lnk_conn.fs_label),
			  "%s", dp->d_info.d_serialno);
	}

	dp->d_iocom.auto_lnk_span.pfs_type = DMSG_PFSTYPE_SERVER;
	dp->d_iocom.auto_lnk_span.proto_version = DMSG_SPAN_PROTO_1;
	dp->d_iocom.auto_lnk_span.peer_type = DMSG_PEER_BLOCK;
	dp->d_iocom.auto_lnk_span.media.block.bytes =
						dp->d_info.d_media_size;
	dp->d_iocom.auto_lnk_span.media.block.blksize =
						dp->d_info.d_media_blksize;
	ksnprintf(dp->d_iocom.auto_lnk_span.cl_label,
		  sizeof(dp->d_iocom.auto_lnk_span.cl_label),
		  "%s/%s", hostname, devname);
	if (dp->d_info.d_serialno) {
		ksnprintf(dp->d_iocom.auto_lnk_span.fs_label,
			  sizeof(dp->d_iocom.auto_lnk_span.fs_label),
			  "%s", dp->d_info.d_serialno);
	}

	kdmsg_iocom_autoinitiate(&dp->d_iocom, NULL);

	return (0);
}

static int
disk_rcvdmsg(kdmsg_msg_t *msg)
{
	struct disk *dp = msg->state->iocom->handle;

	/*
	 * Handle debug messages (these might not be in transactions)
	 */
	switch(msg->any.head.cmd & DMSGF_CMDSWMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * Execute shell command (not supported atm)
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		return(0);
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("diskiocom: DEBUGMSG: %s\n", msg->aux_data);
		}
		return(0);
	}

	/*
	 * All remaining messages must be in a transaction. 
	 *
	 * NOTE!  We currently don't care if the transaction is just
	 *	  the span transaction (for disk probes) or if it is the
	 *	  BLK_OPEN transaction.
	 *
	 * NOTE!  We are switching on the first message's command.  The
	 *	  actual message command within the transaction may be
	 *	  different (if streaming within a transaction).
	 */
	if (msg->state == &msg->state->iocom->state0) {
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		return(0);
	}

	switch(msg->state->rxcmd & DMSGF_CMDSWMASK) {
	case DMSG_BLK_OPEN:
		disk_blk_open(dp, msg);
		break;
	case DMSG_BLK_READ:
		/*
		 * not reached normally but leave in for completeness
		 */
		disk_blk_read(dp, msg);
		break;
	case DMSG_BLK_WRITE:
		disk_blk_write(dp, msg);
		break;
	case DMSG_BLK_FLUSH:
		disk_blk_flush(dp, msg);
		break;
	case DMSG_BLK_FREEBLKS:
		disk_blk_freeblks(dp, msg);
		break;
	default:
		if ((msg->any.head.cmd & DMSGF_REPLY) == 0) {
			if (msg->any.head.cmd & DMSGF_DELETE)
				kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
			else
				kdmsg_msg_result(msg, DMSG_ERR_NOSUPP);
		}
		break;
	}
	return (0);
}

static
void
disk_blk_open(struct disk *dp, kdmsg_msg_t *msg)
{
	struct dios_open *openst;
	int error = DMSG_ERR_NOSUPP;
	int fflags;

	openst = msg->state->any.any;
	if ((msg->any.head.cmd & DMSGF_CMDSWMASK) == DMSG_BLK_OPEN) {
		if (openst == NULL) {
			openst = kmalloc(sizeof(*openst), M_DEVBUF,
						M_WAITOK | M_ZERO);
			msg->state->any.any = openst;
		}
		fflags = 0;
		if (msg->any.blk_open.modes & DMSG_BLKOPEN_RD)
			fflags = FREAD;
		if (msg->any.blk_open.modes & DMSG_BLKOPEN_WR)
			fflags |= FWRITE;
		error = dev_dopen(dp->d_rawdev, fflags, S_IFCHR, proc0.p_ucred, NULL);
		if (error) {
			error = DMSG_ERR_IO;
		} else {
			if (msg->any.blk_open.modes & DMSG_BLKOPEN_RD)
				++openst->openrd;
			if (msg->any.blk_open.modes & DMSG_BLKOPEN_WR)
				++openst->openwr;
		}
	}
#if 0
	if ((msg->any.head.cmd & DMSGF_CMDSWMASK) == DMSG_BLK_CLOSE &&
	    openst) {
		fflags = 0;
		if ((msg->any.blk_open.modes & DMSG_BLKOPEN_RD) &&
		    openst->openrd) {
			fflags = FREAD;
		}
		if ((msg->any.blk_open.modes & DMSG_BLKOPEN_WR) &&
		    openst->openwr) {
			fflags |= FWRITE;
		}
		error = dev_dclose(dp->d_rawdev, fflags, S_IFCHR, NULL);
		if (error) {
			error = DMSG_ERR_IO;
		} else {
			if (msg->any.blk_open.modes & DMSG_BLKOPEN_RD)
				--openst->openrd;
			if (msg->any.blk_open.modes & DMSG_BLKOPEN_WR)
				--openst->openwr;
		}
	}
#endif
	if (msg->any.head.cmd & DMSGF_DELETE) {
		if (openst) {
			while (openst->openrd && openst->openwr) {
				--openst->openrd;
				--openst->openwr;
				dev_dclose(dp->d_rawdev, FREAD|FWRITE, S_IFCHR, NULL);
			}
			while (openst->openrd) {
				--openst->openrd;
				dev_dclose(dp->d_rawdev, FREAD, S_IFCHR, NULL);
			}
			while (openst->openwr) {
				--openst->openwr;
				dev_dclose(dp->d_rawdev, FWRITE, S_IFCHR, NULL);
			}
			kfree(openst, M_DEVBUF);
			msg->state->any.any = NULL;
		}
		kdmsg_msg_reply(msg, error);
	} else {
		kdmsg_msg_result(msg, error);
	}
}

static
void
disk_blk_read(struct disk *dp, kdmsg_msg_t *msg)
{
	struct dios_io *iost;
	struct buf *bp;
	struct bio *bio;
	int error = DMSG_ERR_NOSUPP;
	int reterr = 1;

	/*
	 * Only DMSG_BLK_READ commands imply read ops.
	 */
	iost = msg->state->any.any;
	if ((msg->any.head.cmd & DMSGF_CMDSWMASK) == DMSG_BLK_READ) {
		if (msg->any.blk_read.bytes < DEV_BSIZE ||
		    msg->any.blk_read.bytes > MAXPHYS) {
			error = DMSG_ERR_PARAM;
			goto done;
		}
		if (iost == NULL) {
			iost = kmalloc(sizeof(*iost), M_DEVBUF,
				       M_WAITOK | M_ZERO);
			msg->state->any.any = iost;
		}
		reterr = 0;
		bp = geteblk(msg->any.blk_read.bytes);
		bio = &bp->b_bio1;
		bp->b_cmd = BUF_CMD_READ;
		bp->b_bcount = msg->any.blk_read.bytes;
		bp->b_resid = bp->b_bcount;
		bio->bio_offset = msg->any.blk_read.offset;
		bio->bio_caller_info1.ptr = msg->state;
		bio->bio_done = diskiodone;
		/* kdmsg_state_hold(msg->state); */

		atomic_add_int(&iost->count, 1);
		if (msg->any.head.cmd & DMSGF_DELETE)
			iost->eof = 1;
		BUF_KERNPROC(bp);
		dev_dstrategy(dp->d_rawdev, bio);
	}
done:
	if (reterr) {
		if (msg->any.head.cmd & DMSGF_DELETE) {
			if (iost && iost->count == 0) {
				kfree(iost, M_DEVBUF);
				msg->state->any.any = NULL;
			}
			kdmsg_msg_reply(msg, error);
		} else {
			kdmsg_msg_result(msg, error);
		}
	}
}

static
void
disk_blk_write(struct disk *dp, kdmsg_msg_t *msg)
{
	struct dios_io *iost;
	struct buf *bp;
	struct bio *bio;
	int error = DMSG_ERR_NOSUPP;
	int reterr = 1;

	/*
	 * Only DMSG_BLK_WRITE commands imply read ops.
	 */
	iost = msg->state->any.any;
	if ((msg->any.head.cmd & DMSGF_CMDSWMASK) == DMSG_BLK_WRITE) {
		if (msg->any.blk_write.bytes < DEV_BSIZE ||
		    msg->any.blk_write.bytes > MAXPHYS) {
			error = DMSG_ERR_PARAM;
			goto done;
		}
		if (iost == NULL) {
			iost = kmalloc(sizeof(*iost), M_DEVBUF,
				       M_WAITOK | M_ZERO);
			msg->state->any.any = iost;
		}

		/*
		 * Issue WRITE.  Short data implies zeros.  Try to optimize
		 * the buffer cache buffer for the case where we can just
		 * use the message's data pointer.
		 */
		reterr = 0;
		if (msg->aux_size >= msg->any.blk_write.bytes)
			bp = getpbuf(NULL);
		else
			bp = geteblk(msg->any.blk_write.bytes);
		bio = &bp->b_bio1;
		bp->b_cmd = BUF_CMD_WRITE;
		bp->b_bcount = msg->any.blk_write.bytes;
		bp->b_resid = bp->b_bcount;
		if (msg->aux_size >= msg->any.blk_write.bytes) {
			bp->b_data = msg->aux_data;
		} else {
			bcopy(msg->aux_data, bp->b_data, msg->aux_size);
			bzero(bp->b_data + msg->aux_size,
			      msg->any.blk_write.bytes - msg->aux_size);
		}
		bio->bio_offset = msg->any.blk_write.offset;
		bio->bio_caller_info1.ptr = msg->state;
		bio->bio_done = diskiodone;
		/* kdmsg_state_hold(msg->state); */

		atomic_add_int(&iost->count, 1);
		if (msg->any.head.cmd & DMSGF_DELETE)
			iost->eof = 1;
		BUF_KERNPROC(bp);
		dev_dstrategy(dp->d_rawdev, bio);
	}
done:
	if (reterr) {
		if (msg->any.head.cmd & DMSGF_DELETE) {
			if (iost && iost->count == 0) {
				kfree(iost, M_DEVBUF);
				msg->state->any.any = NULL;
			}
			kdmsg_msg_reply(msg, error);
		} else {
			kdmsg_msg_result(msg, error);
		}
	}
}

static
void
disk_blk_flush(struct disk *dp, kdmsg_msg_t *msg)
{
	struct dios_io *iost;
	struct buf *bp;
	struct bio *bio;
	int error = DMSG_ERR_NOSUPP;
	int reterr = 1;

	/*
	 * Only DMSG_BLK_FLUSH commands imply read ops.
	 */
	iost = msg->state->any.any;
	if ((msg->any.head.cmd & DMSGF_CMDSWMASK) == DMSG_BLK_FLUSH) {
		if (iost == NULL) {
			iost = kmalloc(sizeof(*iost), M_DEVBUF,
				       M_WAITOK | M_ZERO);
			msg->state->any.any = iost;
		}
		reterr = 0;
		bp = getpbuf(NULL);
		bio = &bp->b_bio1;
		bp->b_cmd = BUF_CMD_FLUSH;
		bp->b_bcount = msg->any.blk_flush.bytes;
		bp->b_resid = 0;
		bio->bio_offset = msg->any.blk_flush.offset;
		bio->bio_caller_info1.ptr = msg->state;
		bio->bio_done = diskiodone;
		/* kdmsg_state_hold(msg->state); */

		atomic_add_int(&iost->count, 1);
		if (msg->any.head.cmd & DMSGF_DELETE)
			iost->eof = 1;
		BUF_KERNPROC(bp);
		dev_dstrategy(dp->d_rawdev, bio);
	}
	if (reterr) {
		if (msg->any.head.cmd & DMSGF_DELETE) {
			if (iost && iost->count == 0) {
				kfree(iost, M_DEVBUF);
				msg->state->any.any = NULL;
			}
			kdmsg_msg_reply(msg, error);
		} else {
			kdmsg_msg_result(msg, error);
		}
	}
}

static
void
disk_blk_freeblks(struct disk *dp, kdmsg_msg_t *msg)
{
	struct dios_io *iost;
	struct buf *bp;
	struct bio *bio;
	int error = DMSG_ERR_NOSUPP;
	int reterr = 1;

	/*
	 * Only DMSG_BLK_FREEBLKS commands imply read ops.
	 */
	iost = msg->state->any.any;
	if ((msg->any.head.cmd & DMSGF_CMDSWMASK) == DMSG_BLK_FREEBLKS) {
		if (iost == NULL) {
			iost = kmalloc(sizeof(*iost), M_DEVBUF,
				       M_WAITOK | M_ZERO);
			msg->state->any.any = iost;
		}
		reterr = 0;
		bp = getpbuf(NULL);
		bio = &bp->b_bio1;
		bp->b_cmd = BUF_CMD_FREEBLKS;
		bp->b_bcount = msg->any.blk_freeblks.bytes;
		bp->b_resid = 0;
		bio->bio_offset = msg->any.blk_freeblks.offset;
		bio->bio_caller_info1.ptr = msg->state;
		bio->bio_done = diskiodone;
		/* kdmsg_state_hold(msg->state); */

		atomic_add_int(&iost->count, 1);
		if (msg->any.head.cmd & DMSGF_DELETE)
			iost->eof = 1;
		BUF_KERNPROC(bp);
		dev_dstrategy(dp->d_rawdev, bio);
	}
	if (reterr) {
		if (msg->any.head.cmd & DMSGF_DELETE) {
			if (iost && iost->count == 0) {
				kfree(iost, M_DEVBUF);
				msg->state->any.any = NULL;
			}
			kdmsg_msg_reply(msg, error);
		} else {
			kdmsg_msg_result(msg, error);
		}
	}
}

static
void
diskiodone(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	kdmsg_state_t *state = bio->bio_caller_info1.ptr;
	kdmsg_msg_t *rmsg;
	struct dios_io *iost = state->any.any;
	int error;
	int resid = 0;
	int bytes;
	uint32_t cmd;
	void *data;

	cmd = DMSG_LNK_ERROR;
	data = NULL;
	bytes = 0;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		cmd = DMSG_LNK_ERROR;
		data = bp->b_data;
		bytes = bp->b_bcount;
		/* fall through */
	case BUF_CMD_WRITE:
		if (bp->b_flags & B_ERROR) {
			error = bp->b_error;
		} else {
			error = 0;
			resid = bp->b_resid;
		}
		break;
	case BUF_CMD_FLUSH:
	case BUF_CMD_FREEBLKS:
		if (bp->b_flags & B_ERROR)
			error = bp->b_error;
		else
			error = 0;
		break;
	default:
		panic("diskiodone: Unknown bio cmd = %d\n",
		      bio->bio_buf->b_cmd);
		error = 0;	/* avoid compiler warning */
		break;		/* NOT REACHED */
	}

	/*
	 * Convert error to DMSG_ERR_* code.
	 */
	if (error)
		error = DMSG_ERR_IO;

	/*
	 * Convert LNK_ERROR or BLK_ERROR if non-zero resid.  READS will
	 * have already converted cmd to BLK_ERROR and set up data to return.
	 */
	if (resid && cmd == DMSG_LNK_ERROR)
		cmd = DMSG_BLK_ERROR;
	/* XXX txcmd is delayed so this won't work for streaming */
	if ((state->txcmd & DMSGF_CREATE) == 0)	/* assume serialized */
		cmd |= DMSGF_CREATE;
	if (iost->eof) {
		if (atomic_fetchadd_int(&iost->count, -1) == 1)
			cmd |= DMSGF_DELETE;
	} else {
		atomic_add_int(&iost->count, -1);
	}
	cmd |= DMSGF_REPLY;

	/*
	 * Allocate a basic or extended reply.  Be careful not to populate
	 * extended header fields unless we allocated an extended reply.
	 */
	rmsg = kdmsg_msg_alloc(state, cmd, NULL, 0);
	if (data) {
		rmsg->aux_data = kmalloc(bytes, state->iocom->mmsg, M_INTWAIT);
		rmsg->aux_size = bytes;
		rmsg->flags |= KDMSG_FLAG_AUXALLOC;
		bcopy(data, rmsg->aux_data, bytes);
	}
	rmsg->any.blk_error.head.error = error;
	if ((cmd & DMSGF_BASECMDMASK) == DMSG_BLK_ERROR)
		rmsg->any.blk_error.resid = resid;
	bio->bio_caller_info1.ptr = NULL;
	/* kdmsg_state_drop(state); */
	kdmsg_msg_write(rmsg);
	if (bp->b_flags & B_PAGING) {
		relpbuf(bio->bio_buf, NULL);
	} else {
		bp->b_flags |= B_INVAL | B_AGE;
		brelse(bp);
	}
}
