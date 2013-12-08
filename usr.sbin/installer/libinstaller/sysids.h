/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 * $FreeBSD: /repoman/r/ncvs/src/sbin/i386/fdisk/fdisk.c,v 1.36.2.14 2004/01/30 14:40:47 harti Exp $
 * $_DragonFly: src/sbin/i386/fdisk/fdisk.c,v 1.9 2004/07/08 17:50:46 cpressey Exp $
 */

/*
 *
 * Ported to 386bsd by Julian Elischer  Thu Oct 15 20:26:46 PDT 1992
 *
 * 14-Dec-89  Robert Baron (rvb) at Carnegie-Mellon University
 *	Copyright (c) 1989	Robert. V. Baron
 *	Created.
 */

/*
 * sysids.h
 * $Id: sysids.h,v 1.2 2005/02/06 21:05:18 cpressey Exp $
 */

static struct part_type
{
	unsigned char type;
	const char *name;
} part_types[] =
{
	 {0x00, "unused"}
	,{0x01, "Primary DOS with 12 bit FAT"}
	,{0x02, "XENIX / filesystem"}
	,{0x03, "XENIX /usr filesystem"}
	,{0x04, "Primary DOS with 16 bit FAT (<= 32MB)"}
	,{0x05, "Extended DOS"}
	,{0x06, "Primary 'big' DOS (> 32MB)"}
	,{0x07, "OS/2 HPFS, NTFS, QNX-2 (16 bit) or Advanced UNIX"}
	,{0x08, "AIX filesystem"}
	,{0x09, "AIX boot partition or Coherent"}
	,{0x0A, "OS/2 Boot Manager or OPUS"}
	,{0x0B, "DOS or Windows 95 with 32 bit FAT"}
	,{0x0C, "DOS or Windows 95 with 32 bit FAT, LBA"}
	,{0x0E, "Primary 'big' DOS (> 32MB, LBA)"}
	,{0x0F, "Extended DOS, LBA"}
	,{0x10, "OPUS"}
	,{0x11, "OS/2 BM: hidden DOS with 12-bit FAT"}
	,{0x12, "Compaq diagnostics"}
	,{0x14, "OS/2 BM: hidden DOS with 16-bit FAT (< 32MB)"}
	,{0x16, "OS/2 BM: hidden DOS with 16-bit FAT (>= 32MB)"}
	,{0x17, "OS/2 BM: hidden IFS (e.g. HPFS)"}
	,{0x18, "AST Windows swapfile"}
	,{0x24, "NEC DOS"}
	,{0x39, "plan9"}
	,{0x3C, "PartitionMagic recovery"}
	,{0x40, "VENIX 286"}
	,{0x41, "Linux/MINIX (sharing disk with DRDOS)"}
	,{0x42, "SFS or Linux swap (sharing disk with DRDOS)"}
	,{0x43, "Linux native (sharing disk with DRDOS)"}
	,{0x4D, "QNX 4.2 Primary"}
	,{0x4E, "QNX 4.2 Secondary"}
	,{0x4F, "QNX 4.2 Tertiary"}
	,{0x50, "DM"}
	,{0x51, "DM"}
	,{0x52, "CP/M or Microport SysV/AT"}
	,{0x53, "DM6 Aux3"}
	,{0x54, "DM6"}
	,{0x55, "EZ-Drive (disk manager)"}
	,{0x56, "GB"}
	,{0x5C, "Priam Edisk (disk manager)"} /* according to S. Widlake */
	,{0x61, "Speed"}
	,{0x63, "ISC UNIX, other System V/386, GNU HURD or Mach"}
	,{0x64, "Novell Netware 2.xx"}
	,{0x65, "Novell Netware 3.xx"}
	,{0x70, "DiskSecure Multi-Boot"}
	,{0x75, "PCIX"}
	,{0x77, "QNX4.x"}
	,{0x78, "QNX4.x 2nd part"}
	,{0x79, "QNX4.x 3rd part"}
	,{0x80, "Minix 1.1 ... 1.4a"}
	,{0x81, "Minix 1.4b ... 1.5.10"}
	,{0x82, "Linux swap or Solaris x86"}
	,{0x83, "Linux filesystem"}
	,{0x84, "OS/2 hidden C: drive"}
	,{0x85, "Linux extended"}
	,{0x86, "NTFS volume set??"}
	,{0x87, "NTFS volume set??"}
	,{0x93, "Amoeba filesystem"}
	,{0x94, "Amoeba bad block table"}
	,{0x9F, "BSD/OS"}
	,{0xA0, "Suspend to Disk"}
	,{0xA5, "DragonFly/FreeBSD/NetBSD/386BSD"}
	,{0xA6, "OpenBSD"}
	,{0xA7, "NEXTSTEP"}
	,{0xA9, "NetBSD"}
	,{0xAC, "IBM JFS"}
	,{0xB7, "BSDI BSD/386 filesystem"}
	,{0xB8, "BSDI BSD/386 swap"}
	,{0xBE, "Solaris x86 boot"}
	,{0xC1, "DRDOS/sec with 12-bit FAT"}
	,{0xC4, "DRDOS/sec with 16-bit FAT (< 32MB)"}
	,{0xC6, "DRDOS/sec with 16-bit FAT (>= 32MB)"}
	,{0xC7, "Syrinx"}
	,{0xDB, "Concurrent CPM or C.DOS or CTOS"}
	,{0xE1, "Speed"}
	,{0xE3, "Speed"}
	,{0xE4, "Speed"}
	,{0xEB, "BeOS file system"}
	,{0xEE, "EFI GPT"}
	,{0xEF, "EFI System Partition"}
	,{0xF1, "Speed"}
	,{0xF2, "DOS 3.3+ Secondary"}
	,{0xF4, "Speed"}
	,{0xFE, "SpeedStor >1024 cyl. or LANstep"}
	,{0xFF, "BBT (Bad Blocks Table)"}
};
