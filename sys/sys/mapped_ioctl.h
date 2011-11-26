/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Simon 'corecode' Schubert <corecode@fs.ei.tum.de>.
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
 * $DragonFly: src/sys/sys/mapped_ioctl.h,v 1.4 2006/05/20 02:42:13 dillon Exp $
 */
#ifndef _SYS_MAPPED_IOCTL_H_
#define _SYS_MAPPED_IOCTL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_IOCCOM_H_
#include <sys/ioccom.h>
#endif

struct file;
struct thread;
struct ucred;
struct ioctl_map_entry;

typedef int	(ioctl_wrap_func)(struct file *, u_long, u_long, caddr_t, 
				  struct ucred *);
typedef u_long	(ioctl_map_func)(u_long, u_long, u_long, u_long, u_long, u_long);

struct ioctl_map_range {
	u_long		start;		/* Start of source range; inclusive */
	u_long		end;		/* End of source range; inclusive */
	u_long		maptocmd;	/* Start of destination range */
	u_long		maptoend;	/* End of destination range */
	ioctl_wrap_func *wrapfunc;	/* Ioctl handler to use */
	ioctl_map_func  *mapfunc;	/* Handler to map source to dest */
};

#define MAPPED_IOCTL_MAPRANGE(c,e,t,r,f,m)	{ (c), (e), (t), (r), (f), (m) }
#define MAPPED_IOCTL_MAPF(c,t,f)		MAPPED_IOCTL_MAPRANGE((c), (c), (t), (t), (f), NULL)
#define MAPPED_IOCTL_MAP(c,t)			MAPPED_IOCTL_MAPF((c), (t), NULL)
#define MAPPED_IOCTL_IO(c,f)			MAPPED_IOCTL_MAPF((c), _IO(0, 0), (f))
#define MAPPED_IOCTL_IOR(c,f,t) 		MAPPED_IOCTL_MAPF((c), _IOR(0, 0, t), (f))
#define MAPPED_IOCTL_IOW(c,f,t) 		MAPPED_IOCTL_MAPF((c), _IOW(0, 0, t), (f))
#define MAPPED_IOCTL_IOWR(c,f,t)		MAPPED_IOCTL_MAPF((c), _IOWR(0, 0, t), (f))

struct ioctl_map {
	u_long mask;
	const char *sys;
	LIST_HEAD(, ioctl_map_entry) mapping;
};

struct ioctl_map_handler {
	struct ioctl_map	*map;
	const char		*subsys;
	struct ioctl_map_range	*cmd_ranges;
};

int mapped_ioctl(int fd, u_long com, caddr_t uspc_data,
		 struct ioctl_map *map, struct sysmsg *msg);
int mapped_ioctl_register_handler(struct ioctl_map_handler *he);
int mapped_ioctl_unregister_handler(struct ioctl_map_handler *he);

#endif /* !_SYS_MAPPED_IOCTL_H_ */
