/*
 * Copyright (c) 1989, 1993
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
 * $FreeBSD: src/lib/libc/gen/devname.c,v 1.2.2.2 2001/07/31 20:10:19 tmm Exp $
 * $DragonFly: src/lib/libc/gen/devname.c,v 1.4 2004/01/06 15:38:09 eirikn Exp $
 *
 * @(#)devname.c	8.2 (Berkeley) 4/29/95
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char *
xdevname(dev_t dev, mode_t type)
{
	struct {
		mode_t type;
		dev_t dev;
	} bkey;
	static DB *db;
	static int failure;
	DBT data, key;

	if (!db && !failure &&
	    !(db = dbopen(_PATH_DEVDB, O_RDONLY, 0, DB_HASH, NULL))) {
		warn("warning: %s", _PATH_DEVDB);
		failure = 1;
	}
	if (failure)
		return (NULL);

	/*
	 * Keys are a mode_t followed by a dev_t.  The former is the type of
	 * the file (mode & S_IFMT), the latter is the st_rdev field.  Be
	 * sure to clear any padding that may be found in bkey.
	 */
	memset(&bkey, 0, sizeof(bkey));
	bkey.dev = dev;
	bkey.type = type;
	key.data = &bkey;
	key.size = sizeof(bkey);
	return ((db->get)(db, &key, &data, 0) ? NULL : (char *)data.data);
}

char *
devname_r(dev_t dev, mode_t type, char *buf, size_t len)
{
	int i;
	size_t j;
	char *r;

	/* First check the DB file. */
	r = xdevname(dev, type);
	if (r != NULL) {
		strlcpy(buf, r, len);
		return (buf);
	}

#if 0
	/* The kern.devname sysctl does not exist */
	/* Then ask the kernel. */
	if ((type & S_IFMT) == S_IFCHR) {
		j = sizeof(buf);
		i = sysctlbyname("kern.devname", buf, &j, &dev, sizeof (dev));
		if (i == 0)
		    return (buf);
	}
#endif

	/* Finally just format it */
	if (minor(dev) > 255) {
		snprintf(buf, len, "#%c%d:0x%x", 
		    (type & S_IFMT) == S_IFCHR ? 'C' : 'B',
		    major(dev), minor(dev));
	} else {
		snprintf(buf, len, "#%c%d:%d", 
		    (type & S_IFMT) == S_IFCHR ? 'C' : 'B',
		    major(dev), minor(dev));
	}
	return (buf);
}

char *
devname(dev_t dev, mode_t type)
{
	static char buf[30];	 /* XXX: pick up from <sys/conf.h> */

	strncpy(buf, devname_r(dev, type, buf, sizeof(buf)), sizeof(buf));

	return (buf);
}
