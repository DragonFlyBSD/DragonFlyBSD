/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

#include "hammer2.h"

static int docleanup(const char *path);

int
cmd_cleanup(const char *sel_path)
{
	struct statfs *fsary;
	char *fstype;
	char *path;
	int i;
	int n;
	int r;
	int rc;

	if (sel_path)
		return (docleanup(sel_path));

	n = getmntinfo(&fsary, MNT_NOWAIT);
	if (n <= 0) {
		fprintf(stderr, "hammer2 cleanup: no HAMMER2 mounts\n");
		return 0;
	}
	rc = 0;
	for (i = 0; i < n; ++i) {
		fstype = fsary[i].f_fstypename;
		path = fsary[i].f_mntonname;

		if (strcmp(fstype, "hammer2") == 0) {
			r = docleanup(path);
			if (r)
				rc = r;
		}
	}
	return rc;
}

static
int
docleanup(const char *path)
{
	int rc;

	printf("hammer2 cleanup \"%s\"\n", path);
	rc = cmd_bulkfree(path);

	return rc;
}
