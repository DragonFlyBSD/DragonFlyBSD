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
 * $DragonFly: src/sbin/jscan/dump_mirror.c,v 1.8 2005/11/06 12:32:56 swildner Exp $
 */

#include "jscan.h"
#include <sys/vfscache.h>

static void dump_mirror_stream(struct jsession *ss, struct jstream *js);
static int dump_mirror_toprecord(struct jsession *ss, struct jstream *js,
				 off_t *off, off_t recsize, int level);
static int dump_mirror_subrecord(enum jdirection direction, struct jstream *js,
				 off_t *off, off_t recsize, int level,
				 struct jattr *jattr);
static int dump_mirror_payload(int16_t rectype, struct jstream *js, off_t off,
				 int recsize, int level, struct jattr *jattr);
static int dump_mirror_rebuild_redo(u_int16_t rectype, 
				    struct jstream *js, struct jattr *jattr);
static int dump_mirror_rebuild_undo(u_int16_t rectype,
				    struct jstream *js, struct jattr *jattr);
static void undo_recreate(const char *filename, 
				    struct jstream *js, struct jattr *jattr);
static void dosetattr(const char *filename, int fd, struct jattr *jattr);

void
dump_mirror(struct jsession *ss, struct jdata *jd)
{
    struct jstream *js;

    if ((js = jaddrecord(ss, jd)) != NULL) {
	dump_mirror_stream(ss, js);
	jscan_dispose(js);
    }
    jsession_update_transid(ss, jd->jd_transid);
}

static void
dump_mirror_stream(struct jsession *ss, struct jstream *js)
{
	struct journal_rawrecbeg head;
	int16_t sid;
	mode_t save_umask;

	save_umask = umask(0);
	jsread(js, 0, &head, sizeof(head));

	sid = head.streamid & JREC_STREAMID_MASK;
	if (sid >= JREC_STREAMID_JMIN && sid < JREC_STREAMID_JMAX) {
	    off_t off = sizeof(head);
	    dump_mirror_toprecord(ss, js, &off,
				  js->js_normalized_total -
				      sizeof(struct journal_rawrecbeg), 
				  1);
	} else {
	    switch(head.streamid & JREC_STREAMID_MASK) {
	    case JREC_STREAMID_SYNCPT & JREC_STREAMID_MASK:
		break;
	    case JREC_STREAMID_PAD & JREC_STREAMID_MASK:
		break;
	    case JREC_STREAMID_DISCONT & JREC_STREAMID_MASK:
		break;
	    case JREC_STREAMID_ANNOTATE & JREC_STREAMID_MASK:
		break;
	    default:
		break;
	    }
	}
	umask(save_umask);
}

/*
 * Execute a meta-transaction, e.g. something like 'WRITE'.  Meta-transactions
 * are almost universally nested.
 */
static int
dump_mirror_toprecord(struct jsession *ss, struct jstream *js,
		      off_t *off, off_t recsize, int level)
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
	    *off = base + sizeof(sub);
	    error = dump_mirror_subrecord(ss->ss_direction, js, off,
					  payload, level + 1, &jattr);
	} else if (sub.rectype & JMASK_SUBRECORD) {
	    *off = base + sizeof(sub) + payload;
	} else if ((sub.rectype & JTYPE_MASK) == JLEAF_PAD) {
	} else {
	}
	if (ss->ss_direction == JD_FORWARDS)
	    dump_mirror_rebuild_redo(sub.rectype, js, &jattr);
	else
	    dump_mirror_rebuild_undo(sub.rectype, js, &jattr);
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

/*
 * Parse a meta-transaction's nested records.  The highest subrecord layer
 * starts at layer = 2 (the top layer specifying the command is layer = 1).
 *
 * The nested subrecord contains informational records containing primarily
 * namespace data, and further subrecords containing nested
 * audit, undo, and redo data.
 */
static int
dump_mirror_subrecord(enum jdirection direction, struct jstream *js,
		      off_t *off, off_t recsize, int level,
		      struct jattr *jattr)
{
    struct journal_subrecord sub;
    int payload;
    int subsize;
    int error;
    int skip;
    u_int16_t rectype;
    off_t base = *off;

    error = 0;
    while (recsize > 0) {
	if ((error = jsread(js, base, &sub, sizeof(sub))) != 0)
	    break;
	rectype = sub.rectype & JTYPE_MASK;	/* includes the nested bit */
	if (sub.recsize == -1) {
	    payload = 0x7FFFFFFF;
	    subsize = 0x7FFFFFFF;
	} else {
	    payload = sub.recsize - sizeof(sub);
	    subsize = (sub.recsize + 7) & ~7;
	}

	skip = 1;
	*off = base + sizeof(sub);

	switch(rectype) {
	case JTYPE_REDO:	/* NESTED */
	    /*
	     * Process redo information when scanning forwards.
	     */
	    if (direction == JD_FORWARDS) {
		error = dump_mirror_subrecord(direction, js, off, payload,
					      level + 1, jattr);
		skip = 0;
	    }
	    break;
	case JTYPE_UNDO:	/* NESTED */
	    /*
	     * Process undo information when scanning backwards.
	     */
	    if (direction == JD_BACKWARDS) {
		error = dump_mirror_subrecord(direction, js, off, payload,
					      level + 1, jattr);
		skip = 0;
	    }
	    break;
	case JTYPE_CRED:	/* NESTED */
	    /*
	     * Ignore audit information
	     */
	    break;
	default:		/* NESTED or non-NESTED */
	    /*
	     * Execute these.  Nested records might contain attribute
	     * information under an UNDO or REDO parent, for example.
	     */
	    if (rectype & JMASK_NESTED) {
		error = dump_mirror_subrecord(direction, js, off, payload,
					      level + 1, jattr);
		skip = 0;
	    } else if (rectype & JMASK_SUBRECORD) {
		error = dump_mirror_payload(sub.rectype, js, *off, payload,
					    level, jattr);
	    }
	    break;
	}
	if (error)
	    break;

	/*
	 * skip only applies to nested subrecords.  If the record size
	 * is unknown the record MUST be a nested record, and if we have
	 * not processed it we must recurse to figure out the actual size.
	 */
	if (sub.recsize == -1) {
	    assert(sub.rectype & JMASK_NESTED);
	    if (skip) {
		error = dump_mirror_subrecord(direction, js, off, payload,
					      level + 1, NULL);
	    }
	    recsize -= ((*off + 7) & ~7) - base;
	    base = (*off + 7) & ~7;
	} else {
	    if (subsize == 0)
		subsize = sizeof(sub);
	    recsize -= subsize;
	    base += subsize;
	}
	if (error)
	    break;
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
	jattr->symlinkdata = dupdatastr(buf, recsize);
	jattr->symlinklen = recsize;
	break;
    case JLEAF_FILEDATA:
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
dump_mirror_rebuild_redo(u_int16_t rectype, struct jstream *js,
			struct jattr *jattr)
{
    struct jattr_data *data;
    int error = 0;
    int fd;

    if (verbose_opt > 2) {
	fprintf(stderr, "REDO %04x %s %s\n", 
		js->js_head->streamid, type_to_name(rectype),
		jattr->pathref ? jattr->pathref : jattr->path1);
    }
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
		dosetattr(jattr->path1, fd, jattr);
		close(fd);
	    }
	}
	break;
    case JTYPE_MKNOD:
	/* XXX */
	break;
    case JTYPE_LINK:
	if (jattr->pathref && jattr->path1) {
	    link(jattr->pathref, jattr->path1);
	}
	break;
    case JTYPE_SYMLINK:
	if (jattr->symlinkdata && jattr->path1) {
	    symlink(jattr->symlinkdata, jattr->path1);
	}
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

/*
 * UNDO function using parsed primary data and parsed UNDO data.  This
 * must typically
 */
static int
dump_mirror_rebuild_undo(u_int16_t rectype, struct jstream *js,
			struct jattr *jattr)
{
    struct jattr_data *data;
    int error = 0;
    int fd;

    if (verbose_opt > 2) {
	fprintf(stderr, "UNDO %04x %s %s\n", 
		js->js_head->streamid, type_to_name(rectype),
		jattr->pathref ? jattr->pathref : jattr->path1);
    }
    switch(rectype) {
    case JTYPE_SETATTR:
	if (jattr->pathref)
	    dosetattr(jattr->pathref, -1, jattr);
	break;
    case JTYPE_WRITE:
    case JTYPE_PUTPAGES:
	if (jattr->pathref && jattr->seekpos != -1) {
	    if ((fd = open(jattr->pathref, O_RDWR)) >= 0) {
		lseek(fd, jattr->seekpos, 0);
		for (data = &jattr->data; data; data = data->next) {
		    if (data->bytes)
			jsreadcallback(js, write, fd, data->off, data->bytes);
		}
		close(fd);
	    }
	}
	if (jattr->size != -1)
	    truncate(jattr->pathref, jattr->size);
	break;
    case JTYPE_SETACL:
	break;
    case JTYPE_SETEXTATTR:
	break;
    case JTYPE_CREATE:
	/*
	 * note: both path1 and pathref will exist.
	 */
	if (jattr->path1)
	    remove(jattr->path1);
	break;
    case JTYPE_MKNOD:
	if (jattr->path1)
	    remove(jattr->path1);
	break;
    case JTYPE_LINK:
	if (jattr->path1) {
	    undo_recreate(jattr->path1, js, jattr);
	}
	break;
    case JTYPE_SYMLINK:
	if (jattr->symlinkdata && jattr->path1) {
	    undo_recreate(jattr->path1, js, jattr);
	}
	break;
    case JTYPE_WHITEOUT:
	/* XXX */
	break;
    case JTYPE_REMOVE:
	if (jattr->path1) {
	    undo_recreate(jattr->path1, js, jattr);
	}
	break;
    case JTYPE_MKDIR:
	if (jattr->path1) {
	    rmdir(jattr->path1);
	}
	break;
    case JTYPE_RMDIR:
	if (jattr->path1 && jattr->modes != (mode_t)-1) {
	    mkdir(jattr->path1, jattr->modes);
	}
	break;
    case JTYPE_RENAME:
	if (jattr->path2) {
	    undo_recreate(jattr->path2, js, jattr);
	}
	break;
    }
    return(error);
}

/*
 * This is a helper function for undoing operations which completely destroy
 * the file that had existed previously.  The caller will clean up the
 * attributes (including file truncations/extensions) after the fact.
 */
static void
undo_recreate(const char *filename, struct jstream *js, struct jattr *jattr)
{
    struct jattr_data *data;
    int fd;

    if (verbose_opt > 2)
	fprintf(stderr, "RECREATE %s (type %d)\n", filename, jattr->vtype);

    remove(filename);
    switch(jattr->vtype) {
    case VREG:
	if (jattr->size != -1) {
	    if ((fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0600)) >= 0) {
		if (jattr->seekpos != -1) {
		    lseek(fd, jattr->seekpos, 0);
		    for (data = &jattr->data; data; data = data->next) {
			if (data->bytes)
			    jsreadcallback(js, write, fd, data->off, data->bytes);
		    }
		}
		dosetattr(filename, fd, jattr);
		close(fd);
	    }
	}
	break;
    case VDIR:
	mkdir(filename, 0600);
	dosetattr(filename, -1, jattr);
	break;
    case VBLK:
    case VCHR:
	if (jattr->udev) {
	    mknod(filename, S_IFBLK|0666, jattr->udev);
	    dosetattr(filename, -1, jattr);
	}
	break;
    case VLNK:
	if (jattr->symlinkdata) {
	    symlink(jattr->symlinkdata, filename);
	    dosetattr(filename, -1, jattr);
	}
	break;
    default:
	break;
    }
}

static void
dosetattr(const char *filename, int fd, struct jattr *jattr)
{
    if (fd >= 0) {
	if (jattr->uid != (uid_t)-1 && jattr->gid != (gid_t)-1)
	    fchown(fd, jattr->uid, jattr->gid);
	else if (jattr->uid != (uid_t)-1)
	    fchown(fd, jattr->uid, -1);
	else if (jattr->gid != (gid_t)-1)
	    fchown(fd, -1, jattr->gid);

	if (jattr->modes != (mode_t)-1)
	    fchmod(fd, jattr->modes);
	if (jattr->fflags != -1)
	    fchflags(fd, jattr->fflags);
	if (jattr->size != -1)
	    ftruncate(fd, jattr->size);
    } else {
	if (jattr->uid != (uid_t)-1 && jattr->gid != (gid_t)-1)
	    lchown(filename, jattr->uid, jattr->gid);
	else if (jattr->uid != (uid_t)-1)
	    lchown(filename, jattr->uid, -1);
	else if (jattr->gid != (gid_t)-1)
	    lchown(filename, -1, jattr->gid);

	if (jattr->modes != (mode_t)-1)
	    lchmod(filename, jattr->modes);
	if (jattr->fflags != -1)
	    chflags(filename, jattr->fflags);
	if (jattr->size != -1)
	    truncate(filename, jattr->size);
    }
}

