/*      $NetBSD: filter_netbsd.c,v 1.3 2009/12/02 01:53:25 haad Exp $        */

/*
 * Copyright (C) 2001-2004 Sistina Software, Inc. All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc. All rights reserved.
 * Copyright (C) 2008 Adam Hamsik. All rights reserved.
 * Copyright (C) 2010 Alex Hornung. All rights reserved.
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU Lesser General Public License v.2.1.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "lib.h"
#include "dev-cache.h"
#include "filter.h"
#include "lvm-string.h"
#include "config.h"
#include "metadata.h"
#include "activate.h"

#include <sys/filio.h>
#include <sys/device.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#define NUMBER_OF_MAJORS 4096

#define LVM_SUCCESS 1
#define LVM_FAILURE 0

/* -1 means LVM won't use this major number. */
static int _char_device_major[NUMBER_OF_MAJORS];
static int _block_device_major[NUMBER_OF_MAJORS];

typedef struct {
	const char *name;
	const int max_partitions;
} device_info_t;

static int _md_major = -1;
static int _device_mapper_major = -1;

int md_major(void)
{
	return _md_major;
}

int dev_subsystem_part_major(const struct device *dev)
{
	return 0;
}

const char *dev_subsystem_name(const struct device *dev)
{
	return "";
}


/*
 * Test if device passes filter tests and can be inserted in to cache.
 */
static int _passes_lvm_type_device_filter(struct dev_filter *f __attribute((unused)),
					  struct device *dev)
{
	const char *name = dev_name(dev);
	int fd, type;
	int ret = LVM_FAILURE;
	uint64_t size;

	log_debug("Checking: %s", name);

	if ((fd = open(name, O_RDONLY)) < 0) {
		log_debug("%s: Skipping: Could not open device", name);
		return LVM_FAILURE;
	}
	if (ioctl(fd, FIODTYPE, &type) == -1) {
		close(fd);
		log_debug("%s: Skipping: Could not get device type", name);
		return LVM_FAILURE;
	} else {
		if (!(type & D_DISK)) {
			close(fd);
			log_debug("%s: Skipping: Device is not of type D_DISK", name);
			return LVM_FAILURE;
		}
	}

	close(fd);

	/* Skip suspended devices */
	if (MAJOR(dev->dev) == _device_mapper_major &&
	    ignore_suspended_devices() && !device_is_usable(dev->dev)) {
		log_debug("%s: Skipping: Suspended dm device", name);
		return LVM_FAILURE;
	}

	/* Check it's accessible */
	if (!dev_open_flags(dev, O_RDONLY, 0, 1)) {
		log_debug("%s: Skipping: open failed", name);
		return LVM_FAILURE;
	}

	/* Check it's not too small */
	if (!dev_get_size(dev, &size)) {
		log_debug("%s: Skipping: dev_get_size failed", name);
		goto out;
	}

	if (size < PV_MIN_SIZE) {
		log_debug("%s: Skipping: Too small to hold a PV", name);
		goto out;
	}

	if (is_partitioned_dev(dev)) {
		log_debug("%s: Skipping: Partition table signature found",
			  name);
		goto out;
	}

	ret = LVM_SUCCESS;

      out:
	dev_close(dev);

	return ret;
}

int max_partitions(int major)
{
	/* XXX */
	return 64;
}

struct dev_filter *lvm_type_filter_create(const char *proc,
					  const struct config_node *cn)
{
	struct dev_filter *f;

	if (!(f = dm_malloc(sizeof(struct dev_filter)))) {
		log_error("LVM type filter allocation failed");
		return NULL;
	}

	f->passes_filter = _passes_lvm_type_device_filter;
	f->destroy = lvm_type_filter_destroy;
	f->private = NULL;

	return f;
}

void lvm_type_filter_destroy(struct dev_filter *f)
{
	dm_free(f);
	return;
}
