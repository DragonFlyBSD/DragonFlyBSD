/*
 * Copyright (c) 2004,2005 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sbin/jscan/jscan.h,v 1.1 2005/03/07 02:38:28 dillon Exp $
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/journal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

enum jdirection { JF_FORWARDS, JF_BACKWARDS};

struct jfile {
    off_t		jf_pos;		/* current seek position */
    off_t		jf_setpt;	/* saved seek position */
    struct jdata	*jf_saved;	/* saved data */
    FILE		*jf_fp;
    enum jdirection	jf_direction;
    int			jf_error;
};

struct jdata {
    struct jdata	*jd_next;
    int			jd_size;
    off_t		jd_pos;
    char		jd_data[4];	/* must be last field */
};

struct jstream {
    struct jstream	*js_next;	/* linked list / same transaction */
    int			js_size;	/* amount of data, in bytes */
    off_t		js_off;
    char		*js_alloc_buf;
    int			js_alloc_size;
    struct jstream	*js_cache;
    off_t		js_cache_off;
    char		js_data[4];	/* variable length */
};

struct jhash {
    struct jhash	*jh_hash;
    struct jstream	*jh_first;
    struct jstream	*jh_last;
    int16_t		jh_transid;
};

#define JHASH_SIZE	1024
#define JHASH_MASK	(JHASH_SIZE - 1)

const char *type_to_name(int16_t rectype);

struct jstream *jscan_stream(struct jfile *jf);
void jscan_dispose(struct jstream *js);
struct jfile *jopen_stream(const char *path, enum jdirection jdir);
void jclose_stream(struct jfile *jf);
void jalign(struct jfile *jf);
int jread(struct jfile *jf, void *buf, int bytes);
void jset(struct jfile *jf);
void jreturn(struct jfile *jf);
void jflush(struct jfile *jf);
void jf_warn(struct jfile *jf, const char *ctl, ...);

void dump_debug(struct jfile *jf);

int jsreadany(struct jstream *js, off_t off, const void **bufp);
int jsreadp(struct jstream *js, off_t off, const void **bufp, int bytes);
int jsread(struct jstream *js, off_t off, void *buf, int bytes);

