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
 * $DragonFly: src/sbin/jscan/subs.c,v 1.1 2005/03/07 02:38:28 dillon Exp $
 */

#include "jscan.h"

void
jf_warn(struct jfile *jf, const char *ctl, ...)
{
    va_list va;

    fprintf(stderr, "@0x%016llx ", jf->jf_pos);
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


