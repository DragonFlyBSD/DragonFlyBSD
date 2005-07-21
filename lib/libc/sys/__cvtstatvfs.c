/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/lib/libc/sys/Attic/__cvtstatvfs.c,v 1.1 2005/07/21 21:33:26 joerg Exp $
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/statvfs.h>

#include <string.h>

#include "libc_private.h"

void
__cvtstatvfs(const struct statfs *in, struct statvfs *out)
{
	memset(out, 0, sizeof(*out));

	out->f_bsize = in->f_bsize;
	out->f_frsize = in->f_bsize;
	out->f_blocks = in->f_blocks;
	out->f_bfree = in->f_bfree;
	out->f_bavail = in->f_bavail;
	out->f_files = in->f_files;
	out->f_ffree = in->f_ffree;
	/*
	 * XXX
	 * This field counts the number of available inodes to non-root
	 * users, but this information is not available via statfs.
	 * Just ignore this issue by returning the totoal number instead.
	 */
	out->f_favail = in->f_ffree;
	/*
	 * XXX
	 * This field has a different meaning for statfs and statvfs.
	 * For the former it is the cookie exported for NFS and not
	 * intended for normal userland use.
	 */
	out->f_fsid = 0; 

	out->f_flag = 0;
	if (in->f_flags & MNT_RDONLY)
		out->f_flag |= ST_RDONLY;
	if (in->f_flags & MNT_NOSUID)
		out->f_flag |= ST_NOSUID;
	out->f_namemax = 0;
	out->f_owner = in->f_owner;
	/*
	 * XXX
	 * statfs contains the type as string, statvfs expects it as
	 * enumeration.
	 */
	out->f_type = 0;

	out->f_syncreads = in->f_syncreads;
	out->f_syncwrites = in->f_syncwrites;
	out->f_asyncreads = in->f_asyncreads;
	out->f_asyncwrites = in->f_asyncwrites;
}
