/*-
 * Copyright (c) 2001,2002 Søren Schmidt <sos@FreeBSD.org>
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
 * $FreeBSD: src/sys/dev/pst/pst-raid.c,v 1.2.2.1 2002/08/18 12:32:36 sos Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/rman.h>
#include <sys/buf2.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/stdarg.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include "pst-iop.h"

/* device structures */ 
static d_strategy_t pststrategy;
static struct dev_ops pst_ops = {
	{ "pst", 0, D_DISK },
	.d_open =	nullopen,
	.d_close =	nullclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_strategy =	pststrategy,
};

struct pst_softc {
    struct iop_softc		*iop;
    struct i2o_lct_entry	*lct;
    struct i2o_bsa_device	*info;
    cdev_t			device;
    struct devstat		stats;
    struct disk			disk;
    struct bio_queue_head	bio_queue;
    int				outstanding;
};

struct pst_request {
    struct pst_softc		*psc;		/* pointer to softc */
    u_int32_t			mfa;		/* frame address */
    struct callout		timeout;	/* callout handle */
    struct bio			*bio;		/* associated bio ptr */
};

/* prototypes */
static int pst_probe(device_t);
static int pst_attach(device_t);
#if 0
static int pst_shutdown(device_t);
#endif
static void pst_start(struct pst_softc *);
static void pst_done(struct iop_softc *, u_int32_t, struct i2o_single_reply *);
static int pst_rw(struct pst_request *);
static void pst_timeout(void *);
static void bpack(int8_t *, int8_t *, int);

/* local vars */
static MALLOC_DEFINE(M_PSTRAID, "pst", "Promise SuperTrak RAID driver");

int
pst_add_raid(struct iop_softc *sc, struct i2o_lct_entry *lct)
{
    struct pst_softc *psc;
    device_t child = device_add_child(sc->dev, "pst", -1);

    if (!child)
	return ENOMEM;
    psc = kmalloc(sizeof(struct pst_softc), M_PSTRAID, M_INTWAIT | M_ZERO); 
    psc->iop = sc;
    psc->lct = lct;
    device_set_softc(child, psc);
    return bus_generic_attach(sc->dev);
}

static int
pst_probe(device_t dev)
{
    device_set_desc(dev, "Promise SuperTrak RAID");
    return 0;
}

static int
pst_attach(device_t dev)
{
    struct pst_softc *psc = device_get_softc(dev);
    struct i2o_get_param_reply *reply;
    struct i2o_device_identity *ident;
    struct disk_info info;
    int lun = device_get_unit(dev);
    int8_t name [32];

    if (!(reply = iop_get_util_params(psc->iop, psc->lct->local_tid,
				      I2O_PARAMS_OPERATION_FIELD_GET,
				      I2O_BSA_DEVICE_INFO_GROUP_NO)))
	return ENODEV;

    psc->info = kmalloc(sizeof(struct i2o_bsa_device), M_PSTRAID, M_INTWAIT);
    bcopy(reply->result, psc->info, sizeof(struct i2o_bsa_device));
    contigfree(reply, PAGE_SIZE, M_PSTRAID);

    if (!(reply = iop_get_util_params(psc->iop, psc->lct->local_tid,
				      I2O_PARAMS_OPERATION_FIELD_GET,
				      I2O_UTIL_DEVICE_IDENTITY_GROUP_NO)))
	return ENODEV;
    ident = (struct i2o_device_identity *)reply->result;
#ifdef PSTDEBUG	   
    kprintf("pst: vendor=<%.16s> product=<%.16s>\n",
	   ident->vendor, ident->product);
    kprintf("pst: description=<%.16s> revision=<%.8s>\n",
	   ident->description, ident->revision);
    kprintf("pst: capacity=%lld blocksize=%d\n",
	   psc->info->capacity, psc->info->block_size);
#endif
    bpack(ident->vendor, ident->vendor, 16);
    bpack(ident->product, ident->product, 16);
    ksprintf(name, "%s %s", ident->vendor, ident->product);
    contigfree(reply, PAGE_SIZE, M_PSTRAID);

    bioq_init(&psc->bio_queue);

    psc->device = disk_create(lun, &psc->disk, &pst_ops);
    psc->device->si_drv1 = psc;
    psc->device->si_iosize_max = 64 * 1024; /*I2O_SGL_MAX_SEGS * PAGE_SIZE;*/

    bzero(&info, sizeof(info));
    info.d_media_blksize = psc->info->block_size;	/* mandatory */
    info.d_media_size = psc->info->capacity;		/* (in bytes) */

    info.d_secpertrack = 63;				/* optional */
    info.d_nheads = 255;
    info.d_secpercyl = info.d_nheads * info.d_secpertrack;
    info.d_ncylinders =
	(psc->info->capacity / psc->info->block_size) / info.d_secpercyl;

    disk_setdiskinfo(&psc->disk, &info);

    devstat_add_entry(&psc->stats, "pst", lun, psc->info->block_size,
		      DEVSTAT_NO_ORDERED_TAGS,
		      DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_IDE,
		      DEVSTAT_PRIORITY_DISK);

    kprintf("pst%d: %juMB <%.40s> [%d/%d/%d] on %.16s\n", lun,
	   (uintmax_t)(info.d_media_size / (1024 * 1024)), name,
	   info.d_ncylinders , info.d_nheads, info.d_secpertrack,
	   device_get_nameunit(psc->iop->dev));
#if 0
    EVENTHANDLER_REGISTER(shutdown_post_sync, pst_shutdown,
			  dev, SHUTDOWN_PRI_DRIVER);
#endif
    return 0;
}

#if 0
static int
pst_shutdown(device_t dev)
{
    struct pst_softc *psc = device_get_softc(dev);
    struct i2o_bsa_cache_flush_message *msg;
    int mfa;

    mfa = iop_get_mfa(psc->iop);
    msg = (struct i2o_bsa_cache_flush_message *)(psc->iop->ibase + mfa);
    bzero(msg, sizeof(struct i2o_bsa_cache_flush_message));
    msg->version_offset = 0x01;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_bsa_cache_flush_message) >> 2;
    msg->target_address = psc->lct->local_tid;
    msg->initiator_address = I2O_TID_HOST;
    msg->function = I2O_BSA_CACHE_FLUSH;
    msg->control_flags = 0x0; /* 0x80 = post progress reports */
    if (iop_queue_wait_msg(psc->iop, mfa, (struct i2o_basic_message *)msg))
	kprintf("pst: shutdown failed!\n");
    return 0;
}
#endif

static int
pststrategy(struct dev_strategy_args *ap)
{
    struct pst_softc *psc = ap->a_head.a_dev->si_drv1;

    crit_enter();
    bioqdisksort(&psc->bio_queue, ap->a_bio);
    pst_start(psc);
    crit_exit();
    return(0);
}

static void
pst_start(struct pst_softc *psc)
{
    struct pst_request *request;
    struct buf *bp;
    struct bio *bio;
    u_int32_t mfa;

    if (psc->outstanding < (I2O_IOP_OUTBOUND_FRAME_COUNT - 1) &&
	(bio = bioq_first(&psc->bio_queue))) {
	if ((mfa = iop_get_mfa(psc->iop)) != 0xffffffff) {
	    request = kmalloc(sizeof(struct pst_request),
			       M_PSTRAID, M_INTWAIT | M_ZERO);
	    psc->outstanding++;
	    request->psc = psc;
	    request->mfa = mfa;
	    request->bio = bio;
	    callout_init(&request->timeout);
	    if (!dumping)
	        callout_reset(&request->timeout, 10 * hz, pst_timeout, request);
	    bioq_remove(&psc->bio_queue, bio);
	    bp = bio->bio_buf;
	    devstat_start_transaction(&psc->stats);
	    if (pst_rw(request)) {
		devstat_end_transaction_buf(&psc->stats, bp);
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		biodone(bio);
		iop_free_mfa(request->psc->iop, request->mfa);
		psc->outstanding--;
		callout_stop(&request->timeout);
		kfree(request, M_PSTRAID);
	    }
	}
    }
}

static void
pst_done(struct iop_softc *sc, u_int32_t mfa, struct i2o_single_reply *reply)
{
    struct pst_request *request =
        (struct pst_request *)reply->transaction_context;
    struct pst_softc *psc = request->psc;
    struct buf *bp = request->bio->bio_buf;

    callout_stop(&request->timeout);
    bp->b_resid = bp->b_bcount - reply->donecount;
    devstat_end_transaction_buf(&psc->stats, bp);
    if (reply->status) {
	bp->b_error = EIO;
	bp->b_flags |= B_ERROR;
    }
    biodone(request->bio);
    kfree(request, M_PSTRAID);
    crit_enter();
    psc->iop->reg->oqueue = mfa;
    psc->outstanding--;
    pst_start(psc);
    crit_exit();
}

static void
pst_timeout(void *xrequest)
{
    struct pst_request *request = xrequest;
    struct buf *bp = request->bio->bio_buf;

    crit_enter();
    kprintf("pst: timeout mfa=0x%08x cmd=%s\n",
	   request->mfa, (bp->b_cmd == BUF_CMD_READ) ? "READ" : "WRITE");
    iop_free_mfa(request->psc->iop, request->mfa);
    if ((request->mfa = iop_get_mfa(request->psc->iop)) == 0xffffffff) {
	kprintf("pst: timeout no mfa possible\n");
	devstat_end_transaction_buf(&request->psc->stats, bp);
	bp->b_error = EIO;
	bp->b_flags |= B_ERROR;
	biodone(request->bio);
	request->psc->outstanding--;
	crit_exit();
	return;
    }
    if (!dumping)
	callout_reset(&request->timeout, 10 * hz, pst_timeout, request);
    if (pst_rw(request)) {
	iop_free_mfa(request->psc->iop, request->mfa);
	devstat_end_transaction_buf(&request->psc->stats, bp);
	bp->b_error = EIO;
	bp->b_flags |= B_ERROR;
	biodone(request->bio);
	request->psc->outstanding--;
    }
    crit_exit();
}

int
pst_rw(struct pst_request *request)
{
    struct i2o_bsa_rw_block_message *msg;
    int sgl_flag;
    struct buf *bp = request->bio->bio_buf;

    msg = (struct i2o_bsa_rw_block_message *)
	  (request->psc->iop->ibase + request->mfa);
    bzero(msg, sizeof(struct i2o_bsa_rw_block_message));
    msg->version_offset = 0x81;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_bsa_rw_block_message) >> 2;
    msg->target_address = request->psc->lct->local_tid;
    msg->initiator_address = I2O_TID_HOST;
    if (bp->b_cmd == BUF_CMD_READ) {
	msg->function = I2O_BSA_BLOCK_READ;
	msg->control_flags = 0x0; /* 0x0c = read cache + readahead */
	msg->fetch_ahead = 0x0; /* 8 Kb */
	sgl_flag = 0;
    }
    else {
	msg->function = I2O_BSA_BLOCK_WRITE;
	msg->control_flags = 0x0; /* 0x10 = write behind cache */
	msg->fetch_ahead = 0x0;
	sgl_flag = I2O_SGL_DIR;
    }
    msg->initiator_context = (u_int32_t)pst_done;
    msg->transaction_context = (u_int32_t)request;
    msg->time_multiplier = 1;
    msg->bytecount = bp->b_bcount;
    msg->lba = request->bio->bio_offset;	/* 64 bits */
    if (!iop_create_sgl((struct i2o_basic_message *)msg, bp->b_data,
			bp->b_bcount, sgl_flag))
	return -1;
    request->psc->iop->reg->iqueue = request->mfa;
    return 0;
}

static void
bpack(int8_t *src, int8_t *dst, int len)
{
    int i, j, blank;
    int8_t *ptr, *buf = dst;

    for (i = j = blank = 0 ; i < len; i++) {
	if (blank && src[i] == ' ') continue;
	if (blank && src[i] != ' ') {
	    dst[j++] = src[i];
	    blank = 0;
	    continue;
	}
	if (src[i] == ' ') {
	    blank = 1;
	    if (i == 0)
		continue;
	}
	dst[j++] = src[i];
    }
    if (j < len) 
	dst[j] = 0x00;
    for (ptr = buf; ptr < buf+len; ++ptr)
	if (!*ptr)
	    *ptr = ' ';
    for (ptr = buf + len - 1; ptr >= buf && *ptr == ' '; --ptr)
        *ptr = 0;
}

static device_method_t pst_methods[] = {
    DEVMETHOD(device_probe,	pst_probe),
    DEVMETHOD(device_attach,	pst_attach),
    DEVMETHOD_END
};
	
static driver_t pst_driver = {
    "pst",
    pst_methods,
    sizeof(struct pst_softc),
};

static devclass_t pst_devclass;

DRIVER_MODULE(pst, pstpci, pst_driver, pst_devclass, NULL, NULL);
