/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project by
 * Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of The
 * DragonFly Project nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific, prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (c) 1998 Robert Nordier All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely permitted
 * provided that the above copyright notice and this paragraph and the
 * following disclaimer are duplicated in all such forms.
 *
 * This software is provided "AS IS" and without any express or implied
 * warranties, including, without limitation, the implied warranties of
 * merchantability and fitness for a particular purpose.
 *
 * $FreeBSD: src/sys/boot/i386/boot2/boot2.c,v 1.64 2003/08/25 23:28:31 obrien
 * Exp $
 */

#define AOUT_H_FORCE32
#include <sys/param.h>
#ifdef DISKLABEL64
#include <sys/disklabel64.h>
#else
#include <sys/disklabel32.h>
#endif
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/dtype.h>
#include <sys/dirent.h>
/*#include <sys/reboot.h> already included by common/rbx.h */
#include <machine/bootinfo.h>
#include <machine/elf.h>
#include <machine/psl.h>

#include <stdarg.h>

#include <a.out.h>

#include <btxv86.h>

#ifdef DISKLABEL64
#include "boot2_64.h"
#else
#include "boot2_32.h"
#endif
#include "boot2.h"

#include "lib.h"
#include "../bootasm.h"
#include "rbx.h"
#include "paths.h"

/* Define to 0 to omit serial support */
#ifndef SERIAL
#define SERIAL 1
#endif

#define IO_KEYBOARD	1
#define IO_SERIAL	2

#if SERIAL
#define DO_KBD (ioctrl & IO_KEYBOARD)
#define DO_SIO (ioctrl & IO_SERIAL)
#else
#define DO_KBD (1)
#define DO_SIO (0)
#endif

#define SECOND		18	/* Circa that many ticks in a second. */

#define NDEV		3
#define MEM_BASE	0x12
#define MEM_EXT 	0x15
#define V86_CY(x)	((x) & PSL_C)
#define V86_ZR(x)	((x) & PSL_Z)

#define DRV_HARD	0x80
#define DRV_MASK	0x7f

#define TYPE_AD		0
#define TYPE_DA		1
#define TYPE_MAXHARD	TYPE_DA
#define TYPE_FD		2

#define INVALID_S	"Bad %s\n"
#define NULL 		((void *)0)

extern uint32_t	_end;

static const char *const dev_nm[NDEV] = {"ad", "da", "fd"};
static const unsigned char dev_maj[NDEV] = {30, 4, 2};

static struct dsk {
	unsigned	drive;
	unsigned	type;
	unsigned	unit;
	uint8_t		slice;
	uint8_t		part;
	unsigned	start;
	int		init;
} dsk;

static unsigned char	cmd [512], cmddup[512];
static const char      *kname = NULL;
//uint32_t		opts = RB_DUAL;	/* Shoulld we default to DUAL mode ? */
uint32_t		opts = 0;
static struct bootinfo 	bootinfo;
uint8_t			autoboot = 1;

#if SERIAL
static int		comspeed = 0;
static uint8_t		ioctrl = IO_KEYBOARD;
#endif

/*
 * boot2 encapsulated ABI elements provided to *fsread.c
 *
 * NOTE: boot2_dmadat is extended by per-filesystem APIs
 */
uint32_t		fs_off;
int			ls;		/* used in ufsread_64 */
int			no_io_error;	/* used in hammer2_64 */
struct boot2_dmadat     *boot2_dmadat;

void			exit      (int);
static void		load(void);
static int		parse(void);
static int		dskprobe(void);
static int		xfsread(boot2_ino_t, void *, size_t);
static int		drvread(void *, unsigned, unsigned);
static int		keyhit(unsigned);
static inline void	getstr(void);

/* should be moved to common/libi386 */
inline void
memcpy(void *restrict d, const void *restrict s, int len)
{
	char           *dd = d;
	const char     *ss = s;

	while (--len >= 0)
		dd[len] = ss[len];
}

/* should be moved to common/libi386 */
int
strcmp(const char *s1, const char *s2)
{
	for (; *s1 == *s2 && *s1; s1++, s2++);
	return ((int)((unsigned char)*s1 - (unsigned char)*s2));
}

static int		xputc(int);
static int		xgetc(int);
static int		getc (int);
#if defined(UFS) && defined(HAMMER2FS)
const struct boot2_fsapi *fsapi;
#elif defined(UFS)
#define fsapi	(&boot2_ufs_api)
#elif defined(HAMMER2FS)
#define fsapi	(&boot2_hammer2_api)
#endif

#define NQprintf(_fmt, ...) ({			\
	if (!OPT_CHECK(RB_QUIET))		\
		printf(_fmt, ##__VA_ARGS__);	\
})

/*
 * Setup the (serial) console. If the initialization fails, don't try
 * to use the serial port. This can happen if the serial port is unmapped
 * (happens on new laptops a lot (8250)).
 */
#if SERIAL
static void
check_ioctrl(int newcomspeed) {
	ioctrl = OPT_CHECK(RB_DUAL) ? (IO_SERIAL | IO_KEYBOARD) :
		OPT_CHECK(RB_SERIAL) ? IO_SERIAL : IO_KEYBOARD;
	if (DO_SIO) {
		if (comspeed != newcomspeed && sio_init(115200 / newcomspeed) == 0)
			comspeed = newcomspeed;
		else {
			ioctrl &= ~IO_SERIAL;
			opts &= ~(RB_DUAL | RB_SERIAL);
			comspeed = 0;
		}
		sio_putc('\n');
		//NQprintf("SIO:%u\n", comspeed);
	}
}
#endif

int
main(void)
{
	boot2_ino_t	ino;
	uint8_t		i;
	*cmd=0;

	boot2_dmadat =
		(void *)(roundup2(__base + (int32_t) & _end, 0x10000) - __base);
	v86.ctl = V86_FLAGS;
	v86.efl = PSL_RESERVED_DEFAULT | PSL_I;
	dsk.drive = *(uint8_t *) PTOV(MEM_BTX_USR_ARG);
	dsk.type = dsk.drive & DRV_HARD ? TYPE_AD : TYPE_FD;
	dsk.unit = dsk.drive & DRV_MASK;
	dsk.slice = *(uint8_t *) PTOV(MEM_BTX_USR_ARG + 1) + 1;
	bootinfo.bi_version = BOOTINFO_VERSION;
	bootinfo.bi_size = sizeof(bootinfo);

	/*
	 * Probe the default disk and process the configuration file if
	 * successful.
	 */
	if (dskprobe() == 0) {
		if ((ino = fsapi->fslookup(PATH_CONFIG)) ||
		    (ino = fsapi->fslookup(PATH_DOTCONFIG)))
			fsapi->fsread(ino, cmd, sizeof(cmd));
	}
	/*
	 * Parse config file if present.  parse() will re-probe if necessary.
	 */
	if (cmd[0]) {
		memcpy(cmddup, cmd, sizeof(cmd));
		if (parse())
			autoboot = 0;
		if (!OPT_CHECK(RB_QUIET))
			printf("%s: %s", PATH_CONFIG, cmddup);
		/* Do not process this command twice */
		*cmd = 0;
	}
#if SERIAL
	else if (!(*(uint8_t *) PTOV(0x496) & 0x10)) {	/* probe keyboard, if not present switch to dual mode at least */
		opts |= OPT_SET(RB_DUAL);
		check_ioctrl(SIOSPD);
	}
#endif

	/*
	 * Try to exec stage 3 boot loader. If interrupted by a keypress, or
	 * in case of failure, try to load a kernel directly instead.
	 *
	 * Checking: ["/loader", "/kernel", "/boot/loader", "/boot/kernel"]
	 * to support booting from boot and root partition
	 *
	 * Once path has been found (and kname set) we keep using it until told otherwise
	 */
	if (!kname) {
		const char * const paths[] = {
			PATH_BOOTPREFIX /*+*/ PATH_LOADER,
			PATH_BOOTPREFIX /*+*/ PATH_KERNEL,
			PATH_ROOTPREFIX /*+*/ PATH_LOADER,
			PATH_ROOTPREFIX /*+*/ PATH_KERNEL
		};
		for (i = 0; i < 5; i++) {
			const char *path = paths[i];
			if (path && (ino = fsapi->fslookup(path))) {
				kname = path;
				break;
			}
		}
	}

	if (kname && autoboot && !keyhit(3 * SECOND)) {
		if (!parse())
			load();				/* never return */
	}
	autoboot = 0;
	/* Present the user with the boot2 prompt. */
	for (;;) {
		//printf("DragonFly boot2\n"
		printf("DragonFly\n"
		       "%u:%s(%u,%c)%s: ",
		       dsk.drive & DRV_MASK, dev_nm[dsk.type],
		       dsk.unit, 'a' + dsk.part, kname);
		if (DO_SIO)
			sio_flush();
		getstr();				/* get input */
		if (cmd[0] == '\0')			/* empty line -> boot */
			load();				/* never return */
		if (parse())				/* parse input */
			printf(INVALID_S, "cmd\a");
	}
}

/*
Options	Message goes to
none			internal console
-h			only serial console
-D			both serial and internal consoles (prefer internal)
-Dh			both serial and internal consoles (prefer serial)
-P & keyboard present	internal console
-P & keyboard absent	serial console
*/
static int
parse(void)
{
	char	       *arg = cmd;
	char	       *p, *q;
	uint8_t 	drv;
	char	 	c;
	int 		i;
#if SERIAL
	static int	newcomspeed = SIOSPD;
#endif
	while ((c = *arg++)) {
		if (c == ' ' || c == '\t' || c == '\n')
			continue;
		for (p = arg; *p && *p != '\n' && *p != ' ' && *p != '\t'; p++);
		if (*p)
			*p++ = 0;
		if (c == '-') {
			while ((c = *arg++)) {
				uint8_t match = 0;
				if (c == 'P') {				/* Probe Keyboard */
					const char     *cp;
					if (*(uint8_t *) PTOV(0x496) & 0x10) {
						cp = "yes";
					} else {
						opts &= ~OPT_SET(RB_DUAL);
						opts |= OPT_SET(RB_SERIAL);
						cp = "no";
					}
					NQprintf("KBD: %s\n", cp);
					match = 1;
#if SERIAL
				} else if (c == 'S') {			/* Set Serial Speed */
					int j;
					while ((u_int) (i = *arg++ - '0') <= 9)
						j = j * 10 + i;
					if (j > 0 && i == -'0') {
						opts |= OPT_SET(RB_SERIAL);
						newcomspeed = j;
						match = 1;
					}
#endif
				} else {				/* Match/Set flags */
					for(i = 0; i < NOPT; i++) {
						if (c==optstr[i]) {
							opts ^= OPT_SET(1 << flags[i]);
							match = 1;
							break;
						}
					}
				}
				if (!match)
					return -1;			/* Unknown flag/cmd */
			}
#if SERIAL
			check_ioctrl(newcomspeed);
#endif
			if (!OPT_CHECK(RB_QUIET)) {			/* Could use RB_VERBOSE here instead */
				for(i = 0; i < NOPT; i++)
					putchar(OPT_CHECK(1 << flags[i]) ? optstr[i] : '_');
				putchar('\n');
			}
		} else {
			for (q = arg--; *q && *q != '('; q++);
			if (*q) {
				drv = -1;
				if (arg[1] == ':') {
					drv = *arg - '0';
					if (drv > 9)
						return (-1);
					arg += 2;
				}
				if (q - arg != 2)
					return -1;
				for (i = 0; arg[0] != dev_nm[i][0] ||
				     arg[1] != dev_nm[i][1]; i++)
					if (i == NDEV - 1)
						return -1;
				dsk.type = i;
				arg += 3;
				dsk.unit = *arg - '0';
				if (arg[1] != ',' || dsk.unit > 9)
					return -1;
				arg += 2;
				dsk.slice = WHOLE_DISK_SLICE;
				if (arg[1] == ',') {
					dsk.slice = *arg - '0' + 1;
					if (dsk.slice > NDOSPART + 1)
						return -1;
					arg += 2;
				}
				if (arg[1] != ')')
					return -1;
				dsk.part = *arg - 'a';
				if (dsk.part > 7)
					return (-1);
				arg += 2;
				if (drv == -1)
					drv = dsk.unit;
				dsk.drive = (dsk.type <= TYPE_MAXHARD
					     ? DRV_HARD : 0) + drv;
			}
			NQprintf("path=%s\n", arg);	/* temp */
			kname = arg;
		}
		arg = p;
	}
	return dskprobe();
}

static int
dskprobe(void)
{
	struct dos_partition *dp;
#ifdef DISKLABEL64
	struct disklabel64 *d;
#else
	struct disklabel32 *d;
#endif
	char           *sec;
	unsigned	i;
	uint8_t		sl;

	/*
	 * Probe slice table
	 */
	sec = boot2_dmadat->secbuf;
	dsk.start = 0;
	if (drvread(sec, DOSBBSECTOR, 1))
		return -1;
	dp = (void *)(sec + DOSPARTOFF);
	sl = dsk.slice;
	if (sl < BASE_SLICE) {
		for (i = 0; i < NDOSPART; i++)
			if ((dp[i].dp_typ == DOSPTYP_386BSD ||
			     dp[i].dp_typ == DOSPTYP_DFLYBSD) &&
			    (dp[i].dp_flag & 0x80 || sl < BASE_SLICE)) {
				sl = BASE_SLICE + i;
				if (dp[i].dp_flag & 0x80 ||
				    dsk.slice == COMPATIBILITY_SLICE)
					break;
			}
		if (dsk.slice == WHOLE_DISK_SLICE)
			dsk.slice = sl;
	}
	if (sl != WHOLE_DISK_SLICE) {
		if (sl != COMPATIBILITY_SLICE)
			dp += sl - BASE_SLICE;
		if (dp->dp_typ != DOSPTYP_386BSD && dp->dp_typ != DOSPTYP_DFLYBSD) {
			printf(INVALID_S, "slice");
			return -1;
		}
		dsk.start = dp->dp_start;
	}
	/*
	 * Probe label and partition table
	 */
#ifdef DISKLABEL64
	if (drvread(sec, dsk.start, (sizeof(struct disklabel64) + 511) / 512))
		return -1;
	d = (void *)sec;
	if (d->d_magic != DISKMAGIC64) {
		printf(INVALID_S, "label");
		return -1;
	} else {
		if (dsk.part >= d->d_npartitions || d->d_partitions[dsk.part].p_bsize == 0) {
			printf(INVALID_S, "partition");
			return -1;
		}
		dsk.start += d->d_partitions[dsk.part].p_boffset / 512;
	}
#else
	if (drvread(sec, dsk.start + LABELSECTOR32, 1))
		return -1;
	d = (void *)(sec + LABELOFFSET32);
	if (d->d_magic != DISKMAGIC32 || d->d_magic2 != DISKMAGIC32) {
		if (dsk.part != RAW_PART) {
			printf(INVALID_S, "label");
			return -1;
		}
	} else {
		if (!dsk.init) {
			if (d->d_type == DTYPE_SCSI)
				dsk.type = TYPE_DA;
			dsk.init++;
		}
		if (dsk.part >= d->d_npartitions ||
		    !d->d_partitions[dsk.part].p_size) {
			printf(INVALID_S, "partition");
			return -1;
		}
		dsk.start += d->d_partitions[dsk.part].p_offset;
		dsk.start -= d->d_partitions[RAW_PART].p_offset;
	}
#endif
	/*
	 * Probe filesystem
	 */
#if defined(UFS) && defined(HAMMER2FS)
	if (boot2_ufs_api.fsinit() == 0) {
		fsapi = &boot2_ufs_api;
	} else if (boot2_hammer2_api.fsinit() == 0) {
		fsapi = &boot2_hammer2_api;
	} else {
		printf("fs probe failed\n");
		fsapi = &boot2_ufs_api;
		return -1;
	}
	return 0;
#else
	return fsapi->fsinit();
#endif
}

/*
 * Read from the probed disk.  We have established the slice and partition
 * base sector.
 */
int
dskread(void *buf, unsigned lba, unsigned nblk)
{
	return drvread(buf, dsk.start + lba, nblk);
}

static int
xfsread(boot2_ino_t inode, void *buf, size_t nbyte)
{
	if ((size_t) fsapi->fsread(inode, buf, nbyte) != nbyte) {
		printf(INVALID_S, "format");
		return -1;
	}
	return 0;
}

static void
load(void)
{
	union {
		struct exec	ex;
		Elf32_Ehdr	eh;
	}		hdr;
	static Elf32_Phdr ep[2];
	static Elf32_Shdr es[2];
	caddr_t		p;
	boot2_ino_t	ino;
	uint32_t	addr;
	int		i         , j;

	if (!(ino = fsapi->fslookup(kname))) {
		NQprintf("\nCan't find %s\n", kname);
		return;
	}
	if (xfsread(ino, &hdr, sizeof(hdr)))
		return;

	if (N_GETMAGIC(hdr.ex) == ZMAGIC) {
		addr = hdr.ex.a_entry & 0xffffff;
		p = PTOV(addr);
		fs_off = PAGE_SIZE;
		if (xfsread(ino, p, hdr.ex.a_text))
			return;
		p += roundup2(hdr.ex.a_text, PAGE_SIZE);
		if (xfsread(ino, p, hdr.ex.a_data))
			return;
	} else if (IS_ELF(hdr.eh)) {
		fs_off = hdr.eh.e_phoff;
		for (j = i = 0; i < hdr.eh.e_phnum && j < 2; i++) {
			if (xfsread(ino, ep + j, sizeof(ep[0])))
				return;
			if (ep[j].p_type == PT_LOAD)
				j++;
		}
		for (i = 0; i < 2; i++) {
			p = PTOV(ep[i].p_paddr & 0xffffff);
			fs_off = ep[i].p_offset;
			if (xfsread(ino, p, ep[i].p_filesz))
				return;
		}
		p += roundup2(ep[1].p_memsz, PAGE_SIZE);
		bootinfo.bi_symtab = VTOP(p);
		if (hdr.eh.e_shnum == hdr.eh.e_shstrndx + 3) {
			fs_off = hdr.eh.e_shoff + sizeof(es[0]) *
				(hdr.eh.e_shstrndx + 1);
			if (xfsread(ino, &es, sizeof(es)))
				return;
			for (i = 0; i < 2; i++) {
				*(Elf32_Word *) p = es[i].sh_size;
				p += sizeof(es[i].sh_size);
				fs_off = es[i].sh_offset;
				if (xfsread(ino, p, es[i].sh_size))
					return;
				p += es[i].sh_size;
			}
		}
		addr = hdr.eh.e_entry & 0xffffff;
		bootinfo.bi_esymtab = VTOP(p);
	} else {
		printf(INVALID_S, "format");
		return;
	}

	bootinfo.bi_kernelname = VTOP(kname);
	bootinfo.bi_bios_dev = dsk.drive;
	__exec((caddr_t) addr, RB_BOOTINFO | (opts & RB_MASK),
	       MAKEBOOTDEV(dev_maj[dsk.type], dsk.slice, dsk.unit, dsk.part),
	       0, 0, 0, VTOP(&bootinfo));
}

/* XXX - Needed for btxld to link the boot2 binary; do not remove. */
void
exit(int x)
{
}

/*
 * boot encapsulated ABI
 */
void
printf(const char *fmt,...)
{
	va_list		ap;
	static char	buf [10];
	char           *s;
	unsigned	u;
#ifdef HAMMER2FS
	uint64_t	q;
#endif
	int		c;

	va_start(ap, fmt);
	while ((c = *fmt++)) {
		if (c == '%') {
			c = *fmt++;
			switch (c) {
			case 'c':
				putchar(va_arg(ap, int));
				continue;
			case 's':
				for (s = va_arg(ap, char *); *s; s++)
					putchar(*s);
				continue;
#ifdef HAMMER2FS
			case 'q':
			case 'x': 
				if (c=='q') {	/* save space */
					++fmt;	/* skip the 'x' */
					q = va_arg(ap, uint64_t);
				} else {
					u = va_arg(ap, unsigned);
				}
				do {
					if ((q & 15) < 10)
						*s++ = '0' + (q & 15);
					else
						*s++ = 'a' + (q & 15) - 10;
				} while (q >>= 4);
				while (--s >= buf)
					putchar(*s);
				continue;
#endif
			case 'u':
				u = va_arg(ap, unsigned);
				s = buf;
				do {
					*s++ = '0' + u % 10U;
				} while (u /= 10U);
				while (--s >= buf)
					putchar(*s);
				continue;
			}
		}
		putchar(c);
	}
	va_end(ap);
	return;
}

static inline void
getstr(void)
{
	unsigned char  *s;
	int	 	c;

	s = cmd;
	for (;;) {
		switch (c = xgetc(0)) {
		case 0:
			break;
		case '\177':
		case '\b':
			if (s > cmd) {
				s--;
				printf("\b \b");
			}
			break;
		case '\n':
		case '\r':
			putchar('\n');
			*s = '\0';
			return;
		default:
			if (s - cmd < sizeof(cmd) - 1)
				*s++ = c;
			putchar(c);
		}
	}
}

static inline void
putc(int c)
{
	v86.addr = 0x10;
	v86.eax = 0xe00 | (c & 0xff);
	v86.ebx = 0x7;
	v86int();
}

/*
 * boot encapsulated ABI
 */
void
putchar(int c)
{
	if (c == '\n')
		xputc('\r');
	xputc(c);
}

/*
 * boot encapsulated ABI
 */
static int
drvread(void *buf, unsigned lba, unsigned nblk)
{
	static unsigned	c = 0x2d5c7c2f;	/* twiddle */

	if (autoboot && !OPT_CHECK(RB_QUIET)) {
		xputc(c = c << 8 | c >> 24);
		xputc('\b');
	}
	v86.ctl = V86_ADDR | V86_CALLF | V86_FLAGS;
	v86.addr = XREADORG;	/* call to xread in boot1 */
	v86.es = VTOPSEG(buf);
	v86.eax = lba;
	v86.ebx = VTOPOFF(buf);
	v86.ecx = lba >> 16;
	v86.edx = nblk << 8 | dsk.drive;
	v86int();
	v86.ctl = V86_FLAGS;
	if (V86_CY(v86.efl) && !no_io_error && !OPT_CHECK(RB_QUIET)) {
		printf("error %u lba %u\n", v86.eax >> 8 & 0xff, lba);
		return -1;
	}
	if (autoboot && !OPT_CHECK(RB_QUIET)) {
		xputc('\b');
	}
	return 0;
}

static int
keyhit (unsigned ticks)
{
	uint32_t 	t0, t1;
	if (OPT_CHECK(RB_NOINTR))
		return	0;
	t0 = 0;
	for (;;) {
		if (xgetc(1)) {
			return 1;
		}
		t1 = *(uint32_t *) PTOV(0x46c);
		if (!t0)
			t0 = t1;
		if ((uint32_t) (t1 - t0) >= ticks)
			return 0;
	}
}

static int
xputc (int c)
{
	if (DO_KBD)
		putc(c);
	if (DO_SIO)
		sio_putc(c);
	return (c);
}

static int
getc (int fn)
{
	v86.addr = 0x16;
	v86.eax = fn << 8;
	v86int();
	return fn == 0 ? v86.eax & 0xff : !V86_ZR(v86.efl);
}

static int
xgetc (int fn)
{
	if (OPT_CHECK(RB_NOINTR))
		return (0);
	for (;;) {
		if (DO_KBD && getc(1))
			return (fn ? 1 : getc(0));
		if (DO_SIO && sio_ischar())
			return (fn ? 1 : sio_getc());
		if (fn)
			return (0);
	}
}
