/*
 * Copyright (c) 2003-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Mihai Carabas <mihai.carabas@gmail.com>
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

#ifndef _VMM_VMM_H_
#define _VMM_VMM_H_

#define MAX_NAME_LEN 256

#include <sys/param.h>
#include <sys/vmm.h>

#define ERROR_IF(func)					\
	do {						\
	if ((err = (func))) {				\
		kprintf("VMM: %s error at line: %d\n",	\
		   __func__, __LINE__);			\
		goto error;				\
	}						\
	} while(0)					\

#define ERROR2_IF(func)					\
	do {						\
	if ((err = (func))) {				\
		kprintf("VMM: %s error at line: %d\n",	\
		   __func__, __LINE__);			\
		goto error2;				\
	}						\
	} while(0)					\

#ifdef VMM_DEBUG
#define dkprintf(fmt, args...)		kprintf(fmt, ##args)
#else
#define dkprintf(fmt, args...)
#endif

#define INSTRUCTION_MAX_LENGTH		15

struct vmm_ctl {
	char name[MAX_NAME_LEN];
	int (*init)(void);
	int (*enable)(void);
	int (*disable)(void);
	int (*vminit)(struct vmm_guest_options *);
	int (*vmdestroy)(void);
	int (*vmrun)(void);
	int (*vm_set_tls_area)(void);
	void (*vm_lwp_return)(struct lwp *lp, struct trapframe *frame);
	void (*vm_set_guest_cr3)(register_t);
	int (*vm_get_gpa)(struct proc *, register_t *, register_t);


};

struct vmm_proc {
	uint64_t	guest_cr3;
	uint64_t	vmm_cr3;
};

struct vmm_ctl* get_ctl_intel(void);
struct vmm_ctl* get_ctl_amd(void);

#endif
