/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
 */

#ifndef _SYS_REAPER_H_
#define _SYS_REAPER_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/lock.h>
#endif

struct reaper_status {
	uint32_t	flags;
	uint32_t	refs;
	long		reserved1[15];
	pid_t		pid_head;
	int		reserved2[15];
};

union reaper_info {
	struct reaper_status	status;
};

#define _REAPER_PRESENT

#define REAPER_OP_ACQUIRE	0x0001
#define REAPER_OP_RELEASE	0x0002
#define REAPER_OP_STATUS	0x0003

#define REAPER_STAT_OWNED	0x00000001
#define REAPER_STAT_REALINIT	0x00000002

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct proc;

struct sysreaper {
	struct lock	lock;		/* thread or topo access */
	struct sysreaper *parent;	/* upward topology only */
	struct proc	*p;		/* who the reaper is */
	uint32_t	flags;		/* control flags */
	u_int		refs;		/* shared structure refs */
};

#endif

#if !defined(_KERNEL)

int reapctl(int op, union reaper_info *data);

#endif

#endif
