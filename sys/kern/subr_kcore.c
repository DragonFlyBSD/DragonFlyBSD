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
 * 
 * $DragonFly: src/sys/kern/subr_kcore.c,v 1.1 2004/11/24 22:51:01 joerg Exp $
 */

/*
 * This file is shared between the kernel and libkcore and they have
 * to be kept synchronized.
 */

#define	_KERNEL_STRUCTURES

#include <sys/param.h>
#include <sys/file.h>
#include <sys/kcore.h>
#include <sys/kinfo.h>

#ifdef _KERNEL
#  include <sys/systm.h>
#  include <sys/proc.h>
#else
#  include <sys/user.h>
#  include <string.h>
#endif

void
kcore_make_file(struct kinfo_file *ufile, struct file *kfile,
		pid_t pid, uid_t owner, int n)
{
	bzero(ufile, sizeof(*ufile));
	ufile->f_size = sizeof(*ufile);
	ufile->f_pid = pid;
	ufile->f_uid = owner;

	ufile->f_fd = n;
	ufile->f_file = kfile;
	ufile->f_data = kfile->f_data;
	ufile->f_type = kfile->f_type;
	ufile->f_count = kfile->f_count;
	ufile->f_msgcount = kfile->f_msgcount;
	ufile->f_offset = kfile->f_offset;
	ufile->f_flag = kfile->f_flag;
}
