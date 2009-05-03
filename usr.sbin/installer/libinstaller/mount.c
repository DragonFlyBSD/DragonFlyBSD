/*
 * Copyright (c) 2004-09 The DragonFly Project.  All rights reserved.
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

#include "libaura/fspred.h"

#include "commands.h"
#include "functions.h"

static int
compare(const void *a, const void *b)
{
	const struct statfs *sa = a;
	const struct statfs *sb = b;

	return -strcmp(sa->f_mntonname,
	    sb->f_mntonname);
}

/*
 * Unmount all mountpoints under a given mountpoint in order (e.g. /var/tmp is
 * unmounted before /var).
 */
void
unmount_all_under(struct i_fn_args *a, struct commands *cmds, const char *fmt, ...)
{
	struct statfs *mt_array;
	int count, i;
	char *mtpt;
	va_list args;

	va_start(args, fmt);
	vasprintf(&mtpt, fmt, args);
	va_end(args);

	count = getmntinfo(&mt_array, MNT_WAIT);

	/* Order mount points in reverse lexicographically order. */
	qsort((void*)mt_array, count, sizeof(struct statfs), compare);

	for (i = 0; i < count; i++) {
		if (strncmp(mtpt, mt_array[i].f_mntonname, strlen(mtpt)) != 0)
			continue;
		if (strlen(mtpt) > strlen(mt_array[i].f_mntonname))
			continue;

		command_add(cmds, "%s%s %s",
		    a->os_root, cmd_name(a, "UMOUNT"), mt_array[i].f_mntonname);
	}

	free(mtpt);
}
