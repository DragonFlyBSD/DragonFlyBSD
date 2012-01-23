/*
 * Implementation of SCSI Processor Target Peripheral driver for CAM.
 *
 * Copyright (c) 1998 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/cam/scsi/scsi_pt.c,v 1.17 2000/01/17 06:27:37 mjacob Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/buf.h>
#include <sys/devicestat.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/ptio.h>
#include <sys/buf2.h>
#include <sys/thread2.h>

#include "../cam.h"
#include "../cam_ccb.h"
#include "../cam_extend.h"
#include "../cam_periph.h"
#include "../cam_xpt_periph.h"
#include "../cam_debug.h"

#include "scsi_all.h"
#include "scsi_message.h"
#include "scsi_pt.h"

#include "opt_pt.h"

typedef enum {
	PT_STATE_PROBE,
	PT_STATE_NORMAL
} pt_state;

typedef enum {
	PT_FLAG_NONE		= 0x00,
	PT_FLAG_OPEN		= 0x01,
	PT_FLAG_DEVICE_INVALID	= 0x02,
	PT_FLAG_RETRY_UA	= 0x04
} pt_flags;

typedef enum {
	PT_CCB_BUFFER_IO	= 0x01,
	PT_CCB_WAITING		= 0x02,
	PT_CCB_RETRY_UA		= 0x04,
	PT_CCB_BUFFER_IO_UA	= PT_CCB_BUFFER_IO|PT_CCB_RETRY_UA
} pt_ccb_state;

/* Offsets into our private area for storing information */
#define ccb_state	ppriv_field0
#define ccb_bio		ppriv_ptr1

struct pt_softc {
	struct	 bio_queue_head bio_queue;
	struct	 devstat device_stats;
	LIST_HEAD(, ccb_hdr) pending_ccbs;
	pt_state state;
	pt_flags flags;	
	union	 ccb saved_ccb;
	int	 io_timeout;
	cdev_t	 dev;
};

static	d_open_t	ptopen;
static	d_close_t	ptclose;
static	d_strategy_t	ptstrategy;
static	periph_init_t	ptinit;
static	void		ptasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	periph_ctor_t	ptctor;
static	periph_oninv_t	ptoninvalidate;
static	periph_dtor_t	ptdtor;
static	periph_start_t	ptstart;
static	void		ptdone(struct cam_periph *periph,
			       union ccb *done_ccb);
static	d_ioctl_t	ptioctl;
static  int		pterror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);

void	scsi_send_receive(struct ccb_scsiio *csio, u_int32_t retries,
			  void (*cbfcnp)(struct cam_periph *, union ccb *),
			  u_int tag_action, int readop, u_int byte2,
			  u_int32_t xfer_len, u_int8_t *data_ptr,
			  u_int8_t sense_len, u_int32_t timeout);

static struct periph_driver ptdriver =
{
	ptinit, "pt",
	TAILQ_HEAD_INITIALIZER(ptdriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(pt, ptdriver);

static struct dev_ops pt_ops = {
	{ "pt", 0, 0 },
	.d_open =	ptopen,
	.d_close =	ptclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	ptioctl,
	.d_strategy =	ptstrategy,
};

static struct extend_array *ptperiphs;

#ifndef SCSI_PT_DEFAULT_TIMEOUT
#define SCSI_PT_DEFAULT_TIMEOUT		60
#endif

static int
ptopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cam_periph *periph;
	struct pt_softc *softc;
	int unit;
	int error = 0;

	unit = minor(dev);
	periph = cam_extend_get(ptperiphs, unit);
	if (cam_periph_acquire(periph) != CAM_REQ_CMP)
		return (ENXIO);	

	softc = (struct pt_softc *)periph->softc;

	cam_periph_lock(periph);
	if (softc->flags & PT_FLAG_DEVICE_INVALID) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return(ENXIO);
	}

	if ((softc->flags & PT_FLAG_OPEN) == 0)
		softc->flags |= PT_FLAG_OPEN;
	else {
		error = EBUSY;
		cam_periph_release(periph);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
	    ("ptopen: dev=%s\n", devtoname(dev)));

	cam_periph_unlock(periph);
	return (error);
}

static int
ptclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct	cam_periph *periph;
	struct	pt_softc *softc;
	int	unit;

	unit = minor(dev);
	periph = cam_extend_get(ptperiphs, unit);
	if (periph == NULL)
		return (ENXIO);	

	softc = (struct pt_softc *)periph->softc;

	cam_periph_lock(periph);

	softc->flags &= ~PT_FLAG_OPEN;
	cam_periph_unlock(periph);
	cam_periph_release(periph);
	return (0);
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static int
ptstrategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct cam_periph *periph;
	struct pt_softc *softc;
	u_int  unit;
	
	unit = minor(dev);
	periph = cam_extend_get(ptperiphs, unit);
	if (periph == NULL) {
		bp->b_error = ENXIO;
		goto bad;		
	}
	cam_periph_lock(periph);
	softc = (struct pt_softc *)periph->softc;

	/*
	 * If the device has been made invalid, error out
	 */
	if ((softc->flags & PT_FLAG_DEVICE_INVALID)) {
		cam_periph_unlock(periph);
		bp->b_error = ENXIO;
		goto bad;
	}
	
	/*
	 * Place it in the queue of disk activities for this disk
	 */
	bioq_insert_tail(&softc->bio_queue, bio);
	
	/*
	 * Schedule ourselves for performing the work.
	 */
	xpt_schedule(periph, /* XXX priority */1);
	cam_periph_unlock(periph);

	return(0);
bad:
	bp->b_flags |= B_ERROR;

	/*
	 * Correctly set the buf to indicate a completed xfer
	 */
	bp->b_resid = bp->b_bcount;
	biodone(bio);
	return(0);
}

static void
ptinit(void)
{
	cam_status status;

	/*
	 * Create our extend array for storing the devices we attach to.
	 */
	ptperiphs = cam_extend_new();
	if (ptperiphs == NULL) {
		kprintf("pt: Failed to alloc extend array!\n");
		return;
	}
	
	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, ptasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		kprintf("pt: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

static cam_status
ptctor(struct cam_periph *periph, void *arg)
{
	struct pt_softc *softc;
	struct ccb_getdev *cgd;

	cgd = (struct ccb_getdev *)arg;
	if (periph == NULL) {
		kprintf("ptregister: periph was NULL!!\n");
		return(CAM_REQ_CMP_ERR);
	}

	if (cgd == NULL) {
		kprintf("ptregister: no getdev CCB, can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = kmalloc(sizeof(*softc), M_DEVBUF, M_INTWAIT | M_ZERO);
	LIST_INIT(&softc->pending_ccbs);
	softc->state = PT_STATE_NORMAL;
	bioq_init(&softc->bio_queue);

	softc->io_timeout = SCSI_PT_DEFAULT_TIMEOUT * 1000;

	periph->softc = softc;
	
	cam_periph_unlock(periph);
	cam_extend_set(ptperiphs, periph->unit_number, periph);

	devstat_add_entry(&softc->device_stats, "pt",
			  periph->unit_number, 0,
			  DEVSTAT_NO_BLOCKSIZE,
			  SID_TYPE(&cgd->inq_data) | DEVSTAT_TYPE_IF_SCSI,
			  DEVSTAT_PRIORITY_OTHER);

	make_dev(&pt_ops, periph->unit_number, UID_ROOT,
		  GID_OPERATOR, 0600, "%s%d", periph->periph_name,
		  periph->unit_number);
	cam_periph_lock(periph);
	/*
	 * Add async callbacks for bus reset and
	 * bus device reset calls.  I don't bother
	 * checking if this fails as, in most cases,
	 * the system will function just fine without
	 * them and the only alternative would be to
	 * not attach the device on failure.
	 */
	xpt_register_async(AC_SENT_BDR | AC_BUS_RESET | AC_LOST_DEVICE,
			   ptasync, periph, periph->path);

	/* Tell the user we've attached to the device */
	xpt_announce_periph(periph, NULL);

	return(CAM_REQ_CMP);
}

static void
ptoninvalidate(struct cam_periph *periph)
{
	struct pt_softc *softc;
	struct bio *q_bio;
	struct buf *q_bp;

	softc = (struct pt_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, ptasync, periph, periph->path);

	softc->flags |= PT_FLAG_DEVICE_INVALID;

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
	while ((q_bio = bioq_takefirst(&softc->bio_queue)) != NULL) {
		q_bp = q_bio->bio_buf;
		q_bp->b_resid = q_bp->b_bcount;
		q_bp->b_error = ENXIO;
		q_bp->b_flags |= B_ERROR;
		biodone(q_bio);
	}

	xpt_print(periph->path, "lost device\n");
}

static void
ptdtor(struct cam_periph *periph)
{
	struct pt_softc *softc;

	softc = (struct pt_softc *)periph->softc;

	devstat_remove_entry(&softc->device_stats);

	cam_extend_release(ptperiphs, periph->unit_number);
	xpt_print(periph->path, "removing device entry\n");
	dev_ops_remove_minor(&pt_ops, periph->unit_number);
	kfree(softc, M_DEVBUF);
}

static void
ptasync(void *callback_arg, u_int32_t code, struct cam_path *path, void *arg)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		cam_status status;
 
		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (SID_TYPE(&cgd->inq_data) != T_PROCESSOR)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(ptctor, ptoninvalidate, ptdtor,
					  ptstart, "pt", CAM_PERIPH_BIO,
					  cgd->ccb_h.path, ptasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			kprintf("ptasync: Unable to attach to new device "
				"due to status 0x%x\n", status);
		break;
	}
	case AC_SENT_BDR:
	case AC_BUS_RESET:
	{
		struct pt_softc *softc;
		struct ccb_hdr *ccbh;

		softc = (struct pt_softc *)periph->softc;
		/*
		 * Don't fail on the expected unit attention
		 * that will occur.
		 */
		softc->flags |= PT_FLAG_RETRY_UA;
		LIST_FOREACH(ccbh, &softc->pending_ccbs, periph_links.le)
			ccbh->ccb_state |= PT_CCB_RETRY_UA;
		/* FALLTHROUGH */
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static void
ptstart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct pt_softc *softc;
	struct buf *bp;
	struct bio *bio;

	softc = (struct pt_softc *)periph->softc;

	/*
	 * See if there is a buf with work for us to do..
	 */
	bio = bioq_first(&softc->bio_queue);
	if (periph->immediate_priority <= periph->pinfo.priority) {
		CAM_DEBUG_PRINT(CAM_DEBUG_SUBTRACE,
				("queuing for immediate ccb\n"));
		start_ccb->ccb_h.ccb_state = PT_CCB_WAITING;
		SLIST_INSERT_HEAD(&periph->ccb_list, &start_ccb->ccb_h,
				  periph_links.sle);
		periph->immediate_priority = CAM_PRIORITY_NONE;
		wakeup(&periph->ccb_list);
	} else if (bio == NULL) {
		xpt_release_ccb(start_ccb);
	} else {
		bioq_remove(&softc->bio_queue, bio);
		bp = bio->bio_buf;

		devstat_start_transaction(&softc->device_stats);

		scsi_send_receive(&start_ccb->csio,
				  /*retries*/4,
				  ptdone,
				  MSG_SIMPLE_Q_TAG,
				  (bp->b_cmd == BUF_CMD_READ),
				  /*byte2*/0,
				  bp->b_bcount,
				  bp->b_data,
				  /*sense_len*/SSD_FULL_SIZE,
				  /*timeout*/softc->io_timeout);

		start_ccb->ccb_h.ccb_state = PT_CCB_BUFFER_IO_UA;

		/*
		 * Block out any asyncronous callbacks
		 * while we touch the pending ccb list.
		 */
		LIST_INSERT_HEAD(&softc->pending_ccbs, &start_ccb->ccb_h,
				 periph_links.le);

		start_ccb->ccb_h.ccb_bio = bio;
		bio = bioq_first(&softc->bio_queue);

		xpt_action(start_ccb);
		
		if (bio != NULL) {
			/* Have more work to do, so ensure we stay scheduled */
			xpt_schedule(periph, /* XXX priority */1);
		}
	}
}

static void
ptdone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct pt_softc *softc;
	struct ccb_scsiio *csio;

	softc = (struct pt_softc *)periph->softc;
	csio = &done_ccb->csio;
	switch (csio->ccb_h.ccb_state) {
	case PT_CCB_BUFFER_IO:
	case PT_CCB_BUFFER_IO_UA:
	{
		struct buf *bp;
		struct bio *bio;

		bio = (struct bio *)done_ccb->ccb_h.ccb_bio;
		bp = bio->bio_buf;

		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			int error;
			int sf;
			
			if ((csio->ccb_h.ccb_state & PT_CCB_RETRY_UA) != 0)
				sf = SF_RETRY_UA;
			else
				sf = 0;

			error = pterror(done_ccb, CAM_RETRY_SELTO, sf);
			if (error == ERESTART) {
				/*
				 * A retry was scheuled, so
				 * just return.
				 */
				return;
			}
			if (error != 0) {
				struct buf *q_bp;
				struct bio *q_bio;

				if (error == ENXIO) {
					/*
					 * Catastrophic error.  Mark our device
					 * as invalid.
					 */
					xpt_print(periph->path,
					    "Invalidating device\n");
					softc->flags |= PT_FLAG_DEVICE_INVALID;
				}

				/*
				 * return all queued I/O with EIO, so that
				 * the client can retry these I/Os in the
				 * proper order should it attempt to recover.
				 */
				while ((q_bio = bioq_takefirst(&softc->bio_queue)) != NULL) {
					q_bp = q_bio->bio_buf;
					q_bp->b_resid = q_bp->b_bcount;
					q_bp->b_error = EIO;
					q_bp->b_flags |= B_ERROR;
					biodone(q_bio);
				}
				bp->b_error = error;
				bp->b_resid = bp->b_bcount;
				bp->b_flags |= B_ERROR;
			} else {
				bp->b_resid = csio->resid;
				bp->b_error = 0;
				if (bp->b_resid != 0) {
					/* Short transfer ??? */
					bp->b_flags |= B_ERROR;
				}
			}
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
		} else {
			bp->b_resid = csio->resid;
			if (bp->b_resid != 0)
				bp->b_flags |= B_ERROR;
		}

		/*
		 * Block out any asyncronous callbacks
		 * while we touch the pending ccb list.
		 */
		LIST_REMOVE(&done_ccb->ccb_h, periph_links.le);

		devstat_end_transaction_buf(&softc->device_stats, bp);
		biodone(bio);
		break;
	}
	case PT_CCB_WAITING:
		/* Caller will release the CCB */
		wakeup(&done_ccb->ccb_h.cbfcnp);
		return;
	}
	xpt_release_ccb(done_ccb);
}

static int
pterror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct pt_softc	  *softc;
	struct cam_periph *periph;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct pt_softc *)periph->softc;

	return(cam_periph_error(ccb, cam_flags, sense_flags,
				&softc->saved_ccb));
}

static int
ptioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t addr = ap->a_data;
	struct cam_periph *periph;
	struct pt_softc *softc;
	int unit;
	int error = 0;

	unit = minor(dev);
	periph = cam_extend_get(ptperiphs, unit);

	if (periph == NULL)
		return(ENXIO);

	softc = (struct pt_softc *)periph->softc;

	cam_periph_lock(periph);

	switch(ap->a_cmd) {
	case PTIOCGETTIMEOUT:
		if (softc->io_timeout >= 1000)
			*(int *)addr = softc->io_timeout / 1000;
		else
			*(int *)addr = 0;
		break;
	case PTIOCSETTIMEOUT:
		if (*(int *)addr < 1) {
			error = EINVAL;
			break;
		}

		softc->io_timeout = *(int *)addr * 1000;

		break;
	default:
		error = cam_periph_ioctl(periph, ap->a_cmd, addr, pterror);
		break;
	}

	cam_periph_unlock(periph);

	return(error);
}

void
scsi_send_receive(struct ccb_scsiio *csio, u_int32_t retries,
		  void (*cbfcnp)(struct cam_periph *, union ccb *),
		  u_int tag_action, int readop, u_int byte2,
		  u_int32_t xfer_len, u_int8_t *data_ptr, u_int8_t sense_len,
		  u_int32_t timeout)
{
	struct scsi_send_receive *scsi_cmd;

	scsi_cmd = (struct scsi_send_receive *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = readop ? RECEIVE : SEND;
	scsi_cmd->byte2 = byte2;
	scsi_ulto3b(xfer_len, scsi_cmd->xfer_len);
	scsi_cmd->control = 0;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/readop ? CAM_DIR_IN : CAM_DIR_OUT,
		      tag_action,
		      data_ptr,
		      xfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}
