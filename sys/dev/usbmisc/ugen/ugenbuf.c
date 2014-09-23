/*
 * Copyright (c) 2004 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>.
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
 * $DragonFly: src/sys/dev/usbmisc/ugen/ugenbuf.c,v 1.3 2006/09/05 00:55:43 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include "ugenbuf.h"

static MALLOC_DEFINE(M_UGENBUF, "ugenbufs", "Temporary buffer space");
static void *ugencache_buf;
static int ugencache_size;

/*
 * getugenbuf()
 *
 *	Allocate a temporary buffer for UGEN.  This routine is called from
 *	mainline code only and the BGL is held.
 */
void *
getugenbuf(int reqsize, int *bsize)
{
    void *buf;

    if (reqsize < 256)
	reqsize = 256;
    if (reqsize > 262144)
	reqsize = 262144;
    *bsize = reqsize;

    buf = ugencache_buf;
    if (buf == NULL) {
	buf = kmalloc(reqsize, M_UGENBUF, M_WAITOK);
    } else if (ugencache_size != reqsize) {
	ugencache_buf = NULL;
	kfree(buf, M_UGENBUF);
	buf = kmalloc(reqsize, M_UGENBUF, M_WAITOK);
    } else {
	buf = ugencache_buf;
	ugencache_buf = NULL;
    }
    return(buf);
}

/*
 * relugenbuf()
 *
 *	Release a temporary buffer for UGEN.  This routine is called from
 *	mainline code only and the BGL is held.
 */
void
relugenbuf(void *buf, int bsize)
{
    if (ugencache_buf == NULL) {
	ugencache_buf = buf;
	ugencache_size = bsize;
    } else {
	kfree(buf, M_UGENBUF);
    }
}
