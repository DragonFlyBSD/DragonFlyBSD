/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_disksubr.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/subr_disk.c,v 1.20.2.6 2001/10/05 07:14:57 peter Exp $
 * $FreeBSD: src/sys/ufs/ufs/ufs_disksubr.c,v 1.44.2.3 2001/03/05 05:42:19 obrien Exp $
 * $DragonFly: src/sys/kern/subr_disk.c,v 1.40 2008/06/05 18:06:32 swildner Exp $
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
#include <sys/sysctl.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>
#include <vfs/devfs/devfs.h>
#include <sys/thread.h>
#include <sys/thread2.h>

#include <sys/queue.h>
#include <sys/lock.h>

static MALLOC_DEFINE(M_DISK, "disk", "disk data");

static void disk_msg_autofree_reply(lwkt_port_t, lwkt_msg_t);
static void disk_msg_core(void *);
static int disk_probe_slice(struct disk *dp, cdev_t dev, int slice, int reprobe);
static void disk_probe(struct disk *dp, int reprobe);
static void _setdiskinfo(struct disk *disk, struct disk_info *info);

static d_open_t diskopen;
static d_close_t diskclose; 
static d_ioctl_t diskioctl;
static d_strategy_t diskstrategy;
static d_psize_t diskpsize;
static d_clone_t diskclone;
static d_dump_t diskdump;

static LIST_HEAD(, disk) disklist = LIST_HEAD_INITIALIZER(&disklist);
static struct lwkt_token disklist_token;

static struct dev_ops disk_ops = {
	{ "disk", 0, D_DISK },
	.d_open = diskopen,
	.d_close = diskclose,
	.d_read = physread,
	.d_write = physwrite,
	.d_ioctl = diskioctl,
	.d_strategy = diskstrategy,
	.d_dump = diskdump,
	.d_psize = diskpsize,
	.d_clone = diskclone
};

static struct objcache 	*disk_msg_cache;

struct objcache_malloc_args disk_msg_malloc_args = {
	sizeof(struct disk_msg), M_DISK };

static struct lwkt_port disk_dispose_port;
static struct lwkt_port disk_msg_port;


static int
disk_probe_slice(struct disk *dp, cdev_t dev, int slice, int reprobe)
{
	struct disk_info *info = &dp->d_info;
	struct diskslice *sp = &dp->d_slice->dss_slices[slice];
	disklabel_ops_t ops;
	struct partinfo part;
	const char *msg;
	cdev_t ndev;
	unsigned long i;

	//lp.opaque = NULL;

	ops = &disklabel32_ops;
	msg = ops->op_readdisklabel(dev, sp, &sp->ds_label, info);
	if (msg && !strcmp(msg, "no disk label")) {
		devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice: trying with disklabel64\n");
		ops = &disklabel64_ops;
		msg = ops->op_readdisklabel(dev, sp, &sp->ds_label, info);
	}
	devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice: label: %s\n", (msg)?msg:"is NULL");
	if (msg == NULL) {
		devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice: found %d partitions in the label\n", ops->op_getnumparts(sp->ds_label));
		if (slice != WHOLE_DISK_SLICE)
			ops->op_adjust_label_reserved(dp->d_slice, slice, sp);
		else
			sp->ds_reserved = 0;

		sp->ds_ops = ops;
		devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice: lp.opaque: %x\n", sp->ds_label.opaque);
		for (i = 0; i < ops->op_getnumparts(sp->ds_label); i++) {
			ops->op_loadpartinfo(sp->ds_label, i, &part);
			devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice: partinfo says fstype=%d for part %d\n", part.fstype, i);
			if (part.fstype) {
				if (reprobe &&
					(ndev = devfs_find_device_by_name("%s%c",
					dev->si_name, 'a'+ (char)i))) {
					/* Device already exists and is still valid */
					devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice: reprobing and device remained valid, mark it\n");
					ndev->si_flags |= SI_REPROBE_TEST;
				} else {
					ndev = make_dev(&disk_ops,
						dkmakeminor(dkunit(dp->d_cdev), slice, i),
						UID_ROOT, GID_OPERATOR, 0640,
						"%s%c", dev->si_name, 'a'+ (char)i);
					ndev->si_disk = dp;
					if (dp->d_info.d_serialno) {
						make_dev_alias(ndev, "serno/%s.s%d%c", dp->d_info.d_serialno, slice - 1, 'a' + (char)i);
					}
					ndev->si_flags |= SI_REPROBE_TEST;
				}

				devfs_debug(DEVFS_DEBUG_DEBUG, "disk_probe_slice:end: lp.opaque: %x\n", ndev->si_disk->d_slice->dss_slices[slice].ds_label.opaque);
			}
		}
	} else if (info->d_dsflags & DSO_COMPATLABEL) {
		msg = NULL;
		if (sp->ds_size >= 0x100000000ULL)
			ops = &disklabel64_ops;
		else
			ops = &disklabel32_ops;
		sp->ds_label = ops->op_clone_label(info, sp);
	} else {
		if (sp->ds_type == DOSPTYP_386BSD /* XXX */)
			log(LOG_WARNING, "%s: cannot find label (%s)\n",
			    dev->si_name, msg);
	}

	if (msg == NULL) {
		sp->ds_wlabel = FALSE;
	}

	return (msg ? EINVAL : 0);
}


static void
disk_probe(struct disk *dp, int reprobe)
{
	struct disk_info *info = &dp->d_info;
	cdev_t dev = dp->d_cdev;
	cdev_t ndev;
	int error, i, sno;
	struct diskslice *sp;

	KKASSERT (info->d_media_blksize != 0);

	dp->d_slice = dsmakeslicestruct(BASE_SLICE, info);

	error = mbrinit(dev, info, &(dp->d_slice));
	if (error)
		return;

	for (i = 0; i < dp->d_slice->dss_nslices; i++) {
		/*
		 * Ignore the whole-disk slice, it has already been created.
		 */
		if (i == WHOLE_DISK_SLICE)
			continue;
		sp = &dp->d_slice->dss_slices[i];

		/*
		 * Handle s0.  s0 is a compatibility slice if there are no
		 * other slices and it has not otherwise been set up, else
		 * we ignore it.
		 */
		if (i == COMPATIBILITY_SLICE) {
			sno = 0;
			if (sp->ds_type == 0 &&
			    dp->d_slice->dss_nslices == BASE_SLICE) {
				sp->ds_size = info->d_media_blocks;
				sp->ds_reserved = 0;
			}
		} else {
			sno = i - 1;
			sp->ds_reserved = 0;
		}

		/*
		 * Ignore 0-length slices
		 */
		if (sp->ds_size == 0)
			continue;

		if (reprobe &&
		    (ndev = devfs_find_device_by_name("%ss%d",
						      dev->si_name, sno))) {
			/*
			 * Device already exists and is still valid
			 */
			ndev->si_flags |= SI_REPROBE_TEST;
		} else {
			/*
			 * Else create new device
			 */
			ndev = make_dev(&disk_ops,
					dkmakewholeslice(dkunit(dev), i),
					UID_ROOT, GID_OPERATOR, 0640,
					"%ss%d", dev->si_name, sno);
			if (dp->d_info.d_serialno) {
				make_dev_alias(ndev, "serno/%s.s%d",
					       dp->d_info.d_serialno, sno);
			}
			ndev->si_disk = dp;
			ndev->si_flags |= SI_REPROBE_TEST;
		}
		sp->ds_dev = ndev;
		if (sp->ds_type == DOSPTYP_386BSD) {
			if (dp->d_slice->dss_first_bsd_slice == 0)
				dp->d_slice->dss_first_bsd_slice = i;
			disk_probe_slice(dp, ndev, i, reprobe);
		}
	}
}


static void
disk_msg_core(void *arg)
{
    uint8_t  run = 1;
	struct disk	*dp;
	struct diskslice *sp;
	lwkt_tokref ilock;
    disk_msg_t msg;

	lwkt_initport_thread(&disk_msg_port, curthread);
	wakeup(curthread);

    while (run) {
        msg = (disk_msg_t)lwkt_waitport(&disk_msg_port, 0);
		devfs_debug(DEVFS_DEBUG_DEBUG, "disk_msg_core, new msg: %x\n", (unsigned int)msg->hdr.u.ms_result);

        switch (msg->hdr.u.ms_result) {

        case DISK_DISK_PROBE:
			dp = (struct disk *)msg->load;
			disk_probe(dp, 0);
			break;

		case DISK_DISK_DESTROY:
			dp = (struct disk *)msg->load;
			devfs_destroy_subnames(dp->d_cdev->si_name);
			devfs_destroy_dev(dp->d_cdev);
			lwkt_gettoken(&ilock, &disklist_token);
			LIST_REMOVE(dp, d_list);
			lwkt_reltoken(&ilock);
#if 0
			devfs_destroy_dev(dp->d_rawdev); /* XXX: needed? when? */
#endif
			if (dp->d_info.d_serialno) {
				kfree(dp->d_info.d_serialno, M_TEMP);
				dp->d_info.d_serialno = NULL;
			}
			break;

		case DISK_UNPROBE:
			dp = (struct disk *)msg->load;
			devfs_destroy_subnames(dp->d_cdev->si_name);
			break;

		case DISK_SLICE_REPROBE:
			dp = (struct disk *)msg->load;
			sp = (struct diskslice *)msg->load2;
			devfs_clr_subnames_flag(sp->ds_dev->si_name, SI_REPROBE_TEST);
			devfs_debug(DEVFS_DEBUG_DEBUG,
				    "DISK_SLICE_REPROBE: %s\n",
				    sp->ds_dev->si_name);
			disk_probe_slice(dp, sp->ds_dev, dkslice(sp->ds_dev), 1);
			devfs_destroy_subnames_without_flag(sp->ds_dev->si_name,
												SI_REPROBE_TEST);
			break;

		case DISK_DISK_REPROBE:
			dp = (struct disk *)msg->load;
			devfs_clr_subnames_flag(dp->d_cdev->si_name, SI_REPROBE_TEST);
			devfs_debug(DEVFS_DEBUG_DEBUG,
				    "DISK_DISK_REPROBE: %s\n",
				    dp->d_cdev->si_name);
			disk_probe(dp, 1);
			devfs_destroy_subnames_without_flag(dp->d_cdev->si_name,
												SI_REPROBE_TEST);
			break;

		case DISK_SYNC:
			break;

        default:
            devfs_debug(DEVFS_DEBUG_WARNING, "disk_msg_core: unknown message received at core\n");
        }

        lwkt_replymsg((lwkt_msg_t)msg, 0);
    }
	lwkt_exit();
}


/**
 * Acts as a message drain. Any message that is replied to here gets destroyed and
 * the memory freed.
 **/
static void
disk_msg_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
    objcache_put(disk_msg_cache, msg);
}


void
disk_msg_send(uint32_t cmd, void *load, void *load2)
{
    disk_msg_t disk_msg;
	lwkt_port_t port = &disk_msg_port;

    disk_msg = objcache_get(disk_msg_cache, M_WAITOK);

    lwkt_initmsg(&disk_msg->hdr, &disk_dispose_port, 0);

	disk_msg->hdr.u.ms_result = cmd;
	disk_msg->load = load;
	disk_msg->load2 = load2;
	KKASSERT(port);
    lwkt_sendmsg(port, (lwkt_msg_t)disk_msg);
}

void
disk_msg_send_sync(uint32_t cmd, void *load, void *load2)
{
	struct lwkt_port rep_port;
	disk_msg_t disk_msg = objcache_get(disk_msg_cache, M_WAITOK);
	disk_msg_t	msg_incoming;
	lwkt_port_t port = &disk_msg_port;

	lwkt_initport_thread(&rep_port, curthread);
	lwkt_initmsg(&disk_msg->hdr, &rep_port, 0);

	disk_msg->hdr.u.ms_result = cmd;
	disk_msg->load = load;
	disk_msg->load2 = load2;

	KKASSERT(port);
    lwkt_sendmsg(port, (lwkt_msg_t)disk_msg);
	msg_incoming = lwkt_waitport(&rep_port, 0);
}

/*
 * Create a raw device for the dev_ops template (which is returned).  Also
 * create a slice and unit managed disk and overload the user visible
 * device space with it.
 *
 * NOTE: The returned raw device is NOT a slice and unit managed device.
 * It is an actual raw device representing the raw disk as specified by
 * the passed dev_ops.  The disk layer not only returns such a raw device,
 * it also uses it internally when passing (modified) commands through.
 */
cdev_t
disk_create(int unit, struct disk *dp, struct dev_ops *raw_ops)
{
	lwkt_tokref ilock;
	cdev_t rawdev;

	rawdev = make_only_dev(raw_ops, dkmakewholedisk(unit),
			    UID_ROOT, GID_OPERATOR, 0640,
			    "%s%d", raw_ops->head.name, unit);


	bzero(dp, sizeof(*dp));

	dp->d_rawdev = rawdev;
	dp->d_raw_ops = raw_ops;
	dp->d_dev_ops = &disk_ops;
	dp->d_cdev = make_dev(&disk_ops,
			    dkmakewholedisk(unit),
			    UID_ROOT, GID_OPERATOR, 0640,
			    "%s%d", raw_ops->head.name, unit);

	dp->d_cdev->si_disk = dp;

	devfs_debug(DEVFS_DEBUG_DEBUG, "disk_create called for %s\n",
			dp->d_cdev->si_name);
	lwkt_gettoken(&ilock, &disklist_token);
	LIST_INSERT_HEAD(&disklist, dp, d_list);
	lwkt_reltoken(&ilock);
	return (dp->d_rawdev);
}


static void
_setdiskinfo(struct disk *disk, struct disk_info *info)
{
	char *oldserialno;

	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "_setdiskinfo called for disk -1-: %x\n", disk);
	oldserialno = disk->d_info.d_serialno;
	bcopy(info, &disk->d_info, sizeof(disk->d_info));
	info = &disk->d_info;

	/*
	 * The serial number is duplicated so the caller can throw
	 * their copy away.
	 */
	if (info->d_serialno && info->d_serialno[0]) {
		info->d_serialno = kstrdup(info->d_serialno, M_TEMP);
		if (disk->d_cdev) {
			make_dev_alias(disk->d_cdev, "serno/%s",
					info->d_serialno);
		}
	} else {
		info->d_serialno = NULL;
	}
	if (oldserialno)
		kfree(oldserialno, M_TEMP);

	/*
	 * The caller may set d_media_size or d_media_blocks and we
	 * calculate the other.
	 */
	KKASSERT(info->d_media_size == 0 || info->d_media_blksize == 0);
	if (info->d_media_size == 0 && info->d_media_blocks) {
		info->d_media_size = (u_int64_t)info->d_media_blocks * 
				     info->d_media_blksize;
	} else if (info->d_media_size && info->d_media_blocks == 0 && 
		   info->d_media_blksize) {
		info->d_media_blocks = info->d_media_size / 
				       info->d_media_blksize;
	}

	/*
	 * The si_* fields for rawdev are not set until after the
	 * disk_create() call, so someone using the cooked version
	 * of the raw device (i.e. da0s0) will not get the right
	 * si_iosize_max unless we fix it up here.
	 */
	if (disk->d_cdev && disk->d_rawdev &&
	    disk->d_cdev->si_iosize_max == 0) {
		disk->d_cdev->si_iosize_max = disk->d_rawdev->si_iosize_max;
		disk->d_cdev->si_bsize_phys = disk->d_rawdev->si_bsize_phys;
		disk->d_cdev->si_bsize_best = disk->d_rawdev->si_bsize_best;
	}
}

/*
 * Disk drivers must call this routine when media parameters are available
 * or have changed.
 */
void
disk_setdiskinfo(struct disk *disk, struct disk_info *info)
{
	_setdiskinfo(disk, info);
	devfs_debug(DEVFS_DEBUG_DEBUG, "disk_setdiskinfo called for disk -2-: %x\n", disk);
	disk_msg_send(DISK_DISK_PROBE, disk, NULL);
}

void
disk_setdiskinfo_sync(struct disk *disk, struct disk_info *info)
{
	_setdiskinfo(disk, info);
	devfs_debug(DEVFS_DEBUG_DEBUG, "disk_setdiskinfo_sync called for disk -2-: %x\n", disk);
	disk_msg_send_sync(DISK_DISK_PROBE, disk, NULL);
}

/*
 * This routine is called when an adapter detaches.  The higher level
 * managed disk device is destroyed while the lower level raw device is
 * released.
 */
void
disk_destroy(struct disk *disk)
{
	disk_msg_send_sync(DISK_DISK_DESTROY, disk, NULL);
	return;
}

int
disk_dumpcheck(cdev_t dev, u_int64_t *count, u_int64_t *blkno, u_int *secsize)
{
	struct partinfo pinfo;
	int error;

	bzero(&pinfo, sizeof(pinfo));
	error = dev_dioctl(dev, DIOCGPART, (void *)&pinfo, 0, proc0.p_ucred);
	if (error)
		return (error);
	if (pinfo.media_blksize == 0)
		return (ENXIO);
	*count = (u_int64_t)Maxmem * PAGE_SIZE / pinfo.media_blksize;
	if (dumplo64 < pinfo.reserved_blocks ||
	    dumplo64 + *count > pinfo.media_blocks) {
		return (ENOSPC);
	}
	*blkno = dumplo64 + pinfo.media_offset / pinfo.media_blksize;
	*secsize = pinfo.media_blksize;
	return (0);
}

void
disk_unprobe(struct disk *disk)
{
	if (disk == NULL)
		return;

	disk_msg_send_sync(DISK_UNPROBE, disk, NULL);
}

void 
disk_invalidate (struct disk *disk)
{
	devfs_debug(DEVFS_DEBUG_INFO, "disk_invalidate for %s\n", disk->d_cdev->si_name);
	if (disk->d_slice)
		dsgone(&disk->d_slice);
}

struct disk *
disk_enumerate(struct disk *disk)
{
	struct disk *dp;
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &disklist_token);
	if (!disk)
		dp = (LIST_FIRST(&disklist));
	else
		dp = (LIST_NEXT(disk, d_list));
	lwkt_reltoken(&ilock);

	return dp;
}

static 
int
sysctl_disks(SYSCTL_HANDLER_ARGS)
{
	struct disk *disk;
	int error, first;

	disk = NULL;
	first = 1;

	while ((disk = disk_enumerate(disk))) {
		if (!first) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error)
				return error;
		} else {
			first = 0;
		}
		error = SYSCTL_OUT(req, disk->d_rawdev->si_name,
				   strlen(disk->d_rawdev->si_name));
		if (error)
			return error;
	}
	error = SYSCTL_OUT(req, "", 1);
	return error;
}
 
SYSCTL_PROC(_kern, OID_AUTO, disks, CTLTYPE_STRING | CTLFLAG_RD, NULL, 0,
    sysctl_disks, "A", "names of available disks");

/*
 * Open a disk device or partition.
 */
static
int
diskopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	int error;

	devfs_debug(DEVFS_DEBUG_DEBUG, "diskopen: name is %s\n", dev->si_name);

	/*
	 * dp can't be NULL here XXX.
	 */
	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);
	error = 0;

	/*
	 * Deal with open races
	 */
	while (dp->d_flags & DISKFLAG_LOCK) {
		dp->d_flags |= DISKFLAG_WANTED;
		error = tsleep(dp, PCATCH, "diskopen", hz);
		if (error)
			return (error);
	}
	dp->d_flags |= DISKFLAG_LOCK;

	devfs_debug(DEVFS_DEBUG_DEBUG, "diskopen: -2- name is %s\n", dev->si_name);

	/*
	 * Open the underlying raw device.
	 */
	if (!dsisopen(dp->d_slice)) {
#if 0
		if (!pdev->si_iosize_max)
			pdev->si_iosize_max = dev->si_iosize_max;
#endif
		error = dev_dopen(dp->d_rawdev, ap->a_oflags,
				  ap->a_devtype, ap->a_cred);
	}
#if 0
	/*
	 * Inherit properties from the underlying device now that it is
	 * open.
	 */
	dev_dclone(dev);
#endif

	if (error)
		goto out;
	error = dsopen(dev, ap->a_devtype, dp->d_info.d_dsflags,
		       &dp->d_slice, &dp->d_info);
	if (!dsisopen(dp->d_slice)) {
		dev_dclose(dp->d_rawdev, ap->a_oflags, ap->a_devtype);
	}
out:	
	dp->d_flags &= ~DISKFLAG_LOCK;
	if (dp->d_flags & DISKFLAG_WANTED) {
		dp->d_flags &= ~DISKFLAG_WANTED;
		wakeup(dp);
	}
	
	return(error);
}

/*
 * Close a disk device or partition
 */
static
int
diskclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	int error;

	error = 0;
	dp = dev->si_disk;

	devfs_debug(DEVFS_DEBUG_DEBUG, "diskclose: name %s\n", dev->si_name);

	dsclose(dev, ap->a_devtype, dp->d_slice);
	if (!dsisopen(dp->d_slice)) {
		devfs_debug(DEVFS_DEBUG_DEBUG, "diskclose is closing underlying device\n");
		error = dev_dclose(dp->d_rawdev, ap->a_fflag, ap->a_devtype);
	}
	return (error);
}

/*
 * First execute the ioctl on the disk device, and if it isn't supported 
 * try running it on the backing device.
 */
static
int
diskioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	int error;

	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);

	devfs_debug(DEVFS_DEBUG_DEBUG, "diskioctl: cmd is: %x (name: %s)\n", ap->a_cmd, dev->si_name);
	devfs_debug(DEVFS_DEBUG_DEBUG, "diskioctl: &dp->d_slice is: %x, %x\n", &dp->d_slice, dp->d_slice);

	devfs_debug(DEVFS_DEBUG_DEBUG, "diskioctl:1: says lp.opaque is: %x\n", dp->d_slice->dss_slices[0].ds_label.opaque);

	error = dsioctl(dev, ap->a_cmd, ap->a_data, ap->a_fflag,
			&dp->d_slice, &dp->d_info);

	devfs_debug(DEVFS_DEBUG_DEBUG, "diskioctl:2: says lp.opaque is: %x\n", dp->d_slice->dss_slices[0].ds_label.opaque);

	if (error == ENOIOCTL) {
		devfs_debug(DEVFS_DEBUG_DEBUG, "diskioctl: going for dev_dioctl instead!\n");
		error = dev_dioctl(dp->d_rawdev, ap->a_cmd, ap->a_data,
				   ap->a_fflag, ap->a_cred);
	}
	return (error);
}

/*
 * Execute strategy routine
 */
static
int
diskstrategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct disk *dp;

	dp = dev->si_disk;

	if (dp == NULL) {
		bio->bio_buf->b_error = ENXIO;
		bio->bio_buf->b_flags |= B_ERROR;
		biodone(bio);
		return(0);
	}
	KKASSERT(dev->si_disk == dp);

	/*
	 * The dscheck() function will also transform the slice relative
	 * block number i.e. bio->bio_offset into a block number that can be
	 * passed directly to the underlying raw device.  If dscheck()
	 * returns NULL it will have handled the bio for us (e.g. EOF
	 * or error due to being beyond the device size).
	 */
	if ((nbio = dscheck(dev, bio, dp->d_slice)) != NULL) {
		dev_dstrategy(dp->d_rawdev, nbio);
	} else {
		devfs_debug(DEVFS_DEBUG_DEBUG, "diskstrategy: dscheck NULL!!! biodone time!\n");
		biodone(bio);
	}
	return(0);
}

/*
 * Return the partition size in ?blocks?
 */
static
int
diskpsize(struct dev_psize_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp;

	dp = dev->si_disk;
	if (dp == NULL)
		return(ENODEV);
	ap->a_result = dssize(dev, &dp->d_slice);
	return(0);
}

/*
 * When new device entries are instantiated, make sure they inherit our
 * si_disk structure and block and iosize limits from the raw device.
 *
 * This routine is always called synchronously in the context of the 
 * client.
 *
 * XXX The various io and block size constraints are not always initialized
 * properly by devices.
 */
static
int
diskclone(struct dev_clone_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	dp = dev->si_disk;

	KKASSERT(dp != NULL);
	dev->si_disk = dp;
	dev->si_iosize_max = dp->d_rawdev->si_iosize_max;
	dev->si_bsize_phys = dp->d_rawdev->si_bsize_phys;
	dev->si_bsize_best = dp->d_rawdev->si_bsize_best;
	return(0);
}

int
diskdump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp = dev->si_disk;
	int error;

	error = disk_dumpcheck(dev, &ap->a_count, &ap->a_blkno, &ap->a_secsize);
	if (error == 0) {
		ap->a_head.a_dev = dp->d_rawdev;
		error = dev_doperate(&ap->a_head);
	}

	return(error);
}


SYSCTL_INT(_debug_sizeof, OID_AUTO, diskslices, CTLFLAG_RD, 
    0, sizeof(struct diskslices), "sizeof(struct diskslices)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, disk, CTLFLAG_RD, 
    0, sizeof(struct disk), "sizeof(struct disk)");


/*
 * Seek sort for disks.
 *
 * The bio_queue keep two queues, sorted in ascending block order.  The first
 * queue holds those requests which are positioned after the current block
 * (in the first request); the second, which starts at queue->switch_point,
 * holds requests which came in after their block number was passed.  Thus
 * we implement a one way scan, retracting after reaching the end of the drive
 * to the first request on the second queue, at which time it becomes the
 * first queue.
 *
 * A one-way scan is natural because of the way UNIX read-ahead blocks are
 * allocated.
 */
void
bioqdisksort(struct bio_queue_head *bioq, struct bio *bio)
{
	struct bio *bq;
	struct bio *bn;
	struct bio *be;
	
	be = TAILQ_LAST(&bioq->queue, bio_queue);
	/*
	 * If the queue is empty or we are an
	 * ordered transaction, then it's easy.
	 */
	if ((bq = bioq_first(bioq)) == NULL || 
	    (bio->bio_buf->b_flags & B_ORDERED) != 0) {
		bioq_insert_tail(bioq, bio);
		return;
	} else if (bioq->insert_point != NULL) {

		/*
		 * A certain portion of the list is
		 * "locked" to preserve ordering, so
		 * we can only insert after the insert
		 * point.
		 */
		bq = bioq->insert_point;
	} else {

		/*
		 * If we lie before the last removed (currently active)
		 * request, and are not inserting ourselves into the
		 * "locked" portion of the list, then we must add ourselves
		 * to the second request list.
		 */
		if (bio->bio_offset < bioq->last_offset) {
			bq = bioq->switch_point;
			/*
			 * If we are starting a new secondary list,
			 * then it's easy.
			 */
			if (bq == NULL) {
				bioq->switch_point = bio;
				bioq_insert_tail(bioq, bio);
				return;
			}
			/*
			 * If we lie ahead of the current switch point,
			 * insert us before the switch point and move
			 * the switch point.
			 */
			if (bio->bio_offset < bq->bio_offset) {
				bioq->switch_point = bio;
				TAILQ_INSERT_BEFORE(bq, bio, bio_act);
				return;
			}
		} else {
			if (bioq->switch_point != NULL)
				be = TAILQ_PREV(bioq->switch_point,
						bio_queue, bio_act);
			/*
			 * If we lie between last_offset and bq,
			 * insert before bq.
			 */
			if (bio->bio_offset < bq->bio_offset) {
				TAILQ_INSERT_BEFORE(bq, bio, bio_act);
				return;
			}
		}
	}

	/*
	 * Request is at/after our current position in the list.
	 * Optimize for sequential I/O by seeing if we go at the tail.
	 */
	if (bio->bio_offset > be->bio_offset) {
		TAILQ_INSERT_AFTER(&bioq->queue, be, bio, bio_act);
		return;
	}

	/* Otherwise, insertion sort */
	while ((bn = TAILQ_NEXT(bq, bio_act)) != NULL) {
		
		/*
		 * We want to go after the current request if it is the end
		 * of the first request list, or if the next request is a
		 * larger cylinder than our request.
		 */
		if (bn == bioq->switch_point
		 || bio->bio_offset < bn->bio_offset)
			break;
		bq = bn;
	}
	TAILQ_INSERT_AFTER(&bioq->queue, bq, bio, bio_act);
}

/*
 * Disk error is the preface to plaintive error messages
 * about failing disk transfers.  It prints messages of the form

hp0g: hard error reading fsbn 12345 of 12344-12347 (hp0 bn %d cn %d tn %d sn %d)

 * if the offset of the error in the transfer and a disk label
 * are both available.  blkdone should be -1 if the position of the error
 * is unknown; the disklabel pointer may be null from drivers that have not
 * been converted to use them.  The message is printed with kprintf
 * if pri is LOG_PRINTF, otherwise it uses log at the specified priority.
 * The message should be completed (with at least a newline) with kprintf
 * or log(-1, ...), respectively.  There is no trailing space.
 */
void
diskerr(struct bio *bio, cdev_t dev, const char *what, int pri, int donecnt)
{
	struct buf *bp = bio->bio_buf;
	const char *term;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		term = "read";
		break;
	case BUF_CMD_WRITE:
		term = "write";
		break;
	default:
		term = "access";
		break;
	}
	//sname = dsname(dev, unit, slice, part, partname);
	kprintf("%s: %s %sing ", dev->si_name, what, term);
	kprintf("offset %012llx for %d",
		(long long)bio->bio_offset,
		bp->b_bcount);

	if (donecnt)
		kprintf(" (%d bytes completed)", donecnt);
}

/*
 * Locate a disk device
 */
cdev_t
disk_locate(const char *devname)
{
	return devfs_find_device_by_name(devname);
}


void
disk_config(void *arg)
{
	disk_msg_send_sync(DISK_SYNC, NULL, NULL);
}


static void
disk_init(void)
{
	struct thread* td_core;
	devfs_debug(DEVFS_DEBUG_DEBUG, "disk_init() called\n");

    disk_msg_cache = objcache_create("disk-msg-cache", 0, 0,
			NULL, NULL, NULL,
			objcache_malloc_alloc,
			objcache_malloc_free,
			&disk_msg_malloc_args );

	lwkt_token_init(&disklist_token);

	/* Initialize the reply-only port which acts as a message drain */
	lwkt_initport_replyonly(&disk_dispose_port, disk_msg_autofree_reply);

	lwkt_create(disk_msg_core, /*args*/NULL, &td_core, NULL,
		    0, 0, "disk_msg_core");

	tsleep(td_core, 0, "diskcore", 0);
}


static void
disk_uninit(void)
{
	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_uninit() called\n");

	objcache_destroy(disk_msg_cache);

}


SYSINIT(disk_register, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, disk_init, NULL);
SYSUNINIT(disk_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, disk_uninit, NULL);
