/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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

#ifndef LIBFSID_H
#define LIBFSID_H

#include <sys/types.h>
#include <sys/uio.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
	FSID_UNKNOWN,
	FSID_HAMMER,
	FSID_UFS,
	FSID_CD9660,
	FSID_EXT2,
	FSID_MSDOSFS
} fsid_t;

typedef fsid_t (probe_func_t)(const char *);
typedef char *(volname_func_t)(const char *);

struct fs_type {
	const char	*fs_name;
	probe_func_t	*fs_probe;
	volname_func_t	*fs_volname;
};

probe_func_t hammer_probe;
probe_func_t ufs_probe;
probe_func_t cd9660_probe;
probe_func_t ext2_probe;
probe_func_t msdosfs_probe;

volname_func_t hammer_volname;
volname_func_t ufs_volname;
volname_func_t cd9660_volname;
volname_func_t ext2_volname;
volname_func_t msdosfs_volname;

fsid_t fsid_probe(const char *dev, const char *fs_name);
fsid_t fsid_probe_all(const char *dev);

char *fsid_volname(const char *dev, const char *fs_name);
char *fsid_volname_all(const char *dev);

/* Extra functions */
const char *fsid_fsname(fsid_t);
int fsid_fs_count(void);

#ifdef _FSID_INTERNAL
int fsid_dev_read(const char *dev, off_t off, size_t len, char *buf);
#endif

#endif /* LIBFSID_H */
