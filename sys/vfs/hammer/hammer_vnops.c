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
 * $DragonFly: src/sys/vfs/hammer/hammer_vnops.c,v 1.1 2007/11/01 20:53:05 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/namecache.h>
#include <sys/vnode.h>
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/stat.h>
#include "hammer.h"

/*
 * USERFS VNOPS
 */
/*static int hammer_vop_vnoperate(struct vop_generic_args *);*/
static int hammer_vop_fsync (struct vop_fsync_args *);
static int hammer_vop_read (struct vop_read_args *);
static int hammer_vop_write (struct vop_write_args *);
static int hammer_vop_access (struct vop_access_args *);
static int hammer_vop_advlock (struct vop_advlock_args *);
static int hammer_vop_close (struct vop_close_args *);
static int hammer_vop_ncreate (struct vop_ncreate_args *);
static int hammer_vop_getattr (struct vop_getattr_args *);
static int hammer_vop_nresolve (struct vop_nresolve_args *);
static int hammer_vop_nlookupdotdot (struct vop_nlookupdotdot_args *);
static int hammer_vop_nlink (struct vop_nlink_args *);
static int hammer_vop_nmkdir (struct vop_nmkdir_args *);
static int hammer_vop_nmknod (struct vop_nmknod_args *);
static int hammer_vop_open (struct vop_open_args *);
static int hammer_vop_pathconf (struct vop_pathconf_args *);
static int hammer_vop_print (struct vop_print_args *);
static int hammer_vop_readdir (struct vop_readdir_args *);
static int hammer_vop_readlink (struct vop_readlink_args *);
static int hammer_vop_nremove (struct vop_nremove_args *);
static int hammer_vop_nrename (struct vop_nrename_args *);
static int hammer_vop_nrmdir (struct vop_nrmdir_args *);
static int hammer_vop_setattr (struct vop_setattr_args *);
static int hammer_vop_strategy (struct vop_strategy_args *);
static int hammer_vop_nsymlink (struct vop_nsymlink_args *);
static int hammer_vop_nwhiteout (struct vop_nwhiteout_args *);

struct vop_ops hammer_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_read,
	.vop_write =		hammer_vop_write,
	.vop_access =		hammer_vop_access,
	.vop_advlock =		hammer_vop_advlock,
	.vop_close =		hammer_vop_close,
	.vop_ncreate =		hammer_vop_ncreate,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_nresolve =		hammer_vop_nresolve,
	.vop_nlookupdotdot =	hammer_vop_nlookupdotdot,
	.vop_nlink =		hammer_vop_nlink,
	.vop_nmkdir =		hammer_vop_nmkdir,
	.vop_nmknod =		hammer_vop_nmknod,
	.vop_open =		hammer_vop_open,
	.vop_pathconf =		hammer_vop_pathconf,
	.vop_print =		hammer_vop_print,
	.vop_readdir =		hammer_vop_readdir,
	.vop_readlink =		hammer_vop_readlink,
	.vop_nremove =		hammer_vop_nremove,
	.vop_nrename =		hammer_vop_nrename,
	.vop_nrmdir =		hammer_vop_nrmdir,
	.vop_setattr =		hammer_vop_setattr,
	.vop_strategy =		hammer_vop_strategy,
	.vop_nsymlink =		hammer_vop_nsymlink,
	.vop_nwhiteout =	hammer_vop_nwhiteout
};

#if 0
static
int
hammer_vop_vnoperate(struct vop_generic_args *)
{
	return (VOCALL(&hammer_vnode_vops, ap));
}
#endif

static
int
hammer_vop_fsync (struct vop_fsync_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_read (struct vop_read_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_write (struct vop_write_args *ap)
{
	/* XXX UIO_NOCOPY */
	return EOPNOTSUPP;
}

static
int
hammer_vop_access (struct vop_access_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_advlock (struct vop_advlock_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_close (struct vop_close_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_ncreate (struct vop_ncreate_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_getattr (struct vop_getattr_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nresolve (struct vop_nresolve_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nlookupdotdot (struct vop_nlookupdotdot_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nlink (struct vop_nlink_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nmkdir (struct vop_nmkdir_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nmknod (struct vop_nmknod_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_open (struct vop_open_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_pathconf (struct vop_pathconf_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_print (struct vop_print_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_readdir (struct vop_readdir_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_readlink (struct vop_readlink_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nremove (struct vop_nremove_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nrename (struct vop_nrename_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nrmdir (struct vop_nrmdir_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_setattr (struct vop_setattr_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_strategy (struct vop_strategy_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nsymlink (struct vop_nsymlink_args *ap)
{
	return EOPNOTSUPP;
}

static
int
hammer_vop_nwhiteout (struct vop_nwhiteout_args *ap)
{
	return EOPNOTSUPP;
}

