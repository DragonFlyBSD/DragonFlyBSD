/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/jscan/dump_debug.c,v 1.2 2005/03/07 05:05:04 dillon Exp $
 */

#include "jscan.h"

static void dump_debug_stream(struct jstream *js);
static int dump_subrecord(struct jstream *js, off_t off, int recsize, int level);
static int dump_payload(int16_t rectype, struct jstream *js, off_t off,
	     int recsize, int level);

void
dump_debug(struct jfile *jf)
{
    struct jstream *js;

    while ((js = jscan_stream(jf)) != NULL) {
	dump_debug_stream(js);
	jscan_dispose(js);
    }
}

static void
dump_debug_stream(struct jstream *js)
{
	struct journal_rawrecbeg head;
	int16_t sid;

	jsread(js, 0, &head, sizeof(head));

	sid = head.streamid & JREC_STREAMID_MASK;
	printf("STREAM %04x {\n", (int)(u_int16_t)head.streamid);
	if (sid >= JREC_STREAMID_JMIN && sid < JREC_STREAMID_JMAX) {
	    dump_subrecord(js, sizeof(head),
			head.recsize - sizeof(struct journal_rawrecbeg) -
			sizeof(struct journal_rawrecend),
			1);
	} else {
	    switch(head.streamid & JREC_STREAMID_MASK) {
	    case JREC_STREAMID_SYNCPT & JREC_STREAMID_MASK:
		printf("    SYNCPT\n");
		break;
	    case JREC_STREAMID_PAD & JREC_STREAMID_MASK:
		printf("    PAD\n");
		break;
	    case JREC_STREAMID_DISCONT & JREC_STREAMID_MASK:
		printf("    DISCONT\n");
		break;
	    case JREC_STREAMID_ANNOTATE & JREC_STREAMID_MASK:
		printf("    ANNOTATION\n");
		break;
	    default:
		printf("    UNKNOWN\n");
		break;
	    }
	}
	printf("}\n");
}

static int
dump_subrecord(struct jstream *js, off_t off, int recsize, int level)
{
    struct journal_subrecord sub;
    int payload;
    int subsize;
    int error;
    int i;

    error = 0;
    while (recsize > 0) {
	if ((error = jsread(js, off, &sub, sizeof(sub))) != 0)
	    break;
	printf("%*.*s", level * 4, level * 4, "");
	printf("@%lld ", off);
	printf("RECORD %s [%04x/%d]", type_to_name(sub.rectype), 
				(int)(u_int16_t)sub.rectype, sub.recsize);
	payload = sub.recsize - sizeof(sub);
	subsize = (sub.recsize + 7) & ~7;
	if (sub.rectype & JMASK_NESTED) {
	    printf(" {\n");
	    if (payload)
		error = dump_subrecord(js, off + sizeof(sub), payload, level + 1);
	    printf("%*.*s}\n", level * 4, level * 4, "");
	} else if (sub.rectype & JMASK_SUBRECORD) {
	    printf(" DATA (%d)", payload);
	    error = dump_payload(sub.rectype, js, off + sizeof(sub), payload, level);
	    printf("\n");
	    if (error)
		break;
	} else {
	    printf("[unknown content]\n", sub.recsize);
	}
	if (subsize == 0)
		subsize = sizeof(sub);
	recsize -= subsize;
	off += subsize;
    }
    return(error);
}

static int
dump_payload(int16_t rectype, struct jstream *js, off_t off, 
	     int recsize, int level)
{
    enum { DT_NONE, DT_STRING, DT_DEC, DT_HEX, DT_OCT,
	   DT_DATA, DT_TIMESTAMP } dt = DT_DATA;
    const char *buf;
    int didalloc;
    int error;
    int i;
    int j;

    error = jsreadp(js, off, (const void **)&buf, recsize);
    if (error)
	return (error);

    switch(rectype & ~JMASK_LAST) {
    case JLEAF_PAD:
    case JLEAF_ABORT:
	break;
    case JLEAF_FILEDATA:
	break;
    case JLEAF_PATH1:
    case JLEAF_PATH2:
    case JLEAF_PATH3:
    case JLEAF_PATH4:
	dt = DT_STRING;
	break;
    case JLEAF_UID:
    case JLEAF_GID:
	dt = DT_DEC;
	break;
    case JLEAF_MODES:
	dt = DT_OCT;
	break;
    case JLEAF_FFLAGS:
	dt = DT_HEX;
	break;
    case JLEAF_PID:
    case JLEAF_PPID:
	dt = DT_DEC;
	break;
    case JLEAF_COMM:
	dt = DT_STRING;
	break;
    case JLEAF_ATTRNAME:
	dt = DT_STRING;
	break;
    case JLEAF_PATH_REF:
	dt = DT_STRING;
	break;
    case JLEAF_RESERVED_0F:
	break;
    case JLEAF_SYMLINKDATA:
	dt = DT_STRING;
	break;
    case JLEAF_SEEKPOS:
	dt = DT_HEX;
	break;
    case JLEAF_INUM:
	dt = DT_HEX;
	break;
    case JLEAF_NLINK:
	dt = DT_DEC;
	break;
    case JLEAF_FSID:
	dt = DT_HEX;
	break;
    case JLEAF_SIZE:
	dt = DT_HEX;
	break;
    case JLEAF_ATIME:
    case JLEAF_MTIME:
    case JLEAF_CTIME:
	dt = DT_TIMESTAMP;
	break;
    case JLEAF_GEN:
	dt = DT_HEX;
	break;
    case JLEAF_FLAGS:
	dt = DT_HEX;
	break;
    case JLEAF_UDEV:
	dt = DT_HEX;
	break;
    case JLEAF_FILEREV:
	dt = DT_HEX;
	break;
    default:
	break;
    }
    switch(dt) {
    case DT_NONE:
	break;
    case DT_STRING:
	printf(" \"");
	for (i = 0; i < recsize; ++i)
	    stringout(stdout, buf[i], 1);
	printf("\"");
	break;
    case DT_DEC:
	switch(recsize) {
	case 1:
	    printf(" %d", (int)*(u_int8_t *)buf);
	    break;
	case 2:
	    printf(" %d", (int)*(u_int16_t *)buf);
	    break;
	case 4:
	    printf(" %d", (int)*(u_int32_t *)buf);
	    break;
	case 8:
	    printf(" %d", (int64_t)*(u_int64_t *)buf);
	    break;
	default:
	    printf(" ?");
	    break;
	}
	break;
    case DT_HEX:
	switch(recsize) {
	case 1:
	    printf(" 0x%02x", (int)*(u_int8_t *)buf);
	    break;
	case 2:
	    printf(" 0x%04x", (int)*(u_int16_t *)buf);
	    break;
	case 4:
	    printf(" 0x%08x", (int)*(u_int32_t *)buf);
	    break;
	case 8:
	    printf(" 0x%016llx", (int64_t)*(u_int64_t *)buf);
	    break;
	default:
	    printf(" ?");
	    break;
	}
	break;
    case DT_OCT:
	switch(recsize) {
	case 1:
	    printf(" %03o", (int)*(u_int8_t *)buf);
	    break;
	case 2:
	    printf(" %06o", (int)*(u_int16_t *)buf);
	    break;
	case 4:
	    printf(" %011o", (int)*(u_int32_t *)buf);
	    break;
	case 8:
	    printf(" %022llo", (int64_t)*(u_int64_t *)buf);
	    break;
	default:
	    printf(" ?");
	    break;
	}
	break;
    case DT_TIMESTAMP:
	{
	    struct tm *tp;
	    time_t t = ((struct timespec *)buf)->tv_sec;
	    char outbuf[64];

	    tp = localtime(&t);
	    strftime(outbuf, sizeof(outbuf), " <%d-%b-%Y %H:%M:%S>", tp);
	    printf("%s", outbuf);
	}
	break;
    case DT_DATA:
    default:
	if (recsize < 16) {
	    for (i = 0; i < recsize; ++i)
		printf(" %02x", (int)(unsigned char)buf[i]);
	    printf(" \"");
	    for (i = 0; i < recsize; ++i)
		stringout(stdout, buf[i], 0);
	    printf("\"");
	} else {
	    printf(" {\n");
	    for (i = 0; i < recsize; i += 16) {
		printf("%*.*s", level * 4 + 4, level * 4 + 4, "");
		for (j = i; j < i + 16 && j < recsize; ++j)
		    printf(" %02x", (int)(unsigned char)buf[j]);
		for (; j < i + 16; ++j)
		    printf("   ");
		printf(" \"");
		for (j = i; j < i + 16 && j < recsize; ++j)
		    stringout(stdout, buf[j], 0);
		printf("\"\n");
	    }
	    printf("%*.*s}", level * 4, level * 4, "");
	}
	break;
    }
    return (0);
}

