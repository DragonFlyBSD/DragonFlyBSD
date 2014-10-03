/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/sys/disklabel.h,v 1.29 2007/06/19 06:07:51 dillon Exp $
 */
/*
 * Disklabel abstraction
 */

#ifndef _SYS_DISKLABEL_H_
#define	_SYS_DISKLABEL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif

struct disklabel32;
struct disklabel64;

typedef union abstracted_disklabel {
	void			*opaque;
	struct disklabel32	*lab32;
	struct disklabel64	*lab64;
} disklabel_t;

struct cdev;
struct diskslice;
struct diskslices;
struct disk_info;
struct partinfo;

struct disklabel_ops {
	struct uuid type;	/* uuid specifies disklabel type */
	int labelsize;		/* size of disklabel in bytes */

	const char *(*op_readdisklabel)
		    (struct cdev *, struct diskslice *, disklabel_t *,
		     struct disk_info *);
	int (*op_setdisklabel)
		    (disklabel_t, disklabel_t, struct diskslices *,
		     struct diskslice *, u_int32_t *);
	int (*op_writedisklabel)
		    (struct cdev *, struct diskslices *, struct diskslice *,
		     disklabel_t);
	disklabel_t (*op_clone_label)
		    (struct disk_info *, struct diskslice *);
	void (*op_adjust_label_reserved)
		    (struct diskslices *, int, struct diskslice *);
	int (*op_getpartbounds)
		    (struct diskslices *, disklabel_t, u_int32_t,
		     u_int64_t *, u_int64_t *);
	void (*op_loadpartinfo)
		    (disklabel_t, u_int32_t, struct partinfo *);
	u_int32_t (*op_getnumparts)
		    (disklabel_t);
	void (*op_makevirginlabel)
		    (disklabel_t, struct diskslices *,
		     struct diskslice *, struct disk_info *);
	void (*op_freedisklabel)
		    (disklabel_t *);
};

typedef struct disklabel_ops	*disklabel_ops_t;

#endif /* !_SYS_DISKLABEL_H_ */
