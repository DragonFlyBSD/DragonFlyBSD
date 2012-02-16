/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
#ifndef _SYS_DEVFS_RULES_H_
#define	_SYS_DEVFS_RULES_H_

#include <sys/ioccom.h>

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#define DEVFS_MAX_POLICY_LENGTH	32

struct devfs_rule {
	u_long	rule_type;
	u_long	rule_cmd;

	char	*mntpoint;
	u_char	mntpointlen;

	char	*name;
	u_char	namlen;

	char	*linkname;
	u_char	linknamlen;


	u_long	dev_type;	/* Type of device to which the rule applies */
	u_short	mode;		/* files access mode and type */
	uid_t	uid;		/* owner user id */
	gid_t	gid;		/* owner group id */

	TAILQ_ENTRY(devfs_rule) 	link;
};

struct devfs_rule_ioctl {
	u_long	rule_type;
	u_long	rule_cmd;

	char	mntpoint[PATH_MAX];

	char	name[PATH_MAX];

	char	linkname[PATH_MAX];

	u_long	dev_type;	/* Type of device to which the rule applies */
	u_short	mode;		/* files access mode and type */
	uid_t	uid;		/* owner user id */
	gid_t	gid;		/* owner group id */
};

#define DEVFS_RULE_NAME		0x01
#define DEVFS_RULE_TYPE		0x02
#define	DEVFS_RULE_JAIL		0x04

#define	DEVFS_RULE_LINK		0x01
#define	DEVFS_RULE_HIDE		0x02
#define	DEVFS_RULE_SHOW		0x04
#define DEVFS_RULE_PERM		0x08

#define	DEVFS_RULE_ADD		_IOWR('d', 221, struct devfs_rule_ioctl)
#define	DEVFS_RULE_APPLY	_IOWR('d', 222, struct devfs_rule_ioctl)
#define	DEVFS_RULE_CLEAR	_IOWR('d', 223, struct devfs_rule_ioctl)
#define	DEVFS_RULE_RESET	_IOWR('d', 224, struct devfs_rule_ioctl)

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_DEVFS_H_
#include <sys/devfs.h>
#endif

TAILQ_HEAD(devfs_rule_head, devfs_rule);


void *devfs_rule_check_apply(struct devfs_node *, void *);
void *devfs_rule_reset_node(struct devfs_node *, void *);
#endif /* _KERNEL */
#endif /* _SYS_DEVFS_RULES_H_ */
