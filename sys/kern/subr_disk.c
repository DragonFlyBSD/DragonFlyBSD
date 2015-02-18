/*
 * Copyright (c) 2003,2004,2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * and Alex Hornung <ahornung@gmail.com>
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
 * 3. Neither the name of the University nor the names of its contributors
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
#include <sys/kerneldump.h>
#include <sys/malloc.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/devfs.h>
#include <sys/thread.h>
#include <sys/dsched.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/udev.h>
#include <sys/uuid.h>

#include <sys/buf2.h>
#include <sys/mplock2.h>
#include <sys/msgport2.h>
#include <sys/thread2.h>

static MALLOC_DEFINE(M_DISK, "disk", "disk data");
static int disk_debug_enable = 0;

static void disk_msg_autofree_reply(lwkt_port_t, lwkt_msg_t);
static void disk_msg_core(void *);
static int disk_probe_slice(struct disk *dp, cdev_t dev, int slice, int reprobe);
static void disk_probe(struct disk *dp, int reprobe);
static void _setdiskinfo(struct disk *disk, struct disk_info *info);
static void bioqwritereorder(struct bio_queue_head *bioq);
static void disk_cleanserial(char *serno);
static int disk_debug(int, char *, ...) __printflike(2, 3);
static cdev_t _disk_create_named(const char *name, int unit, struct disk *dp,
    struct dev_ops *raw_ops, int clone);

static d_open_t diskopen;
static d_close_t diskclose;
static d_ioctl_t diskioctl;
static d_strategy_t diskstrategy;
static d_psize_t diskpsize;
static d_dump_t diskdump;

static LIST_HEAD(, disk) disklist = LIST_HEAD_INITIALIZER(&disklist);
static struct lwkt_token disklist_token;

static struct dev_ops disk_ops = {
	{ "disk", 0, D_DISK | D_MPSAFE | D_TRACKCLOSE },
	.d_open = diskopen,
	.d_close = diskclose,
	.d_read = physread,
	.d_write = physwrite,
	.d_ioctl = diskioctl,
	.d_strategy = diskstrategy,
	.d_dump = diskdump,
	.d_psize = diskpsize,
};

static struct objcache 	*disk_msg_cache;

struct objcache_malloc_args disk_msg_malloc_args = {
	sizeof(struct disk_msg), M_DISK };

static struct lwkt_port disk_dispose_port;
static struct lwkt_port disk_msg_port;

static int
disk_debug(int level, char *fmt, ...)
{
	__va_list ap;

	__va_start(ap, fmt);
	if (level <= disk_debug_enable)
		kvprintf(fmt, ap);
	__va_end(ap);

	return 0;
}

static int
disk_probe_slice(struct disk *dp, cdev_t dev, int slice, int reprobe)
{
	struct disk_info *info = &dp->d_info;
	struct diskslice *sp = &dp->d_slice->dss_slices[slice];
	disklabel_ops_t ops;
	struct partinfo part;
	const char *msg;
	char uuid_buf[128];
	cdev_t ndev;
	int sno;
	u_int i;

	disk_debug(2, "disk_probe_slice (begin): %s (%s)\n",
		   dev->si_name, dp->d_cdev->si_name);

	sno = slice ? slice - 1 : 0;

	ops = &disklabel32_ops;
	msg = ops->op_readdisklabel(dev, sp, &sp->ds_label, info);
	if (msg && !strcmp(msg, "no disk label")) {
		ops = &disklabel64_ops;
		msg = ops->op_readdisklabel(dev, sp, &sp->ds_label, info);
	}

	if (msg == NULL) {
		if (slice != WHOLE_DISK_SLICE)
			ops->op_adjust_label_reserved(dp->d_slice, slice, sp);
		else
			sp->ds_reserved = 0;

		sp->ds_ops = ops;
		for (i = 0; i < ops->op_getnumparts(sp->ds_label); i++) {
			ops->op_loadpartinfo(sp->ds_label, i, &part);
			if (part.fstype) {
				if (reprobe &&
				    (ndev = devfs_find_device_by_name("%s%c",
						dev->si_name, 'a' + i))
				) {
					/*
					 * Device already exists and
					 * is still valid.
					 */
					ndev->si_flags |= SI_REPROBE_TEST;

					/*
					 * Destroy old UUID alias
					 */
					destroy_dev_alias(ndev, "part-by-uuid/*");

					/* Create UUID alias */
					if (!kuuid_is_nil(&part.storage_uuid)) {
						snprintf_uuid(uuid_buf,
						    sizeof(uuid_buf),
						    &part.storage_uuid);
						make_dev_alias(ndev,
						    "part-by-uuid/%s",
						    uuid_buf);
						udev_dict_set_cstr(ndev, "uuid", uuid_buf);
					}
				} else {
					ndev = make_dev_covering(&disk_ops, dp->d_rawdev->si_ops,
						dkmakeminor(dkunit(dp->d_cdev),
							    slice, i),
						UID_ROOT, GID_OPERATOR, 0640,
						"%s%c", dev->si_name, 'a'+ i);
					ndev->si_parent = dev;
					ndev->si_iosize_max = dev->si_iosize_max;
					ndev->si_disk = dp;
					udev_dict_set_cstr(ndev, "subsystem", "disk");
					/* Inherit parent's disk type */
					if (dp->d_disktype) {
						udev_dict_set_cstr(ndev, "disk-type",
						    __DECONST(char *, dp->d_disktype));
					}

					/* Create serno alias */
					if (dp->d_info.d_serialno) {
						make_dev_alias(ndev,
						    "serno/%s.s%d%c",
						    dp->d_info.d_serialno,
						    sno, 'a' + i);
					}

					/* Create UUID alias */
					if (!kuuid_is_nil(&part.storage_uuid)) {
						snprintf_uuid(uuid_buf,
						    sizeof(uuid_buf),
						    &part.storage_uuid);
						make_dev_alias(ndev,
						    "part-by-uuid/%s",
						    uuid_buf);
						udev_dict_set_cstr(ndev, "uuid", uuid_buf);
					}
					ndev->si_flags |= SI_REPROBE_TEST;
				}
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
		if (sp->ds_type == DOSPTYP_386BSD || /* XXX */
		    sp->ds_type == DOSPTYP_NETBSD ||
		    sp->ds_type == DOSPTYP_OPENBSD) {
			log(LOG_WARNING, "%s: cannot find label (%s)\n",
			    dev->si_name, msg);
		}

		if (sp->ds_label.opaque != NULL && sp->ds_ops != NULL) {
			/* Clear out old label - it's not around anymore */
			disk_debug(2,
			    "disk_probe_slice: clear out old diskabel on %s\n",
			    dev->si_name);

			sp->ds_ops->op_freedisklabel(&sp->ds_label);
			sp->ds_ops = NULL;
		}
	}

	if (msg == NULL) {
		sp->ds_wlabel = FALSE;
	}

	return (msg ? EINVAL : 0);
}

/*
 * This routine is only called for newly minted drives or to reprobe
 * a drive with no open slices.  disk_probe_slice() is called directly
 * when reprobing partition changes within slices.
 */
static void
disk_probe(struct disk *dp, int reprobe)
{
	struct disk_info *info = &dp->d_info;
	cdev_t dev = dp->d_cdev;
	cdev_t ndev;
	int error, i, sno;
	struct diskslices *osp;
	struct diskslice *sp;
	char uuid_buf[128];

	KKASSERT (info->d_media_blksize != 0);

	osp = dp->d_slice;
	dp->d_slice = dsmakeslicestruct(BASE_SLICE, info);
	disk_debug(1, "disk_probe (begin): %s\n", dp->d_cdev->si_name);

	error = mbrinit(dev, info, &(dp->d_slice));
	if (error) {
		dsgone(&osp);
		return;
	}

	for (i = 0; i < dp->d_slice->dss_nslices; i++) {
		/*
		 * Ignore the whole-disk slice, it has already been created.
		 */
		if (i == WHOLE_DISK_SLICE)
			continue;

#if 1
		/*
		 * Ignore the compatibility slice s0 if it's a device mapper
		 * volume.
		 */
		if ((i == COMPATIBILITY_SLICE) &&
		    (info->d_dsflags & DSO_DEVICEMAPPER))
			continue;
#endif

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

			/*
			 * Destroy old UUID alias
			 */
			destroy_dev_alias(ndev, "slice-by-uuid/*");

			/* Create UUID alias */
			if (!kuuid_is_nil(&sp->ds_stor_uuid)) {
				snprintf_uuid(uuid_buf, sizeof(uuid_buf),
				    &sp->ds_stor_uuid);
				make_dev_alias(ndev, "slice-by-uuid/%s",
				    uuid_buf);
			}
		} else {
			/*
			 * Else create new device
			 */
			ndev = make_dev_covering(&disk_ops, dp->d_rawdev->si_ops,
					dkmakewholeslice(dkunit(dev), i),
					UID_ROOT, GID_OPERATOR, 0640,
					(info->d_dsflags & DSO_DEVICEMAPPER)?
					"%s.s%d" : "%ss%d", dev->si_name, sno);
			ndev->si_parent = dev;
			ndev->si_iosize_max = dev->si_iosize_max;
			udev_dict_set_cstr(ndev, "subsystem", "disk");
			/* Inherit parent's disk type */
			if (dp->d_disktype) {
				udev_dict_set_cstr(ndev, "disk-type",
				    __DECONST(char *, dp->d_disktype));
			}

			/* Create serno alias */
			if (dp->d_info.d_serialno) {
				make_dev_alias(ndev, "serno/%s.s%d",
					       dp->d_info.d_serialno, sno);
			}

			/* Create UUID alias */
			if (!kuuid_is_nil(&sp->ds_stor_uuid)) {
				snprintf_uuid(uuid_buf, sizeof(uuid_buf),
				    &sp->ds_stor_uuid);
				make_dev_alias(ndev, "slice-by-uuid/%s",
				    uuid_buf);
			}

			ndev->si_disk = dp;
			ndev->si_flags |= SI_REPROBE_TEST;
		}
		sp->ds_dev = ndev;

		/*
		 * Probe appropriate slices for a disklabel
		 *
		 * XXX slice type 1 used by our gpt probe code.
		 * XXX slice type 0 used by mbr compat slice.
		 */
		if (sp->ds_type == DOSPTYP_386BSD ||
		    sp->ds_type == DOSPTYP_NETBSD ||
		    sp->ds_type == DOSPTYP_OPENBSD ||
		    sp->ds_type == 0 ||
		    sp->ds_type == 1) {
			if (dp->d_slice->dss_first_bsd_slice == 0)
				dp->d_slice->dss_first_bsd_slice = i;
			disk_probe_slice(dp, ndev, i, reprobe);
		}
	}
	dsgone(&osp);
	disk_debug(1, "disk_probe (end): %s\n", dp->d_cdev->si_name);
}


static void
disk_msg_core(void *arg)
{
	struct disk	*dp;
	struct diskslice *sp;
	disk_msg_t msg;
	int run;

	lwkt_gettoken(&disklist_token);
	lwkt_initport_thread(&disk_msg_port, curthread);
	wakeup(curthread);	/* synchronous startup */
	lwkt_reltoken(&disklist_token);

	get_mplock();	/* not mpsafe yet? */
	run = 1;

	while (run) {
		msg = (disk_msg_t)lwkt_waitport(&disk_msg_port, 0);

		switch (msg->hdr.u.ms_result) {
		case DISK_DISK_PROBE:
			dp = (struct disk *)msg->load;
			disk_debug(1,
				    "DISK_DISK_PROBE: %s\n",
					dp->d_cdev->si_name);
			disk_iocom_update(dp);
			disk_probe(dp, 0);
			break;
		case DISK_DISK_DESTROY:
			dp = (struct disk *)msg->load;
			disk_debug(1,
				    "DISK_DISK_DESTROY: %s\n",
					dp->d_cdev->si_name);
			disk_iocom_uninit(dp);

			/*
			 * Interlock against struct disk enumerations.
			 * Wait for enumerations to complete then remove
			 * the dp from the list before tearing it down.
			 *
			 * This avoids races against e.g.
			 * dsched_thread_io_alloc().
			 */
			lwkt_gettoken(&disklist_token);
			while (dp->d_refs)
				tsleep(&dp->d_refs, 0, "diskdel", hz / 10);
			LIST_REMOVE(dp, d_list);

			dsched_disk_destroy_callback(dp);
			devfs_destroy_related(dp->d_cdev);
			destroy_dev(dp->d_cdev);
			destroy_only_dev(dp->d_rawdev);

			lwkt_reltoken(&disklist_token);

			if (dp->d_info.d_serialno) {
				kfree(dp->d_info.d_serialno, M_TEMP);
				dp->d_info.d_serialno = NULL;
			}
			break;
		case DISK_UNPROBE:
			dp = (struct disk *)msg->load;
			disk_debug(1,
				    "DISK_DISK_UNPROBE: %s\n",
					dp->d_cdev->si_name);
			devfs_destroy_related(dp->d_cdev);
			break;
		case DISK_SLICE_REPROBE:
			dp = (struct disk *)msg->load;
			sp = (struct diskslice *)msg->load2;
			devfs_clr_related_flag(sp->ds_dev,
						SI_REPROBE_TEST);
			disk_debug(1,
				    "DISK_SLICE_REPROBE: %s\n",
				    sp->ds_dev->si_name);
			disk_probe_slice(dp, sp->ds_dev,
					 dkslice(sp->ds_dev), 1);
			devfs_destroy_related_without_flag(
					sp->ds_dev, SI_REPROBE_TEST);
			break;
		case DISK_DISK_REPROBE:
			dp = (struct disk *)msg->load;
			devfs_clr_related_flag(dp->d_cdev, SI_REPROBE_TEST);
			disk_debug(1,
				    "DISK_DISK_REPROBE: %s\n",
				    dp->d_cdev->si_name);
			disk_probe(dp, 1);
			devfs_destroy_related_without_flag(
					dp->d_cdev, SI_REPROBE_TEST);
			break;
		case DISK_SYNC:
			disk_debug(1, "DISK_SYNC\n");
			break;
		default:
			devfs_debug(DEVFS_DEBUG_WARNING,
				    "disk_msg_core: unknown message "
				    "received at core\n");
			break;
		}
		lwkt_replymsg(&msg->hdr, 0);
	}
	lwkt_exit();
}


/*
 * Acts as a message drain. Any message that is replied to here gets
 * destroyed and the memory freed.
 */
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
	lwkt_sendmsg(port, &disk_msg->hdr);
}

void
disk_msg_send_sync(uint32_t cmd, void *load, void *load2)
{
	struct lwkt_port rep_port;
	disk_msg_t disk_msg;
	lwkt_port_t port;

	disk_msg = objcache_get(disk_msg_cache, M_WAITOK);
	port = &disk_msg_port;

	/* XXX could probably use curthread's built-in msgport */
	lwkt_initport_thread(&rep_port, curthread);
	lwkt_initmsg(&disk_msg->hdr, &rep_port, 0);

	disk_msg->hdr.u.ms_result = cmd;
	disk_msg->load = load;
	disk_msg->load2 = load2;

	lwkt_sendmsg(port, &disk_msg->hdr);
	lwkt_waitmsg(&disk_msg->hdr, 0);
	objcache_put(disk_msg_cache, disk_msg);
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
	return _disk_create_named(NULL, unit, dp, raw_ops, 0);
}

cdev_t
disk_create_clone(int unit, struct disk *dp,
		  struct dev_ops *raw_ops)
{
	return _disk_create_named(NULL, unit, dp, raw_ops, 1);
}

cdev_t
disk_create_named(const char *name, int unit, struct disk *dp,
		  struct dev_ops *raw_ops)
{
	return _disk_create_named(name, unit, dp, raw_ops, 0);
}

cdev_t
disk_create_named_clone(const char *name, int unit, struct disk *dp,
			struct dev_ops *raw_ops)
{
	return _disk_create_named(name, unit, dp, raw_ops, 1);
}

static cdev_t
_disk_create_named(const char *name, int unit, struct disk *dp,
		   struct dev_ops *raw_ops, int clone)
{
	cdev_t rawdev;

	disk_debug(1, "disk_create (begin): %s%d\n", name, unit);

	if (name) {
		rawdev = make_only_dev(raw_ops, dkmakewholedisk(unit),
		    UID_ROOT, GID_OPERATOR, 0640, "%s", name);
	} else {
		rawdev = make_only_dev(raw_ops, dkmakewholedisk(unit),
		    UID_ROOT, GID_OPERATOR, 0640,
		    "%s%d", raw_ops->head.name, unit);
	}

	bzero(dp, sizeof(*dp));

	dp->d_rawdev = rawdev;
	dp->d_raw_ops = raw_ops;
	dp->d_dev_ops = &disk_ops;

	if (name) {
		if (clone) {
			dp->d_cdev = make_only_dev_covering(
					&disk_ops, dp->d_rawdev->si_ops,
					dkmakewholedisk(unit),
					UID_ROOT, GID_OPERATOR, 0640,
					"%s", name);
		} else {
			dp->d_cdev = make_dev_covering(
					&disk_ops, dp->d_rawdev->si_ops,
					dkmakewholedisk(unit),
					UID_ROOT, GID_OPERATOR, 0640,
					"%s", name);
		}
	} else {
		if (clone) {
			dp->d_cdev = make_only_dev_covering(
					&disk_ops, dp->d_rawdev->si_ops,
					dkmakewholedisk(unit),
					UID_ROOT, GID_OPERATOR, 0640,
					"%s%d", raw_ops->head.name, unit);
		} else {
			dp->d_cdev = make_dev_covering(
					&disk_ops, dp->d_rawdev->si_ops,
					dkmakewholedisk(unit),
					UID_ROOT, GID_OPERATOR, 0640,
					"%s%d", raw_ops->head.name, unit);
		}
	}

	udev_dict_set_cstr(dp->d_cdev, "subsystem", "disk");
	dp->d_cdev->si_disk = dp;

	if (name)
		dsched_disk_create_callback(dp, name, unit);
	else
		dsched_disk_create_callback(dp, raw_ops->head.name, unit);

	lwkt_gettoken(&disklist_token);
	LIST_INSERT_HEAD(&disklist, dp, d_list);
	lwkt_reltoken(&disklist_token);

	disk_iocom_init(dp);

	disk_debug(1, "disk_create (end): %s%d\n",
		   (name != NULL)?(name):(raw_ops->head.name), unit);

	return (dp->d_rawdev);
}

int
disk_setdisktype(struct disk *disk, const char *type)
{
	int error;

	KKASSERT(disk != NULL);

	disk->d_disktype = type;
	error = udev_dict_set_cstr(disk->d_cdev, "disk-type",
				   __DECONST(char *, type));
	return error;
}

int
disk_getopencount(struct disk *disk)
{
	return disk->d_opencount;
}

static void
_setdiskinfo(struct disk *disk, struct disk_info *info)
{
	char *oldserialno;

	oldserialno = disk->d_info.d_serialno;
	bcopy(info, &disk->d_info, sizeof(disk->d_info));
	info = &disk->d_info;

	disk_debug(1, "_setdiskinfo: %s\n", disk->d_cdev->si_name);

	/*
	 * The serial number is duplicated so the caller can throw
	 * their copy away.
	 */
	if (info->d_serialno && info->d_serialno[0] &&
	    (info->d_serialno[0] != ' ' || strlen(info->d_serialno) > 1)) {
		info->d_serialno = kstrdup(info->d_serialno, M_TEMP);
		disk_cleanserial(info->d_serialno);
		if (disk->d_cdev) {
			make_dev_alias(disk->d_cdev, "serno/%s",
				       info->d_serialno);
		}
	} else {
		info->d_serialno = NULL;
	}
	if (oldserialno)
		kfree(oldserialno, M_TEMP);

	dsched_disk_update_callback(disk, info);

	/*
	 * The caller may set d_media_size or d_media_blocks and we
	 * calculate the other.
	 */
	KKASSERT(info->d_media_size == 0 || info->d_media_blocks == 0);
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

	/* Add the serial number to the udev_dictionary */
	if (info->d_serialno)
		udev_dict_set_cstr(disk->d_cdev, "serno", info->d_serialno);
}

/*
 * Disk drivers must call this routine when media parameters are available
 * or have changed.
 */
void
disk_setdiskinfo(struct disk *disk, struct disk_info *info)
{
	_setdiskinfo(disk, info);
	disk_msg_send(DISK_DISK_PROBE, disk, NULL);
	disk_debug(1, "disk_setdiskinfo: sent probe for %s\n",
		   disk->d_cdev->si_name);
}

void
disk_setdiskinfo_sync(struct disk *disk, struct disk_info *info)
{
	_setdiskinfo(disk, info);
	disk_msg_send_sync(DISK_DISK_PROBE, disk, NULL);
	disk_debug(1, "disk_setdiskinfo_sync: sent probe for %s\n",
		   disk->d_cdev->si_name);
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
disk_dumpcheck(cdev_t dev, u_int64_t *size,
	       u_int64_t *blkno, u_int32_t *secsize)
{
	struct partinfo pinfo;
	int error;

	bzero(&pinfo, sizeof(pinfo));
	error = dev_dioctl(dev, DIOCGPART, (void *)&pinfo, 0,
			   proc0.p_ucred, NULL, NULL);
	if (error)
		return (error);

	if (pinfo.media_blksize == 0)
		return (ENXIO);

	if (blkno) /* XXX: make sure this reserved stuff is right */
		*blkno = pinfo.reserved_blocks +
			pinfo.media_offset / pinfo.media_blksize;
	if (secsize)
		*secsize = pinfo.media_blksize;
	if (size)
		*size = (pinfo.media_blocks - pinfo.reserved_blocks);

	return (0);
}

int
disk_dumpconf(cdev_t dev, u_int onoff)
{
	struct dumperinfo di;
	u_int64_t	size, blkno;
	u_int32_t	secsize;
	int error;

	if (!onoff)
		return set_dumper(NULL);

	error = disk_dumpcheck(dev, &size, &blkno, &secsize);

	if (error)
		return ENXIO;

	bzero(&di, sizeof(struct dumperinfo));
	di.dumper = diskdump;
	di.priv = dev;
	di.blocksize = secsize;
	di.maxiosize = dev->si_iosize_max;
	di.mediaoffset = blkno * DEV_BSIZE;
	di.mediasize = size * DEV_BSIZE;

	return set_dumper(&di);
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
	dsgone(&disk->d_slice);
}

/*
 * Enumerate disks, pass a marker and an initial NULL dp to initialize,
 * then loop with the previously returned dp.
 *
 * The returned dp will be referenced, preventing its destruction.  When
 * you pass the returned dp back into the loop the ref is dropped.
 *
 * WARNING: If terminating your loop early you must call
 *	    disk_enumerate_stop().
 */
struct disk *
disk_enumerate(struct disk *marker, struct disk *dp)
{
	lwkt_gettoken(&disklist_token);
	if (dp) {
		--dp->d_refs;
		dp = LIST_NEXT(marker, d_list);
		LIST_REMOVE(marker, d_list);
	} else {
		bzero(marker, sizeof(*marker));
		marker->d_flags = DISKFLAG_MARKER;
		dp = LIST_FIRST(&disklist);
	}
	while (dp) {
		if ((dp->d_flags & DISKFLAG_MARKER) == 0)
			break;
		dp = LIST_NEXT(dp, d_list);
	}
	if (dp) {
		++dp->d_refs;
		LIST_INSERT_AFTER(dp, marker, d_list);
	}
	lwkt_reltoken(&disklist_token);
	return (dp);
}

/*
 * Terminate an enumeration early.  Do not call this function if the
 * enumeration ended normally.  dp can be NULL, indicating that you
 * wish to retain the ref count on dp.
 *
 * This function removes the marker.
 */
void
disk_enumerate_stop(struct disk *marker, struct disk *dp)
{
	lwkt_gettoken(&disklist_token);
	LIST_REMOVE(marker, d_list);
	if (dp)
		--dp->d_refs;
	lwkt_reltoken(&disklist_token);
}

static
int
sysctl_disks(SYSCTL_HANDLER_ARGS)
{
	struct disk marker;
	struct disk *dp;
	int error, first;

	first = 1;
	error = 0;
	dp = NULL;

	while ((dp = disk_enumerate(&marker, dp))) {
		if (!first) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error) {
				disk_enumerate_stop(&marker, dp);
				break;
			}
		} else {
			first = 0;
		}
		error = SYSCTL_OUT(req, dp->d_rawdev->si_name,
				   strlen(dp->d_rawdev->si_name));
		if (error) {
			disk_enumerate_stop(&marker, dp);
			break;
		}
	}
	if (error == 0)
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

	/*
	 * dp can't be NULL here XXX.
	 *
	 * d_slice will be NULL if setdiskinfo() has not been called yet.
	 * setdiskinfo() is typically called whether the disk is present
	 * or not (e.g. CD), but the base disk device is created first
	 * and there may be a race.
	 */
	dp = dev->si_disk;
	if (dp == NULL || dp->d_slice == NULL)
		return (ENXIO);
	error = 0;

	/*
	 * Deal with open races
	 */
	get_mplock();
	while (dp->d_flags & DISKFLAG_LOCK) {
		dp->d_flags |= DISKFLAG_WANTED;
		error = tsleep(dp, PCATCH, "diskopen", hz);
		if (error) {
			rel_mplock();
			return (error);
		}
	}
	dp->d_flags |= DISKFLAG_LOCK;

	/*
	 * Open the underlying raw device.
	 */
	if (!dsisopen(dp->d_slice)) {
#if 0
		if (!pdev->si_iosize_max)
			pdev->si_iosize_max = dev->si_iosize_max;
#endif
		error = dev_dopen(dp->d_rawdev, ap->a_oflags,
				  ap->a_devtype, ap->a_cred, NULL);
	}

	if (error)
		goto out;
	error = dsopen(dev, ap->a_devtype, dp->d_info.d_dsflags,
		       &dp->d_slice, &dp->d_info);
	if (!dsisopen(dp->d_slice)) {
		dev_dclose(dp->d_rawdev, ap->a_oflags, ap->a_devtype, NULL);
	}
out:
	dp->d_flags &= ~DISKFLAG_LOCK;
	if (dp->d_flags & DISKFLAG_WANTED) {
		dp->d_flags &= ~DISKFLAG_WANTED;
		wakeup(dp);
	}
	rel_mplock();

	KKASSERT(dp->d_opencount >= 0);
	/* If the open was successful, bump open count */
	if (error == 0)
		atomic_add_int(&dp->d_opencount, 1);

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
	int lcount;

	error = 0;
	dp = dev->si_disk;

	/*
	 * The cdev_t represents the disk/slice/part.  The shared
	 * dp structure governs all cdevs associated with the disk.
	 *
	 * As a safety only close the underlying raw device on the last
	 * close the disk device if our tracking of the slices/partitions
	 * also indicates nothing is open.
	 */
	KKASSERT(dp->d_opencount >= 1);
	lcount = atomic_fetchadd_int(&dp->d_opencount, -1);

	get_mplock();
	dsclose(dev, ap->a_devtype, dp->d_slice);
	if (lcount <= 1 && !dsisopen(dp->d_slice)) {
		error = dev_dclose(dp->d_rawdev, ap->a_fflag, ap->a_devtype, NULL);
	}
	rel_mplock();
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
	u_int u;

	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);

	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "diskioctl: cmd is: %lx (name: %s)\n",
		    ap->a_cmd, dev->si_name);
	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "diskioctl: &dp->d_slice is: %p, %p\n",
		    &dp->d_slice, dp->d_slice);

	if (ap->a_cmd == DIOCGKERNELDUMP) {
		u = *(u_int *)ap->a_data;
		return disk_dumpconf(dev, u);
	}

	if (ap->a_cmd == DIOCRECLUSTER && dev == dp->d_cdev) {
		error = disk_iocom_ioctl(dp, ap->a_cmd, ap->a_data);
		return error;
	}

	if (&dp->d_slice == NULL || dp->d_slice == NULL ||
	    ((dp->d_info.d_dsflags & DSO_DEVICEMAPPER) &&
	     dkslice(dev) == WHOLE_DISK_SLICE)) {
		error = ENOIOCTL;
	} else {
		get_mplock();
		error = dsioctl(dev, ap->a_cmd, ap->a_data, ap->a_fflag,
				&dp->d_slice, &dp->d_info);
		rel_mplock();
	}

	if (error == ENOIOCTL) {
		error = dev_dioctl(dp->d_rawdev, ap->a_cmd, ap->a_data,
				   ap->a_fflag, ap->a_cred, NULL, NULL);
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
		dsched_queue(dp, nbio);
	} else {
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

	if ((ap->a_result == -1) &&
	   (dp->d_info.d_dsflags & DSO_RAWPSIZE)) {
		ap->a_head.a_dev = dp->d_rawdev;
		return dev_doperate(&ap->a_head);
	}
	return(0);
}

static int
diskdump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct disk *dp = dev->si_disk;
	u_int64_t size, offset;
	int error;

	error = disk_dumpcheck(dev, &size, &ap->a_blkno, &ap->a_secsize);
	/* XXX: this should probably go in disk_dumpcheck somehow */
	if (ap->a_length != 0) {
		size *= DEV_BSIZE;
		offset = ap->a_blkno * DEV_BSIZE;
		if ((ap->a_offset < offset) ||
		    (ap->a_offset + ap->a_length - offset > size)) {
			kprintf("Attempt to write outside dump "
				"device boundaries.\n");
			error = ENOSPC;
		}
	}

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
 * Reorder interval for burst write allowance and minor write
 * allowance.
 *
 * We always want to trickle some writes in to make use of the
 * disk's zone cache.  Bursting occurs on a longer interval and only
 * runningbufspace is well over the hirunningspace limit.
 */
int bioq_reorder_burst_interval = 60;	/* should be multiple of minor */
SYSCTL_INT(_kern, OID_AUTO, bioq_reorder_burst_interval,
	   CTLFLAG_RW, &bioq_reorder_burst_interval, 0, "");
int bioq_reorder_minor_interval = 5;
SYSCTL_INT(_kern, OID_AUTO, bioq_reorder_minor_interval,
	   CTLFLAG_RW, &bioq_reorder_minor_interval, 0, "");

int bioq_reorder_burst_bytes = 3000000;
SYSCTL_INT(_kern, OID_AUTO, bioq_reorder_burst_bytes,
	   CTLFLAG_RW, &bioq_reorder_burst_bytes, 0, "");
int bioq_reorder_minor_bytes = 262144;
SYSCTL_INT(_kern, OID_AUTO, bioq_reorder_minor_bytes,
	   CTLFLAG_RW, &bioq_reorder_minor_bytes, 0, "");


/*
 * Order I/Os.  Generally speaking this code is designed to make better
 * use of drive zone caches.  A drive zone cache can typically track linear
 * reads or writes for around 16 zones simultaniously.
 *
 * Read prioritization issues:  It is possible for hundreds of megabytes worth
 * of writes to be queued asynchronously.  This creates a huge bottleneck
 * for reads which reduce read bandwidth to a trickle.
 *
 * To solve this problem we generally reorder reads before writes.
 *
 * However, a large number of random reads can also starve writes and
 * make poor use of the drive zone cache so we allow writes to trickle
 * in every N reads.
 */
void
bioqdisksort(struct bio_queue_head *bioq, struct bio *bio)
{
	/*
	 * The BIO wants to be ordered.  Adding to the tail also
	 * causes transition to be set to NULL, forcing the ordering
	 * of all prior I/O's.
	 */
	if (bio->bio_buf->b_flags & B_ORDERED) {
		bioq_insert_tail(bioq, bio);
		return;
	}

	switch(bio->bio_buf->b_cmd) {
	case BUF_CMD_READ:
		if (bioq->transition) {
			/*
			 * Insert before the first write.  Bleedover writes
			 * based on reorder intervals to prevent starvation.
			 */
			TAILQ_INSERT_BEFORE(bioq->transition, bio, bio_act);
			++bioq->reorder;
			if (bioq->reorder % bioq_reorder_minor_interval == 0) {
				bioqwritereorder(bioq);
				if (bioq->reorder >=
				    bioq_reorder_burst_interval) {
					bioq->reorder = 0;
				}
			}
		} else {
			/*
			 * No writes queued (or ordering was forced),
			 * insert at tail.
			 */
			TAILQ_INSERT_TAIL(&bioq->queue, bio, bio_act);
		}
		break;
	case BUF_CMD_WRITE:
		/*
		 * Writes are always appended.  If no writes were previously
		 * queued or an ordered tail insertion occured the transition
		 * field will be NULL.
		 */
		TAILQ_INSERT_TAIL(&bioq->queue, bio, bio_act);
		if (bioq->transition == NULL)
			bioq->transition = bio;
		break;
	default:
		/*
		 * All other request types are forced to be ordered.
		 */
		bioq_insert_tail(bioq, bio);
		break;
	}
}

/*
 * Move the read-write transition point to prevent reads from
 * completely starving our writes.  This brings a number of writes into
 * the fold every N reads.
 *
 * We bring a few linear writes into the fold on a minor interval
 * and we bring a non-linear burst of writes into the fold on a major
 * interval.  Bursting only occurs if runningbufspace is really high
 * (typically from syncs, fsyncs, or HAMMER flushes).
 */
static
void
bioqwritereorder(struct bio_queue_head *bioq)
{
	struct bio *bio;
	off_t next_offset;
	size_t left;
	size_t n;
	int check_off;

	if (bioq->reorder < bioq_reorder_burst_interval ||
	    !buf_runningbufspace_severe()) {
		left = (size_t)bioq_reorder_minor_bytes;
		check_off = 1;
	} else {
		left = (size_t)bioq_reorder_burst_bytes;
		check_off = 0;
	}

	next_offset = bioq->transition->bio_offset;
	while ((bio = bioq->transition) != NULL &&
	       (check_off == 0 || next_offset == bio->bio_offset)
	) {
		n = bio->bio_buf->b_bcount;
		next_offset = bio->bio_offset + n;
		bioq->transition = TAILQ_NEXT(bio, bio_act);
		if (left < n)
			break;
		left -= n;
	}
}

/*
 * Bounds checking against the media size, used for the raw partition.
 * secsize, mediasize and b_blkno must all be the same units.
 * Possibly this has to be DEV_BSIZE (512).
 */
int
bounds_check_with_mediasize(struct bio *bio, int secsize, uint64_t mediasize)
{
	struct buf *bp = bio->bio_buf;
	int64_t sz;

	sz = howmany(bp->b_bcount, secsize);

	if (bio->bio_offset/DEV_BSIZE + sz > mediasize) {
		sz = mediasize - bio->bio_offset/DEV_BSIZE;
		if (sz == 0) {
			/* If exactly at end of disk, return EOF. */
			bp->b_resid = bp->b_bcount;
			return 0;
		}
		if (sz < 0) {
			/* If past end of disk, return EINVAL. */
			bp->b_error = EINVAL;
			return 0;
		}
		/* Otherwise, truncate request. */
		bp->b_bcount = sz * secsize;
	}

	return 1;
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
	return devfs_find_device_by_name("%s", devname);
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

	disk_msg_cache = objcache_create("disk-msg-cache", 0, 0,
					 NULL, NULL, NULL,
					 objcache_malloc_alloc,
					 objcache_malloc_free,
					 &disk_msg_malloc_args);

	lwkt_token_init(&disklist_token, "disks");

	/*
	 * Initialize the reply-only port which acts as a message drain
	 */
	lwkt_initport_replyonly(&disk_dispose_port, disk_msg_autofree_reply);

	lwkt_gettoken(&disklist_token);
	lwkt_create(disk_msg_core, /*args*/NULL, &td_core, NULL,
		    0, -1, "disk_msg_core");
	tsleep(td_core, 0, "diskcore", 0);
	lwkt_reltoken(&disklist_token);
}

static void
disk_uninit(void)
{
	objcache_destroy(disk_msg_cache);
}

/*
 * Clean out illegal characters in serial numbers.
 */
static void
disk_cleanserial(char *serno)
{
	char c;

	while ((c = *serno) != 0) {
		if (c >= 'a' && c <= 'z')
			;
		else if (c >= 'A' && c <= 'Z')
			;
		else if (c >= '0' && c <= '9')
			;
		else if (c == '-' || c == '@' || c == '+' || c == '.')
			;
		else
			c = '_';
		*serno++= c;
	}
}

TUNABLE_INT("kern.disk_debug", &disk_debug_enable);
SYSCTL_INT(_kern, OID_AUTO, disk_debug, CTLFLAG_RW, &disk_debug_enable,
	   0, "Enable subr_disk debugging");

SYSINIT(disk_register, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, disk_init, NULL);
SYSUNINIT(disk_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, disk_uninit, NULL);
