/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/boot/i386/loader/conf.c,v 1.24 2003/08/25 23:28:32 obrien Exp $
 * $DragonFly: src/sys/boot/pc32/loader/conf.c,v 1.4 2005/09/03 23:52:49 dillon Exp $
 */

#include <stand.h>
#include <bootstrap.h>
#include "libi386/libi386.h"

/*
 * We could use linker sets for some or all of these, but
 * then we would have to control what ended up linked into
 * the bootstrap.  So it's easier to conditionalise things
 * here.
 *
 * XXX rename these arrays to be consistent and less namespace-hostile
 *
 * XXX as libi386 and biosboot merge, some of these can become linker sets.
 */

/* Exported for libstand */
struct devsw *devsw[] = {
    &bioscd,
    &biosdisk,
#if defined(LOADER_NFS_SUPPORT) || defined(LOADER_TFTP_SUPPORT)
    &pxedisk,
#endif
    NULL
};

struct fs_ops *file_system[] = {
    &hammer_fsops,
    &ufs_fsops,
    &ext2fs_fsops,
    &dosfs_fsops,
    &cd9660_fsops,
    &splitfs_fsops,
#ifdef LOADER_GZIP_SUPPORT
    &gzipfs_fsops,
#endif
#ifdef LOADER_BZIP2_SUPPORT
    &bzipfs_fsops,
#endif
#ifdef LOADER_NFS_SUPPORT 
    &nfs_fsops,
#endif
#ifdef LOADER_TFTP_SUPPORT
    &tftp_fsops,
#endif
    NULL
};

/* Exported for i386 only */
/* 
 * Sort formats so that those that can detect based on arguments
 * rather than reading the file go first.
 */
extern struct file_format	i386_elf;
extern struct file_format	i386_elf_obj;
extern struct file_format	x86_64_elf;
extern struct file_format	x86_64_elf_obj;

struct file_format *file_formats[] = {
    &i386_elf,
    &i386_elf_obj,
    &x86_64_elf,
    &x86_64_elf_obj,
    NULL
};

/* 
 * Consoles 
 *
 * We don't prototype these in libi386.h because they require
 * data structures from bootstrap.h as well.
 */
extern struct console vidconsole;
extern struct console comconsole;
extern struct console nullconsole;

struct console *consoles[] = {
    &vidconsole,
    &comconsole,
    &nullconsole,
    NULL
};

extern struct pnphandler isapnphandler;
extern struct pnphandler biospnphandler;
extern struct pnphandler biospcihandler;

struct pnphandler *pnphandlers[] = {
    &biospnphandler,		/* should go first, as it may set isapnp_readport */
    &isapnphandler,
    &biospcihandler,
    NULL
};
