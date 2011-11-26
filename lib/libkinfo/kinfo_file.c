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
 * $DragonFly: src/lib/libkinfo/kinfo_file.c,v 1.4 2004/12/21 15:07:42 joerg Exp $
 */

#include <sys/kinfo.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <kinfo.h>
#include <stdlib.h>

#define	KERN_FILE_SYSCTL "kern.file"

int
kinfo_get_files(struct kinfo_file **file_buf, size_t *len)
{
	int retval;
	void *buf;
	size_t new_len;

	retval = sysctlbyname(KERN_FILE_SYSCTL, NULL, &new_len, NULL, 0);
	if (retval)
		return(retval);
	if ((buf = malloc(new_len)) == NULL)
		return(ENOMEM);
	retval = sysctlbyname(KERN_FILE_SYSCTL, buf, &new_len, NULL, 0);
	if (retval) {
		free(buf);
		return(retval);
	}
	/*
	 * Shrink the buffer to the minimum size, this is not supposed
	 * to fail.
	 */
	if ((buf = reallocf(buf, new_len)) == NULL)
		err(1, "realloc");
	if (new_len != 0 &&
	    ((struct kinfo_file *)buf)->f_size != sizeof(struct kinfo_file)) {
		warnx("kernel size of struct kinfo_file changed");
		free(buf);
		return(EOPNOTSUPP);
	}
	*len = new_len / sizeof(struct kinfo_file);
	*file_buf = buf;
	return(0);
}

/* XXX convert kern.maxfiles to size_t */
int
kinfo_get_maxfiles(int *maxfiles)
{
	size_t len = sizeof(*maxfiles);

	return(sysctlbyname("kern.maxfiles", maxfiles, &len, NULL, 0));
}

/* XXX convert kern.openfiles to size_t */
int
kinfo_get_openfiles(int *openfiles)
{
	size_t len = sizeof(*openfiles);

	return(sysctlbyname("kern.openfiles", openfiles, &len, NULL, 0));
}
