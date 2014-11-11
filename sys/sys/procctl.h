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

#ifndef _SYS_PROCCTL_H_
#define _SYS_PROCCTL_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/lock.h>
#endif

typedef enum idtype {
	/*
	 * These names were mostly lifted from Solaris source code and
	 * still use Solaris style naming to avoid breaking any
	 * OpenSolaris code which has been ported to FreeBSD.  There
	 * is no clear FreeBSD counterpart for all of the names, but
	 * some have a clear correspondence to FreeBSD entities.
	 *
	 * The numerical values are kept synchronized with the Solaris
	 * values.
	 */
	P_PID,			/* A process identifier. */
	P_PPID,			/* A parent process identifier.	*/
	P_PGID,			/* A process group identifier. */
	P_SID,			/* A session identifier. */
	P_CID,			/* A scheduling class identifier. */
	P_UID,			/* A user identifier. */
	P_GID,			/* A group identifier. */
	P_ALL,			/* All processes. */
	P_LWPID,		/* An LWP identifier. */
	P_TASKID,		/* A task identifier. */
	P_PROJID,		/* A project identifier. */
	P_POOLID,		/* A pool identifier. */
	P_JAILID,		/* A zone identifier. */
	P_CTID,			/* A (process) contract identifier. */
	P_CPUID,		/* CPU identifier. */
	P_PSETID		/* Processor set identifier. */
} idtype_t;			/* The type of id_t we are using. */

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

#define _PROCCTL_PRESENT

#define PROC_REAP_ACQUIRE	0x0001
#define PROC_REAP_RELEASE	0x0002
#define PROC_REAP_STATUS	0x0003

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

int procctl(idtype_t idtype, id_t id, int cmd, void *arg);

#endif

#endif
