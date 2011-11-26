/*-
 * Copyright (c) 1998,1999,2000,2001,2002 Søren Schmidt <sos@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/dev/ata/atapi-tape.c,v 1.36.2.12 2002/07/31 11:19:26 sos Exp $
 * $DragonFly: src/sys/dev/disk/ata/atapi-tape.c,v 1.22 2008/01/06 16:55:49 swildner Exp $
 */

#include "opt_ata.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ata.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/mtio.h>
#include <sys/diskslice.h>
#include <sys/devicestat.h>
#include <sys/proc.h>
#include <sys/buf2.h>
#include <sys/thread2.h>

#include "ata-all.h"
#include "atapi-all.h"
#include "atapi-tape.h"

/* device structures */
static	d_open_t	astopen;
static	d_close_t	astclose;
static	d_ioctl_t	astioctl;
static	d_strategy_t	aststrategy;

static struct dev_ops ast_ops = {
	{ "ast", 119, D_TAPE | D_TRACKCLOSE },
	.d_open =	astopen,
	.d_close =	astclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	astioctl,
	.d_strategy =	aststrategy
};

/* prototypes */
static int ast_sense(struct ast_softc *);
static void ast_describe(struct ast_softc *);
static int ast_done(struct atapi_request *);
static int ast_mode_sense(struct ast_softc *, int, void *, int); 
static int ast_mode_select(struct ast_softc *, void *, int);
static int ast_write_filemark(struct ast_softc *, u_int8_t);
static int ast_read_position(struct ast_softc *, int, struct ast_readposition *);
static int ast_space(struct ast_softc *, u_int8_t, int32_t);
static int ast_locate(struct ast_softc *, int, u_int32_t);
static int ast_prevent_allow(struct ast_softc *stp, int);
static int ast_load_unload(struct ast_softc *, u_int8_t);
static int ast_rewind(struct ast_softc *);
static int ast_erase(struct ast_softc *);

/* internal vars */
static u_int32_t ast_lun_map = 0;
static u_int64_t ast_total = 0;
static MALLOC_DEFINE(M_AST, "AST driver", "ATAPI tape driver buffers");

int 
astattach(struct ata_device *atadev)
{
    struct ast_softc *stp;
    struct ast_readposition position;
    cdev_t dev;

    stp = kmalloc(sizeof(struct ast_softc), M_AST, M_WAITOK | M_ZERO);

    stp->device = atadev;
    stp->lun = ata_get_lun(&ast_lun_map);
    ata_set_name(atadev, "ast", stp->lun);
    bioq_init(&stp->bio_queue);

    if (ast_sense(stp)) {
	kfree(stp, M_AST);
	return 0;
    }

    if (!strcmp(atadev->param->model, "OnStream DI-30")) {
	struct ast_transferpage transfer;
	struct ast_identifypage identify;

	stp->flags |= F_ONSTREAM;
	bzero(&transfer, sizeof(struct ast_transferpage));
	ast_mode_sense(stp, ATAPI_TAPE_TRANSFER_PAGE,
		       &transfer, sizeof(transfer));
	bzero(&identify, sizeof(struct ast_identifypage));
	ast_mode_sense(stp, ATAPI_TAPE_IDENTIFY_PAGE,
		       &identify, sizeof(identify));
	strncpy(identify.ident, "FBSD", 4);
	ast_mode_select(stp, &identify, sizeof(identify));
	ast_read_position(stp, 0, &position);
    }

    devstat_add_entry(&stp->stats, "ast", stp->lun, DEV_BSIZE,
		      DEVSTAT_NO_ORDERED_TAGS,
		      DEVSTAT_TYPE_SEQUENTIAL | DEVSTAT_TYPE_IF_IDE,
		      DEVSTAT_PRIORITY_TAPE);
    dev = make_dev(&ast_ops, dkmakeminor(stp->lun, 0, 0),
		   UID_ROOT, GID_OPERATOR, 0640, "ast%d", stp->lun);
    dev->si_drv1 = stp;
    dev->si_iosize_max = 256 * DEV_BSIZE;
    dev = make_dev(&ast_ops, dkmakeminor(stp->lun, 0, 1),
		   UID_ROOT, GID_OPERATOR, 0640, "nast%d", stp->lun);
    dev->si_drv1 = stp;
    dev->si_iosize_max = 256 * DEV_BSIZE;
    stp->device->flags |= ATA_D_MEDIA_CHANGED;
    ast_describe(stp);
    atadev->driver = stp;
    return 1;
}

void	
astdetach(struct ata_device *atadev)
{   
    struct ast_softc *stp = atadev->driver;
    struct buf *bp;
    struct bio *bio;
    
    while ((bio = bioq_first(&stp->bio_queue))) {
	bioq_remove(&stp->bio_queue, bio);
	bp = bio->bio_buf;
	bp->b_flags |= B_ERROR;
	bp->b_error = ENXIO;
	biodone(bio);
    }
    devstat_remove_entry(&stp->stats);
    dev_ops_remove_minor(&ast_ops,/* dkunitmask(), */dkmakeunit(stp->lun));
    ata_free_name(atadev);
    ata_free_lun(&ast_lun_map, stp->lun);
    kfree(stp, M_AST);
    atadev->driver = NULL;
}

static int
ast_sense(struct ast_softc *stp)
{
    int count, error = 0;

    /* get drive capabilities, some drives needs this repeated */
    for (count = 0 ; count < 5 ; count++) {
	if (!(error = ast_mode_sense(stp, ATAPI_TAPE_CAP_PAGE,
				     &stp->cap, sizeof(stp->cap)))) {
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

static void 
ast_describe(struct ast_softc *stp)
{
    if (bootverbose) {
	ata_prtdev(stp->device, "<%.40s/%.8s> tape drive at ata%d as %s\n",
		   stp->device->param->model, stp->device->param->revision,
		   device_get_unit(stp->device->channel->dev),
		   (stp->device->unit == ATA_MASTER) ? "master" : "slave");
	ata_prtdev(stp->device, "%dKB/s, ", stp->cap.max_speed);
	kprintf("transfer limit %d blk%s, ",
	       stp->cap.ctl, (stp->cap.ctl > 1) ? "s" : "");
	kprintf("%dKB buffer, ", (stp->cap.buffer_size * DEV_BSIZE) / 1024);
	kprintf("%s\n", ata_mode2str(stp->device->mode));
	ata_prtdev(stp->device, "Medium: ");
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
	ata_prtdev(stp->device, "TAPE <%.40s> at ata%d-%s %s\n",
		   stp->device->param->model,
		   device_get_unit(stp->device->channel->dev),
		   (stp->device->unit == ATA_MASTER) ? "master" : "slave",
		   ata_mode2str(stp->device->mode));
    }
}

static int
astopen(struct dev_open_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct ast_softc *stp = dev->si_drv1;

    if (!stp)
	return ENXIO;

    if (count_dev(dev) > 1)
	return EBUSY;

    atapi_test_ready(stp->device);

    if (stp->cap.lock)
	ast_prevent_allow(stp, 1);

    if (ast_sense(stp))
	ata_prtdev(stp->device, "sense media type failed\n");

    stp->device->flags &= ~ATA_D_MEDIA_CHANGED;
    stp->flags &= ~(F_DATA_WRITTEN | F_FM_WRITTEN);
    ast_total = 0;
    return 0;
}

static int 
astclose(struct dev_close_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct ast_softc *stp = dev->si_drv1;

    /* flush buffers, some drives fail here, they should report ctl = 0 */
    if (stp->cap.ctl && (stp->flags & F_DATA_WRITTEN))
	ast_write_filemark(stp, 0);

    /* write filemark if data written to tape */
    if (!(stp->flags & F_ONSTREAM) &&
	(stp->flags & (F_DATA_WRITTEN | F_FM_WRITTEN)) == F_DATA_WRITTEN)
	ast_write_filemark(stp, WF_WRITE);

    /* if minor is even rewind on close */
    if (!(minor(dev) & 0x01))
	ast_rewind(stp);

    if (stp->cap.lock && count_dev(dev) == 1)
	ast_prevent_allow(stp, 0);

    stp->flags &= F_CTL_WARN;
#ifdef AST_DEBUG
    ata_prtdev(stp->device, "%llu total bytes transferred\n", ast_total);
#endif
    return 0;
}

static int 
astioctl(struct dev_ioctl_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct ast_softc *stp = dev->si_drv1;
    int error = 0;

    switch (ap->a_cmd) {
    case MTIOCGET:
	{
	    struct mtget *g = (struct mtget *) ap->a_data;

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
	    break;	 
	}
    case MTIOCTOP:
	{	
	    int i;
	    struct mtop *mt = (struct mtop *)ap->a_data;

	    switch ((int16_t) (mt->mt_op)) {

	    case MTWEOF:
		for (i=0; i < mt->mt_count && !error; i++)
		    error = ast_write_filemark(stp, WF_WRITE);
		break;

	    case MTFSF:
		if (mt->mt_count)
		    error = ast_space(stp, SP_FM, mt->mt_count);
		break;

	    case MTBSF:
		if (mt->mt_count)
		    error = ast_space(stp, SP_FM, -(mt->mt_count));
		break;

	    case MTREW:
		error = ast_rewind(stp);
		break;

	    case MTOFFL:
		error = ast_load_unload(stp, SS_EJECT);
		break;

	    case MTNOP:
		error = ast_write_filemark(stp, 0);
		break;

	    case MTERASE:
		error = ast_erase(stp);
		break;

	    case MTEOD:
		error = ast_space(stp, SP_EOD, 0);
		break;

	    case MTRETENS:
		error = ast_load_unload(stp, SS_RETENSION | SS_LOAD);
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
	    break;
	}
    case MTIOCRDSPOS:
	{
	    struct ast_readposition position;

	    if ((error = ast_read_position(stp, 0, &position)))
		break;
	    *(u_int32_t *)ap->a_data = position.tape;
	    break;
	}
    case MTIOCRDHPOS:
	{
	    struct ast_readposition position;

	    if ((error = ast_read_position(stp, 1, &position)))
		break;
	    *(u_int32_t *)ap->a_data = position.tape;
	    break;
	}
    case MTIOCSLOCATE:
	error = ast_locate(stp, 0, *(u_int32_t *)ap->a_data);
	break;
    case MTIOCHLOCATE:
	error = ast_locate(stp, 1, *(u_int32_t *)ap->a_data);
	break;
    default:
	error = ENOTTY;
    }
    return error;
}

static int 
aststrategy(struct dev_strategy_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct bio *bio = ap->a_bio;
    struct buf *bp = bio->bio_buf;
    struct ast_softc *stp = dev->si_drv1;

    if (stp->device->flags & ATA_D_DETACHING) {
	bp->b_flags |= B_ERROR;
	bp->b_error = ENXIO;
	biodone(bio);
	return(0);
    }

    /* if it's a null transfer, return immediatly. */
    if (bp->b_bcount == 0) {
	bp->b_resid = 0;
	biodone(bio);
	return(0);
    }
    if (bp->b_cmd != BUF_CMD_READ && (stp->flags & F_WRITEPROTECT)) {
	bp->b_flags |= B_ERROR;
	bp->b_error = EPERM;
	biodone(bio);
	return(0);
    }
	
    /* check for != blocksize requests */
    if (bp->b_bcount % stp->blksize) {
	ata_prtdev(stp->device, "transfers must be multiple of %d\n",
		   stp->blksize);
	bp->b_flags |= B_ERROR;
	bp->b_error = EIO;
	biodone(bio);
	return(0);
    }

    /* warn about transfers bigger than the device suggests */
    if (bp->b_bcount > stp->blksize * stp->cap.ctl) {	 
	if ((stp->flags & F_CTL_WARN) == 0) {
	    ata_prtdev(stp->device, "WARNING: CTL exceeded %d > %d\n",
		       bp->b_bcount, stp->blksize * stp->cap.ctl);
	    stp->flags |= F_CTL_WARN;
	}
    }

    crit_enter();
    bioq_insert_tail(&stp->bio_queue, bio);
    crit_exit();
    ata_start(stp->device->channel);
    return(0);
}

void 
ast_start(struct ata_device *atadev)
{
    struct ast_softc *stp = atadev->driver;
    struct bio *bio = bioq_first(&stp->bio_queue);
    struct buf *bp;
    u_int32_t blkcount;
    int8_t ccb[16];
    
    if (bio == NULL)
	return;
    bzero(ccb, sizeof(ccb));

    bp = bio->bio_buf;
    if (bp->b_cmd == BUF_CMD_READ)
	ccb[0] = ATAPI_READ;
    else
	ccb[0] = ATAPI_WRITE;
    
    bioq_remove(&stp->bio_queue, bio);
    blkcount = bp->b_bcount / stp->blksize;

    ccb[1] = 1;
    ccb[2] = blkcount>>16;
    ccb[3] = blkcount>>8;
    ccb[4] = blkcount;

    devstat_start_transaction(&stp->stats);

    atapi_queue_cmd(stp->device, ccb, bp->b_data, blkcount * stp->blksize, 
		    ((bp->b_cmd == BUF_CMD_READ) ? ATPR_F_READ : 0),
		    120, ast_done, bio);
}

static int 
ast_done(struct atapi_request *request)
{
    struct bio *bio = request->driver;
    struct buf *bp = bio->bio_buf;
    struct ast_softc *stp = request->device->driver;

    if (request->error) {
	bp->b_error = request->error;
	bp->b_flags |= B_ERROR;
    } else {
	if (bp->b_cmd != BUF_CMD_READ)
	    stp->flags |= F_DATA_WRITTEN;
	bp->b_resid = bp->b_bcount - request->donecount;
	ast_total += (bp->b_bcount - bp->b_resid);
    }
    devstat_end_transaction_buf(&stp->stats, bp);
    biodone(bio);
    return 0;
}

static int
ast_mode_sense(struct ast_softc *stp, int page, void *pagebuf, int pagesize)
{
    int8_t ccb[16] = { ATAPI_MODE_SENSE, 0x08, page, pagesize>>8, pagesize,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int error;
 
    error = atapi_queue_cmd(stp->device, ccb, pagebuf, pagesize, ATPR_F_READ,
			    10, NULL, NULL);
#ifdef AST_DEBUG
    atapi_dump("ast: mode sense ", pagebuf, pagesize);
#endif
    return error;
}

static int	 
ast_mode_select(struct ast_softc *stp, void *pagebuf, int pagesize)
{
    int8_t ccb[16] = { ATAPI_MODE_SELECT, 0x10, 0, pagesize>>8, pagesize,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
     
#ifdef AST_DEBUG
    ata_prtdev(stp->device, "modeselect pagesize=%d\n", pagesize);
    atapi_dump("mode select ", pagebuf, pagesize);
#endif
    return atapi_queue_cmd(stp->device, ccb, pagebuf, pagesize, 0,
			   10, NULL, NULL);
}

static int
ast_write_filemark(struct ast_softc *stp, u_int8_t function)
{
    int8_t ccb[16] = { ATAPI_WEOF, 0x01, 0, 0, function, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    if (stp->flags & F_ONSTREAM)
	ccb[4] = 0x00;		/* only flush buffers supported */
    else {
	if (function) {
	    if (stp->flags & F_FM_WRITTEN)
		stp->flags &= ~F_DATA_WRITTEN;
	    else
		stp->flags |= F_FM_WRITTEN;
	}
    }
    error = atapi_queue_cmd(stp->device, ccb, NULL, 0, 0, 10, NULL, NULL);
    if (error)
	return error;
    return atapi_wait_dsc(stp->device, 10*60);
}

static int
ast_read_position(struct ast_softc *stp, int hard,
		  struct ast_readposition *position)
{
    int8_t ccb[16] = { ATAPI_READ_POSITION, (hard ? 0x01 : 0), 0, 0, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    error = atapi_queue_cmd(stp->device, ccb, (caddr_t)position, 
			    sizeof(struct ast_readposition), ATPR_F_READ, 10,
			    NULL, NULL);
    position->tape = ntohl(position->tape);
    position->host = ntohl(position->host);
    return error;
}

static int
ast_space(struct ast_softc *stp, u_int8_t function, int32_t count)
{
    int8_t ccb[16] = { ATAPI_SPACE, function, count>>16, count>>8, count,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return atapi_queue_cmd(stp->device, ccb, NULL, 0, 0, 60*60, NULL, NULL);
}

static int
ast_locate(struct ast_softc *stp, int hard, u_int32_t pos)
{
    int8_t ccb[16] = { ATAPI_LOCATE, 0x01 | (hard ? 0x4 : 0), 0,
		       pos>>24, pos>>16, pos>>8, pos,
		       0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    error = atapi_queue_cmd(stp->device, ccb, NULL, 0, 0, 10, NULL, NULL);
    if (error)
	return error;
    return atapi_wait_dsc(stp->device, 60*60);
}

static int
ast_prevent_allow(struct ast_softc *stp, int lock)
{
    int8_t ccb[16] = { ATAPI_PREVENT_ALLOW, 0, 0, 0, lock,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return atapi_queue_cmd(stp->device, ccb, NULL, 0, 0,30, NULL, NULL);
}

static int
ast_load_unload(struct ast_softc *stp, u_int8_t function)
{
    int8_t ccb[16] = { ATAPI_START_STOP, 0x01, 0, 0, function, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    if ((function & SS_EJECT) && !stp->cap.eject)
	return 0;
    error = atapi_queue_cmd(stp->device, ccb, NULL, 0, 0, 10, NULL, NULL);
    if (error)
	return error;
    tsleep((caddr_t)&error, 0, "astlu", 1 * hz);
    if (function == SS_EJECT)
	return 0;
    return atapi_wait_dsc(stp->device, 60*60);
}

static int
ast_rewind(struct ast_softc *stp)
{
    int8_t ccb[16] = { ATAPI_REZERO, 0x01, 0, 0, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    error = atapi_queue_cmd(stp->device, ccb, NULL, 0, 0, 10, NULL, NULL);
    if (error)
	return error;
    return atapi_wait_dsc(stp->device, 60*60);
}

static int
ast_erase(struct ast_softc *stp)
{
    int8_t ccb[16] = { ATAPI_ERASE, 3, 0, 0, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0 };
    int error;

    if ((error = ast_rewind(stp)))
	return error;

    return atapi_queue_cmd(stp->device, ccb, NULL, 0, 0, 60*60, NULL, NULL);
}
