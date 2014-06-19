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
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/journal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include "jattr.h"

struct jdata;

enum jdirection { JD_FORWARDS, JD_BACKWARDS, JD_SEQFIRST, JD_SEQLAST };

struct jfile {
    off_t		jf_pos;		/* current seek position */
    int			jf_fd;		/* reading/scanning */
    int			jf_write_fd;	/* appending */
    off_t		jf_write_pos;	/* append position */
    int			jf_error;
    int			jf_open_flags;
    char		*jf_prefix;	/* prefix: name */
    unsigned int	jf_seq_beg;	/* prefix: sequence space */
    unsigned int	jf_seq_end;	/* prefix: sequence space */
    unsigned int	jf_seq;		/* prefix: current sequence number */
    int64_t		jf_last_transid;/* prefix: last recorded transid */
};

/*
 * Output session (debug, record, output, etc)
 */
struct jsession {
    struct jfile	*ss_jfin;
    struct jfile	*ss_jfout;
    enum jdirection	ss_direction;
    const char		*ss_mirror_directory;
    const char		*ss_transid_file;
    int			ss_transid_fd;
    int64_t		ss_transid;
};

#define JF_FULL_DUPLEX	0x0001

struct jdata {
    int64_t		jd_transid;	/* transaction id from header */
    int			jd_alloc;	/* allocated bytes */
    int			jd_size;	/* data bytes */
    int			jd_refs;	/* ref count */
    unsigned int	jd_seq;		/* location data */
    off_t		jd_pos;		/* location data */
    char		jd_data[4];	/* must be last field */
};

struct jstream {
    struct jstream	*js_next;	/* linked list / same transaction */
    struct jsession	*js_session;
    char		*js_alloc_buf;
    int			js_alloc_size;

    /*
     * Normalized fields strip all rawrecbeg, rawrecend, and deadspace except
     * for the initial rawrecbeg header.
     */
    char		*js_normalized_base;
    int			js_normalized_size;
    off_t		js_normalized_off;
    off_t		js_normalized_total;

    /*
     * This is used by the first js record only to cache other records in the
     * chain.
     */
    struct jstream	*js_cache;
    struct jdata	*js_jdata;
    struct journal_rawrecbeg *js_head;
};

struct jhash {
    struct jhash	*jh_hash;
    struct jstream	*jh_first;
    struct jstream	*jh_last;
    struct jsession	*jh_session;
    int16_t		jh_transid;
};

#define JHASH_SIZE	1024
#define JHASH_MASK	(JHASH_SIZE - 1)

#define JMODEF_DEBUG			0x00000001
#define JMODEF_MIRROR			0x00000002
#define JMODEF_UNUSED0004		0x00000004
#define JMODEF_RECORD			0x00000008
#define JMODEF_RECORD_TMP		0x00000010
#define JMODEF_INPUT_FULL		0x00000020
#define JMODEF_INPUT_PIPE		0x00000040
#define JMODEF_INPUT_PREFIX		0x00000080
#define JMODEF_OUTPUT			0x00000100
#define JMODEF_OUTPUT_FULL		0x00000200
#define JMODEF_MEMORY_TRACKING		0x00000400
#define JMODEF_LOOP_FOREVER		0x00000800

#define JMODEF_OUTPUT_TRANSID_GOOD	0x00010000
#define JMODEF_RECORD_TRANSID_GOOD	0x00020000
#define JMODEF_MIRROR_TRANSID_GOOD	0x00040000
#define JMODEF_TRANSID_GOOD_MASK	(JMODEF_OUTPUT_TRANSID_GOOD|\
					 JMODEF_RECORD_TRANSID_GOOD|\
					 JMODEF_MIRROR_TRANSID_GOOD)
#define JMODEF_COMMAND_MASK	(JMODEF_RECORD|JMODEF_MIRROR|JMODEF_DEBUG|\
				 JMODEF_OUTPUT)

extern int jmodes;
extern int fsync_opt;
extern int verbose_opt;
extern off_t prefix_file_size;
extern off_t trans_count;

const char *type_to_name(int16_t rectype);
void stringout(FILE *fp, char c, int exact);
void jattr_reset(struct jattr *jattr);
int64_t buf_to_int64(const void *buf, int bytes);
char *dupdatastr(const void *buf, int bytes);
char *dupdatapath(const void *buf, int bytes);
void get_transid_from_file(const char *path, int64_t *transid, int flags);

void jsession_init(struct jsession *ss, struct jfile *jfin, 
		   enum jdirection direction,
		   const char *transid_file, int64_t transid);
void jsession_update_transid(struct jsession *ss, int64_t transid);
int jsession_check(struct jsession *ss, struct jdata *jd);
void jsession_term(struct jsession *ss);

struct jfile *jopen_fd(int fd);
struct jfile *jopen_prefix(const char *prefix, int rw);
void jclose(struct jfile *jf);
struct jdata *jread(struct jfile *jf, struct jdata *jd, 
		    enum jdirection direction);
struct jdata *jseek(struct jfile *jf, int64_t transid,
		    enum jdirection direction);
void jwrite(struct jfile *jf, struct jdata *jd);
struct jdata *jref(struct jdata *jd);
void jfree(struct jfile *jf, struct jdata *jd);
void jf_warn(struct jfile *jf, const char *ctl, ...) __printflike(2, 3);

struct jstream *jaddrecord(struct jsession *ss, struct jdata *jd);
void jscan_dispose(struct jstream *js);

void dump_debug(struct jsession *ss, struct jdata *jd);
void dump_mirror(struct jsession *ss, struct jdata *jd);
void dump_record(struct jsession *ss, struct jdata *jd);
void dump_output(struct jsession *ss, struct jdata *jd);

int jrecord_init(const char *record_prefix);

int jsreadany(struct jstream *js, off_t off, const void **bufp);
int jsreadp(struct jstream *js, off_t off, const void **bufp, int bytes);
int jsread(struct jstream *js, off_t off, void *buf, int bytes);
int jsreadcallback(struct jstream *js, ssize_t (*func)(int, const void *, size_t), int fd, off_t off, int bytes);

