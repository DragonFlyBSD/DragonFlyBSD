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
#include <sys/time.h>
#include <sys/dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
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
static void hammer2_parse_inode_opts(const char *, fsinfo_t *);
static void hammer2_dump_fsinfo(fsinfo_t *);
static int hammer2_create_image(const char *, fsinfo_t *);
static int hammer2_populate_dir(struct m_vnode *, const char *, fsnode *,
    fsnode *, fsinfo_t *, int);
static void hammer2_validate(const char *, fsnode *, fsinfo_t *);
static void hammer2_size_dir(fsnode *, fsinfo_t *);
static int hammer2_write_file(struct m_vnode *, const char *, fsnode *);
static int hammer2_version_get(struct m_vnode *);
static int hammer2_pfs_get(struct m_vnode *);
static int hammer2_pfs_lookup(struct m_vnode *, const char *);
static int hammer2_pfs_create(struct m_vnode *, const char *);
static int hammer2_pfs_delete(struct m_vnode *, const char *);
static int hammer2_pfs_snapshot(struct m_vnode *, const char *, const char *);
static int hammer2_inode_getx(struct m_vnode *, const char *);
static int hammer2_inode_setcheck(struct m_vnode *, const char *);
static int hammer2_inode_setcomp(struct m_vnode *, const char *);
static int hammer2_bulkfree(struct m_vnode *);
static int hammer2_destroy_path(struct m_vnode *, const char *);
static int hammer2_destroy_inum(struct m_vnode *, hammer2_tid_t);
static int hammer2_growfs(struct m_vnode *, hammer2_off_t);
struct hammer2_linkq;
static int hammer2_readx_handle(struct m_vnode *, const char *, const char *,
    struct hammer2_linkq *);
static int hammer2_readx(struct m_vnode *, const char *, const char *);

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
		{ 'c', "CompressionType", NULL, OPT_STRBUF, 0, 0, "compression type" },
		{ 'C', "CheckType", NULL, OPT_STRBUF, 0, 0, "check type" },
		{ 'd', "Hammer2Debug", NULL, OPT_STRBUF, 0, 0, "debug tunable" },
		{ 'E', "EmergencyMode", &h2_opt->emergency_mode, OPT_BOOL, 0, 0,
		    "emergency mode" },
		{ 'P', "PFS", NULL, OPT_STRBUF, 0, 0, "offline PFS" },
		{ 'I', "Inode", NULL, OPT_STRBUF, 0, 0, "offline inode" },
		{ 'B', "Bulkfree", NULL, OPT_STRBUF, 0, 0, "offline bulkfree" },
		{ 'D', "Destroy", NULL, OPT_STRBUF, 0, 0, "offline destroy" },
		{ 'G', "Growfs", NULL, OPT_STRBUF, 0, 0, "offline growfs" },
		{ 'R', "Read", NULL, OPT_STRBUF, 0, 0, "offline read" },
		{ .name = NULL },
	};

	hammer2_mkfs_init(opt);

	assert(opt->CompType == HAMMER2_COMP_DEFAULT);
	assert(opt->CheckType == HAMMER2_CHECK_DEFAULT);

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
		if (strlen(buf) == 0) {
			h2_opt->ioctl_cmd = HAMMER2IOC_VERSION_GET;
		} else {
			opt->Hammer2Version = strtol(buf, NULL, 0);
			if (opt->Hammer2Version < HAMMER2_VOL_VERSION_MIN ||
			    opt->Hammer2Version >= HAMMER2_VOL_VERSION_WIP)
				errx(1, "I don't understand how to format "
				    "HAMMER2 version %d",
				    opt->Hammer2Version);
		}
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
	case 'c':
		if (strlen(buf) == 0)
			errx(1, "Compression type '%s' cannot be 0-length", buf);
		if (strcasecmp(buf, "none") == 0)
			opt->CompType = HAMMER2_COMP_NONE;
		else if (strcasecmp(buf, "autozero") == 0)
			opt->CompType = HAMMER2_COMP_AUTOZERO;
		else if (strcasecmp(buf, "lz4") == 0)
			opt->CompType = HAMMER2_COMP_LZ4;
		else if (strcasecmp(buf, "zlib") == 0)
			opt->CompType = HAMMER2_COMP_ZLIB;
		else
			errx(1, "Invalid compression type '%s'", buf);
		break;
	case 'C':
		if (strlen(buf) == 0)
			errx(1, "Check type '%s' cannot be 0-length", buf);
		if (strcasecmp(buf, "none") == 0)
			opt->CheckType = HAMMER2_CHECK_NONE;
		else if (strcasecmp(buf, "disabled") == 0)
			opt->CheckType = HAMMER2_CHECK_DISABLED;
		else if (strcasecmp(buf, "iscsi32") == 0)
			opt->CheckType = HAMMER2_CHECK_ISCSI32;
		else if (strcasecmp(buf, "xxhash64") == 0)
			opt->CheckType = HAMMER2_CHECK_XXHASH64;
		else if (strcasecmp(buf, "sha192") == 0)
			opt->CheckType = HAMMER2_CHECK_SHA192;
		else if (strcasecmp(buf, "freemap") == 0)
			opt->CheckType = HAMMER2_CHECK_FREEMAP;
		else
			errx(1, "Invalid check type '%s'", buf);
		break;
	case 'd':
		hammer2_debug = strtoll(buf, NULL, 0);
		break;
	case 'P':
		if (strlen(buf) == 0)
			errx(1, "PFS argument '%s' cannot be 0-length", buf);
		hammer2_parse_pfs_opts(buf, fsopts);
		break;
	case 'I':
		if (strlen(buf) == 0)
			errx(1, "Inode argument '%s' cannot be 0-length", buf);
		hammer2_parse_inode_opts(buf, fsopts);
		break;
	case 'B':
		h2_opt->ioctl_cmd = HAMMER2IOC_BULKFREE_SCAN;
		break;
	case 'D':
		h2_opt->ioctl_cmd = HAMMER2IOC_DESTROY;
		if (strlen(buf) == 0)
			errx(1, "Destroy argument '%s' cannot be 0-length", buf);
		if (buf[0] == '/') {
			strlcpy(h2_opt->destroy_path, buf,
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
	case 'R':
		h2_opt->ioctl_cmd = HAMMER2IOC_READ;
		if (strlen(buf) == 0)
			errx(1, "Read argument '%s' cannot be 0-length", buf);
		strlcpy(h2_opt->read_path, buf, sizeof(h2_opt->read_path));
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

	/* ioctl commands could have NULL dir / root */
	assert(image != NULL);
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
	case HAMMER2IOC_VERSION_GET:
		printf("version get `%s'\n", image);
		TIMER_START(start);
		error = hammer2_version_get(vroot);
		if (error)
			errx(1, "version get `%s' failed '%s'", image,
			    strerror(error));
		TIMER_RESULTS(start, "hammer2_version_get");
		break;
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
	case HAMMER2IOC_INODE_GET:
		printf("inode %s `%s'\n", h2_opt->inode_cmd_name, image);
		TIMER_START(start);
		error = hammer2_inode_getx(vroot, h2_opt->inode_path);
		if (error)
			errx(1, "inode %s `%s' failed '%s'",
			    h2_opt->inode_cmd_name, image, strerror(error));
		TIMER_RESULTS(start, "hammer2_inode_getx");
		break;
	case HAMMER2IOC_INODE_SET:
		printf("inode %s `%s'\n", h2_opt->inode_cmd_name, image);
		TIMER_START(start);
		if (!strcmp(h2_opt->inode_cmd_name, "setcheck")) {
			error = hammer2_inode_setcheck(vroot,
			    h2_opt->inode_path);
			if (error)
				errx(1, "inode %s `%s' failed '%s'",
				    h2_opt->inode_cmd_name, image,
				    strerror(error));
		} else if (!strcmp(h2_opt->inode_cmd_name, "setcomp")) {
			error = hammer2_inode_setcomp(vroot,
			    h2_opt->inode_path);
			if (error)
				errx(1, "inode %s `%s' failed '%s'",
				    h2_opt->inode_cmd_name, image,
				    strerror(error));
		} else {
			assert(0);
		}
		TIMER_RESULTS(start, "hammer2_inode_setx");
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
	case HAMMER2IOC_READ:
		printf("read `%s'\n", image);
		TIMER_START(start);
		error = hammer2_readx(vroot, dir, h2_opt->read_path);
		if (error)
			errx(1, "read `%s' failed '%s'", image,
			    strerror(error));
		TIMER_RESULTS(start, "hammer2_readx");
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

static void
hammer2_parse_inode_opts(const char *buf, fsinfo_t *fsopts)
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

	if (!strcmp(o, "get")) {
		if (n == 0 || n > PATH_MAX)
			errx(1, "invalid file path \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_INODE_GET;
	} else if (!strcmp(o, "setcheck")) {
		if (n == 0 || n > PATH_MAX - 10)
			errx(1, "invalid argument \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_INODE_SET;
	} else if (!strcmp(o, "setcomp")) {
		if (n == 0 || n > PATH_MAX - 10)
			errx(1, "invalid argument \"%s\"", p);
		h2_opt->ioctl_cmd = HAMMER2IOC_INODE_SET;
	} else {
		errx(1, "invalid inode command \"%s\"", o);
	}

	strlcpy(h2_opt->inode_cmd_name, o, sizeof(h2_opt->inode_cmd_name));
	if (n > 0)
		strlcpy(h2_opt->inode_path, p, sizeof(h2_opt->inode_path));

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

	/* ioctl commands could have NULL dir / root */
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
	printf("\tinode_cmd_name \"%s\"\n", h2_opt->inode_cmd_name);
	printf("\tinode_path \"%s\"\n", h2_opt->inode_path);
	printf("\tdestroy_path \"%s\"\n", h2_opt->destroy_path);
	printf("\tdestroy_inum %lld\n", (long long)h2_opt->destroy_inum);
	printf("\tread_path \"%s\"\n", h2_opt->read_path);
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
hammer2_setup_blkdev(const char *image, fsinfo_t *fsopts)
{
	hammer2_makefs_options_t *h2_opt = fsopts->fs_specific;
	hammer2_off_t size;

	if ((fsopts->fd = open(image, O_RDWR)) == -1) {
		warn("can't open `%s' for writing", image);
		return -1;
	}

	size = check_volume(fsopts->fd);
	if (h2_opt->image_size > size) {
		warnx("image size %lld exceeds %s size %lld",
		    (long long)h2_opt->image_size, image, (long long)size);
		return -1;
	}

	return 0;
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
	struct stat st;

	assert(image != NULL);
	assert(fsopts != NULL);

	/* check if image is blk or chr */
	if (stat(image, &st) == 0) {
		if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
			if (hammer2_setup_blkdev(image, fsopts))
				return -1;
			goto done;
		}
	}

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
done:
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
			if (snprintf(f, sizeof(f), "%s/%s/%s",
			    cur->root, cur->path, cur->name) >= (int)sizeof(f))
				errx(1, "path %s too long", f);
			path = f;
		}
		if (S_ISLNK(cur->type)) {
			if (lstat(path, &st) == -1)
				err(1, "no such symlink %s", path);
		} else {
			if (stat(path, &st) == -1)
				err(1, "no such path %s", path);
		}

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

static int
trim_char(char *p, char c)
{
	char *o, tmp[PATH_MAX];
	bool prev_was_c;
	size_t n;
	int i;

	assert(p);
	/* nothing to do */
	if (strlen(p) == 0)
		return 0;

	strlcpy(tmp, p, sizeof(tmp));
	if (strncmp(tmp, p, sizeof(tmp)))
		return ENOSPC;

	/* trim consecutive */
	prev_was_c = false;
	o = p;
	n = strlen(p);

	for (i = 0; i < n; i++) {
		if (tmp[i] == c) {
			if (!prev_was_c)
				*p++ = tmp[i];
			prev_was_c = true;
		} else {
			*p++ = tmp[i];
			prev_was_c = false;
		}
	}
	*p = 0;
	assert(strlen(p) <= strlen(tmp));

	/* assert no consecutive */
	prev_was_c = false;
	p = o;
	n = strlen(p);

	for (i = 0; i < n; i++) {
		if (p[i] == c) {
			assert(!prev_was_c);
			prev_was_c = true;
		} else {
			prev_was_c = false;
		}
	}

	/* trim leading */
	if (*p == c)
		memmove(p, p + 1, strlen(p + 1) + 1);
	assert(*p != '/');

	/* trim trailing */
	p += strlen(p);
	p--;
	if (*p == c)
		*p = 0;
	assert(p[strlen(p) - 1] != '/');

	return 0;
}

static int
trim_slash(char *p)
{
	return trim_char(p, '/');
}

static bool
is_supported_link(const char *s)
{
	/* absolute path can't be supported */
	if (strlen(s) >= 1 && strncmp(s, "/", 1) == 0)
		return false;

	/* XXX ".." is currently unsupported */
	if (strlen(s) >= 3 && strncmp(s, "../", 3) == 0)
		return false;

	return true;
}

static int
hammer2_version_get(struct m_vnode *vp)
{
	hammer2_dev_t *hmp;

	hmp = VTOI(vp)->pmp->pfs_hmps[0];
	if (hmp == NULL)
		return EINVAL;

	printf("version: %d\n", hmp->voldata.version);

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
hammer2_inode_getx(struct m_vnode *dvp, const char *f)
{
	hammer2_ioc_inode_t inode;
	hammer2_inode_t *ip;
	hammer2_inode_meta_t *meta;
	struct m_vnode *vp;
	char *o, *p, *name, *str = NULL;
	char tmp[PATH_MAX];
	int error;
	uuid_t uuid;

	assert(strlen(f) > 0);
	o = p = name = strdup(f);

	error = trim_slash(p);
	if (error)
		return error;
	if (strlen(p) == 0) {
		vp = dvp;
		goto start_ioctl;
	}

	while ((p = strchr(p, '/')) != NULL) {
		*p++ = 0; /* NULL terminate name */
		if (!strcmp(name, ".")) {
			name = p;
			continue;
		}
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, name, strlen(name));
		if (error)
			return error;

		ip = VTOI(vp);
		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			bzero(tmp, sizeof(tmp));
			error = hammer2_readlink(vp, tmp, sizeof(tmp));
			if (error)
				return error;
			if (!is_supported_link(tmp))
				return EINVAL;
			strlcat(tmp, "/", sizeof(tmp));
			strlcat(tmp, p, sizeof(tmp));
			error = trim_slash(tmp);
			if (error)
				return error;
			p = name = tmp;
			continue;
		default:
			return EINVAL;
		}

		dvp = vp;
		name = p;
	}

	error = hammer2_nresolve(dvp, &vp, name, strlen(name));
	if (error)
		return error;
start_ioctl:
	bzero(&inode, sizeof(inode));
	error = hammer2_ioctl_inode_get(VTOI(vp), &inode);
	if (error)
		return error;

	meta = &inode.ip_data.meta;
	printf("--------------------\n");
	printf("flags = 0x%x\n", inode.flags);
	printf("data_count = %ju\n", (uintmax_t)inode.data_count);
	printf("inode_count = %ju\n", (uintmax_t)inode.inode_count);
	printf("--------------------\n");
	printf("version = %u\n", meta->version);
	printf("pfs_subtype = %u (%s)\n", meta->pfs_subtype,
	    hammer2_pfssubtype_to_str(meta->pfs_subtype));
	printf("uflags = 0x%x\n", (unsigned int)meta->uflags);
	printf("rmajor = %u\n", meta->rmajor);
	printf("rminor = %u\n", meta->rminor);
	printf("ctime = %s\n", hammer2_time64_to_str(meta->ctime, &str));
	printf("mtime = %s\n", hammer2_time64_to_str(meta->mtime, &str));
	printf("atime = %s\n", hammer2_time64_to_str(meta->atime, &str));
	printf("btime = %s\n", hammer2_time64_to_str(meta->btime, &str));
	uuid = meta->uid;
	printf("uid = %s\n", hammer2_uuid_to_str(&uuid, &str));
	uuid = meta->gid;
	printf("gid = %s\n", hammer2_uuid_to_str(&uuid, &str));
	printf("type = %u (%s)\n", meta->type,
	    hammer2_iptype_to_str(meta->type));
	printf("op_flags = 0x%x\n", meta->op_flags);
	printf("cap_flags = 0x%x\n", meta->cap_flags);
	printf("mode = 0%o\n", meta->mode);
	printf("inum = 0x%jx\n", (uintmax_t)meta->inum);
	printf("size = %ju\n", (uintmax_t)meta->size);
	printf("nlinks = %ju\n", (uintmax_t)meta->nlinks);
	printf("iparent = 0x%jx\n", (uintmax_t)meta->iparent);
	printf("name_key = 0x%jx\n", (uintmax_t)meta->name_key);
	printf("name_len = %u\n", meta->name_len);
	printf("ncopies = %u\n", meta->ncopies);
	printf("comp_algo = 0x%jx\n", (uintmax_t)meta->comp_algo);
	printf("check_algo = %u\n", meta->check_algo);
	printf("pfs_nmasters = %u\n", meta->pfs_nmasters);
	printf("pfs_type = %u (%s)\n", meta->pfs_type,
	    hammer2_pfstype_to_str(meta->pfs_type));
	printf("pfs_inum = 0x%jx\n", (uintmax_t)meta->pfs_inum);
	uuid = meta->pfs_clid;
	printf("pfs_clid = %s\n", hammer2_uuid_to_str(&uuid, &str));
	uuid = meta->pfs_fsid;
	printf("pfs_fsid = %s\n", hammer2_uuid_to_str(&uuid, &str));
	printf("data_quota = 0x%jx\n", (uintmax_t)meta->data_quota);
	printf("inode_quota = 0x%jx\n", (uintmax_t)meta->inode_quota);
	printf("pfs_lsnap_tid = 0x%jx\n", (uintmax_t)meta->pfs_lsnap_tid);
	printf("decrypt_check = 0x%jx\n", (uintmax_t)meta->decrypt_check);
	printf("--------------------\n");

	free(o);

	return error;
}

static int
hammer2_inode_setcheck(struct m_vnode *dvp, const char *f)
{
	hammer2_ioc_inode_t inode;
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	char *o, *p, *name, *check_algo_str;
	char tmp[PATH_MAX];
	const char *checks[] = { "none", "disabled", "crc32", "xxhash64",
	    "sha192", };
	int check_algo_idx, error;
	uint8_t check_algo;

	assert(strlen(f) > 0);
	o = p = strdup(f);

	p = strrchr(p, ':');
	if (p == NULL)
		return EINVAL;

	*p++ = 0; /* NULL terminate path */
	check_algo_str = p;
	name = p = o;

	/* fail if already empty before trim */
	if (strlen(p) == 0)
		return EINVAL;

	error = trim_slash(p);
	if (error)
		return error;
	if (strlen(check_algo_str) == 0)
		return EINVAL;

	/* convert check_algo_str to check_algo_idx */
	check_algo_idx = nitems(checks);
	while (--check_algo_idx >= 0)
		if (strcasecmp(check_algo_str, checks[check_algo_idx]) == 0)
			break;
	if (check_algo_idx < 0) {
		if (strcasecmp(check_algo_str, "default") == 0) {
			check_algo_str = "xxhash64";
			check_algo_idx = HAMMER2_CHECK_XXHASH64;
		} else if (strcasecmp(check_algo_str, "disabled") == 0) {
			check_algo_str = "disabled";
			check_algo_idx = HAMMER2_CHECK_DISABLED;
		} else {
			printf("invalid check_algo_str: %s\n", check_algo_str);
			return EINVAL;
		}
	}
	check_algo = HAMMER2_ENC_ALGO(check_algo_idx);
	printf("change %s to algo %d (%s)\n", p, check_algo, check_algo_str);

	if (strlen(p) == 0) {
		vp = dvp;
		goto start_ioctl;
	}

	while ((p = strchr(p, '/')) != NULL) {
		*p++ = 0; /* NULL terminate name */
		if (!strcmp(name, ".")) {
			name = p;
			continue;
		}
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, name, strlen(name));
		if (error)
			return error;

		ip = VTOI(vp);
		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			bzero(tmp, sizeof(tmp));
			error = hammer2_readlink(vp, tmp, sizeof(tmp));
			if (error)
				return error;
			if (!is_supported_link(tmp))
				return EINVAL;
			strlcat(tmp, "/", sizeof(tmp));
			strlcat(tmp, p, sizeof(tmp));
			error = trim_slash(tmp);
			if (error)
				return error;
			p = name = tmp;
			continue;
		default:
			return EINVAL;
		}

		dvp = vp;
		name = p;
	}

	error = hammer2_nresolve(dvp, &vp, name, strlen(name));
	if (error)
		return error;
start_ioctl:
	ip = VTOI(vp);

	bzero(&inode, sizeof(inode));
	error = hammer2_ioctl_inode_get(ip, &inode);
	if (error)
		return error;

	inode.flags |= HAMMER2IOC_INODE_FLAG_CHECK;
	inode.ip_data.meta.check_algo = check_algo;
	error = hammer2_ioctl_inode_set(ip, &inode);
	if (error)
		return error;

	free(o);

	return error;
}

static int
hammer2_inode_setcomp(struct m_vnode *dvp, const char *f)
{
	hammer2_ioc_inode_t inode;
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	char *o, *p, *name, *comp_algo_str, *comp_level_str;
	char tmp[PATH_MAX];
	const char *comps[] = { "none", "autozero", "lz4", "zlib", };
	int comp_algo_idx, comp_level_idx, error;
	uint8_t comp_algo, comp_level;

	assert(strlen(f) > 0);
	o = p = strdup(f);

	p = strrchr(p, ':');
	if (p == NULL)
		return EINVAL;

	*p++ = 0; /* NULL terminate comp_algo_str */
	comp_level_str = p;
	p = o;

	p = strrchr(p, ':');
	if (p == NULL) {
		/* comp_level_str not specified */
		comp_algo_str = comp_level_str;
		comp_level_str = NULL;
	} else {
		*p++ = 0; /* NULL terminate path */
		comp_algo_str = p;
	}
	name = p = o;

	/* fail if already empty before trim */
	if (strlen(p) == 0)
		return EINVAL;

	error = trim_slash(p);
	if (error)
		return error;
	if (strlen(comp_algo_str) == 0)
		return EINVAL;

	/* convert comp_algo_str to comp_algo_idx */
	comp_algo_idx = nitems(comps);
	while (--comp_algo_idx >= 0)
		if (strcasecmp(comp_algo_str, comps[comp_algo_idx]) == 0)
			break;
	if (comp_algo_idx < 0) {
		if (strcasecmp(comp_algo_str, "default") == 0) {
			comp_algo_str = "lz4";
			comp_algo_idx = HAMMER2_COMP_LZ4;
		} else if (strcasecmp(comp_algo_str, "disabled") == 0) {
			comp_algo_str = "autozero";
			comp_algo_idx = HAMMER2_COMP_AUTOZERO;
		} else {
			printf("invalid comp_algo_str: %s\n", comp_algo_str);
			return EINVAL;
		}
	}
	comp_algo = HAMMER2_ENC_ALGO(comp_algo_idx);

	/* convert comp_level_str to comp_level_idx */
	if (comp_level_str == NULL) {
		comp_level_idx = 0;
	} else if (isdigit((int)comp_level_str[0])) {
		comp_level_idx = strtol(comp_level_str, NULL, 0);
	} else if (strcasecmp(comp_level_str, "default") == 0) {
		comp_level_idx = 0;
	} else {
		printf("invalid comp_level_str: %s\n", comp_level_str);
		return EINVAL;
	}
	if (comp_level_idx) {
		switch (comp_algo) {
		case HAMMER2_COMP_ZLIB:
			if (comp_level_idx < 6 || comp_level_idx > 9) {
				printf("unsupported comp_level %d for %s\n",
				    comp_level_idx, comp_algo_str);
				return EINVAL;
			}
			break;
		default:
			printf("unsupported comp_level %d for %s\n",
			    comp_level_idx, comp_algo_str);
			return EINVAL;
		}
	}
	comp_level = HAMMER2_ENC_LEVEL(comp_level_idx);
	printf("change %s to algo %d (%s) level %d\n",
	    p, comp_algo, comp_algo_str, comp_level_idx);

	if (strlen(p) == 0) {
		vp = dvp;
		goto start_ioctl;
	}

	while ((p = strchr(p, '/')) != NULL) {
		*p++ = 0; /* NULL terminate name */
		if (!strcmp(name, ".")) {
			name = p;
			continue;
		}
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, name, strlen(name));
		if (error)
			return error;

		ip = VTOI(vp);
		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			bzero(tmp, sizeof(tmp));
			error = hammer2_readlink(vp, tmp, sizeof(tmp));
			if (error)
				return error;
			if (!is_supported_link(tmp))
				return EINVAL;
			strlcat(tmp, "/", sizeof(tmp));
			strlcat(tmp, p, sizeof(tmp));
			error = trim_slash(tmp);
			if (error)
				return error;
			p = name = tmp;
			continue;
		default:
			return EINVAL;
		}

		dvp = vp;
		name = p;
	}

	error = hammer2_nresolve(dvp, &vp, name, strlen(name));
	if (error)
		return error;
start_ioctl:
	ip = VTOI(vp);

	bzero(&inode, sizeof(inode));
	error = hammer2_ioctl_inode_get(ip, &inode);
	if (error)
		return error;

	inode.flags |= HAMMER2IOC_INODE_FLAG_COMP;
	inode.ip_data.meta.comp_algo = comp_algo | comp_level;
	error = hammer2_ioctl_inode_set(ip, &inode);
	if (error)
		return error;

	free(o);

	return error;
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
	char tmp[PATH_MAX];
	int error;

	assert(strlen(f) > 0);
	o = p = name = strdup(f);

	error = trim_slash(p);
	if (error)
		return error;
	if (strlen(p) == 0)
		return EINVAL;

	while ((p = strchr(p, '/')) != NULL) {
		*p++ = 0; /* NULL terminate name */
		if (!strcmp(name, ".")) {
			name = p;
			continue;
		}
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, name, strlen(name));
		if (error)
			return error;

		ip = VTOI(vp);
		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			bzero(tmp, sizeof(tmp));
			error = hammer2_readlink(vp, tmp, sizeof(tmp));
			if (error)
				return error;
			if (!is_supported_link(tmp))
				return EINVAL;
			strlcat(tmp, "/", sizeof(tmp));
			strlcat(tmp, p, sizeof(tmp));
			error = trim_slash(tmp);
			if (error)
				return error;
			p = name = tmp;
			continue;
		default:
			return EINVAL;
		}

		dvp = vp;
		name = p;
	}

	/* XXX When does (or why does not) ioctl modify this inode ? */
	hammer2_inode_modify(VTOI(dvp));

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

struct hammer2_link {
	TAILQ_ENTRY(hammer2_link) entry;
	hammer2_tid_t inum;
	uint64_t nlinks;
	char path[PATH_MAX];
};

TAILQ_HEAD(hammer2_linkq, hammer2_link);

static void
hammer2_linkq_init(struct hammer2_linkq *linkq)
{
	TAILQ_INIT(linkq);
}

static void
hammer2_linkq_cleanup(struct hammer2_linkq *linkq, bool is_root)
{
	struct hammer2_link *e;
	int count = 0;

	/*
	 * If is_root is true, linkq must be empty, or link count is broken.
	 * Note that if an image was made by makefs, hardlinks in the source
	 * directory became hardlinks in the image only if >1 links existed under
	 * that directory, as makefs doesn't determine hardlink via link count.
	 */
	while ((e = TAILQ_FIRST(linkq)) != NULL) {
		count++;
		TAILQ_REMOVE(linkq, e, entry);
		free(e);
	}
	assert(TAILQ_EMPTY(linkq));

	if (count && is_root)
		errx(1, "%d link entries remained", count);
}

static void
hammer2_linkq_add(struct hammer2_linkq *linkq, hammer2_tid_t inum,
    uint64_t nlinks, const char *path)
{
	struct hammer2_link *e;
	int count = 0;

	e = ecalloc(1, sizeof(*e));
	e->inum = inum;
	e->nlinks = nlinks;
	strlcpy(e->path, path, sizeof(e->path));
	TAILQ_INSERT_TAIL(linkq, e, entry);

	TAILQ_FOREACH(e, linkq, entry)
		if (e->inum == inum)
			count++;
	if (count > 1)
		errx(1, "%d link entries exist for inum %jd",
		    count, (intmax_t)inum);
}

static void
hammer2_linkq_del(struct hammer2_linkq *linkq, hammer2_tid_t inum)
{
	struct hammer2_link *e, *next;

	TAILQ_FOREACH_MUTABLE(e, linkq, entry, next)
		if (e->inum == inum) {
			e->nlinks--;
			if (e->nlinks == 1) {
				TAILQ_REMOVE(linkq, e, entry);
				free(e);
			}
		}
}

static void
hammer2_utimes(struct m_vnode *vp, const char *f)
{
	hammer2_inode_t *ip = VTOI(vp);
	struct timeval tv[2];

	hammer2_time_to_timeval(ip->meta.atime, &tv[0]);
	hammer2_time_to_timeval(ip->meta.mtime, &tv[1]);

	utimes(f, tv); /* ignore failure */
}

static int
hammer2_readx_directory(struct m_vnode *dvp, const char *dir, const char *name,
    struct hammer2_linkq *linkq)
{
	struct m_vnode *vp;
	struct dirent *dp;
	struct stat st;
	char *buf, tmp[PATH_MAX];
	off_t offset = 0;
	int ndirent = 0;
	int eofflag = 0;
	int i, error;

	snprintf(tmp, sizeof(tmp), "%s/%s", dir, name);
	if (stat(tmp, &st) == -1 && mkdir(tmp, 0666) == -1)
		err(1, "failed to mkdir %s", tmp);

	buf = ecalloc(1, HAMMER2_PBUFSIZE);

	while (!eofflag) {
		error = hammer2_readdir(dvp, buf, HAMMER2_PBUFSIZE, &offset,
		    &ndirent, &eofflag);
		if (error)
			errx(1, "failed to readdir");
		dp = (void *)buf;

		for (i = 0; i < ndirent; i++) {
			if (strcmp(dp->d_name, ".") &&
			    strcmp(dp->d_name, "..")) {
				error = hammer2_nresolve(dvp, &vp, dp->d_name,
				    strlen(dp->d_name));
				if (error)
					return error;
				error = hammer2_readx_handle(vp, tmp,
				    dp->d_name, linkq);
				if (error)
					return error;
			}
			dp = (void *)((char *)dp +
			    _DIRENT_RECLEN(dp->d_namlen));
		}
	}

	free(buf);
	hammer2_utimes(dvp, tmp);

	return 0;
}

static int
hammer2_readx_link(struct m_vnode *vp, const char *src, const char *lnk,
    struct hammer2_linkq *linkq)
{
	hammer2_inode_t *ip = VTOI(vp);
	struct stat st;
	int error;

	if (!stat(lnk, &st)) {
		error = unlink(lnk);
		if (error)
			return error;
	}

	error = link(src, lnk);
	if (error)
		return error;

	hammer2_linkq_del(linkq, ip->meta.inum);

	return 0;
}

static int
hammer2_readx_regfile(struct m_vnode *vp, const char *dir, const char *name,
    struct hammer2_linkq *linkq)
{
	hammer2_inode_t *ip = VTOI(vp);
	struct hammer2_link *e;
	char *buf, out[PATH_MAX];
	size_t resid, n;
	off_t offset;
	int fd, error;
	bool found = false;

	snprintf(out, sizeof(out), "%s/%s", dir, name);

	if (ip->meta.nlinks > 1) {
		TAILQ_FOREACH(e, linkq, entry)
			if (e->inum == ip->meta.inum) {
				found = true;
				error = hammer2_readx_link(vp, e->path, out,
				    linkq);
				if (error == 0)
					return 0;
				/* ignore failure */
			}
		if (!found)
			hammer2_linkq_add(linkq, ip->meta.inum, ip->meta.nlinks,
			    out);
	}

	fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd == -1)
		err(1, "failed to create %s", out);

	buf = ecalloc(1, HAMMER2_PBUFSIZE);
	resid = ip->meta.size;
	offset = 0;

	while (resid > 0) {
		bzero(buf, HAMMER2_PBUFSIZE);
		error = hammer2_read(vp, buf, HAMMER2_PBUFSIZE, offset);
		if (error)
			errx(1, "failed to read from %s", name);

		n = resid >= HAMMER2_PBUFSIZE ? HAMMER2_PBUFSIZE : resid;
		error = write(fd, buf, n);
		if (error == -1)
			err(1, "failed to write to %s", out);
		else if (error != n)
			return EINVAL;

		resid -= n;
		offset += HAMMER2_PBUFSIZE;
	}
	fsync(fd);
	close(fd);

	free(buf);
	hammer2_utimes(vp, out);

	return 0;
}

static int
hammer2_readx_handle(struct m_vnode *vp, const char *dir, const char *name,
    struct hammer2_linkq *linkq)
{
	hammer2_inode_t *ip = VTOI(vp);

	switch (ip->meta.type) {
	case HAMMER2_OBJTYPE_DIRECTORY:
		return hammer2_readx_directory(vp, dir, name, linkq);
	case HAMMER2_OBJTYPE_REGFILE:
		return hammer2_readx_regfile(vp, dir, name, linkq);
	default:
		/* XXX */
		printf("ignore inode %jd %s \"%s\"\n",
		    (intmax_t)ip->meta.inum,
		    hammer2_iptype_to_str(ip->meta.type),
		    name);
		return 0;
	}
	return EINVAL;
}

static int
hammer2_readx(struct m_vnode *dvp, const char *dir, const char *f)
{
	hammer2_inode_t *ip;
	struct hammer2_linkq linkq;
	struct m_vnode *vp, *ovp = dvp;
	char *o, *p, *name;
	char tmp[PATH_MAX];
	int error;

	if (dir == NULL)
		return EINVAL;

	assert(strlen(f) > 0);
	o = p = name = strdup(f);

	error = trim_slash(p);
	if (error)
		return error;
	if (strlen(p) == 0) {
		vp = dvp;
		goto start_read;
	}

	while ((p = strchr(p, '/')) != NULL) {
		*p++ = 0; /* NULL terminate name */
		if (!strcmp(name, ".")) {
			name = p;
			continue;
		}
		vp = NULL;
		error = hammer2_nresolve(dvp, &vp, name, strlen(name));
		if (error)
			return error;

		ip = VTOI(vp);
		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			bzero(tmp, sizeof(tmp));
			error = hammer2_readlink(vp, tmp, sizeof(tmp));
			if (error)
				return error;
			if (!is_supported_link(tmp))
				return EINVAL;
			strlcat(tmp, "/", sizeof(tmp));
			strlcat(tmp, p, sizeof(tmp));
			error = trim_slash(tmp);
			if (error)
				return error;
			p = name = tmp;
			continue;
		default:
			return EINVAL;
		}

		dvp = vp;
		name = p;
	}

	error = hammer2_nresolve(dvp, &vp, name, strlen(name));
	if (error)
		return error;
start_read:
	hammer2_linkq_init(&linkq);
	error = hammer2_readx_handle(vp, dir, name, &linkq);
	hammer2_linkq_cleanup(&linkq, vp == ovp);
	if (error)
		return error;

	free(o);

	return 0;
}
