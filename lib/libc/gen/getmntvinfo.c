/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * @(#)getmntinfo.c	8.1 (Berkeley) 6/4/93
 */

#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <stdlib.h>

/*
 * Return information about mounted filesystems.
 */
int
getmntvinfo(struct statfs **mntbufp, struct statvfs **mntvbufp, int flags)
{
	static struct statfs *mntsbuf;
	static struct statvfs *mntvbuf;
	static int mntsize;
	static int bufsize;
	static int vbufsize;

	if (mntsize <= 0 && (mntsize = getvfsstat(NULL, NULL, 0, MNT_NOWAIT)) < 0)
		return (0);
	if (vbufsize > 0 && (mntsize = getvfsstat(mntsbuf, mntvbuf, vbufsize, flags)) < 0)
		return (0);
	while (vbufsize <= mntsize * (int)sizeof(struct statvfs)) {
		if (mntsbuf)
			free(mntsbuf);
		bufsize = (mntsize + 1) * sizeof(struct statfs);
		vbufsize = (mntsize + 1) * sizeof(struct statvfs);
		if ((mntsbuf = (struct statfs *)malloc(bufsize)) == NULL)
			return (0);
		if ((mntvbuf = (struct statvfs *)malloc(vbufsize)) == NULL)
			return (0);
		if ((mntsize = getvfsstat(mntsbuf, mntvbuf, vbufsize, flags)) < 0)
			return (0);
	}
	*mntbufp = mntsbuf;
	*mntvbufp = mntvbuf;
	return (mntsize);
}

