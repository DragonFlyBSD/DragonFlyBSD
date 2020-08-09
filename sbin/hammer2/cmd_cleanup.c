/*
 * Copyright (c) 2017-2018 The DragonFly Project.  All rights reserved.
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
static int sameh2prefix(const char *from);
static void saveh2prefix(const char *from);
static void freeh2prefixes(void);

int
cmd_cleanup(const char *sel_path)
{
	struct statfs *fsary;
	char *fstype;
	char *path;
	char *from;
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
		from = fsary[i].f_mntfromname;

		if (strcmp(fstype, "hammer2") != 0)
			continue;
		if (sameh2prefix(from)) {
			printf("hammer2 cleanup \"%s\" (same partition)\n",
				path);
		} else {
			r = docleanup(path);
			if (r)
				rc = r;
			saveh2prefix(from);
		}
	}
	freeh2prefixes();

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

static char **h2prefixes;
static int h2count;
static int h2limit;

static
int
sameh2prefix(const char *from)
{
	char *ptr;
	int rc = 0;
	int i;

	ptr = strdup(from);
	if (strchr(ptr, '@'))
		*strchr(ptr, '@') = 0;
	for (i = 0; i < h2count; ++i) {
		if (strcmp(h2prefixes[i], ptr) == 0) {
			rc = 1;
			break;
		}
	}
	free(ptr);

	return rc;
}

static
void
saveh2prefix(const char *from)
{
	char *ptr;

	if (h2count >= h2limit) {
		h2limit = (h2limit + 8) << 1;
		h2prefixes = realloc(h2prefixes, sizeof(char *) * h2limit);
	}
	ptr = strdup(from);
	if (strchr(ptr, '@'))
		*strchr(ptr, '@') = 0;
	h2prefixes[h2count++] = ptr;
}

static
void
freeh2prefixes(void)
{
	int i;

	for (i = 0; i < h2count; ++i)
		free(h2prefixes[i]);
	free(h2prefixes);
	h2prefixes = NULL;
	h2count = 0;
	h2limit = 0;
}
