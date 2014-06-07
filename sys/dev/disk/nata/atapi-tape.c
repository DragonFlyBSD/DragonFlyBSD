/*-
 * Copyright (c) 1998 - 2006 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ata/atapi-tape.c,v 1.101 2006/01/05 21:27:19 sos Exp $
 */

#include "opt_ata.h"

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mtio.h>
#include <sys/nata.h>
#include <sys/systm.h>

#include "ata-all.h"
#include "atapi-tape.h"
#include "ata_if.h"

/* device structure */
static  d_open_t        ast_open;
static  d_close_t       ast_close;
static  d_ioctl_t       ast_ioctl;
static  d_strategy_t    ast_strategy;
static struct dev_ops ast_ops = {
	{ "ast", 119, D_TAPE | D_TRACKCLOSE },
	.d_open =	ast_open,
	.d_close =	ast_close,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	ast_ioctl,
	.d_strategy =	ast_strategy
};

/* prototypes */
static int ast_sense(device_t);
static void ast_describe(device_t);
static void ast_done(struct ata_request *);
static int ast_mode_sense(device_t, int, void *, int); 
static int ast_mode_select(device_t, void *, int);
static int ast_write_filemark(device_t, u_int8_t);
static int ast_read_position(device_t, int, struct ast_readposition *);
static int ast_space(device_t, u_int8_t, int32_t);
static int ast_locate(device_t, int, u_int32_t);
static int ast_prevent_allow(device_t, int);
static int ast_load_unload(device_t, u_int8_t);
static int ast_rewind(device_t);
static int ast_erase(device_t);
static int ast_test_ready(device_t);
static int ast_wait_dsc(device_t, int);

/* internal vars */
static u_int64_t ast_total = 0;
static MALLOC_DEFINE(M_AST, "ast_driver", "ATAPI tape driver buffers");

static int
ast_probe(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);

    if ((atadev->param.config & ATA_PROTO_ATAPI) &&
	(atadev->param.config & ATA_ATAPI_TYPE_MASK) == ATA_ATAPI_TYPE_TAPE)
	return 0;
    else
	return ENXIO;
}

static int
ast_attach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct ast_softc *stp;
    struct ast_readposition position;
    cdev_t cdev;

    stp = kmalloc(sizeof(struct ast_softc), M_AST, M_WAITOK | M_ZERO);
    device_set_ivars(dev, stp);
    ATA_SETMODE(device_get_parent(dev), dev);

    if (ast_sense(dev)) {
	device_set_ivars(dev, NULL);
	kfree(stp, M_AST);
	return ENXIO;
    }
    if (!strcmp(atadev->param.model, "OnStream DI-30")) {
	struct ast_transferpage transfer;
	struct ast_identifypage identify;

	stp->flags |= F_ONSTREAM;
	bzero(&transfer, sizeof(struct ast_transferpage));
	ast_mode_sense(dev, ATAPI_TAPE_TRANSFER_PAGE,
		       &transfer, sizeof(transfer));
	bzero(&identify, sizeof(struct ast_identifypage));
	ast_mode_sense(dev, ATAPI_TAPE_IDENTIFY_PAGE,
		       &identify, sizeof(identify));
	strncpy(identify.ident, "FBSD", 4);
	ast_mode_select(dev, &identify, sizeof(identify));
	ast_read_position(dev, 0, &position);
    }

    devstat_add_entry(&stp->stats, "ast", device_get_unit(dev), DEV_BSIZE,
		      DEVSTAT_NO_ORDERED_TAGS,
		      DEVSTAT_TYPE_SEQUENTIAL | DEVSTAT_TYPE_IF_IDE,
		      DEVSTAT_PRIORITY_TAPE);
    cdev = make_dev(&ast_ops, 2 * device_get_unit(dev), UID_ROOT, GID_OPERATOR,
		    0640, "ast%d", device_get_unit(dev));
    reference_dev(cdev);
    cdev->si_drv1 = dev;
    if (ch->dma)
	cdev->si_iosize_max = ch->dma->max_iosize;
    else
	cdev->si_iosize_max = min(MAXPHYS,64*1024);
    stp->cdev1 = cdev;
    cdev = make_dev(&ast_ops, 2 * device_get_unit(dev) + 1, UID_ROOT,
		    GID_OPERATOR, 0640, "nast%d", device_get_unit(dev));
    reference_dev(cdev);
    cdev->si_drv1 = dev;
    if (ch->dma)
	cdev->si_iosize_max = ch->dma->max_iosize;
    else
	cdev->si_iosize_max = min(MAXPHYS,64*1024);
    stp->cdev2 = cdev;

    /* announce we are here and ready */
    ast_describe(dev);
    return 0;
}

static int      
ast_detach(device_t dev)
{   
    struct ast_softc *stp = device_get_ivars(dev);
    
    /* detroy devices from the system so we dont get any further requests */
    destroy_dev(stp->cdev1);
    destroy_dev(stp->cdev2);

    /* fail requests on the queue and any thats "in flight" for this device */
    ata_fail_requests(dev);

    /* dont leave anything behind */
    dev_ops_remove_minor(&ast_ops, /*dkunitmask(), */dkmakeunit(device_get_unit(dev)));
    devstat_remove_entry(&stp->stats);
    device_set_ivars(dev, NULL);
    kfree(stp, M_AST);
    return 0;
}

static void
ast_shutdown(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);

    if (atadev->param.support.command2 & ATA_SUPPORT_FLUSHCACHE)
	ata_controlcmd(dev, ATA_FLUSHCACHE, 0, 0, 0);
}

static int
ast_reinit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct ast_softc *stp = device_get_ivars(dev);

    if (((atadev->unit == ATA_MASTER) && !(ch->devices & ATA_ATAPI_MASTER)) ||
	((atadev->unit == ATA_SLAVE) && !(ch->devices & ATA_ATAPI_SLAVE))) {
	device_set_ivars(dev, NULL);
	kfree(stp, M_AST);
	return 1;
    }
    ATA_SETMODE(device_get_parent(dev), dev);
    return 0;
}

static int
ast_open(struct dev_open_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct ata_device *atadev = device_get_softc(dev);
    struct ast_softc *stp = device_get_ivars(dev);

    if (!stp)
	return ENXIO;
    if (!device_is_attached(dev))
	return EBUSY;

    ast_test_ready(dev);
    if (stp->cap.lock)
	ast_prevent_allow(dev, 1);
    if (ast_sense(dev))
	device_printf(dev, "sense media type failed\n");

    atadev->flags &= ~ATA_D_MEDIA_CHANGED;
    stp->flags &= ~(F_DATA_WRITTEN | F_FM_WRITTEN);
    ast_total = 0;
    return 0;
}

static int 
ast_close(struct dev_close_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct ast_softc *stp = device_get_ivars(dev);
    cdev_t cdev = ap->a_head.a_dev;

    /* flush buffers, some drives fail here, they should report ctl = 0 */
    if (stp->cap.ctl && (stp->flags & F_DATA_WRITTEN))
	ast_write_filemark(dev, 0);

    /* write filemark if data written to tape */
    if (!(stp->flags & F_ONSTREAM) &&
	(stp->flags & (F_DATA_WRITTEN | F_FM_WRITTEN)) == F_DATA_WRITTEN)
	ast_write_filemark(dev, ATAPI_WF_WRITE);

    /* if minor is even rewind on close */
    if (!(minor(cdev) & 0x01))
	ast_rewind(dev);

    if (stp->cap.lock && count_dev(cdev) == 1)
	ast_prevent_allow(dev, 0);

    stp->flags &= ~F_CTL_WARN;
#ifdef AST_DEBUG
    device_printf(dev, "%llu total bytes transferred\n",
		  (unsigned long long)ast_total);
#endif
    return 0;
}

static int 
ast_ioctl(struct dev_ioctl_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct ast_softc *stp = device_get_ivars(dev);
    int error = 0;

    switch (ap->a_cmd) {
    case MTIOCGET:
	{
	    struct mtget *g = (struct mtget *)ap->a_data;

	    bzero(g, sizeof(struct mtget));
	    g->mt_type = 7;
	    g->mt_density = 1;
	    g->mt_blksiz = stp->blksize;
	    g->mt_comp = stp->cap.compress;
	    g->mt_density0 = 0; g->mt_density1 = 0;
	    g->mt_density2 = 0; g->mt_density3 = 0;
	    g->mt_blksiz0 = 0; g->mt_blksiz1 = 0;
	    g->mt_blksiz2 = 0; g->mt_blksiz3 = 0;
	    g->mt_comp0 = 0; g->mt_comp1 = 0;
	    g->mt_comp2 = 0; g->mt_comp3 = 0;
	}
	break;   

    case MTIOCTOP:
	{       
	    int i;
	    struct mtop *mt = (struct mtop *)ap->a_data;

	    switch ((int16_t) (mt->mt_op)) {

	    case MTWEOF:
		for (i=0; i < mt->mt_count && !error; i++)
		    error = ast_write_filemark(dev, ATAPI_WF_WRITE);
		break;

	    case MTFSF:
		if (mt->mt_count)
		    error = ast_space(dev, ATAPI_SP_FM, mt->mt_count);
		break;

	    case MTBSF:
		if (mt->mt_count)
		    error = ast_space(dev, ATAPI_SP_FM, -(mt->mt_count));
		break;

	    case MTREW:
		error = ast_rewind(dev);
		break;

	    case MTOFFL:
		error = ast_load_unload(dev, ATAPI_SS_EJECT);
		break;

	    case MTNOP:
		error = ast_write_filemark(dev, 0);
		break;

	    case MTERASE:
		error = ast_erase(dev);
		break;

	    case MTEOD:
		error = ast_space(dev, ATAPI_SP_EOD, 0);
		break;

	    case MTRETENS:
		error = ast_load_unload(dev, ATAPI_SS_RETENSION|ATAPI_SS_LOAD);
		break;

	    case MTFSR:         
	    case MTBSR:
	    case MTCACHE:
	    case MTNOCACHE:
	    case MTSETBSIZ:
	    case MTSETDNSTY:
	    case MTCOMP:
	    default:
		error = EINVAL;
	    }
	}
	break;

    case MTIOCRDSPOS:
	{
	    struct ast_readposition position;

	    if ((error = ast_read_position(dev, 0, &position)))
		break;
	    *(u_int32_t *)ap->a_data = position.tape;
	}
	break;

    case MTIOCRDHPOS:
	{
	    struct ast_readposition position;

	    if ((error = ast_read_position(dev, 1, &position)))
		break;
	    *(u_int32_t *)ap->a_data = position.tape;
	}
	break;

    case MTIOCSLOCATE:
	error = ast_locate(dev, 0, *(u_int32_t *)ap->a_data);
	break;

    case MTIOCHLOCATE:
	error = ast_locate(dev, 1, *(u_int32_t *)ap->a_data);
	break;

    default:
	error = ata_device_ioctl(dev, ap->a_cmd, ap->a_data);
    }
    return error;
}

static int
ast_strategy(struct dev_strategy_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct bio *bp = ap->a_bio;
    struct buf *bbp = bp->bio_buf;
    struct ata_device *atadev = device_get_softc(dev);
    struct ast_softc *stp = device_get_ivars(dev);
    struct ata_request *request;
    u_int32_t blkcount;
    int8_t ccb[16];

    /* if it's a null transfer, return immediatly. */
    if (bbp->b_bcount == 0) {
	bbp->b_resid = 0;
	biodone(bp);
	return 0;
    }
    if (!(bbp->b_cmd == BUF_CMD_READ) && (stp->flags & F_WRITEPROTECT)) {
	bbp->b_flags |= B_ERROR;
	bbp->b_error = EPERM;
	biodone(bp);
	return 0;
    }
    if (bbp->b_cmd != BUF_CMD_READ && bbp->b_cmd != BUF_CMD_WRITE) {
	bbp->b_flags |= B_ERROR;
	bbp->b_error = EIO;
	biodone(bp);
	return 0;
    }
	
    /* check for != blocksize requests */
    if (bbp->b_bcount % stp->blksize) {
	device_printf(dev, "transfers must be multiple of %d\n", stp->blksize);
	bbp->b_flags |= B_ERROR;
	bbp->b_error = EIO;
	biodone(bp);
	return 0;
    }

    /* warn about transfers bigger than the device suggests */
    if (bbp->b_bcount > stp->blksize * stp->cap.ctl) {  
	if ((stp->flags & F_CTL_WARN) == 0) {
	    device_printf(dev, "WARNING: CTL exceeded %d>%d\n",
			  bbp->b_bcount, stp->blksize * stp->cap.ctl);
	    stp->flags |= F_CTL_WARN;
	}
    }

    bzero(ccb, sizeof(ccb));

    if (bbp->b_cmd == BUF_CMD_READ)
	ccb[0] = ATAPI_READ;
    else
	ccb[0] = ATAPI_WRITE;
    
    blkcount = bbp->b_bcount / stp->blksize;

    ccb[1] = 1;
    ccb[2] = blkcount >> 16;
    ccb[3] = blkcount >> 8;
    ccb[4] = blkcount;

    if (!(request = ata_alloc_request())) {
	bbp->b_flags |= B_ERROR;
	bbp->b_error = ENOMEM;
	biodone(bp);
	return 0;
    }
    request->dev = dev;
    request->bio = bp;
    bcopy(ccb, request->u.atapi.ccb,
	  (atadev->param.config & ATA_PROTO_MASK) == 
	  ATA_PROTO_ATAPI_12 ? 16 : 12);
    request->data = bbp->b_data;
    request->bytecount = blkcount * stp->blksize;
    request->transfersize = min(request->bytecount, 65534);
    request->timeout = (ccb[0] == ATAPI_WRITE_BIG) ? 180 : 120;
    request->retries = 2;
    request->callback = ast_done;

    switch (bbp->b_cmd) {
    case BUF_CMD_READ:
	request->flags |= (ATA_R_ATAPI | ATA_R_READ);
	break;
    case BUF_CMD_WRITE:
	request->flags |= (ATA_R_ATAPI | ATA_R_WRITE);
	break;
    default:
	panic("bbp->b_cmd");
    }
    devstat_start_transaction(&stp->stats);
    ata_queue_request(request);
    return 0;
}

static void 
ast_done(struct ata_request *request)
{
    struct ast_softc *stp = device_get_ivars(request->dev);
    struct bio *bp = request->bio;
    struct buf *bbp = bp->bio_buf;

    /* finish up transfer */
    if ((bbp->b_error = request->result))
	bbp->b_flags |= B_ERROR;
    if (bbp->b_cmd == BUF_CMD_WRITE)
	stp->flags |= F_DATA_WRITTEN;
    bbp->b_resid = bbp->b_bcount - request->donecount;
    ast_total += (bbp->b_bcount - bbp->b_resid);
    devstat_end_transaction_buf(&stp->stats, bbp);
    biodone(bp);
    ata_free_request(request);
}

static int
ast_sense(device_t dev)
{
    struct ast_softc *stp = device_get_ivars(dev);
    int count;

    /* get drive capabilities, some bugridden drives needs this repeated */
    for (count = 0 ; count < 5 ; count++) {
	if (!ast_mode_sense(dev, ATAPI_TAPE_CAP_PAGE,
			    &stp->cap, sizeof(stp->cap)) &&
	    stp->cap.page_code == ATAPI_TAPE_CAP_PAGE) {
	    if (stp->cap.blk32k)
		stp->blksize = 32768;
	    if (stp->cap.blk1024)
		stp->blksize = 1024;
	    if (stp->cap.blk512)
		stp->blksize = 512;
	    if (!stp->blksize)
		continue;
	    stp->cap.max_speed = ntohs(stp->cap.max_speed);
	    stp->cap.max_defects = ntohs(stp->cap.max_defects);
	    stp->cap.ctl = ntohs(stp->cap.ctl);
	    stp->cap.speed = ntohs(stp->cap.speed);
	    stp->cap.buffer_size = ntohs(stp->cap.buffer_size);
	    return 0;
	}
    }
    return 1;
}

static int
ast_mode_sense(device_t dev, int page, void *pagebuf, int pagesize)
{
    int8_t ccb[16] = { ATAPI_MODE_SENSE, 0x08, page, pagesize>>8, pagesize,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int error;
 
    error = ata_atapicmd(dev, ccb, pagebuf, pagesize, ATA_R_READ, 10);
    return error;
}

static int       
ast_mode_select(device_t dev, void *pagebuf, int pagesize)
{
    int8_t ccb[16] = { ATAPI_MODE_SELECT, 0x10, 0, pagesize>>8, pagesize,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
     
    return ata_atapicmd(dev, ccb, pagebuf, pagesize, 0, 10);
}

static int
ast_write_filemark(device_t dev, u_int8_t function)
{
    struct ast_softc *stp = device_get_ivars(dev);
    int8_t ccb[16] = { ATAPI_WEOF, 0x01, 0, 0, function, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    if (stp->flags & F_ONSTREAM)
	ccb[4] = 0x00;          /* only flush buffers supported */
    else {
	if (function) {
	    if (stp->flags & F_FM_WRITTEN)
		stp->flags &= ~F_DATA_WRITTEN;
	    else
		stp->flags |= F_FM_WRITTEN;
	}
    }
    error = ata_atapicmd(dev, ccb, NULL, 0, 0, 10);
    if (error)
	return error;
    return ast_wait_dsc(dev, 10*60);
}

static int
ast_read_position(device_t dev, int hard, struct ast_readposition *position)
{
    int8_t ccb[16] = { ATAPI_READ_POSITION, (hard ? 0x01 : 0), 0, 0, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    error = ata_atapicmd(dev, ccb, (caddr_t)position, 
			 sizeof(struct ast_readposition), ATA_R_READ, 10);
    position->tape = ntohl(position->tape);
    position->host = ntohl(position->host);
    return error;
}

static int
ast_space(device_t dev, u_int8_t function, int32_t count)
{
    int8_t ccb[16] = { ATAPI_SPACE, function, count>>16, count>>8, count,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return ata_atapicmd(dev, ccb, NULL, 0, 0, 60*60);
}

static int
ast_locate(device_t dev, int hard, u_int32_t pos)
{
    int8_t ccb[16] = { ATAPI_LOCATE, 0x01 | (hard ? 0x4 : 0), 0,
		       pos>>24, pos>>16, pos>>8, pos,
		       0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    error = ata_atapicmd(dev, ccb, NULL, 0, 0, 10);
    if (error)
	return error;
    return ast_wait_dsc(dev, 60*60);
}

static int
ast_prevent_allow(device_t dev, int lock)
{
    int8_t ccb[16] = { ATAPI_PREVENT_ALLOW, 0, 0, 0, lock,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return ata_atapicmd(dev, ccb, NULL, 0, 0, 30);
}

static int
ast_load_unload(device_t dev, u_int8_t function)
{
    struct ast_softc *stp = device_get_ivars(dev);
    int8_t ccb[16] = { ATAPI_START_STOP, 0x01, 0, 0, function, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    if ((function & ATAPI_SS_EJECT) && !stp->cap.eject)
	return 0;
    error = ata_atapicmd(dev, ccb, NULL, 0, 0, 10);
    if (error)
	return error;
    tsleep((caddr_t)&error, 0, "astlu", 1 * hz);
    if (function == ATAPI_SS_EJECT)
	return 0;
    return ast_wait_dsc(dev, 60*60);
}

static int
ast_rewind(device_t dev)
{
    int8_t ccb[16] = { ATAPI_REZERO, 0x01, 0, 0, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    error = ata_atapicmd(dev, ccb, NULL, 0, 0, 10);
    if (error)
	return error;
    return ast_wait_dsc(dev, 60*60);
}

static int
ast_erase(device_t dev)
{
    int8_t ccb[16] = { ATAPI_ERASE, 3, 0, 0, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    if ((error = ast_rewind(dev)))
	return error;

    return ata_atapicmd(dev, ccb, NULL, 0, 0, 60*60);
}

static int
ast_test_ready(device_t dev)
{
    int8_t ccb[16] = { ATAPI_TEST_UNIT_READY, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return ata_atapicmd(dev, ccb, NULL, 0, 0, 30);
}

static int
ast_wait_dsc(device_t dev, int timeout)
{
    int error = 0;
    int8_t ccb[16] = { ATAPI_POLL_DSC, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    timeout *= hz;
    while (timeout > 0) {
	error = ata_atapicmd(dev, ccb, NULL, 0, 0, 0);
	if (error != EBUSY)
	    break;
	tsleep(&error, 0, "atpwt", hz / 2);
	timeout -= (hz / 2);
    }
    return error;
}

static void 
ast_describe(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct ast_softc *stp = device_get_ivars(dev);

    if (bootverbose) {
	device_printf(dev, "<%.40s/%.8s> tape drive at ata%d as %s\n",
		      atadev->param.model, atadev->param.revision,
		      device_get_unit(ch->dev),
		      (atadev->unit == ATA_MASTER) ? "master" : "slave");
	device_printf(dev, "%dKB/s, ", stp->cap.max_speed);
	kprintf("transfer limit %d blk%s, ",
	       stp->cap.ctl, (stp->cap.ctl > 1) ? "s" : "");
	kprintf("%dKB buffer, ", (stp->cap.buffer_size * DEV_BSIZE) / 1024);
	kprintf("%s\n", ata_mode2str(atadev->mode));
	device_printf(dev, "Medium: ");
	switch (stp->cap.medium_type) {
	    case 0x00:
		kprintf("none"); break;
	    case 0x17:
		kprintf("Travan 1 (400 Mbyte)"); break;
	    case 0xb6:
		kprintf("Travan 4 (4 Gbyte)"); break;
	    case 0xda:
		kprintf("OnStream ADR (15Gyte)"); break;
	    default:
		kprintf("unknown (0x%x)", stp->cap.medium_type);
	}
	if (stp->cap.readonly) kprintf(", readonly");
	if (stp->cap.reverse) kprintf(", reverse");
	if (stp->cap.eformat) kprintf(", eformat");
	if (stp->cap.qfa) kprintf(", qfa");
	if (stp->cap.lock) kprintf(", lock");
	if (stp->cap.locked) kprintf(", locked");
	if (stp->cap.prevent) kprintf(", prevent");
	if (stp->cap.eject) kprintf(", eject");
	if (stp->cap.disconnect) kprintf(", disconnect");
	if (stp->cap.ecc) kprintf(", ecc");
	if (stp->cap.compress) kprintf(", compress");
	if (stp->cap.blk512) kprintf(", 512b");
	if (stp->cap.blk1024) kprintf(", 1024b");
	if (stp->cap.blk32k) kprintf(", 32kb");
	kprintf("\n");
    }
    else {
	device_printf(dev, "TAPE <%.40s/%.8s> at ata%d-%s %s\n",
		      atadev->param.model, atadev->param.revision,
		      device_get_unit(ch->dev),
		      (atadev->unit == ATA_MASTER) ? "master" : "slave",
		      ata_mode2str(atadev->mode));
    }
}

static device_method_t ast_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,     ast_probe),
    DEVMETHOD(device_attach,    ast_attach),
    DEVMETHOD(device_detach,    ast_detach),
    DEVMETHOD(device_shutdown,  ast_shutdown),
			   
    /* ATA methods */
    DEVMETHOD(ata_reinit,       ast_reinit),

    DEVMETHOD_END
};
	    
static driver_t ast_driver = {
    "ast",
    ast_methods,
    0,
};

static devclass_t ast_devclass;

DRIVER_MODULE(ast, ata, ast_driver, ast_devclass, NULL, NULL);
MODULE_VERSION(ast, 1);
MODULE_DEPEND(ast, ata, 1, 1, 1);
