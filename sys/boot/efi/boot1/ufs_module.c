/*-
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 * Copyright (c) 2014 Nathan Whitehorn
 * All rights reserved.
 * Copyright (c) 2015 Eric McCorkle
 * All rights reverved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/boot/efi/boot1/ufs_module.c 295320 2016-02-05 15:35:33Z smh $
 */

#include <stdarg.h>
#include <stdbool.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/disklabel64.h>
#include <sys/dtype.h>
#include <efi.h>

#include "boot_module.h"

static dev_info_t *devinfo;
static dev_info_t *devices;
static u_int64_t label_off;

static int
dskread(void *buf, u_int64_t lba, int nblk)
{
	int size;
	EFI_STATUS status;

	lba += label_off / DEV_BSIZE;	/* adjust for disklabel partition */
	lba = lba / (devinfo->dev->Media->BlockSize / DEV_BSIZE);
	size = nblk * DEV_BSIZE;

	status = devinfo->dev->ReadBlocks(devinfo->dev,
	    devinfo->dev->Media->MediaId, lba, size, buf);

	if (status != EFI_SUCCESS) {
		DPRINTF("dskread: failed dev: %p, id: %u, lba: %lu, size: %d, "
		    "status: %llu\n", devinfo->dev,
		    devinfo->dev->Media->MediaId, lba, size,
		    status);
		return (-1);
	}

	return (0);
}

/* For ufsread.c */
uint64_t fs_off;
int ls;

static struct ufs_dmadat __dmadat;
static struct ufs_dmadat *boot2_dmadat;

#include "ufsread.c"

#define fsread boot2_ufs_read
#define fsread_size boot2_ufs_read_size
#define lookup boot2_ufs_lookup

static int
init_dev(dev_info_t* dev)
{
	devinfo = dev;
	boot2_dmadat = &__dmadat;
	struct disklabel64 *label;
	EFI_STATUS status;
	size_t label_size;

	label_size = (sizeof(*label) + 2047) & ~(size_t)2047;

	label_off = 0;
	status = BS->AllocatePool(EfiLoaderData, label_size, (void **)&label);
	if (status == EFI_SUCCESS) {
		if (dskread(label, 0, label_size / DEV_BSIZE) == 0 &&
		    label->d_magic == DISKMAGIC64 &&
		    label->d_npartitions > 0 &&
		    label->d_partitions[0].p_fstype == FS_BSDFFS) {
			label_off = label->d_partitions[0].p_boffset;
		}
		BS->FreePool(label);
	}
	return fsread(0, NULL, 0);
}

static EFI_STATUS
probe(dev_info_t* dev)
{

	if (init_dev(dev) < 0)
		return (EFI_UNSUPPORTED);

	add_device(&devices, dev);

	return (EFI_SUCCESS);
}

static EFI_STATUS
load(const char *filepath, dev_info_t *dev, void **bufp, size_t *bufsize)
{
	ufs_ino_t ino;
	EFI_STATUS status;
	size_t size;
	ssize_t read;
	void *buf;

	DPRINTF("Loading '%s' from %s\n", filepath, devpath_str(dev->devpath));

	if (init_dev(dev) < 0) {
		DPRINTF("Failed to init device\n");
		return (EFI_UNSUPPORTED);
	}

	if ((ino = lookup(filepath)) == 0) {
		DPRINTF("Failed to lookup '%s' (file not found?)\n", filepath);
		return (EFI_NOT_FOUND);
	}

	if (fsread_size(ino, NULL, 0, &size) < 0 || size <= 0) {
		printf("Failed to read size of '%s' ino: %d\n", filepath, ino);
		return (EFI_INVALID_PARAMETER);
	}

	if ((status = BS->AllocatePool(EfiLoaderData, size, &buf)) !=
	    EFI_SUCCESS) {
		printf("Failed to allocate read buffer %zu for '%s' (%llu)\n",
		    size, filepath, status);
		return (status);
	}

	read = fsread(ino, buf, size);
	if ((size_t)read != size) {
		printf("Failed to read '%s' (%zd != %zu)\n", filepath, read,
		    size);
		(void)BS->FreePool(buf);
		return (EFI_INVALID_PARAMETER);
	}

	DPRINTF("Load complete\n");

	*bufp = buf;
	*bufsize = size;

	return (EFI_SUCCESS);
}

static void
status(void)
{
	int i;
	dev_info_t *dev;

	for (dev = devices, i = 0; dev != NULL; dev = dev->next, i++)
		;

	printf("%s found ", ufs_module.name);
	switch (i) {
	case 0:
		printf("no partitions\n");
		break;
	case 1:
		printf("%d partition\n", i);
		break;
	default:
		printf("%d partitions\n", i);
	}
}

static dev_info_t *
_devices(void)
{

	return (devices);
}

const boot_module_t ufs_module =
{
	.name = "UFS",
	.probe = probe,
	.load = load,
	.status = status,
	.devices = _devices
};
