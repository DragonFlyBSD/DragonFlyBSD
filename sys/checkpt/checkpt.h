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
 * $DragonFly: src/sys/checkpt/Attic/checkpt.h,v 1.2 2003/11/20 06:05:27 dillon Exp $
 */

#ifndef PRIVATE_CKPT_H_
#define PRIVATE_CKPT_H_

#define CKPT_FREEZE    0x1
#define CKPT_THAW      0x2
#define CKPT_FREEZEPID 0x3
#define CKPT_THAWBIN   0x4

char *ckpt_expand_name(const char *name, uid_t uid, pid_t pid);

struct ckpt_generic_args {
	int type;
	int fd;
};

struct ckpt_freeze_args {
	struct ckpt_generic_args gen;
};

struct ckpt_thaw_args {
	struct ckpt_generic_args gen;
	int retval;
}; 

struct ckpt_freeze_pid_args {
	struct ckpt_generic_args gen;
	int pid;
};

struct ckpt_thaw_bin_args {
	struct ckpt_generic_args gen;
	char *bin;
	int binlen;
};

struct ckpt_args {
	struct sysmsg sysmsg;
	union usrmsg usrmsg;
	union {
		struct ckpt_generic_args gen;
		struct ckpt_freeze_args cfa;
		struct ckpt_thaw_args cta;
		struct ckpt_freeze_pid_args cfpa;
		struct ckpt_thaw_bin_args ctba;
	} args;
};

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

#endif /* PRIVATE_CKPT_H_ */

