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

#ifndef	_SYS_UPMAP_H_
#define	_SYS_UPMAP_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif

#define UPMAP_MAXPROCTITLE	1024
#define UPMAP_MAPSIZE		65536
#define KPMAP_MAPSIZE		65536

#define UPMAP_VERSION		1
#define KPMAP_VERSION		1

typedef uint64_t	forkid_t;

typedef struct ukpheader {
	uint16_t	type;		/* element type */
	uint16_t	offset;		/* offset from map base, max 65535 */
} ukpheader_t;

#define UKPLEN_MASK		0x0F00
#define UKPLEN_1		0x0000
#define UKPLEN_2		0x0100
#define UKPLEN_4		0x0200
#define UKPLEN_8		0x0300
#define UKPLEN_16		0x0400
#define UKPLEN_32		0x0500
#define UKPLEN_64		0x0600
#define UKPLEN_128		0x0700
#define UKPLEN_256		0x0800
#define UKPLEN_512		0x0900
#define UKPLEN_1024		0x0A00

#define UKPLEN_TS		((sizeof(struct timespec) == 8) ? \
					UKPLEN_8 : UKPLEN_16)

#define UKPTYPE_VERSION		(0x0001 | UKPLEN_4)	/* always first */

#define UPTYPE_RUNTICKS		(0x0010 | UKPLEN_4)
#define UPTYPE_FORKID		(0x0011 | UKPLEN_8)
#define UPTYPE_PID		(0x0012 | UKPLEN_4)
#define UPTYPE_PROC_TITLE	(0x0013 | UKPLEN_1024)

#define KPTYPE_UPTICKS		(0x8000 | UKPLEN_4)
#define KPTYPE_TS_UPTIME	(0x8001 | UKPLEN_TS)
#define KPTYPE_TS_REALTIME	(0x8002 | UKPLEN_TS)
#define KPTYPE_TSC_FREQ		(0x8003 | UKPLEN_8)
#define KPTYPE_TICK_FREQ	(0x8003 | UKPLEN_8)

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * (writable) user per-process map via /dev/upmap.
 *
 * ABSOLUTE LOCATIONS CAN CHANGE, ITERATE HEADERS FOR THE TYPE YOU DESIRE
 * UNTIL YOU HIT TYPE 0, THEN CACHE THE RESULTING POINTER.
 *
 * If you insist, at least check that the version matches UPMAP_VERSION.
 */
struct sys_upmap {
	ukpheader_t	header[64];
	uint32_t	version;
	uint32_t	runticks;	/* running scheduler ticks */
	forkid_t	forkid;		/* unique 2^64 (fork detect) NOT MONO */
	uint32_t	unused01;	/* cpu migrations (kpmap detect) */
	pid_t		pid;		/* process id */
	uint32_t	reserved[16];
	char		proc_title[UPMAP_MAXPROCTITLE];
};

/*
 * (read-only) kernel per-cpu map via /dev/kpmap.
 *
 * ABSOLUTE LOCATIONS CAN CHANGE, ITERATE HEADERS FOR THE TYPE YOU DESIRE
 * UNTIL YOU HIT TYPE 0, THEN CACHE THE RESULTING POINTER.
 *
 * If you insist, at least check that the version matches KPMAP_VERSION.
 */
struct sys_kpmap {
	ukpheader_t	header[64];
	int32_t		version;
	int32_t		upticks;
	struct timespec	ts_uptime;	/* mono uptime @ticks (uncompensated) */
	struct timespec ts_realtime;	/* realtime @ticks resolution */
	int64_t		tsc_freq;	/* (if supported by cpu) */
	int32_t		tick_freq;	/* scheduler tick frequency */
};

#endif

#ifdef _KERNEL
extern struct sys_kpmap *kpmap;
#endif

#endif
