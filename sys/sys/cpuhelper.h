/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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

#ifndef _SYS_CPUHELPER_H_
#define _SYS_CPUHELPER_H_

#ifndef _KERNEL
#error "kernel only header file"
#endif

#include <sys/msgport.h>
#include <sys/msgport2.h>

struct cpuhelper_msg;

typedef void	(*cpuhelper_cb_t)(struct cpuhelper_msg *);

struct cpuhelper_msg {
	struct lwkt_msg		ch_lmsg;	/* MUST be the first field */
	cpuhelper_cb_t		ch_cb;
	void			*ch_cbarg;
	int			ch_cbarg1;
};

void		cpuhelper_assert(int cpuid, bool in);
int		cpuhelper_domsg(struct cpuhelper_msg *msg, int cpuid);
void		cpuhelper_replymsg(struct cpuhelper_msg *msg, int error);
void		cpuhelper_initmsg(struct cpuhelper_msg *msg, lwkt_port_t rport,
		    cpuhelper_cb_t cb, void *cbarg, int flags);

#endif	/* !_SYS_CPUHELPER_H_ */
