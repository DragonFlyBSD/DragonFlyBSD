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
 */

#ifndef	_KINFO_H_
#define	_KINFO_H_

#include <sys/cdefs.h>
#include <sys/kinfo.h>

#include <kinfo_pcpu.h>

/* Forward references */
struct rtstatistics;

__BEGIN_DECLS;
/* File */
int	kinfo_get_files(struct kinfo_file **, size_t *);
int	kinfo_get_maxfiles(int *);
int	kinfo_get_openfiles(int *);

/* Networking */
int kinfo_get_net_rtstatistics(struct rtstatistics *);

/* Scheduling / Time */
int	kinfo_get_cpus(int *);
int	kinfo_get_sched_cputime(struct kinfo_cputime *);
int	kinfo_get_sched_hz(int *);
int	kinfo_get_sched_profhz(int *);
int	kinfo_get_sched_stathz(int *);

/* TTYs */
int	kinfo_get_tty_tk_nin(uint64_t *);
int	kinfo_get_tty_tk_nout(uint64_t *);

/* VFS */
int	kinfo_get_vfs_bufspace(long *);

/* Per-CPU accumulators */
PCPU_STATISTICS_PROT(cputime, struct kinfo_cputime);
PCPU_STATISTICS_PROT(route, struct rtstatistics);
__END_DECLS;

#endif
