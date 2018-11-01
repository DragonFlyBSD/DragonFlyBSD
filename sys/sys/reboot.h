/*
 * Copyright (c) 1982, 1986, 1988, 1993, 1994
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
 *	@(#)reboot.h	8.3 (Berkeley) 12/13/94
 * $FreeBSD: src/sys/sys/reboot.h,v 1.18.2.1 2001/12/17 18:44:43 guido Exp $
 */

#ifndef _SYS_REBOOT_H_
#define _SYS_REBOOT_H_

/*
 * Arguments to reboot system call.  These are passed to
 * the boot program and on to init.
 *
 * Note: if neither MUTE, SERIAL, or VIDEO is set, multi-console (DUAL) mode is
 * assumed.
 */
#define	RB_AUTOBOOT	0		/* flags for system auto-booting itself */

#define	RB_ASKNAME	0x00000001	/* ask for file name to reboot from (-a) */
#define	RB_SINGLE	0x00000002	/* reboot to single user only (-s) */
#define	RB_NOSYNC	0x00000004	/* dont sync before reboot (unused) */
#define	RB_HALT		0x00000008	/* don't reboot, just halt */
#define	RB_INITNAME	0x00000010	/* name given for /etc/init (unused) */
#define	RB_DFLTROOT	0x00000020	/* use compiled-in rootdev (-r) */
#define	RB_KDB		0x00000040	/* give control to kernel debugger (-d) */
#define	RB_RDONLY	0x00000080	/* mount root fs read-only (unused) */
#define	RB_DUMP		0x00000100	/* dump kernel memory before reboot (unused) */
#define	RB_MINIROOT	0x00000200	/* mini-root present in memory at boot time (unused) */
#define RB_CONFIG	0x00000400	/* was: invoke user configuration routing (-c) */
#define RB_VERBOSE	0x00000800	/* print all potentially useful info (-v) */
#define	RB_SERIAL	0x00001000	/* user serial port as console (-h) */
#define	RB_CDROM	0x00002000	/* use cdrom as root (-C) */
#define	RB_POWEROFF	0x00004000	/* if you can, turn the power off (unused) */
#define	RB_GDB		0x00008000	/* use GDB remote debugger instead of DDB (-g) */
#define	RB_MUTE		0x00010000	/* Come up with the console muted (-m) */
#define	RB_SELFTEST	0x00020000	/* don't boot to normal operation, do selftest (unused) */
#define RB_RESERVED01	0x00040000	/* reserved for internal use of boot blocks */
#define RB_RESERVED02	0x00080000	/* reserved for internal use of boot blocks */
#define	RB_PAUSE	0x00100000	/* pause after each output line during probe (-p) */
//#define RB_REROOT	0x00200000	/* freebsd */
#define RB_QUIET	0x00200000	/* Don't generate output during boot1/boot2 (-q) */
//#define RB_REROOT	0x00400000	/* unmount the rootfs and mount it again (fbsd)*/
//#define RB_POWERCYCLE	0x00800000	/* Power cycle if possible (fbsd) */
//#define RB_UNUSED03	0x01000000
//#define RB_UNUSED04	0x02000000
//#define RB_UNUSED05	0x04000000
#define RB_NOINTR	0x08000000	/* Non Interruptable come up (-n) */
#define RB_DUAL		0x10000000	/* use comconsole and vidconsole (-D) */
//#define RB_PROBE	0x20000000	/* Probe multiple consoles (fbsd) */

/* temp fixup */
#define RB_VIDEO	0x40000000	/* use video console */
#define RB_MULTIPLE	0x40000000	/* use multiple consoles (-D) */
/* end temp */

#define	RB_BOOTINFO	0x8000000	/* have `struct bootinfo *' arg */

#endif
