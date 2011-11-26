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
 * $DragonFly: src/sys/vfs/userfs/userfs_elms.c,v 1.2 2007/09/07 21:42:59 dillon Exp $
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
#include <sys/syslink.h>
#include <sys/syslink_vfs.h>
#include "userfs.h"

void
user_elm_push_vnode(struct syslink_elm *par, struct vnode *vp)
{
	struct syslink_elm *elm;

	elm = sl_elm_make(par, SLVFS_ELM_VNODE, sizeof(int64_t));
	/* XXX vnode id */
}

void
user_elm_push_offset(struct syslink_elm *par, off_t offset)
{
	struct syslink_elm *elm;

	elm = sl_elm_make(par, SLVFS_ELM_OFFSET, sizeof(int64_t));
	*(int64_t *)(elm + 1) = offset;
}

void
user_elm_push_bio(struct syslink_elm *par, int cmd, int bcount)
{
	sl_elm_make_aux(par, SLVFS_ELM_IOCMD, cmd);
	sl_elm_make_aux(par, SLVFS_ELM_IOCOUNT, bcount);
}

void
user_elm_push_mode(struct syslink_elm *par, mode_t mode)
{
	panic("user_elm_push_mode: not implemented yet");
}

void
user_elm_push_cred(struct syslink_elm *par, struct ucred *cred)
{
	panic("user_elm_push_cred: not implemented yet");
}

void
user_elm_push_nch(struct syslink_elm *par, struct nchandle *nch)
{
	panic("user_elm_push_nch: not implemented yet");
}

void
user_elm_push_vattr(struct syslink_elm *par, struct vattr *vat)
{
	panic("user_elm_push_nch: not implemented yet");
}

int
user_elm_parse_vattr(struct syslink_elm *par, struct vattr *vat)
{
	panic("user_elm_parse_vattr: not implemented yet");
}

