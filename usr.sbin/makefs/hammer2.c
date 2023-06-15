/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2011-2022 The DragonFly Project.  All rights reserved.
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

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>
#include <assert.h>
#include <util.h>

#include "makefs.h"
#include "hammer2.h"

#define APRINTF(X, ...)	\
    printf("%s: " X, __func__, ## __VA_ARGS__)

static void hammer2_parse_pfs_opts(const char *, fsinfo_t *);
static void hammer2_dump_fsinfo(fsinfo_t *);
static int hammer2_create_image(const char *, fsinfo_t *);
static int hammer2_populate_dir(struct m_vnode *, const char *, fsnode *,
    fsnode *, fsinfo_t *, int);
static void hammer2_validate(const char *, fsnode *, fsinfo_t *);
static void hammer2_size_dir(fsnode *, fsinfo_t *);
static int hammer2_write_file(struct m_vnode *, const char *, fsnode *);
static int hammer2_pfs_get(struct m_vnode *);
static int hammer2_pfs_lookup(struct m_vnode *, const char *);
static int hammer2_pfs_create(struct m_vnode *, const char *);
static int hammer2_pfs_delete(struct m_vnode *, const char *);
static int hammer2_pfs_snapshot(struct m_vnode *, const char *, const char *);
static int hammer2_bulkfree(struct m_vnode *);
static int hammer2_destroy_path(struct m_vnode *, const char *);
static int hammer2_destroy_inum(struct m_vnode *, hammer2_tid_t);
static int hammer2_growfs(struct m_vnode *, hammer2_off_t);

fsnode *hammer2_curnode;

void
hammer2_prep_opts(fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = ecalloc(1, sizeof(*h2_opt));
	hammer2_mkfs_options_t *opt = &h2_opt->mkfs_options;

	const option_t hammer2_options[] = {
		/* newfs_hammer2(8) compatible options */
		{ 'b', "BootAreaSize", NULL, OPT_STRBUF, 0, 0, "boot area size" },
		{ 'r', "AuxAreaSize", NULL, OPT_STRBUF, 0, 0, "aux area size" },
		{ 'V', "Hammer2Version", NULL, OPT_STRBUF, 0, 0, "file system version" },
		{ 'L', "Label", NULL, OPT_STRBUF, 0, 0, "PFS label" },
		/* makefs(8) specific options */
		{ 'm', "MountLabel", NULL, OPT_STRBUF, 0, 0, "destination PFS label" },
		{ 'v', "NumVolhdr", &h2_opt->num_volhdr, OPT_INT32,
		    1, HAMMER2_NUM_VOLHDRS, "number of volume headers" },
		{ 'd', "Hammer2Debug", NULL, OPT_STRBUF, 0, 0, "debug tunable" },
		{ 'E', "EmergencyMode", &h2_opt->emergency_mode, OPT_BOOL, 0, 0,
		    "emergency mode" },
		{ 'P', "PFS", NULL, OPT_STRBUF, 0, 0, "offline PFS" },
		{ 'B', "Bulkfree", NULL, OPT_STRBUF, 0, 0, "offline bulkfree" },
		{ 'D', "Destroy", NULL, OPT_STRBUF, 0, 0, "offline destroy" },
		{ 'G', "Growfs", NULL, OPT_STRBUF, 0, 0, "offline growfs" },
		{ .name = NULL },
	};

	hammer2_mkfs_init(opt);

	/* make this tunable ? */
	assert(opt->CompType == HAMMER2_COMP_NEWFS_DEFAULT);
	assert(opt->CheckType == HAMMER2_CHECK_XXHASH64);

	/* force debug mode for mkfs */
	opt->DebugOpt = 1;

	fsopts->fs_specific = h2_opt;
	fsopts->fs_options = copy_opts(hammer2_options);
	fsopts->sectorsize = DEV_BSIZE;
}

void
hammer2_cleanup_opts(fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_mkfs_options_t *opt = &h2_opt->mkfs_options;

	hammer2_mkfs_cleanup(opt);

	free(h2_opt);
	free(fsopts->fs_options);
}

int
hammer2_parse_opts(const char *option, fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_mkfs_options_t *opt = &h2_opt->mkfs_options;

	option_t *hammer2_options = fsopts->fs_options;
	char buf[1024]; /* > HAMMER2_INODE_MAXNAME */
	int i;

	assert(option != NULL);
	assert(fsopts != NULL);

	if (debug & DEBUG_FS_PARSE_OPTS)
		APRINTF("got `%s'\n", option);

	i = set_option(hammer2_options, option, buf, sizeof(buf));
	if (i == -1)
		return 0;

	if (hammer2_options[i].name == NULL)
		abort();

	switch (hammer2_options[i].letter) {
	case 'b':
		opt->BootAreaSize = getsize(buf, HAMMER2_NEWFS_ALIGN,
		    HAMMER2_BOOT_MAX_BYTES, 2);
		break;
	case 'r':
		opt->AuxAreaSize = getsize(buf, HAMMER2_NEWFS_ALIGN,
		    HAMMER2_AUX_MAX_BYTES, 2);
		break;
	case 'V':
		opt->Hammer2Version = strtol(buf, NULL, 0);
		if (opt->Hammer2Version < HAMMER2_VOL_VERSION_MIN ||
		    opt->Hammer2Version >= HAMMER2_VOL_VERSION_WIP)
			errx(1, "I don't understand how to format "
			    "HAMMER2 version %d",
			    opt->Hammer2Version);
		break;
	case 'L':
		h2_opt->label_specified = 1;
		if (strcasecmp(buf, "none") == 0)
			break;
		if (opt->NLabels >= MAXLABELS)
			errx(1, "Limit of %d local labels", MAXLABELS - 1);
		if (strlen(buf) == 0)
			errx(1, "Volume label '%s' cannot be 0-length", buf);
		if (strlen(buf) >= HAMMER2_INODE_MAXNAME)
			errx(1, "Volume label '%s' is too long (%d chars max)",
			    buf, HAMMER2_INODE_MAXNAME - 1);
		opt->Label[opt->NLabels++] = strdup(buf);
		break;
	case 'm':
		if (strlen(buf) == 0)
			errx(1, "Volume label '%s' cannot be 0-length", buf);
		if (strlen(buf) >= HAMMER2_INODE_MAXNAME)
			errx(1, "Volume label '%s' is too long (%d chars max)",
			    buf, HAMMER2_INODE_MAXNAME - 1);
		strlcpy(h2_opt->mount_label, buf, sizeof(h2_opt->mount_label));
		break;
	case 'd':
		hammer2_debug = strtoll(buf, NULL, 0);
		break;
	case 'P':
		if (strlen(buf) == 0)
			errx(1, "PFS argument '%s' cannot be 0-length", buf);
		hammer2_parse_pfs_opts(buf, fsopts);
		break;
	case 'B':
		h2_opt->ioctl_cmd = HAMMER2IOC_BULKFREE_SCAN;
		break;
	case 'D':
		h2_opt->ioctl_cmd = HAMMER2IOC_DESTROY;
		if (strlen(buf) == 0)
			errx(1, "Destroy argument '%s' cannot be 0-length", buf);
		if (buf[0] == '/') {
			strlcpy(h2_opt->destroy_path, buf + 1,
			    sizeof(h2_opt->destroy_path));
		} else if (strncmp(buf, "0x", 2) == 0 ||
		    (buf[0] >= '0' && buf[0] <= '9')) {
			h2_opt->destroy_inum = strtoull(buf, NULL, 0);
			if (errno)
				err(1, "strtoull");
		} else {
			errx(1, "Invalid destroy argument %s", buf);
		}
		break;
	case 'G':
		h2_opt->ioctl_cmd = HAMMER2IOC_GROWFS;
		break;
	default:
		break;
	}

	return 1;
}

void
hammer2_makefs(const char *image, const char *dir, fsnode *root,
    fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	struct mount mp;
	struct hammer2_mount_info info;
	struct m_vnode devvp, *vroot;
	hammer2_inode_t *iroot;
	struct timeval start;
	int error;

	/* ioctl commands could have NULL root */
	assert(image != NULL);
	assert(dir != NULL);
	assert(fsopts != NULL);

	if (debug & DEBUG_FS_MAKEFS)
		APRINTF("image \"%s\" directory \"%s\" root %p\n",
		    image, dir, root);

	/* validate tree and options */
	TIMER_START(start);
	hammer2_validate(dir, root, fsopts);
	TIMER_RESULTS(start, "hammer2_validate");

	if (h2_opt->ioctl_cmd) {
		/* open existing image */
		fsopts->fd = open(image, O_RDWR);
		if (fsopts->fd < 0)
			err(1, "failed to open `%s'", image);
	} else {
		/* create image */
		TIMER_START(start);
		if (hammer2_create_image(image, fsopts) == -1)
			errx(1, "image file `%s' not created", image);
		TIMER_RESULTS(start, "hammer2_create_image");
	}
	assert(fsopts->fd > 0);

	if (debug & DEBUG_FS_MAKEFS)
		putchar('\n');

	/* vfs init */
	error = hammer2_vfs_init();
	if (error)
		errx(1, "failed to vfs init, error %d", error);

	/* mount image */
	memset(&devvp, 0, sizeof(devvp));
	devvp.fs = fsopts;
	memset(&mp, 0, sizeof(mp));
	memset(&info, 0, sizeof(info));
	error = hammer2_vfs_mount(&devvp, &mp, h2_opt->mount_label, &info);
	if (error)
		errx(1, "failed to mount, error %d", error);
	assert(mp.mnt_data);

	/* get root vnode */
	vroot = NULL;
	error = hammer2_vfs_root(&mp, &vroot);
	if (error)
		errx(1, "failed to get root vnode, error %d", error);
	assert(vroot);

	iroot = VTOI(vroot);
	assert(iroot);
	printf("root inode inum %lld, mode 0%o, refs %d\n",
	    (long long)iroot->meta.inum, iroot->meta.mode, iroot->refs);

	if (h2_opt->emergency_mode)
		hammer2_ioctl_emerg_mode(iroot, 1);

	switch (h2_opt->ioctl_cmd) {
	case HAMMER2IOC_PFS_GET:
		printf("PFS %s `%s'\n", h2_opt->pfs_cmd_name, image);
		TIMER_START(start);
		error = hammer2_pfs_get(vroot);
		if (error)
			errx(1, "PFS %s`%s' failed '%s'", h2_opt->pfs_cmd_name,
			    image, strerror(error));
		TIMER_RESULTS(start, "hammer2_pfs_get");
		break;
	case HAMMER2IOC_PFS_LOOKUP:
		printf("PFS %s `%s'\n", h2_opt->pfs_cmd_name, image);
		TIMER_START(start);
		error = hammer2_pfs_lookup(vroot, h2_opt->pfs_name);
		if (error)
			errx(1, "PFS %s`%s' failed '%s'", h2_opt->pfs_cmd_name,
			    image, strerror(error));
		TIMER_RESULTS(start, "hammer2_pfs_lookup");
		break;
	case HAMMER2IOC_PFS_CREATE:
		printf("PFS %s `%s'\n", h2_opt->pfs_cmd_name, image);
		TIMER_START(start);
		error = hammer2_pfs_create(vroot, h2_opt->pfs_name);
		if (error)
			errx(1, "PFS %s`%s' failed '%s'", h2_opt->pfs_cmd_name,
			    image, strerror(error));
		TIMER_RESULTS(start, "hammer2_pfs_create");
		break;
	case HAMMER2IOC_PFS_DELETE:
		printf("PFS %s `%s'\n", h2_opt->pfs_cmd_name, image);
		TIMER_START(start);
		error = hammer2_pfs_delete(vroot, h2_opt->pfs_name);
		if (error)
			errx(1, "PFS %s`%s' failed '%s'", h2_opt->pfs_cmd_name,
			    image, strerror(error));
		TIMER_RESULTS(start, "hammer2_pfs_delete");
		break;
	case HAMMER2IOC_PFS_SNAPSHOT:
		printf("PFS %s `%s'\n", h2_opt->pfs_cmd_name, image);
		TIMER_START(start);
		error = hammer2_pfs_snapshot(vroot, h2_opt->pfs_name,
		    h2_opt->mount_label);
		if (error)
			errx(1, "PFS %s`%s' failed '%s'", h2_opt->pfs_cmd_name,
			    image, strerror(error));
		TIMER_RESULTS(start, "hammer2_pfs_snapshot");
		break;
	case HAMMER2IOC_BULKFREE_SCAN:
		printf("bulkfree `%s'\n", image);
		TIMER_START(start);
		error = hammer2_bulkfree(vroot);
		if (error)
			errx(1, "bulkfree `%s' failed '%s'", image,
			    strerror(error));
		TIMER_RESULTS(start, "hammer2_bulkfree");
		break;
	case HAMMER2IOC_DESTROY:
		TIMER_START(start);
		if (strlen(h2_opt->destroy_path)) {
			printf("destroy `%s' in `%s'\n",
			    h2_opt->destroy_path, image);
			error = hammer2_destroy_path(vroot,
			    h2_opt->destroy_path);
			if (error)
				errx(1, "destroy `%s' in `%s' failed '%s'",
				    h2_opt->destroy_path, image,
				    strerror(error));
		} else {
			printf("destroy %lld in `%s'\n",
			    (long long)h2_opt->destroy_inum, image);
			error = hammer2_destroy_inum(vroot,
			    h2_opt->destroy_inum);
			if (error)
				errx(1, "destroy %lld in `%s' failed '%s'",
				    (long long)h2_opt->destroy_inum, image,
				    strerror(error));
		}
		TIMER_RESULTS(start, "hammer2_destroy");
		break;
	case HAMMER2IOC_GROWFS:
		printf("growfs `%s'\n", image);
		TIMER_START(start);
		error = hammer2_growfs(vroot, h2_opt->image_size);
		if (error)
			errx(1, "growfs `%s' failed '%s'", image,
			    strerror(error));
		TIMER_RESULTS(start, "hammer2_growfs");
		break;
	default:
		printf("populating `%s'\n", image);
		TIMER_START(start);
		if (hammer2_populate_dir(vroot, dir, root, root, fsopts, 0))
			errx(1, "image file `%s' not populated", image);
		TIMER_RESULTS(start, "hammer2_populate_dir");
		break;
	}

	/* unmount image */
	error = hammer2_vfs_unmount(&mp, 0);
	if (error)
		errx(1, "failed to unmount, error %d", error);

	/* check leaked resource */
	if (vnode_count)
		printf("XXX %lld vnode left\n", (long long)vnode_count);
	if (hammer2_chain_allocs)
		printf("XXX %ld chain left\n", hammer2_chain_allocs);
	bcleanup();

	/* vfs uninit */
	error = hammer2_vfs_uninit();
	if (error)
		errx(1, "failed to vfs uninit, error %d", error);

	if (close(fsopts->fd) == -1)
		err(1, "closing `%s'", image);
	fsopts->fd = -1;

	printf("image `%s' complete\n", image);
}

/* end of public functions */

static void
hammer2_parse_pfs_opts(const char *buf, fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	char *o, *p;
	size_t n;

	o = p = strdup(buf);
	p = strchr(p, ':');
	if (p != NULL) {
		*p++ = 0;
		n = strlen(p);
	} else {
		n = 0;
	}

	if (!strcmp(o, "get") || !strcmp(o, "list")) {
		h2_opt->ioctl_cmd = HAMMER2IOC_PFS_GET;
	} else if (!strcmp(o, "lookup")) {
		if (n == 0 || n > NAME_MAX)
			errx(1, "invalid PFS name \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_PFS_LOOKUP;
	} else if (!strcmp(o, "create")) {
		if (n == 0 || n > NAME_MAX)
			errx(1, "invalid PFS name \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_PFS_CREATE;
	} else if (!strcmp(o, "delete")) {
		if (n == 0 || n > NAME_MAX)
			errx(1, "invalid PFS name \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_PFS_DELETE;
	} else if (!strcmp(o, "snapshot")) {
		if (n > NAME_MAX)
			errx(1, "invalid PFS name \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_PFS_SNAPSHOT;
	} else {
		errx(1, "invalid PFS command \"%s\"", o);
	}

	strlcpy(h2_opt->pfs_cmd_name, o, sizeof(h2_opt->pfs_cmd_name));
	if (n > 0)
		strlcpy(h2_opt->pfs_name, p, sizeof(h2_opt->pfs_name));

	free(o);
}

static hammer2_off_t
hammer2_image_size(fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_off_t image_size, used_size = 0;
	int num_level1, delta_num_level1;

	/* use 4 volume headers by default */
	num_level1 = h2_opt->num_volhdr * 2; /* default 4 x 2 */
	assert(num_level1 != 0);
	assert(num_level1 <= 8);

	/* add 4MiB segment for each level1 */
	used_size += HAMMER2_ZONE_SEG64 * num_level1;

	/* add boot/aux area, but exact size unknown at this point */
	used_size += HAMMER2_BOOT_NOM_BYTES + HAMMER2_AUX_NOM_BYTES;

	/* add data size */
	used_size += fsopts->size;

	/* XXX add extra level1 for meta data and indirect blocks */
	used_size += HAMMER2_FREEMAP_LEVEL1_SIZE;

	/* XXX add extra level1 for safety */
	if (used_size > HAMMER2_FREEMAP_LEVEL1_SIZE * 10)
		used_size += HAMMER2_FREEMAP_LEVEL1_SIZE;

	/* use 8GiB image size by default */
	image_size = HAMMER2_FREEMAP_LEVEL1_SIZE * num_level1;
	printf("trying default image size %s\n", sizetostr(image_size));

	/* adjust if image size isn't large enough */
	if (used_size > image_size) {
		/* determine extra level1 needed */
		delta_num_level1 = howmany(used_size - image_size,
		    HAMMER2_FREEMAP_LEVEL1_SIZE);

		/* adjust used size with 4MiB segment for each extra level1 */
		used_size += HAMMER2_ZONE_SEG64 * delta_num_level1;

		/* adjust image size with extra level1 */
		image_size += HAMMER2_FREEMAP_LEVEL1_SIZE * delta_num_level1;
		printf("trying adjusted image size %s\n",
		    sizetostr(image_size));

		if (used_size > image_size)
			errx(1, "invalid used_size %lld > image_size %lld",
			    (long long)used_size, (long long)image_size);
	}

	return image_size;
}

static const char *
hammer2_label_name(int label_type)
{
	switch (label_type) {
	case HAMMER2_LABEL_NONE:
		return "NONE";
	case HAMMER2_LABEL_BOOT:
		return "BOOT";
	case HAMMER2_LABEL_ROOT:
		return "ROOT";
	case HAMMER2_LABEL_DATA:
		return "DATA";
	default:
		assert(0);
	}
	return NULL;
}

static void
hammer2_validate(const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_mkfs_options_t *opt = &h2_opt->mkfs_options;
	hammer2_off_t image_size = 0, minsize, maxsize;
	const char *s;

	assert(dir != NULL);
	assert(fsopts != NULL);

	if (debug & DEBUG_FS_VALIDATE) {
		APRINTF("before defaults set:\n");
		hammer2_dump_fsinfo(fsopts);
	}

	/* makefs only supports "DATA" for default PFS label */
	if (!h2_opt->label_specified) {
		opt->DefaultLabelType = HAMMER2_LABEL_DATA;
		s = hammer2_label_name(opt->DefaultLabelType);
		printf("using default label \"%s\"\n", s);
	}

	/* set default mount PFS label */
	if (!strcmp(h2_opt->mount_label, "")) {
		s = hammer2_label_name(HAMMER2_LABEL_DATA);
		strlcpy(h2_opt->mount_label, s, sizeof(h2_opt->mount_label));
		printf("using default mount label \"%s\"\n", s);
	}

	/* set default number of volume headers */
	if (!h2_opt->num_volhdr) {
		h2_opt->num_volhdr = HAMMER2_NUM_VOLHDRS;
		printf("using default %d volume headers\n", h2_opt->num_volhdr);
	}

	/* done if ioctl commands */
	if (h2_opt->ioctl_cmd) {
		if (h2_opt->ioctl_cmd == HAMMER2IOC_GROWFS)
			goto ignore_size_dir;
		else
			goto done;
	}

	/* calculate data size */
	if (fsopts->size != 0)
		fsopts->size = 0; /* shouldn't reach here to begin with */
	if (root == NULL)
		errx(1, "fsnode tree not constructed");
	hammer2_size_dir(root, fsopts);
	printf("estimated data size %s from %lld inode\n",
	    sizetostr(fsopts->size), (long long)fsopts->inodes);

	/* determine image size from data size */
	image_size = hammer2_image_size(fsopts);
	assert((image_size & HAMMER2_FREEMAP_LEVEL1_MASK) == 0);
ignore_size_dir:
	minsize = roundup(fsopts->minsize, HAMMER2_FREEMAP_LEVEL1_SIZE);
	maxsize = roundup(fsopts->maxsize, HAMMER2_FREEMAP_LEVEL1_SIZE);
	if (image_size < minsize)
		image_size = minsize;
	else if (maxsize > 0 && image_size > maxsize)
		errx(1, "`%s' size of %lld is larger than the maxsize of %lld",
		    dir, (long long)image_size, (long long)maxsize);

	assert((image_size & HAMMER2_FREEMAP_LEVEL1_MASK) == 0);
	h2_opt->image_size = image_size;
	printf("using %s image size\n", sizetostr(h2_opt->image_size));
done:
	if (debug & DEBUG_FS_VALIDATE) {
		APRINTF("after defaults set:\n");
		hammer2_dump_fsinfo(fsopts);
	}
}

static void
hammer2_dump_fsinfo(fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_mkfs_options_t *opt = &h2_opt->mkfs_options;
	int i;
	char *s;

	assert(fsopts != NULL);

	APRINTF("fsinfo_t at %p\n", fsopts);

	printf("\tinodes %lld\n", (long long)fsopts->inodes);
	printf("\tsize %lld, minsize %lld, maxsize %lld\n",
	    (long long)fsopts->size,
	    (long long)fsopts->minsize,
	    (long long)fsopts->maxsize);

	printf("\thammer2_debug 0x%x\n", hammer2_debug);

	printf("\tlabel_specified %d\n", h2_opt->label_specified);
	printf("\tmount_label \"%s\"\n", h2_opt->mount_label);
	printf("\tnum_volhdr %d\n", h2_opt->num_volhdr);
	printf("\tioctl_cmd %ld\n", h2_opt->ioctl_cmd);
	printf("\temergency_mode %d\n", h2_opt->emergency_mode);
	printf("\tpfs_cmd_name \"%s\"\n", h2_opt->pfs_cmd_name);
	printf("\tpfs_name \"%s\"\n", h2_opt->pfs_name);
	printf("\tdestroy_path \"%s\"\n", h2_opt->destroy_path);
	printf("\tdestroy_inum %lld\n", (long long)h2_opt->destroy_inum);
	printf("\timage_size 0x%llx\n", (long long)h2_opt->image_size);

	printf("\tHammer2Version %d\n", opt->Hammer2Version);
	printf("\tBootAreaSize 0x%jx\n", opt->BootAreaSize);
	printf("\tAuxAreaSize 0x%jx\n", opt->AuxAreaSize);
	printf("\tNLabels %d\n", opt->NLabels);
	printf("\tCompType %d\n", opt->CompType);
	printf("\tCheckType %d\n", opt->CheckType);
	printf("\tDefaultLabelType %d\n", opt->DefaultLabelType);
	printf("\tDebugOpt %d\n", opt->DebugOpt);

	s = NULL;
	hammer2_uuid_to_str(&opt->Hammer2_FSType, &s);
	printf("\tHammer2_FSType \"%s\"\n", s);
	s = NULL;
	hammer2_uuid_to_str(&opt->Hammer2_VolFSID, &s);
	printf("\tHammer2_VolFSID \"%s\"\n", s);
	s = NULL;
	hammer2_uuid_to_str(&opt->Hammer2_SupCLID, &s);
	printf("\tHammer2_SupCLID \"%s\"\n", s);
	s = NULL;
	hammer2_uuid_to_str(&opt->Hammer2_SupFSID, &s);
	printf("\tHammer2_SupFSID \"%s\"\n", s);

	for (i = 0; i < opt->NLabels; i++) {
		printf("\tLabel[%d] \"%s\"\n", i, opt->Label[i]);
		s = NULL;
		hammer2_uuid_to_str(&opt->Hammer2_PfsCLID[i], &s);
		printf("\t Hammer2_PfsCLID[%d] \"%s\"\n", i, s);
		s = NULL;
		hammer2_uuid_to_str(&opt->Hammer2_PfsFSID[i], &s);
		printf("\t Hammer2_PfsFSID[%d] \"%s\"\n", i, s);
	}

	free(s);
}

static int
hammer2_create_image(const char *image, fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_mkfs_options_t *opt = &h2_opt->mkfs_options;
	char *av[] = { (char *)image, }; /* XXX support multi-volumes */
	char *buf;
	int i, bufsize, oflags;
	off_t bufrem;

	assert(image != NULL);
	assert(fsopts != NULL);

	/* create image */
	oflags = O_RDWR | O_CREAT;
	if (fsopts->offset == 0)
		oflags |= O_TRUNC;
	if ((fsopts->fd = open(image, oflags, 0666)) == -1) {
		warn("can't open `%s' for writing", image);
		return -1;
	}

	/* zero image */
	bufsize = HAMMER2_PBUFSIZE;
	bufrem = h2_opt->image_size;
	if (fsopts->sparse) {
		if (ftruncate(fsopts->fd, bufrem) == -1) {
			warn("sparse option disabled");
			fsopts->sparse = 0;
		}
	}
	if (fsopts->sparse) {
		/* File truncated at bufrem. Remaining is 0 */
		bufrem = 0;
		buf = NULL;
	} else {
		if (debug & DEBUG_FS_CREATE_IMAGE)
			APRINTF("zero-ing image `%s', %lld sectors, "
			    "using %d byte chunks\n",
			    image, (long long)bufrem, bufsize);
		buf = ecalloc(1, bufsize);
	}

	if (fsopts->offset != 0) {
		if (lseek(fsopts->fd, fsopts->offset, SEEK_SET) == -1) {
			warn("can't seek");
			free(buf);
			return -1;
		}
	}

	while (bufrem > 0) {
		i = write(fsopts->fd, buf, MIN(bufsize, bufrem));
		if (i == -1) {
			warn("zeroing image, %lld bytes to go",
			    (long long)bufrem);
			free(buf);
			return -1;
		}
		bufrem -= i;
	}
	if (buf)
		free(buf);

	/* make the file system */
	if (debug & DEBUG_FS_CREATE_IMAGE)
		APRINTF("calling mkfs(\"%s\", ...)\n", image);
	hammer2_mkfs(1, av, opt); /* success if returned */

	return fsopts->fd;
}

static off_t
hammer2_phys_size(off_t size)
{
	off_t radix_size, phys_size = 0;
	int i;

	if (size > HAMMER2_PBUFSIZE) {
		phys_size += rounddown(size, HAMMER2_PBUFSIZE);
		size = size % HAMMER2_PBUFSIZE;
	}

	for (i = HAMMER2_RADIX_MIN; i <= HAMMER2_RADIX_MAX; i++) {
		radix_size = 1UL << i;
		if (radix_size >= size) {
			phys_size += radix_size;
			break;
		}
	}

	return phys_size;
}

/* calculate data size */
static void
hammer2_size_dir(fsnode *root, fsinfo_t *fsopts)
{
	fsnode *node;

	assert(fsopts != NULL);

	if (debug & DEBUG_FS_SIZE_DIR)
		APRINTF("entry: bytes %lld inodes %lld\n",
		    (long long)fsopts->size, (long long)fsopts->inodes);

	for (node = root; node != NULL; node = node->next) {
		if (node == root) { /* we're at "." */
			assert(strcmp(node->name, ".") == 0);
		} else if ((node->inode->flags & FI_SIZED) == 0) {
			/* don't count duplicate names */
			node->inode->flags |= FI_SIZED;
			if (debug & DEBUG_FS_SIZE_DIR_NODE)
				APRINTF("`%s' size %lld\n",
				    node->name,
				    (long long)node->inode->st.st_size);
			fsopts->inodes++;
			fsopts->size += sizeof(hammer2_inode_data_t);
			if (node->type == S_IFREG) {
				size_t st_size = node->inode->st.st_size;
				if (st_size > HAMMER2_EMBEDDED_BYTES)
					fsopts->size += hammer2_phys_size(st_size);
			} else if (node->type == S_IFLNK) {
				size_t nlen = strlen(node->symlink);
				if (nlen > HAMMER2_EMBEDDED_BYTES)
					fsopts->size += hammer2_phys_size(nlen);
			}
		}
		if (node->type == S_IFDIR)
			hammer2_size_dir(node->child, fsopts);
	}

	if (debug & DEBUG_FS_SIZE_DIR)
		APRINTF("exit: size %lld inodes %lld\n",
		    (long long)fsopts->size, (long long)fsopts->inodes);
}

static void
hammer2_print(const struct m_vnode *dvp, const struct m_vnode *vp,
    const fsnode *node, int depth, const char *msg)
{
	if (debug & DEBUG_FS_POPULATE) {
		if (1) {
			int indent = depth * 2;
			char *type;
			if (S_ISDIR(node->type))
				type = "dir";
			else if (S_ISREG(node->type))
				type = "reg";
			else if (S_ISLNK(node->type))
				type = "lnk";
			else if (S_ISFIFO(node->type))
				type = "fifo";
			else
				type = "???";
			printf("%*.*s", indent, indent, "");
			printf("dvp=%p/%d vp=%p/%d \"%s\" %s %s\n",
			    dvp, dvp ? VTOI(dvp)->refs : 0,
			    vp, vp ? VTOI(vp)->refs : 0,
			    node->name, type, msg);
		} else {
			char type;
			if (S_ISDIR(node->type))
				type = 'd';
			else if (S_ISREG(node->type))
				type = 'r';
			else if (S_ISLNK(node->type))
				type = 'l';
			else if (S_ISFIFO(node->type))
				type = 'f';
			else
				type = '?';
			printf("%c", type);
			fflush(stdout);
		}
	}
}

static int
hammer2_populate_dir(struct m_vnode *dvp, const char *dir, fsnode *root,
    fsnode *parent, fsinfo_t *fsopts, int depth)
{
	fsnode *cur;
	struct m_vnode *vp;
	struct stat st;
	char f[MAXPATHLEN];
	const char *path;
	int hardlink;
	int error;

	assert(dvp != NULL);
	assert(dir != NULL);
	assert(root != NULL);
	assert(parent != NULL);
	assert(fsopts != NULL);

	/* assert root directory */
	assert(S_ISDIR(root->type));
	assert(!strcmp(root->name, "."));
	assert(!root->child);
	assert(!root->parent || root->parent->child == root);

	hammer2_print(dvp, NULL, root, depth, "enter");
	if (stat(dir, &st) == -1)
		err(1, "no such path %s", dir);
	if (!S_ISDIR(st.st_mode))
		errx(1, "no such dir %s", dir);

	for (cur = root->next; cur != NULL; cur = cur->next) {
		/* global variable for HAMMER2 vnops */
		hammer2_curnode = cur;

		/* construct source path */
		if (cur->contents) {
			path = cur->contents;
		} else {
			if (S_ISDIR(cur->type)) {
				/* this should be same as root/path/name */
				if (snprintf(f, sizeof(f), "%s/%s",
				    dir, cur->name) >= (int)sizeof(f))
					errx(1, "path %s too long", f);
			} else {
				if (snprintf(f, sizeof(f), "%s/%s/%s",
				    cur->root, cur->path, cur->name) >= (int)sizeof(f))
					errx(1, "path %s too long", f);
			}
			path = f;
		}
		if (stat(path, &st) == -1)
			err(1, "no such file %s", f);

		/* update node state */
		if ((cur->inode->flags & FI_ALLOCATED) == 0) {
			cur->inode->flags |= FI_ALLOCATED;
			if (cur != root)
				cur->parent = parent;
		}

		/* detect hardlink */
		if (cur->inode->flags & FI_WRITTEN) {
			assert(!S_ISDIR(cur->type));
			hardlink = 1;
		} else {
			hardlink = 0;
		}
		cur->inode->flags |= FI_WRITTEN;

		/* make sure it doesn't exist yet */
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, cur->name,
		    strlen(cur->name));
		if (!error)
			errx(1, "hammer2_nresolve(\"%s\") already exists",
			    cur->name);
		hammer2_print(dvp, vp, cur, depth, "nresolve");

		/* if directory, mkdir and recurse */
		if (S_ISDIR(cur->type)) {
			assert(cur->child);

			vp = NULL;
			error = hammer2_nmkdir(dvp, &vp, cur->name,
			    strlen(cur->name), cur->inode->st.st_mode);
			if (error)
				errx(1, "hammer2_nmkdir(\"%s\") failed: %s",
				    cur->name, strerror(error));
			assert(vp);
			hammer2_print(dvp, vp, cur, depth, "nmkdir");

			error = hammer2_populate_dir(vp, path, cur->child, cur,
			    fsopts, depth + 1);
			if (error)
				errx(1, "failed to populate %s: %s",
				    path, strerror(error));
			cur->inode->param = vp;
			continue;
		}

		/* if regular file, creat and write its data */
		if (S_ISREG(cur->type) && !hardlink) {
			assert(cur->child == NULL);

			vp = NULL;
			error = hammer2_ncreate(dvp, &vp, cur->name,
			    strlen(cur->name), cur->inode->st.st_mode);
			if (error)
				errx(1, "hammer2_ncreate(\"%s\") failed: %s",
				    cur->name, strerror(error));
			assert(vp);
			hammer2_print(dvp, vp, cur, depth, "ncreate");

			error = hammer2_write_file(vp, path, cur);
			if (error)
				errx(1, "hammer2_write_file(\"%s\") failed: %s",
				    path, strerror(error));
			cur->inode->param = vp;
			continue;
		}

		/* if symlink, create a symlink against target */
		if (S_ISLNK(cur->type)) {
			assert(cur->child == NULL);

			vp = NULL;
			error = hammer2_nsymlink(dvp, &vp, cur->name,
			    strlen(cur->name), cur->symlink,
			    cur->inode->st.st_mode);
			if (error)
				errx(1, "hammer2_nsymlink(\"%s\") failed: %s",
				    cur->name, strerror(error));
			assert(vp);
			hammer2_print(dvp, vp, cur, depth, "nsymlink");
			cur->inode->param = vp;
			continue;
		}

		/* if fifo, create a fifo */
		if (S_ISFIFO(cur->type) && !hardlink) {
			assert(cur->child == NULL);

			vp = NULL;
			error = hammer2_nmknod(dvp, &vp, cur->name,
			    strlen(cur->name), VFIFO, cur->inode->st.st_mode);
			if (error)
				errx(1, "hammer2_nmknod(\"%s\") failed: %s",
				    cur->name, strerror(error));
			assert(vp);
			hammer2_print(dvp, vp, cur, depth, "nmknod");
			cur->inode->param = vp;
			continue;
		}

		/* if hardlink, creat a hardlink */
		if ((S_ISREG(cur->type) || S_ISFIFO(cur->type)) && hardlink) {
			char buf[64];
			assert(cur->child == NULL);

			/* source vnode must not be NULL */
			vp = cur->inode->param;
			assert(vp);
			/* currently these conditions must be true */
			assert(vp->v_data);
			assert(vp->v_type == VREG || vp->v_type == VFIFO);
			assert(vp->v_logical);
			assert(!vp->v_vflushed);
			assert(vp->v_malloced);
			assert(VTOI(vp)->refs > 0);

			error = hammer2_nlink(dvp, vp, cur->name,
			    strlen(cur->name));
			if (error)
				errx(1, "hammer2_nlink(\"%s\") failed: %s",
				    cur->name, strerror(error));
			snprintf(buf, sizeof(buf), "nlink=%lld",
			    (long long)VTOI(vp)->meta.nlinks);
			hammer2_print(dvp, vp, cur, depth, buf);
			continue;
		}

		/* other types are unsupported */
		printf("ignore %s 0%o\n", path, cur->type);
	}

	return 0;
}

static int
hammer2_write_file(struct m_vnode *vp, const char *path, fsnode *node)
{
	struct stat *st = &node->inode->st;
	size_t nsize, bufsize;
	off_t offset;
	int fd, error;
	char *p;

	nsize = st->st_size;
	if (nsize == 0)
		return 0;
	/* check nsize vs maximum file size */

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(1, "failed to open %s", path);

	p = mmap(0, nsize, PROT_READ, MAP_FILE|MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED)
		err(1, "failed to mmap %s", path);
	close(fd);

	for (offset = 0; offset < nsize; ) {
		bufsize = MIN(nsize - offset, HAMMER2_PBUFSIZE);
		assert(bufsize <= HAMMER2_PBUFSIZE);
		error = hammer2_write(vp, p + offset, bufsize, offset);
		if (error)
			errx(1, "failed to write to %s vnode: %s",
			    path, strerror(error));
		offset += bufsize;
		if (bufsize == HAMMER2_PBUFSIZE)
			assert((offset & (HAMMER2_PBUFSIZE - 1)) == 0);
	}
	munmap(p, nsize);

	return 0;
}

struct pfs_entry {
	TAILQ_ENTRY(pfs_entry) entry;
	char name[NAME_MAX+1];
	char s[NAME_MAX+1];
};

static int
hammer2_pfs_get(struct m_vnode *vp)
{
	hammer2_ioc_pfs_t pfs;
	TAILQ_HEAD(, pfs_entry) head;
	struct pfs_entry *p, *e;
	char *pfs_id_str;
	const char *type_str;
	int error;

	bzero(&pfs, sizeof(pfs));
	TAILQ_INIT(&head);

	while ((pfs.name_key = pfs.name_next) != (hammer2_key_t)-1) {
		error = hammer2_ioctl_pfs_get(VTOI(vp), &pfs);
		if (error)
			return error;

		pfs_id_str = NULL;
		hammer2_uuid_to_str(&pfs.pfs_clid, &pfs_id_str);

		if (pfs.pfs_type == HAMMER2_PFSTYPE_MASTER) {
			if (pfs.pfs_subtype == HAMMER2_PFSSUBTYPE_NONE)
				type_str = "MASTER";
			else
				type_str = hammer2_pfssubtype_to_str(
				    pfs.pfs_subtype);
		} else {
			type_str = hammer2_pfstype_to_str(pfs.pfs_type);
		}
		e = ecalloc(1, sizeof(*e));
		snprintf(e->name, sizeof(e->name), "%s", pfs.name);
		snprintf(e->s, sizeof(e->s), "%-11s %s", type_str, pfs_id_str);
		free(pfs_id_str);

		p = TAILQ_FIRST(&head);
		while (p) {
			if (strcmp(e->name, p->name) <= 0) {
				TAILQ_INSERT_BEFORE(p, e, entry);
				break;
			}
			p = TAILQ_NEXT(p, entry);
		}
		if (!p)
			TAILQ_INSERT_TAIL(&head, e, entry);
	}

	printf("Type        "
	    "ClusterId (pfs_clid)                 "
	    "Label\n");
	while ((p = TAILQ_FIRST(&head)) != NULL) {
		printf("%s %s\n", p->s, p->name);
		TAILQ_REMOVE(&head, p, entry);
		free(p);
	}

	return 0;
}

static int
hammer2_pfs_lookup(struct m_vnode *vp, const char *pfs_name)
{
	hammer2_ioc_pfs_t pfs;
	char *pfs_id_str;
	int error;

	bzero(&pfs, sizeof(pfs));
	strlcpy(pfs.name, pfs_name, sizeof(pfs.name));

	error = hammer2_ioctl_pfs_lookup(VTOI(vp), &pfs);
	if (error == 0) {
		printf("name: %s\n", pfs.name);
		printf("type: %s\n", hammer2_pfstype_to_str(pfs.pfs_type));
		printf("subtype: %s\n",
		    hammer2_pfssubtype_to_str(pfs.pfs_subtype));

		pfs_id_str = NULL;
		hammer2_uuid_to_str(&pfs.pfs_fsid, &pfs_id_str);
		printf("fsid: %s\n", pfs_id_str);
		free(pfs_id_str);

		pfs_id_str = NULL;
		hammer2_uuid_to_str(&pfs.pfs_clid, &pfs_id_str);
		printf("clid: %s\n", pfs_id_str);
		free(pfs_id_str);
	}

	return error;
}

static int
hammer2_pfs_create(struct m_vnode *vp, const char *pfs_name)
{
	hammer2_ioc_pfs_t pfs;
	int error;

	bzero(&pfs, sizeof(pfs));
	strlcpy(pfs.name, pfs_name, sizeof(pfs.name));
	pfs.pfs_type = HAMMER2_PFSTYPE_MASTER;
	uuid_create(&pfs.pfs_clid, NULL);
	uuid_create(&pfs.pfs_fsid, NULL);

	error = hammer2_ioctl_pfs_create(VTOI(vp), &pfs);
	if (error == EEXIST)
		fprintf(stderr,
		    "NOTE: Typically the same name is "
		    "used for cluster elements on "
		    "different mounts,\n"
		    "      but cluster elements on the "
		    "same mount require unique names.\n"
		    "hammer2: pfs_create(%s): already present\n",
		    pfs_name);

	return error;
}

static int
hammer2_pfs_delete(struct m_vnode *vp, const char *pfs_name)
{
	hammer2_ioc_pfs_t pfs;

	bzero(&pfs, sizeof(pfs));
	strlcpy(pfs.name, pfs_name, sizeof(pfs.name));

	return hammer2_ioctl_pfs_delete(VTOI(vp), &pfs);
}

static int
hammer2_pfs_snapshot(struct m_vnode *vp, const char *pfs_name,
    const char *mount_label)
{
	hammer2_ioc_pfs_t pfs;
	struct tm *tp;
	time_t t;

	bzero(&pfs, sizeof(pfs));
	strlcpy(pfs.name, pfs_name, sizeof(pfs.name));

	if (strlen(pfs.name) == 0) {
		time(&t);
		tp = localtime(&t);
		snprintf(pfs.name, sizeof(pfs.name),
		    "%s.%04d%02d%02d.%02d%02d%02d",
		    mount_label,
		    tp->tm_year + 1900,
		    tp->tm_mon + 1,
		    tp->tm_mday,
		    tp->tm_hour,
		    tp->tm_min,
		    tp->tm_sec);
	}

	return hammer2_ioctl_pfs_snapshot(VTOI(vp), &pfs);
}

static int
hammer2_bulkfree(struct m_vnode *vp)
{
	hammer2_ioc_bulkfree_t bfi;
	size_t usermem;
	size_t usermem_size = sizeof(usermem);

	bzero(&bfi, sizeof(bfi));
	usermem = 0;
	if (sysctlbyname("hw.usermem", &usermem, &usermem_size, NULL, 0) == 0)
		bfi.size = usermem / 16;
	else
		bfi.size = 0;
	if (bfi.size < 8192 * 1024)
		bfi.size = 8192 * 1024;

	return hammer2_ioctl_bulkfree_scan(VTOI(vp), &bfi);
}

static int
hammer2_destroy_path(struct m_vnode *dvp, const char *f)
{
	hammer2_ioc_destroy_t destroy;
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	char *o, *p, *name;
	int error;

	assert(strlen(f) > 0);
	o = p = strdup(f);

	/* Trim trailing '/'. */
	p += strlen(p);
	p--;
	while (p >= o && *p == '/')
		p--;
	*++p = 0;

	name = p = o;
	if (strlen(p) == 0)
		return EINVAL;

	while ((p = strchr(p, '/')) != NULL) {
		*p++ = 0; /* NULL terminate name */
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, name, strlen(name));
		if (error)
			return error;

		ip = VTOI(vp);
		assert(ip->meta.type == HAMMER2_OBJTYPE_DIRECTORY);
		/* XXX When does (or why does not) ioctl modify this inode ? */
		hammer2_inode_modify(ip);

		dvp = vp;
		name = p;
	}

	bzero(&destroy, sizeof(destroy));
	destroy.cmd = HAMMER2_DELETE_FILE;
	snprintf(destroy.path, sizeof(destroy.path), "%s", name);

	printf("%s\t", f);
	fflush(stdout);

	error = hammer2_ioctl_destroy(VTOI(dvp), &destroy);
	if (error)
		printf("%s\n", strerror(error));
	else
		printf("ok\n");
	free(o);

	return error;
}

static int
hammer2_destroy_inum(struct m_vnode *vp, hammer2_tid_t inum)
{
	hammer2_ioc_destroy_t destroy;
	int error;

	bzero(&destroy, sizeof(destroy));
	destroy.cmd = HAMMER2_DELETE_INUM;
	destroy.inum = inum;

	printf("%jd\t", (intmax_t)destroy.inum);
	fflush(stdout);

	error = hammer2_ioctl_destroy(VTOI(vp), &destroy);
	if (error)
		printf("%s\n", strerror(error));
	else
		printf("ok\n");

	return error;
}

static int
hammer2_growfs(struct m_vnode *vp, hammer2_off_t size)
{
	hammer2_ioc_growfs_t growfs;
	int error;

	bzero(&growfs, sizeof(growfs));
	growfs.size = size;

	error = hammer2_ioctl_growfs(VTOI(vp), &growfs, NULL);
	if (!error) {
		if (growfs.modified)
			printf("grown to %016jx\n", (intmax_t)growfs.size);
		else
			printf("no size change - %016jx\n",
			    (intmax_t)growfs.size);
	}

	return error;
}
