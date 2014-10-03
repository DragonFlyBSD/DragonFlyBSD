/*        $NetBSD: device-mapper.c,v 1.22 2010/03/26 15:46:04 jakllsch Exp $ */

/*
 * Copyright (c) 2010-2011 Alex Hornung <alex@alexhornung.com>
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Hamsik.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * I want to say thank you to all people who helped me with this project.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ctype.h>

#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/dtype.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <dev/disk/dm/dm.h>

#include "netbsd-dm.h"

static	d_ioctl_t	dmioctl;
static	d_open_t	dmopen;
static	d_close_t	dmclose;
static	d_psize_t	dmsize;
static	d_strategy_t	dmstrategy;
static	d_dump_t	dmdump;

/* attach and detach routines */
void dmattach(int);
static int dm_modcmd(module_t mod, int cmd, void *unused);
static int dmdestroy(void);

static void dm_doinit(void);

static int dm_cmd_to_fun(prop_dictionary_t);
static int disk_ioctl_switch(cdev_t, u_long, void *);
static int dm_ioctl_switch(u_long);
#if 0
static void dmminphys(struct buf *);
#endif

/* ***Variable-definitions*** */
struct dev_ops dm_ops = {
	{ "dm", 0, D_DISK | D_MPSAFE },
	.d_open		= dmopen,
	.d_close	= dmclose,
	.d_read		= physread,
	.d_write	= physwrite,
	.d_ioctl	= dmioctl,
	.d_strategy	= dmstrategy,
	.d_psize	= dmsize,
	.d_dump		= dmdump,
/* D_DISK */
};

MALLOC_DEFINE(M_DM, "dm", "Device Mapper allocations");

int dm_debug_level = 0;

extern uint64_t dm_dev_counter;

static cdev_t dmcdev;

static moduledata_t dm_mod = {
    "dm",
    dm_modcmd,
    NULL
};
DECLARE_MODULE(dm, dm_mod, SI_SUB_RAID, SI_ORDER_ANY);
MODULE_VERSION(dm, 1);

/*
 * This array is used to translate cmd to function pointer.
 *
 * Interface between libdevmapper and lvm2tools uses different
 * names for one IOCTL call because libdevmapper do another thing
 * then. When I run "info" or "mknodes" libdevmapper will send same
 * ioctl to kernel but will do another things in userspace.
 *
 */
static struct cmd_function cmd_fn[] = {
		{ .cmd = "version", .fn = dm_get_version_ioctl},
		{ .cmd = "targets", .fn = dm_list_versions_ioctl},
		{ .cmd = "create",  .fn = dm_dev_create_ioctl},
		{ .cmd = "info",    .fn = dm_dev_status_ioctl},
		{ .cmd = "mknodes", .fn = dm_dev_status_ioctl},
		{ .cmd = "names",   .fn = dm_dev_list_ioctl},
		{ .cmd = "suspend", .fn = dm_dev_suspend_ioctl},
		{ .cmd = "remove",  .fn = dm_dev_remove_ioctl},
		{ .cmd = "remove_all", .fn = dm_dev_remove_all_ioctl},
		{ .cmd = "rename",  .fn = dm_dev_rename_ioctl},
		{ .cmd = "resume",  .fn = dm_dev_resume_ioctl},
		{ .cmd = "clear",   .fn = dm_table_clear_ioctl},
		{ .cmd = "deps",    .fn = dm_table_deps_ioctl},
		{ .cmd = "reload",  .fn = dm_table_load_ioctl},
		{ .cmd = "status",  .fn = dm_table_status_ioctl},
		{ .cmd = "table",   .fn = dm_table_status_ioctl},
		{ .cmd = "message", .fn = dm_message_ioctl},
		{NULL, NULL}
};

/* New module handle routine */
static int
dm_modcmd(module_t mod, int cmd, void *unused)
{
	int error;

	error = 0;

	switch (cmd) {
	case MOD_LOAD:
		dm_doinit();
		kprintf("Device Mapper version %d.%d.%d loaded\n",
		    DM_VERSION_MAJOR, DM_VERSION_MINOR, DM_VERSION_PATCHLEVEL);
		break;

	case MOD_UNLOAD:
		/*
		 * Disable unloading of dm module if there are any devices
		 * defined in driver. This is probably too strong we need
		 * to disable auto-unload only if there is mounted dm device
		 * present.
		 */
		if (dm_dev_counter > 0)
			return EBUSY;

		error = dmdestroy();
		if (error)
			break;
		kprintf("Device Mapper unloaded\n");
		break;

	default:
		break;
	}

	return error;
}

static void
dm_doinit(void)
{
	dm_target_init();
	dm_dev_init();
	dm_pdev_init();
	dmcdev = make_dev(&dm_ops, 0, UID_ROOT, GID_OPERATOR, 0640, "mapper/control");
}

/* Destroy routine */
static int
dmdestroy(void)
{
	destroy_dev(dmcdev);

	dm_dev_uninit();
	dm_pdev_uninit();
	dm_target_uninit();

	return 0;
}

static int
dmopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	dm_dev_t *dmv;

	/* Shortcut for the control device */
	if (minor(dev) == 0)
		return 0;

	if ((dmv = dm_dev_lookup(NULL, NULL, minor(dev))) == NULL)
		return ENXIO;

	dmv->is_open = 1;
	dm_dev_unbusy(dmv);

	aprint_debug("dm open routine called %" PRIu32 "\n",
	    minor(ap->a_head.a_dev));
	return 0;
}

static int
dmclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	dm_dev_t *dmv;

	/* Shortcut for the control device */
	if (minor(dev) == 0)
		return 0;

	if ((dmv = dm_dev_lookup(NULL, NULL, minor(dev))) == NULL)
		return ENXIO;

	dmv->is_open = 0;
	dm_dev_unbusy(dmv);

	aprint_debug("dm close routine called %" PRIu32 "\n",
	    minor(ap->a_head.a_dev));
	return 0;
}


static int
dmioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_long cmd = ap->a_cmd;
	void *data = ap->a_data;

	int r, err;
	prop_dictionary_t dm_dict_in;

	err = r = 0;

	aprint_debug("dmioctl called\n");

	KKASSERT(data != NULL);

	if (( r = disk_ioctl_switch(dev, cmd, data)) == ENOTTY) {
		struct plistref *pref = (struct plistref *) data;

		/* Check if we were called with NETBSD_DM_IOCTL ioctl
		   otherwise quit. */
		if ((r = dm_ioctl_switch(cmd)) != 0)
			return r;

		if((r = prop_dictionary_copyin_ioctl(pref, cmd, &dm_dict_in)) != 0)
			return r;

		if ((r = dm_check_version(dm_dict_in)) != 0)
			goto cleanup_exit;

		/* run ioctl routine */
		if ((err = dm_cmd_to_fun(dm_dict_in)) != 0)
			goto cleanup_exit;

cleanup_exit:
		r = prop_dictionary_copyout_ioctl(pref, cmd, dm_dict_in);
		prop_object_release(dm_dict_in);
	}

	/*
	 * Return the error of the actual command if one one has
	 * happened. Otherwise return 'r' which indicates errors
	 * that occurred during helper operations.
	 */
	return (err != 0)?err:r;
}

/*
 * Translate command sent from libdevmapper to func.
 */
static int
dm_cmd_to_fun(prop_dictionary_t dm_dict){
	int i, r;
	prop_string_t command;

	r = 0;

	if ((command = prop_dictionary_get(dm_dict, DM_IOCTL_COMMAND)) == NULL)
		return EINVAL;

	for(i = 0; cmd_fn[i].cmd != NULL; i++)
		if (prop_string_equals_cstring(command, cmd_fn[i].cmd))
			break;

	if (cmd_fn[i].cmd == NULL)
		return EINVAL;

	aprint_debug("ioctl %s called\n", cmd_fn[i].cmd);
	r = cmd_fn[i].fn(dm_dict);

	return r;
}

/* Call apropriate ioctl handler function. */
static int
dm_ioctl_switch(u_long cmd)
{

	switch(cmd) {

	case NETBSD_DM_IOCTL:
		aprint_debug("dm NetBSD_DM_IOCTL called\n");
		break;
	default:
		 aprint_debug("dm unknown ioctl called\n");
		 return ENOTTY;
		 break; /* NOT REACHED */
	}

	 return 0;
}

 /*
  * Check for disk specific ioctls.
  */

static int
disk_ioctl_switch(cdev_t dev, u_long cmd, void *data)
{
	dm_dev_t *dmv;

	/* disk ioctls make sense only on block devices */
	if (minor(dev) == 0)
		return ENOTTY;

	switch(cmd) {
	case DIOCGPART:
	{
		struct partinfo *dpart;
		u_int64_t size;
		dpart = data;
		bzero(dpart, sizeof(*dpart));

		if ((dmv = dev->si_drv1) == NULL)
			return ENODEV;
		if (dmv->diskp->d_info.d_media_blksize == 0) {
			return ENOTSUP;
		} else {
			size = dm_table_size(&dmv->table_head);
			dpart->media_offset  = 0;
			dpart->media_size    = size * DEV_BSIZE;
			dpart->media_blocks  = size;
			dpart->media_blksize = DEV_BSIZE;
			dpart->fstype = FS_BSDFFS;
		}
		break;
	}

	default:
		aprint_debug("unknown disk_ioctl called\n");
		return ENOTTY;
		break; /* NOT REACHED */
	}

	return 0;
}

/*
 * Do all IO operations on dm logical devices.
 */
static int
dmstrategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	int bypass;

	dm_dev_t *dmv;
	dm_table_t  *tbl;
	dm_table_entry_t *table_en;
	struct buf *nestbuf;

	uint64_t buf_start, buf_len, issued_len;
	uint64_t table_start, table_end;
	uint64_t start, end;

	buf_start = bio->bio_offset;
	buf_len = bp->b_bcount;

	tbl = NULL;

	table_end = 0;
	issued_len = 0;

	dmv = dev->si_drv1;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
	case BUF_CMD_WRITE:
	case BUF_CMD_FREEBLKS:
		bypass = 0;
		break;
	case BUF_CMD_FLUSH:
		bypass = 1;
		KKASSERT(buf_len == 0);
		break;
	default:
		bp->b_error = EIO;
		bp->b_resid = bp->b_bcount;
		biodone(bio);
		return 0;
	}

	if (bypass == 0 &&
	    bounds_check_with_mediasize(bio, DEV_BSIZE,
					dm_table_size(&dmv->table_head)) <= 0) {
		bp->b_resid = bp->b_bcount;
		biodone(bio);
		return 0;
	}

	/* Select active table */
	tbl = dm_table_get_entry(&dmv->table_head, DM_TABLE_ACTIVE);

	nestiobuf_init(bio);
	devstat_start_transaction(&dmv->stats);

	/*
	 * Find out what tables I want to select.
	 */
	SLIST_FOREACH(table_en, tbl, next) {
		/*
		 * I need need number of bytes not blocks.
		 */
		table_start = table_en->start * DEV_BSIZE;
		table_end = table_start + (table_en->length) * DEV_BSIZE;

		/*
		 * Calculate the start and end
		 */
		start = MAX(table_start, buf_start);
		end = MIN(table_end, buf_start + buf_len);

		aprint_debug("----------------------------------------\n");
		aprint_debug("table_start %010" PRIu64", table_end %010"
		    PRIu64 "\n", table_start, table_end);
		aprint_debug("buf_start %010" PRIu64", buf_len %010"
		    PRIu64"\n", buf_start, buf_len);
		aprint_debug("start-buf_start %010"PRIu64", end %010"
		    PRIu64"\n", start - buf_start, end);
		aprint_debug("start %010" PRIu64" , end %010"
                    PRIu64"\n", start, end);
		aprint_debug("\n----------------------------------------\n");

		if (bypass) {
			nestbuf = getpbuf(NULL);
			nestbuf->b_flags |= bio->bio_buf->b_flags & B_HASBOGUS;

			nestiobuf_add(bio, nestbuf, 0, 0, &dmv->stats);
			nestbuf->b_bio1.bio_offset = 0;
			table_en->target->strategy(table_en, nestbuf);
		} else if (start < end) {
			nestbuf = getpbuf(NULL);
			nestbuf->b_flags |= bio->bio_buf->b_flags & B_HASBOGUS;

			nestiobuf_add(bio, nestbuf,
				      start - buf_start, (end - start),
				      &dmv->stats);
			issued_len += end - start;

			nestbuf->b_bio1.bio_offset = (start - table_start);
			table_en->target->strategy(table_en, nestbuf);
		}
	}

	if (issued_len < buf_len)
		nestiobuf_error(bio, EINVAL);
	nestiobuf_start(bio);
	dm_table_release(&dmv->table_head, DM_TABLE_ACTIVE);

	return 0;
}

static int
dmdump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	dm_dev_t *dmv;
	dm_table_t  *tbl;
	dm_table_entry_t *table_en;
	uint64_t buf_start, buf_len, issued_len;
	uint64_t table_start, table_end;
	uint64_t start, end, data_offset;
	off_t offset;
	size_t length;
	int error = 0;

	buf_start = ap->a_offset;
	buf_len = ap->a_length;

	tbl = NULL;

	table_end = 0;
	issued_len = 0;

	dmv = dev->si_drv1;

	/* Select active table */
	tbl = dm_table_get_entry(&dmv->table_head, DM_TABLE_ACTIVE);


	/*
	 * Find out what tables I want to select.
	 */
	SLIST_FOREACH(table_en, tbl, next) {
		/*
		 * I need need number of bytes not blocks.
		 */
		table_start = table_en->start * DEV_BSIZE;
		table_end = table_start + (table_en->length) * DEV_BSIZE;

		/*
		 * Calculate the start and end
		 */
		start = MAX(table_start, buf_start);
		end = MIN(table_end, buf_start + buf_len);

		if (ap->a_length == 0) {
			if (table_en->target->dump == NULL) {
				error = ENXIO;
				goto out;
			}

			table_en->target->dump(table_en, NULL, 0, 0);
		} else if (start < end) {
			data_offset = start - buf_start;
			offset = start - table_start;
			length = end - start;

			if (table_en->target->dump == NULL) {
				error = ENXIO;
				goto out;
			}

			table_en->target->dump(table_en,
			    (char *)ap->a_virtual + data_offset,
			    length, offset);

			issued_len += end - start;
		}
	}

	if (issued_len < buf_len)
		error = EINVAL;

out:
	dm_table_release(&dmv->table_head, DM_TABLE_ACTIVE);

	return error;
}

static int
dmsize(struct dev_psize_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	dm_dev_t *dmv;
	uint64_t size;

	size = 0;

	if ((dmv = dev->si_drv1) == NULL)
		return ENXIO;

	size = dm_table_size(&dmv->table_head);
	ap->a_result = (int64_t)size;

	return 0;
}

#if 0
static void
dmminphys(struct buf *bp)
{

	bp->b_bcount = MIN(bp->b_bcount, MAXPHYS);
}
#endif

void
dmsetdiskinfo(struct disk *disk, dm_table_head_t *head)
{
	struct disk_info info;
	uint64_t dmp_size;

	dmp_size = dm_table_size(head);

	bzero(&info, sizeof(struct disk_info));
	info.d_media_blksize = DEV_BSIZE;
	info.d_media_blocks = dmp_size;
#if 0
	/* this is set by disk_setdiskinfo */
	info.d_media_size = dmp_size * DEV_BSIZE;
#endif
	info.d_dsflags = DSO_MBRQUIET | DSO_DEVICEMAPPER | DSO_RAWPSIZE;

	info.d_secpertrack = 32;
	info.d_nheads = 64;
	info.d_secpercyl = info.d_secpertrack * info.d_nheads;
	info.d_ncylinders = dmp_size / info.d_secpercyl;

	disk_setdiskinfo(disk, &info);
}

/*
 * Transform char s to uint64_t offset number.
 */
uint64_t
atoi64(const char *s)
{
	uint64_t n;
	n = 0;

	while (*s != '\0') {
		if (!isdigit(*s))
			break;

		n = (10 * n) + (*s - '0');
		s++;
	}

	return n;
}

void
dm_builtin_init(void *arg)
{
	modeventhand_t evh = (modeventhand_t)arg;

	KKASSERT(evh != NULL);
	evh(NULL, MOD_LOAD, NULL);
}

void
dm_builtin_uninit(void *arg)
{
	modeventhand_t evh = (modeventhand_t)arg;

	KKASSERT(evh != NULL);
	evh(NULL, MOD_UNLOAD, NULL);
}

TUNABLE_INT("debug.dm_debug", &dm_debug_level);
SYSCTL_INT(_debug, OID_AUTO, dm_debug, CTLFLAG_RW, &dm_debug_level,
	       0, "Enable device mapper debugging");
