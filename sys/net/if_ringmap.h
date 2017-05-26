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

#ifndef	_NET_IF_RINGMAP_H_
#define	_NET_IF_RINGMAP_H_

#ifndef _KERNEL
#error "kernel only header file"
#endif

#include <sys/bus.h>
#include <sys/sysctl.h>

struct if_ringmap;

struct if_ringmap *if_ringmap_alloc(device_t dev, int cnt, int cnt_max);
void	if_ringmap_free(struct if_ringmap *rm);
void	if_ringmap_match(device_t dev, struct if_ringmap *rm0,
	    struct if_ringmap *rm1);
void	if_ringmap_align(device_t dev, struct if_ringmap *rm0,
	    struct if_ringmap *rm1);
int	if_ringmap_count(const struct if_ringmap *rm);
int	if_ringmap_cpumap(const struct if_ringmap *rm, int ring);
void	if_ringmap_rdrtable(const struct if_ringmap *rm, int table[],
	    int table_nent);
int	if_ringmap_cpumap_sysctl(SYSCTL_HANDLER_ARGS);

#endif	/* !_NET_IF_RINGMAP_H_ */
