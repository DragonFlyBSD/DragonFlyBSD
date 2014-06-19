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

#include "jscan.h"

void
jf_warn(struct jfile *jf, const char *ctl, ...)
{
    va_list va;

    fprintf(stderr, "@0x%016jx ", (uintmax_t)jf->jf_pos);
    va_start(va, ctl);
    vfprintf(stderr, ctl, va);
    va_end(va);
    fprintf(stderr, "\n");
}

const char *
type_to_name(int16_t rectype)
{
    const char *str;

    switch((u_int16_t)rectype & ~JMASK_LAST) {
    case JLEAF_PAD:
	str = "PAD";
	break;
    case JLEAF_ABORT:
	str = "ABORT";
	break;
    case JTYPE_ASSOCIATE:
	str = "ASSOCIATE";
	break;
    case JTYPE_DISASSOCIATE:
	str = "DISASSOCIATE";
	break;
    case JTYPE_UNDO:
	str = "UNDO";
	break;
    case JTYPE_REDO:
	str = "REDO";
	break;
    case JTYPE_AUDIT:
	str = "AUDIT";
	break;
    case JTYPE_SETATTR:
	str = "SETATTR";
	break;
    case JTYPE_WRITE:
	str = "WRITE";
	break;
    case JTYPE_PUTPAGES:
	str = "PUTPAGES";
	break;
    case JTYPE_SETACL:
	str = "SETACL";
	break;
    case JTYPE_SETEXTATTR:
	str = "SETEXTATTR";
	break;
    case JTYPE_CREATE:
	str = "CREATE";
	break;
    case JTYPE_MKNOD:
	str = "MKNOD";
	break;
    case JTYPE_LINK:
	str = "LINK";
	break;
    case JTYPE_SYMLINK:
	str = "SYMLINK";
	break;
    case JTYPE_WHITEOUT:
	str = "WHITEOUT";
	break;
    case JTYPE_REMOVE:
	str = "REMOVE";
	break;
    case JTYPE_MKDIR:
	str = "MKDIR";
	break;
    case JTYPE_RMDIR:
	str = "RMDIR";
	break;
    case JTYPE_RENAME:
	str = "RENAME";
	break;
    case JTYPE_VATTR:
	str = "vattr";
	break;
    case JTYPE_CRED:
	str = "cred";
	break;
    case JLEAF_FILEDATA:
	str = "filedata";
	break;
    case JLEAF_PATH1:
	str = "path1";
	break;
    case JLEAF_PATH2:
	str = "path2";
	break;
    case JLEAF_PATH3:
	str = "path3";
	break;
    case JLEAF_PATH4:
	str = "path4";
	break;
    case JLEAF_UID:
	str = "uid";
	break;
    case JLEAF_GID:
	str = "gid";
	break;
    case JLEAF_VTYPE:
	str = "vtype";
	break;
    case JLEAF_MODES:
	str = "modes";
	break;
    case JLEAF_FFLAGS:
	str = "fflags";
	break;
    case JLEAF_PID:
	str = "pid";
	break;
    case JLEAF_PPID:
	str = "ppid";
	break;
    case JLEAF_COMM:
	str = "comm";
	break;
    case JLEAF_ATTRNAME:
	str = "attrname";
	break;
    case JLEAF_PATH_REF:
	str = "path_ref";
	break;
    case JLEAF_RESERVED_0F:
	str = "?";
	break;
    case JLEAF_SYMLINKDATA:
	str = "symlinkdata";
	break;
    case JLEAF_SEEKPOS:
	str = "seekpos";
	break;
    case JLEAF_INUM:
	str = "inum";
	break;
    case JLEAF_NLINK:
	str = "nlink";
	break;
    case JLEAF_FSID:
	str = "fsid";
	break;
    case JLEAF_SIZE:
	str = "size";
	break;
    case JLEAF_ATIME:
	str = "atime";
	break;
    case JLEAF_MTIME:
	str = "mtime";
	break;
    case JLEAF_CTIME:
	str = "ctime";
	break;
    case JLEAF_GEN:
	str = "gen";
	break;
    case JLEAF_FLAGS:
	str = "flags";
	break;
    case JLEAF_UDEV:
	str = "udev";
	break;
    case JLEAF_FILEREV:
	str = "filerev";
	break;
    default:
	str = "?";
	break;
    }
    return (str);
}

void
stringout(FILE *fp, char c, int exact)
{
    if ((c >= 'a' && c <= 'z') ||
	(c >= 'A' && c <= 'Z') ||
	(c >= '0' && c <= '9')
    ) {
	putc(c, fp);
    } else if (isprint((unsigned char)c) && c != '\\' && c != '\"') {
	putc(c, fp);
    } else if (exact == 0) {
	putc('.', fp);
    } else if (c == 0) {
	fprintf(fp, "\\0");
    } else if (c == '\n') {
	fprintf(fp, "\\n");
    } else {
	fprintf(fp, "\\x%02x", (int)(unsigned char)c);
    }
}

void
jattr_reset(struct jattr *jattr)
{
    struct jattr *undo;
    struct jattr_data *data;

    if (jattr->path1)
	free(jattr->path1);
    if (jattr->path2)
	free(jattr->path2);
    if (jattr->path3)
	free(jattr->path3);
    if (jattr->path4)
	free(jattr->path4);
    if (jattr->comm)
	free(jattr->comm);
    if (jattr->attrname)
	free(jattr->attrname);
    if (jattr->pathref)
	free(jattr->pathref);
    if (jattr->symlinkdata)
	free(jattr->symlinkdata);
    while ((data = jattr->data.next) != NULL) {
	jattr->data.next = data->next;
	free(data);
    }
    if ((undo = jattr->undo) != NULL)
	jattr_reset(jattr->undo);
    bzero(jattr, sizeof(*jattr));
    jattr->undo = undo;
    jattr->uid = (uid_t)-1;
    jattr->gid = (gid_t)-1;
    jattr->size = (off_t)-1;
    jattr->modes = -1;
    jattr->flags = -1;
    jattr->seekpos = -1;
}

int64_t
buf_to_int64(const void *buf, int bytes)
{
    int64_t v;

    switch(bytes) {
    case 1:
	v = (int64_t)*(const u_int8_t *)buf;
	break;
    case 2:
	v = (int64_t)*(const u_int16_t *)buf;
	break;
    case 4:
	v = (int64_t)*(const u_int32_t *)buf;
	break;
    case 8:
	v = *(const int64_t *)buf;
	break;
    default:
	v = 0;
    }
    return(v);
}

char *
dupdatastr(const void *buf, int bytes)
{
    char *res;

    res = malloc(bytes + 1);
    bcopy(buf, res, bytes);
    res[bytes] = 0;

    return(res);
}

/*
 * Similar to dupdatastr() but contains sanity checks.
 */
char *
dupdatapath(const void *buf, int bytes)
{
    char *res;
    char *scan;

    res = malloc(bytes + 1);
    bcopy(buf, res, bytes);
    res[bytes] = 0;

    if (res[0] == '/') {
	fprintf(stderr, "Bad path: %s\n", res);
	free(res);
	return(NULL);
    }
    scan = res;
    for (;;) {
	if (scan[0] == '.' && scan[1] == '.' &&
	    (scan[2] == 0 || scan[2] == '/')
	) {
	    fprintf(stderr, "Bad path: %s\n", res);
	    free(res);
	    return(NULL);
	}
	if ((scan = strchr(scan, '/')) == NULL)
	    break;
	++scan;
    }
    return(res);
}

void
get_transid_from_file(const char *path, int64_t *transid, int flags)
{
    int n;
    int fd;
    char buf[32];

    *transid = 0;
    if ((fd = open(path, O_RDONLY)) >= 0) {
	n = read(fd, buf, sizeof(buf) - 1);
	if (n >= 0)
	    buf[n] = 0;
	*transid = strtoull(buf, NULL, 16);
	jmodes |= flags;
	close(fd);
    }
}

