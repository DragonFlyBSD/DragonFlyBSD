/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/jscan/dump_mirror.c,v 1.2 2005/07/05 02:38:34 dillon Exp $
 */

#include "jscan.h"

static void dump_mirror_stream(struct jstream *js);
static int dump_mirror_toprecord(struct jstream *js, off_t *off, 
				 off_t recsize, int level);
static int dump_mirror_subrecord(struct jstream *js, off_t *off, 
				 off_t recsize, int level, struct jattr *jattr);
static int dump_mirror_payload(int16_t rectype, struct jstream *js, off_t off,
				 int recsize, int level, struct jattr *jattr);
static int dump_mirror_rebuild(u_int16_t rectype, struct jstream *js, struct jattr *jattr);

void
dump_mirror(struct jfile *jf)
{
    struct jstream *js;

    while ((js = jscan_stream(jf)) != NULL) {
	dump_mirror_stream(js);
	jscan_dispose(js);
    }
}

static void
dump_mirror_stream(struct jstream *js)
{
	struct journal_rawrecbeg head;
	int16_t sid;
	mode_t save_umask;

	save_umask = umask(0);
	jsread(js, 0, &head, sizeof(head));

	sid = head.streamid & JREC_STREAMID_MASK;
	if (debug_opt) {
	    printf("STREAM %04x DATA (%lld) {\n",
		(int)(u_int16_t)head.streamid, js->js_normalized_total);
	}
	if (sid >= JREC_STREAMID_JMIN && sid < JREC_STREAMID_JMAX) {
	    off_t off = sizeof(head);
	    dump_mirror_toprecord(js, &off, js->js_normalized_total -
				  sizeof(struct journal_rawrecbeg), 
				  1);
	} else {
	    switch(head.streamid & JREC_STREAMID_MASK) {
	    case JREC_STREAMID_SYNCPT & JREC_STREAMID_MASK:
		if (debug_opt)
		    printf("    SYNCPT\n");
		break;
	    case JREC_STREAMID_PAD & JREC_STREAMID_MASK:
		if (debug_opt)
		    printf("    PAD\n");
		break;
	    case JREC_STREAMID_DISCONT & JREC_STREAMID_MASK:
		if (debug_opt)
		    printf("    DISCONT\n");
		break;
	    case JREC_STREAMID_ANNOTATE & JREC_STREAMID_MASK:
		if (debug_opt)
		    printf("    ANNOTATION\n");
		break;
	    default:
		if (debug_opt)
		    printf("    UNKNOWN\n");
		break;
	    }
	}
	umask(save_umask);
	if (debug_opt) {
	    printf("}\n");
	    fflush(stdout);
	}
}

static int
dump_mirror_toprecord(struct jstream *js, off_t *off, off_t recsize, int level)
{
    struct journal_subrecord sub;
    struct jattr jattr;
    int payload;
    int subsize;
    int error;
    off_t base = *off;

    error = 0;
    bzero(&jattr, sizeof(jattr));
    jattr_reset(&jattr);

    while (recsize > 0) {
	if ((error = jsread(js, base, &sub, sizeof(sub))) != 0)
	    break;
	if (debug_opt) {
	    printf("%*.*s", level * 4, level * 4, "");
	    printf("@%lld ", base);
	    printf("RECORD %s [%04x/%d]", type_to_name(sub.rectype), 
		   (int)(u_int16_t)sub.rectype, sub.recsize);
	}
	if (sub.recsize == -1) {
	    if ((sub.rectype & JMASK_NESTED) == 0) {
		printf("Record size of -1 only works for nested records\n");
		error = -1;
		break;
	    }
	    payload = 0x7FFFFFFF;
	    subsize = 0x7FFFFFFF;
	} else {
	    payload = sub.recsize - sizeof(sub);
	    subsize = (sub.recsize + 7) & ~7;
	}
	if (sub.rectype & JMASK_NESTED) {
	    if (debug_opt)
		printf(" {\n");
	    *off = base + sizeof(sub);
	    error = dump_mirror_subrecord(js, off,
					  payload, level + 1, &jattr);
	    if (debug_opt)
		printf("%*.*s}\n", level * 4, level * 4, "");
	} else if (sub.rectype & JMASK_SUBRECORD) {
	    if (debug_opt) {
		printf(" DATA (%d)", payload);
		error = dump_debug_payload(sub.rectype, js, base + sizeof(sub), payload, level);
	    }
	    *off = base + sizeof(sub) + payload;
	    if (debug_opt)
		printf("\n");
	} else if ((sub.rectype & JTYPE_MASK) == JLEAF_PAD) {
	    if (debug_opt) {
		if (payload)
		    printf(" DATA (%d)", payload);
		printf("\n");
	    }
	} else {
	    if (debug_opt)
		printf("[%d bytes of unknown content]\n", payload);
	}
	dump_mirror_rebuild(sub.rectype, js, &jattr);
	jattr_reset(&jattr);
	if (error)
	    break;
	if (sub.recsize == -1) {
	    if ((sub.rectype & JMASK_NESTED) == 0) {
		printf("Record size of -1 only works for nested records\n");
		error = -1;
		break;
	    }
	    recsize -= ((*off + 7) & ~7) - base;
	    base = (*off + 7) & ~7;
	} else {
	    if (subsize == 0)
		subsize = sizeof(sub);
	    recsize -= subsize;
	    base += subsize;
	}
	if (sub.rectype & JMASK_LAST)
	    break;
    }
    *off = base;
    return(error);
}

static int
dump_mirror_subrecord(struct jstream *js, off_t *off, off_t recsize, int level,
		      struct jattr *jattr)
{
    struct journal_subrecord sub;
    int payload;
    int subsize;
    int error;
    u_int16_t rectype;
    off_t base = *off;

    error = 0;
    while (recsize > 0) {
	if ((error = jsread(js, base, &sub, sizeof(sub))) != 0)
	    break;
	rectype = sub.rectype & JTYPE_MASK;
	if (debug_opt) {
	    printf("%*.*s", level * 4, level * 4, "");
	    printf("@%lld ", base);
	    printf("SRECORD %s [%04x/%d]", type_to_name(sub.rectype), 
		   (int)(u_int16_t)sub.rectype, sub.recsize);
	}
	if (sub.recsize == -1) {
	    payload = 0x7FFFFFFF;
	    subsize = 0x7FFFFFFF;
	} else {
	    payload = sub.recsize - sizeof(sub);
	    subsize = (sub.recsize + 7) & ~7;
	}
	if (sub.rectype & JMASK_NESTED) {
	    if (debug_opt)
		printf(" {\n");

	    /*
	     * Only recurse through vattr records.  XXX currently assuming
	     * only on VATTR subrecord.
	     */
	    *off = base + sizeof(sub);
	    if (payload && rectype == JTYPE_VATTR) {
		error = dump_mirror_subrecord(js, off, 
					      payload, level + 1, jattr);
	    } else {
		error = dump_mirror_subrecord(js, off, 
					      payload, level + 1, NULL);
	    }
	    if (debug_opt)
		printf("%*.*s}\n", level * 4, level * 4, "");
	} else if (sub.rectype & JMASK_SUBRECORD) {
	    if (debug_opt) {
		printf(" DATA (%d)", payload);
		dump_debug_payload(sub.rectype, js, base + sizeof(sub), 
				   payload, level);
	    }
	    error = dump_mirror_payload(sub.rectype, js, base + sizeof(sub),
				       payload, level, jattr);
	    *off = base + sizeof(sub) + payload;
	    if (debug_opt)
		printf("\n");
	} else if ((sub.rectype & JTYPE_MASK) == JLEAF_PAD) {
	    if (debug_opt) {
		if (payload)
		    printf(" DATA (%d)", payload);
		printf("\n");
	    }
	} else {
	    if (debug_opt)
		printf("[%d bytes of unknown content]\n", sub.recsize);
	}
	if (error)
	    break;
	if (sub.recsize == -1) {
	    recsize -= ((*off + 7) & ~7) - base;
	    base = (*off + 7) & ~7;
	} else {
	    if (subsize == 0)
		subsize = sizeof(sub);
	    recsize -= subsize;
	    base += subsize;
	}
	if (sub.rectype & JMASK_LAST)
	    break;
    }
    *off = base;
    return(error);
}

static int
dump_mirror_payload(int16_t rectype, struct jstream *js, off_t off, 
	     int recsize, int level __unused, struct jattr *jattr)
{
    const char *buf;
    struct jattr_data *data;
    int error;

    if (jattr == NULL)
	return (0);

    if ((rectype & ~JMASK_LAST) != JLEAF_FILEDATA) {
	error = jsreadp(js, off, (const void **)&buf, recsize);
	if (error)
	    return (error);
    } else {
	buf = NULL;
	error = 0;
    }

    switch(rectype & ~JMASK_LAST) {
    case JLEAF_PAD:
    case JLEAF_ABORT:
	break;
    case JLEAF_SYMLINKDATA:
    case JLEAF_FILEDATA:
	printf("DOING FILEDATA1 %p  off %08llx bytes %d\n", jattr->last_data, off, recsize);
	if ((data = jattr->last_data) == NULL) {
		jattr->data.off = off;
		jattr->data.bytes = recsize;
		jattr->last_data = &jattr->data;
	} else {
		data->next = malloc(sizeof(jattr->data));
		data = data->next;
		data->off = off;
		data->bytes = recsize;
		data->next = NULL;
		jattr->last_data = data;
	}
	printf("DOING FILEDATA2 %p\n", jattr->last_data);
	break;
    case JLEAF_PATH1:
	jattr->path1 = dupdatapath(buf, recsize);
	break;
    case JLEAF_PATH2:
	jattr->path2 = dupdatapath(buf, recsize);
	break;
    case JLEAF_PATH3:
	jattr->path3 = dupdatapath(buf, recsize);
	break;
    case JLEAF_PATH4:
	jattr->path4 = dupdatapath(buf, recsize);
	break;
    case JLEAF_UID:
	jattr->uid = buf_to_int64(buf, recsize);
	break;
    case JLEAF_GID:
	jattr->gid = buf_to_int64(buf, recsize);
	break;
    case JLEAF_VTYPE:
	jattr->vtype = buf_to_int64(buf, recsize);
	break;
    case JLEAF_MODES:
	jattr->modes = buf_to_int64(buf, recsize);
	break;
    case JLEAF_FFLAGS:
	jattr->fflags = buf_to_int64(buf, recsize);
	break;
    case JLEAF_PID:
	jattr->pid = buf_to_int64(buf, recsize);
	break;
    case JLEAF_PPID:
	jattr->ppid = buf_to_int64(buf, recsize);
	break;
    case JLEAF_COMM:
	jattr->comm = dupdatastr(buf, recsize);
	break;
    case JLEAF_ATTRNAME:
	jattr->attrname = dupdatastr(buf, recsize);
	break;
    case JLEAF_PATH_REF:
	jattr->pathref = dupdatapath(buf, recsize);
	break;
    case JLEAF_RESERVED_0F:
	break;
    case JLEAF_SEEKPOS:
	jattr->seekpos = buf_to_int64(buf, recsize);
	break;
    case JLEAF_INUM:
	jattr->inum = buf_to_int64(buf, recsize);
	break;
    case JLEAF_NLINK:
	jattr->nlink = buf_to_int64(buf, recsize);
	break;
    case JLEAF_FSID:
	jattr->fsid = buf_to_int64(buf, recsize);
	break;
    case JLEAF_SIZE:
	jattr->size = buf_to_int64(buf, recsize);
	break;
    case JLEAF_ATIME:
	jattr->atime = *(const struct timeval *)buf;
	break;
    case JLEAF_MTIME:
	jattr->mtime = *(const struct timeval *)buf;
	break;
    case JLEAF_CTIME:
	jattr->ctime = *(const struct timeval *)buf;
	break;
    case JLEAF_GEN:
	jattr->gen = buf_to_int64(buf, recsize);
	break;
    case JLEAF_FLAGS:
	jattr->flags = buf_to_int64(buf, recsize);
	break;
    case JLEAF_UDEV:
	jattr->udev = buf_to_int64(buf, recsize);
	break;
    case JLEAF_FILEREV:
	jattr->filerev = buf_to_int64(buf, recsize);
	break;
    default:
	break;
    }
    return (0);
}

static int
dump_mirror_rebuild(u_int16_t rectype, struct jstream *js, struct jattr *jattr)
{
    struct jattr_data *data;
    int error = 0;
    int fd;

again:
    switch(rectype) {
    case JTYPE_SETATTR:
	if (jattr->pathref) {
	    if (jattr->uid != (uid_t)-1)
		chown(jattr->pathref, jattr->uid, -1);
	    if (jattr->gid != (gid_t)-1)
		chown(jattr->pathref, -1, jattr->gid);
	    if (jattr->modes != (mode_t)-1)
		chmod(jattr->pathref, jattr->modes);
	    if (jattr->fflags != -1)
		chflags(jattr->pathref, jattr->fflags);
	    if (jattr->size != -1)
		truncate(jattr->pathref, jattr->size);
	}
	break;
    case JTYPE_WRITE:
    case JTYPE_PUTPAGES:
	if (jattr->pathref && jattr->seekpos != -1) {
	    if ((fd = open(jattr->pathref, O_RDWR)) >= 0) {
		lseek(fd, jattr->seekpos, 0);
		for (data = &jattr->data; data; data = data->next) {
		    printf("WRITEBLOCK @ %016llx/%d\n", data->off, data->bytes);
		    if (data->bytes)
			jsreadcallback(js, write, fd, data->off, data->bytes);
		}
		close(fd);
	    }
	}
	break;
    case JTYPE_SETACL:
	break;
    case JTYPE_SETEXTATTR:
	break;
    case JTYPE_CREATE:
	/*
	 * note: both path1 and pathref will exist.
	 */
	if (jattr->path1 && jattr->modes != (mode_t)-1) {
	    if ((fd = open(jattr->path1, O_CREAT, jattr->modes)) >= 0) {
		close(fd);
		rectype = JTYPE_SETATTR;
		goto again;
	    }
	}
	break;
    case JTYPE_MKNOD:
	break;
    case JTYPE_LINK:
	break;
    case JTYPE_SYMLINK:
	break;
    case JTYPE_WHITEOUT:
	break;
    case JTYPE_REMOVE:
	if (jattr->path1) {
	    remove(jattr->path1);
	}
	break;
    case JTYPE_MKDIR:
	if (jattr->path1 && jattr->modes != (mode_t)-1) {
	    mkdir(jattr->path1, jattr->modes);
	}
	break;
    case JTYPE_RMDIR:
	if (jattr->path1) {
	    rmdir(jattr->path1);
	}
	break;
    case JTYPE_RENAME:
	if (jattr->path1 && jattr->path2) {
	    rename(jattr->path1, jattr->path2);
	}
	break;
    }
    return(error);
}

