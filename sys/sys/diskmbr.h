/*
 * Copyright (c) 1987, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)disklabel.h	8.2 (Berkeley) 7/10/94
 */

#ifndef _SYS_DISKMBR_H_
#define	_SYS_DISKMBR_H_

#include <sys/types.h>

#define	DOSBBSECTOR	0	/* DOS boot block relative sector number */
#define	DOSPARTOFF	446
#define	DOSPARTSIZE	16
#define	NDOSPART	4
#define	NEXTDOSPART	32
#define	DOSMAGICOFF	510
#define	DOSMAGIC	0xAA55

#define	DOSPTYP_EXT	0x05	/* DOS extended partition */
#define	DOSPTYP_EXTLBA	0x0F	/* DOS extended partition */
#define	DOSPTYP_ONTRACK	0x54	/* Ontrack Disk Manager */
#define	DOSPTYP_DFLYBSD	0x6C	/* DragonFly BSD partition type */
				/* NOTE: DragonFly BSD had been using 0xA5
				 * forever but after many years we're finally
				 * shifting to our own as 0xA5 causes conflicts
				 * in GRUB. */
#define	DOSPTYP_LINSWP	0x82	/* Linux swap partition */
#define	DOSPTYP_LINUX	0x83	/* Linux partition */
#define	DOSPTYP_386BSD	0xA5	/* 386BSD partition type */
#define	DOSPTYP_OPENBSD	0xA6	/* OpenBSD partition type */
#define	DOSPTYP_NETBSD	0xA9	/* NetBSD partition type */
#define	DOSPTYP_PMBR	0xEE	/* GPT Protective MBR */
#define	DOSPTYP_EFI	0xEF	/* EFI system partition */

#ifndef _STANDALONE
static const struct dos_ptype
{
	unsigned char type;
	const char *name;
} dos_ptypes[] = {
	{ 0x00, "unused" },
	{ 0x01, "Primary DOS with 12 bit FAT" },
	{ 0x02, "XENIX / filesystem" },
	{ 0x03, "XENIX /usr filesystem" },
	{ 0x04, "Primary DOS with 16 bit FAT (<= 32MB)" },
	{ DOSPTYP_EXT, "Extended DOS" },
	{ 0x06, "Primary 'big' DOS (> 32MB)" },
	{ 0x07, "OS/2 HPFS, NTFS, QNX-2 (16 bit) or Advanced UNIX" },
	{ 0x08, "AIX filesystem" },
	{ 0x09, "AIX boot partition or Coherent" },
	{ 0x0A, "OS/2 Boot Manager or OPUS" },
	{ 0x0B, "DOS or Windows 95 with 32 bit FAT" },
	{ 0x0C, "DOS or Windows 95 with 32 bit FAT, LBA" },
	{ 0x0E, "Primary 'big' DOS (> 32MB, LBA)" },
	{ DOSPTYP_EXTLBA, "Extended DOS, LBA" },
	{ 0x10, "OPUS" },
	{ 0x11, "OS/2 BM: hidden DOS with 12-bit FAT" },
	{ 0x12, "Compaq diagnostics" },
	{ 0x14, "OS/2 BM: hidden DOS with 16-bit FAT (< 32MB)" },
	{ 0x16, "OS/2 BM: hidden DOS with 16-bit FAT (>= 32MB)" },
	{ 0x17, "OS/2 BM: hidden IFS (e.g. HPFS)" },
	{ 0x18, "AST Windows swapfile" },
	{ 0x24, "NEC DOS" },
	{ 0x39, "plan9" },
	{ 0x3C, "PartitionMagic recovery" },
	{ 0x40, "VENIX 286" },
	{ 0x41, "Linux/MINIX (sharing disk with DRDOS)" },
	{ 0x42, "SFS or Linux swap (sharing disk with DRDOS)" },
	{ 0x43, "Linux native (sharing disk with DRDOS)" },
	{ 0x4D, "QNX 4.2 Primary" },
	{ 0x4E, "QNX 4.2 Secondary" },
	{ 0x4F, "QNX 4.2 Tertiary" },
	{ 0x50, "DM" },
	{ 0x51, "DM" },
	{ 0x52, "CP/M or Microport SysV/AT" },
	{ 0x53, "DM6 Aux3" },
	{ DOSPTYP_ONTRACK, "DM6" },
	{ 0x55, "EZ-Drive (disk manager)" },
	{ 0x56, "GB" },
	{ 0x5C, "Priam Edisk (disk manager)" }, /* according to S. Widlake */
	{ 0x61, "Speed" },
	{ 0x63, "ISC UNIX, other System V/386, GNU HURD or Mach" },
	{ 0x64, "Novell Netware 2.xx" },
	{ 0x65, "Novell Netware 3.xx" },
	{ DOSPTYP_DFLYBSD, "DragonFly BSD" },
	{ 0x70, "DiskSecure Multi-Boot" },
	{ 0x75, "PCIX" },
	{ 0x77, "QNX4.x" },
	{ 0x78, "QNX4.x 2nd part" },
	{ 0x79, "QNX4.x 3rd part" },
	{ 0x80, "Minix 1.1 ... 1.4a" },
	{ 0x81, "Minix 1.4b ... 1.5.10" },
	{ DOSPTYP_LINSWP, "Linux swap or Solaris x86" },
	{ DOSPTYP_LINUX, "Linux filesystem" },
	{ 0x84, "OS/2 hidden C: drive" },
	{ 0x85, "Linux extended" },
	{ 0x86, "NTFS volume set??" },
	{ 0x87, "NTFS volume set??" },
	{ 0x93, "Amoeba filesystem" },
	{ 0x94, "Amoeba bad block table" },
	{ 0x9F, "BSD/OS" },
	{ 0xA0, "Suspend to Disk" },
	{ DOSPTYP_386BSD, "DragonFly/FreeBSD/NetBSD/386BSD" },
	{ DOSPTYP_OPENBSD, "OpenBSD" },
	{ 0xA7, "NEXTSTEP" },
	{ DOSPTYP_NETBSD, "NetBSD" },
	{ 0xAC, "IBM JFS" },
	{ 0xB7, "BSDI BSD/386 filesystem" },
	{ 0xB8, "BSDI BSD/386 swap" },
	{ 0xBE, "Solaris x86 boot" },
	{ 0xC1, "DRDOS/sec with 12-bit FAT" },
	{ 0xC4, "DRDOS/sec with 16-bit FAT (< 32MB)" },
	{ 0xC6, "DRDOS/sec with 16-bit FAT (>= 32MB)" },
	{ 0xC7, "Syrinx" },
	{ 0xDB, "Concurrent CPM or C.DOS or CTOS" },
	{ 0xE1, "Speed" },
	{ 0xE3, "Speed" },
	{ 0xE4, "Speed" },
	{ 0xEB, "BeOS file system" },
	{ DOSPTYP_PMBR, "EFI GPT" },
	{ DOSPTYP_EFI, "EFI System Partition" },
	{ 0xF1, "Speed" },
	{ 0xF2, "DOS 3.3+ Secondary" },
	{ 0xF4, "Speed" },
	{ 0xFE, "SpeedStor >1024 cyl. or LANstep" },
	{ 0xFF, "BBT (Bad Blocks Table)" }
};
#endif /* !_STANDALONE */

/*
 * Note that sector numbers in a legacy MBR DOS partition start at 1 instead
 * of 0, so the first sector of the disk would be cyl 0, head 0, sector 1
 * and translate to block 0 in the actual disk I/O.
 */
struct dos_partition {
	unsigned char	dp_flag;	/* bootstrap flags */
	unsigned char	dp_shd;		/* starting head */
	unsigned char	dp_ssect;	/* starting sector */
	unsigned char	dp_scyl;	/* starting cylinder */
	unsigned char	dp_typ;		/* partition type */
	unsigned char	dp_ehd;		/* end head */
	unsigned char	dp_esect;	/* end sector */
	unsigned char	dp_ecyl;	/* end cylinder */
	uint32_t	dp_start;	/* absolute starting sector number */
	uint32_t	dp_size;	/* partition size in sectors */
};

#ifndef CTASSERT
#define CTASSERT(x)		_CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)		__CTASSERT(x, y)
#define __CTASSERT(x, y)	typedef char __assert_ ## y [(x) ? 1 : -1]
#endif

CTASSERT(sizeof (struct dos_partition) == DOSPARTSIZE);

#define	DPSECT(s) ((s) & 0x3f)		/* isolate relevant bits of sector */
#define	DPCYL(c, s) ((c) + (((s) & 0xc0)<<2)) /* and those that are cylinder */

#endif /* !_SYS_DISKMBR_H_ */
