/*	$NetBSD: fsutil.c,v 1.7 1998/07/30 17:41:03 thorpej Exp $	*/

/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/fsck/fsutil.c,v 1.2.2.1 2001/08/01 05:47:55 obrien Exp $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fstab.h>
#include <err.h>
#include <paths.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include "fsutil.h"

static const char *dev = NULL;
static int hot = 0;
static int preen = 0;

extern char *__progname;

static void vmsg(int, const char *, va_list) __printflike(2, 0);

void
setcdevname(const char *cd, int pr)
{
	dev = cd;
	preen = pr;
}

const char *
cdevname(void)
{
	return dev;
}

int
hotroot(void)
{
	return hot;
}

/*VARARGS*/
void
errexit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(8);
}

static void
vmsg(int fatal, const char *fmt, va_list ap)
{
	if (!fatal && preen)
		printf("%s: ", dev);

	vprintf(fmt, ap);

	if (fatal && preen)
		printf("\n");

	if (fatal && preen) {
		printf(
		    "%s: UNEXPECTED INCONSISTENCY; RUN %s MANUALLY.\n",
		    dev, __progname);
		exit(8);
	}
}

/*VARARGS*/
void
pfatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(1, fmt, ap);
	va_end(ap);
}

/*VARARGS*/
void
pwarn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vmsg(0, fmt, ap);
	va_end(ap);
}

void
perror(const char *s)
{
	pfatal("%s (%s)", s, strerror(errno));
}

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(1, fmt, ap);
	va_end(ap);
	exit(8);
}

const char *
unrawname(const char *name)
{
	static char unrawbuf[32];
	const char *dp;
	struct stat stb;

	if ((dp = strrchr(name, '/')) == NULL)
		return (name);
	if (stat(name, &stb) < 0)
		return (name);
	if (!S_ISCHR(stb.st_mode))
		return (name);
	if (dp[1] != 'r')
		return (name);
	snprintf(unrawbuf, 32, "%.*s/%s", (int)(dp - name), name, dp + 2);
	return (unrawbuf);
}

const char *
rawname(const char *name)
{
	static char rawbuf[32];
	const char *dp;

	if ((dp = strrchr(name, '/')) == NULL)
		return (0);
	snprintf(rawbuf, 32, "%.*s/r%s", (int)(dp - name), name, dp + 1);
	return (rawbuf);
}

const char *
devcheck(const char *origname)
{
	struct stat stslash, stchar;

	if (stat("/", &stslash) < 0) {
		perror("/");
		printf("Can't stat root\n");
		return (origname);
	}
	if (stat(origname, &stchar) < 0) {
		perror(origname);
		printf("Can't stat %s\n", origname);
		return (origname);
	}
	if (!S_ISCHR(stchar.st_mode)) {
		perror(origname);
		printf("%s is not a char device\n", origname);
	}
	return (origname);
}

/*
 * Get the mount point information for name.
 */
struct statfs *
getmntpt(const char *name)
{
	struct stat devstat, mntdevstat;
	char device[sizeof(_PATH_DEV) - 1 + MNAMELEN];
	char *devname;
	struct statfs *mntbuf, *statfsp;
	int i, mntsize, isdev;

	if (stat(name, &devstat) != 0)
		return (NULL);
	if (S_ISCHR(devstat.st_mode) || S_ISBLK(devstat.st_mode))
		isdev = 1;
	else
		isdev = 0;
	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (i = 0; i < mntsize; i++) {
		statfsp = &mntbuf[i];
		devname = statfsp->f_mntfromname;
		if (*devname != '/') {
			strcpy(device, _PATH_DEV);
			strcat(device, devname);
			strcpy(statfsp->f_mntfromname, device);
		}
		if (isdev == 0) {
			if (strcmp(name, statfsp->f_mntonname))
				continue;
			return (statfsp);
		}
		if (stat(devname, &mntdevstat) == 0 &&
		    mntdevstat.st_rdev == devstat.st_rdev)
			return (statfsp);
	}
	statfsp = NULL;
	return (statfsp);
}

#if 0
/*
 * XXX this code is from NetBSD, but fails in FreeBSD because we
 * don't have blockdevs. I don't think its needed.
 */
const char *
blockcheck(const char *origname)
{
	struct stat stslash, stblock, stchar;
	const char *newname, *raw;
	struct fstab *fsp;
	int retried = 0;

	hot = 0;
	if (stat("/", &stslash) < 0) {
		perror("/");
		printf("Can't stat root\n");
		return (origname);
	}
	newname = origname;
retry:
	if (stat(newname, &stblock) < 0) {
		perror(newname);
		printf("Can't stat %s\n", newname);
		return (origname);
	}
	if (S_ISBLK(stblock.st_mode)) {
		if (stslash.st_dev == stblock.st_rdev)
			hot++;
		raw = rawname(newname);
		if (stat(raw, &stchar) < 0) {
			perror(raw);
			printf("Can't stat %s\n", raw);
			return (origname);
		}
		if (S_ISCHR(stchar.st_mode)) {
			return (raw);
		} else {
			printf("%s is not a character device\n", raw);
			return (origname);
		}
	} else if (S_ISCHR(stblock.st_mode) && !retried) {
		newname = unrawname(newname);
		retried++;
		goto retry;
	} else if ((fsp = getfsfile(newname)) != 0 && !retried) {
		newname = fsp->fs_spec;
		retried++;
		goto retry;
	}
	/*
	 * Not a block or character device, just return name and
	 * let the user decide whether to use it.
	 */
	return (origname);
}
#endif

