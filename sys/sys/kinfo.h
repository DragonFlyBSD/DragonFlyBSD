/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 * $DragonFly: src/sys/sys/kinfo.h,v 1.3 2005/01/31 22:29:59 joerg Exp $
 */

#ifndef _SYS_KINFO_H
#define _SYS_KINFO_H

struct kinfo_file {
	size_t	 f_size;	/* size of struct kinfo_file */
	pid_t	 f_pid;		/* owning process */
	uid_t	 f_uid;		/* effective uid of owning process */
	int	 f_fd;		/* descriptor number */
	void	*f_file;	/* address of struct file */
	short	 f_type;	/* descriptor type */
	int	 f_count;	/* reference count */
	int	 f_msgcount;	/* references from message queue */
	off_t	 f_offset;	/* file offset */
	void	*f_data;	/* file descriptor specific data */
	u_int	 f_flag;	/* flags (see fcntl.h) */
};

struct kinfo_cputime {
	uint64_t	cp_user;
	uint64_t	cp_nice;
	uint64_t	cp_sys;
	uint64_t	cp_intr;
	uint64_t	cp_idle;
};

struct kinfo_clockinfo {
	int	ci_hz;		/* clock frequency */
	int	ci_tick;	/* micro-seconds per hz tick */
	int	ci_tickadj;	/* clock skew rate for adjtime() */
	int	ci_stathz;	/* statistics clock frequency */
	int	ci_profhz;	/* profiling clock frequency */
};

struct kinfo_prison {
	int		 pr_version;
	int		 pr_id;
	char		 pr_path[MAXPATHLEN];
	char 		 pr_host[MAXHOSTNAMELEN];
	uint32_t	 pr_ip;
};
#define	KINFO_PRISON_VERSION	1

#endif
