/*-
 * Copyright (c) 2003 Kip Macy All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/checkpoint.h,v 1.2 2006/05/20 02:42:13 dillon Exp $
 */

#ifndef _SYS_CHECKPOINT_H_
#define _SYS_CHECKPOINT_H_

#ifndef _SYS_PROCFS_H_
#include <sys/procfs.h>
#endif

#define CKPT_FREEZE    0x1
#define CKPT_THAW      0x2
#define CKPT_FREEZEPID 0x3	/* not yet supported */
#define CKPT_THAWBIN   0x4	/* not yet supported */

#ifdef _KERNEL

typedef struct {
	prstatus_t *status;
	prfpregset_t *fpregset;
	prpsinfo_t *psinfo;
} pstate_t;

typedef struct {
	pstate_t *pstate;
	int offset;
	int nthreads;
} lc_args_t;

#else

int sys_checkpoint(int type, int fd, pid_t pid, int retval);

#endif

#endif /* PRIVATE_CKPT_H_ */
