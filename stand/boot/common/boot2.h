/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
/*
 * boot2 encapsulated ABI.  The boot2 standalone code provides these functions
 * to the boot2 add-on modules (ufsread.c and hammer2.c).
 */

#ifndef _BOOT_COMMON_BOOT2_H_
#define _BOOT_COMMON_BOOT2_H_


#include <sys/param.h>
#include <sys/dtype.h>
#include <sys/dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <machine/bootinfo.h>
#include <machine/elf.h>

/*
 * We only need a 32 bit ino_t for UFS-only boot code.  We have to squeeze
 * space usage, else we'd just use 64 bits across the board.
 */
#if defined(HAMMERFS) || defined(HAMMER2FS)
typedef uint64_t boot2_ino_t;
#else
typedef uint32_t boot2_ino_t;
#endif

struct boot2_fsapi {
	int (*fsinit)(void);
	boot2_ino_t (*fslookup)(const char *);
	ssize_t (*fsread)(boot2_ino_t, void *, size_t);
};

/*
 * secbuf needs to be big enough for the label reads
 * (32 and 64 bit disklabels).
 */
struct boot2_dmadat {
	char	secbuf[DEV_BSIZE*4];
	/* extended by *fsread() modules */
};

extern uint32_t fs_off;
extern int	no_io_error;
extern int	ls;
extern struct boot2_dmadat *boot2_dmadat;

extern int dskread(void *, unsigned, unsigned);
extern void printf(const char *,...);
extern void putchar(int);
extern int strcmp(const char *s1, const char *s2);
extern void memcpy(void *d, const void *s, int len);

#ifdef UFS
extern const struct boot2_fsapi boot2_ufs_api;
#endif
#ifdef HAMMERFS
extern const struct boot2_fsapi boot2_hammer_api;
#endif
#ifdef HAMMER2FS
extern const struct boot2_fsapi boot2_hammer2_api;
#endif

#endif
