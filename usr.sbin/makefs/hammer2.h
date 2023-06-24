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

#ifndef _HAMMER2_H
#define _HAMMER2_H

#include <sys/ioccom.h>
#include <limits.h>

#include "hammer2/hammer2.h"

#define HAMMER2IOC_READ		_IOWR('h', 999, int)

typedef struct {
	hammer2_mkfs_options_t mkfs_options;
	int label_specified;
	char mount_label[HAMMER2_INODE_MAXNAME];
	int num_volhdr;

	/* HAMMER2IOC_xxx */
	long ioctl_cmd;

	/* HAMMER2IOC_EMERG_MODE */
	bool emergency_mode;

	/* HAMMER2IOC_PFS_xxx */
	char pfs_cmd_name[NAME_MAX+1];
	char pfs_name[NAME_MAX+1];

	/* HAMMER2IOC_INODE_xxx */
	char inode_cmd_name[NAME_MAX];
	char inode_path[PATH_MAX];

	/* HAMMER2IOC_DESTROY */
	char destroy_path[PATH_MAX];
	hammer2_tid_t destroy_inum;

	/* HAMMER2IOC_READ */
	char read_path[PATH_MAX];

	hammer2_off_t image_size;
} hammer2_makefs_options_t;

#endif /* _HAMMER2_H */
