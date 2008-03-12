/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * 
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

/*
 * mount.c
 * $Id: mount.c,v 1.4 2005/02/10 03:33:49 cpressey Exp $
 */

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aura/fspred.h"

#include "commands.h"
#include "functions.h"

#if (__NetBSD_Version__ >= 200040000)
#define STATFS statvfs
#else
#define STATFS statfs
#endif

static void	unmount_all_under_r(struct i_fn_args *, struct commands *,
				    const char *, struct STATFS *, int);

static void
unmount_all_under_r(struct i_fn_args *a, struct commands *cmds,
		    const char *mtpt, struct STATFS *mt_array, int count)
{
	struct STATFS *mt_ptr;
	int k = count;
	int unmount_me = 0;

	for (mt_ptr = mt_array; k > 0; mt_ptr++, k--) {
		if (strcmp(mt_ptr->f_mntonname, mtpt) == 0)
			unmount_me = 1;

		if (strncmp(mt_ptr->f_mntonname, mtpt, strlen(mtpt)) == 0 &&
		    strlen(mtpt) < strlen(mt_ptr->f_mntonname)) {
			unmount_all_under_r(a, cmds,
			    mt_ptr->f_mntonname, mt_array, count);
		}
	}

	if (unmount_me) {
		command_add(cmds, "%s%s %s",
		    a->os_root, cmd_name(a, "UMOUNT"), mtpt);
	}
}

/*
 * Unmount all mountpoints under a given mountpoint.  Recursively unmounts
 * dependent mountpoints, so that unmount_all'ing /mnt will first unmount
 * /mnt/usr/local, then /mnt/usr, then /mnt itself.
 */
void
unmount_all_under(struct i_fn_args *a, struct commands *cmds, const char *fmt, ...)
{
	struct STATFS *mt_array;
	int count;
	char *mtpt;
	va_list args;

	va_start(args, fmt);
	vasprintf(&mtpt, fmt, args);
	va_end(args);

	count = getmntinfo(&mt_array, MNT_WAIT);
	unmount_all_under_r(a, cmds, mtpt, mt_array, count);

	free(mtpt);
}
