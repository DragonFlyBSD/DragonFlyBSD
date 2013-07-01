/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <paths.h>
#include <limits.h>
#include <fstab.h>

static void finddevlabel(char **pathp, const char *devname);
static int xlatedevpath(char **pathp, struct stat *st);
static char *dodequote(char *base);

/*
 * Acquire device path.
 *
 */
char *
getdevpath(const char *devname, int flags)
{
	struct stat st;
	char *path = NULL;
	int stgood = 0;

	if (devname[0] == '/' || devname[0] == '.') {
		asprintf(&path, "%s", devname);
	} else {
		asprintf(&path, "/dev/%s", devname);
		if (lstat(path, &st) < 0) {
			free(path);
			path = NULL;
			finddevlabel(&path, devname);
			if (path == NULL)
				asprintf(&path, "%s", devname);
		} else {
			stgood = 1;
		}
	}

	/*
	 * Translate softlinks if requested.  If the lstat() of the
	 * pre-translated path fails NULL is expected to be returned.
	 * lstat() is not called on the post-translated path.
	 */
	if ((flags & GETDEVPATH_RAWDEV) && path) {
		if (stgood == 0 && lstat(path, &st) == 0)
			stgood = 1;
		if (stgood)
			stgood = xlatedevpath(&path, &st);
		if (stgood == 0) {
			free(path);
			path = NULL;
		}

	}
	if (path == NULL)
		errno = ENOENT;
	return(path);
}

static void
finddevlabel(char **pathp, const char *devname)
{
	const char *prefix = _PATH_DEVTAB_PATHS;
	const char *ptr1;
	const char *trailer;
	char *label;
	char *ptr2;
	char *ptr3;
	char *dtpath;
	char *bufp;
	char buf[256];
	FILE *fp;
	size_t len;	/* directory prefix length */
	size_t tlen;	/* devname length without trailer */

	if ((trailer = strrchr(devname, '.')) != NULL)
		tlen = trailer - devname;
	else
		tlen = 0;

	while (*prefix && *pathp == NULL) {
		/*
		 * Directory search path
		 */
		ptr1 = strchr(prefix, ':');
		len = (ptr1) ? (size_t)(ptr1 - prefix) : strlen(prefix);
		asprintf(&dtpath, "%*.*s/devtab", (int)len, (int)len, prefix);

		/*
		 * Each devtab file
		 */
		if ((fp = fopen(dtpath, "r")) != NULL) {
			while (fgets(buf, sizeof(buf), fp) != NULL) {
				/*
				 * Extract label field, check degenerate
				 * cases.
				 */
				label = strtok_r(buf, " \t\r\n", &bufp);
				if (label == NULL || *label == 0 ||
				    *label == '#') {
					continue;
				}

				/*
				 * Match label, with or without the
				 * trailer (aka ".s1a").  The trailer
				 * is tacked on if the match is without
				 * the trailer.
				 */
				if (strcmp(devname, label) == 0) {
					trailer = "";
				} else if (tlen && strlen(label) == tlen &&
					   strncmp(devname, label, tlen) == 0) {
					trailer = devname + tlen;
				} else {
					continue;
				}

				/*
				 * Match, extract and process remaining fields.
				 */
				ptr2 = strtok_r(NULL, " \t\r\n", &bufp);
				ptr3 = strtok_r(NULL, " \t\r\n", &bufp);
				if (ptr2 == NULL || ptr3 == NULL)
					continue;
				if (*ptr2 == 0 || *ptr3 == 0)
					continue;
				ptr3 = dodequote(ptr3);
				if (strcmp(ptr2, "path") == 0) {
					asprintf(pathp, "%s%s", ptr3, trailer);
				} else {
					asprintf(pathp, "/dev/%s/%s%s",
						 ptr2, ptr3, trailer);
				}
				break;
			}
			fclose(fp);
		}
		free(dtpath);
		prefix += len;
		if (*prefix == ':')
			++prefix;
	}
}

static int
xlatedevpath(char **pathp, struct stat *st)
{
	char *path;
	int n;
	int len;

	/*
	 * If not a softlink return unchanged.
	 */
	if (!S_ISLNK(st->st_mode))
		return(1);

	/*
	 * If the softlink isn't reasonable return bad (0)
	 */
	len = (int)st->st_size;
	if (len < 0 || len > PATH_MAX)
		return(0);

	/*
	 * Read the link, return if the result is not what we expected.
	 */
	path = malloc(len + 1);
	n = readlink(*pathp, path, len);
	if (n < 0 || n > len) {
		free(path);
		return(0);
	}

	/*
	 * Success, replace (*pathp).
	 */
	path[n] = 0;
	free(*pathp);
	*pathp = path;
	return(1);
}

static char *
dodequote(char *base)
{
	int len = strlen(base);

	if (len && base[0] == '\"' && base[len-1] == '\"') {
		base[len - 1] = 0;
		++base;
	}
	return(base);
}
