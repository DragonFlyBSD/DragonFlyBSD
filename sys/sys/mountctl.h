/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 *
 * $DragonFly: src/sys/sys/mountctl.h,v 1.1 2004/12/24 05:00:22 dillon Exp $
 */

/*
 * Data structures for the journaling API
 */

#define MOUNTCTL_INSTALL_VFS_JOURNAL	1
#define MOUNTCTL_REMOVE_VFS_JOURNAL	2
#define MOUNTCTL_RESYNC_VFS_JOURNAL	3
#define MOUNTCTL_JOURNAL_VFS_STATUS	4

#define MOUNTCTL_INSTALL_BLK_JOURNAL	8
#define MOUNTCTL_REMOVE_BLK_JOURNAL	9
#define MOUNTCTL_RESYNC_BLK_JOURNAL	10
#define MOUNTCTL_JOURNAL_BLK_STATUS	11

struct mountctl_install_journal {
	int	id;		/* journal identifier */
	int	flags;		/* journaling flags */
	int	fd;		/* streaming descriptor (-1 to close) */
	int	unused01;
	int64_t	membufsize;	/* backing store */
	int64_t	swapbufsize;	/* backing store */
	int64_t	transid;	/* starting with specified transaction id */
	int64_t unused02;
	int	stallwarn;	/* stall warning (seconds) */
	int	stallerror;	/* stall error (seconds) */
	int	unused03;
	int	unused04;
};

#define MC_JOURNAL_REVERSABLE		0x00000001	/* reversable log */
#define MC_JOURNAL_BINARY		0x00000002	/* use binary format */
#define MC_JOURNAL_UNUSED04		0x00000004
#define MC_JOURNAL_STREAM_TWO_WAY	0x00000008	/* trans id ack */
#define MC_JOURNAL_FD_PROVIDED		0x00000100	/* stream desc */
#define MC_JOURNAL_MBSIZE_PROVIDED	0x00000200	/* data space */
#define MC_JOURNAL_SWSIZE_PROVIDED	0x00000400	/* data space */
#define MC_JOURNAL_TRANSID_PROVIDED	0x00000800	/* restart transid */
#define MC_JOURNAL_STALLWARN_PROVIDED	0x00001000	/* restart transid */
#define MC_JOURNAL_STALLERROR_PROVIDED	0x00002000	/* restart transid */

struct mountctl_remove_journal {
	int	id;
	int	flags;
};

#define MC_JOURNAL_REMOVE_TRASH		0x00000001	/* data -> trash */
#define MC_JOURNAL_REMOVE_ASSYNC	0x00000002	/* asynchronous op */

struct mountctl_journal_status {
	int	id;
	int	flags;
	int	fd;
	int64_t	membufsize;
	int64_t	membufused;
	int64_t	membufqueued;
	int64_t	swapbufsize;
	int64_t	swapbufused;
	int64_t	swapbufqueued;
	int64_t transidstart;
	int64_t transidcurrent;
	int64_t transidqueued;
	int64_t transidacked;
	int64_t bytessent;
	int64_t bytesacked;
	struct timeval lastack;
};

#define MC_JOURNAL_STATUS_NEXT		0x80000000	/* find next id */


