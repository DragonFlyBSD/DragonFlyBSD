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

#define	RB_ASKNAME	1 << 0		/* ask for file name to reboot from (-a) */
#define	RB_SINGLE	1 << 1		/* reboot to single user only (-s) */
#define	RB_NOSYNC	1 << 2		/* dont sync before reboot (unused) */
#define	RB_HALT		1 << 3		/* don't reboot, just halt */
#define	RB_INITNAME	1 << 4		/* name given for /etc/init (unused) */
#define	RB_DFLTROOT	1 << 5		/* use compiled-in rootdev (-r) */
#define	RB_KDB		1 << 6		/* give control to kernel debugger (-d) */
#define	RB_RDONLY	1 << 7		/* mount root fs read-only (unused) */
#define	RB_DUMP		1 << 8		/* dump kernel memory before reboot (unused) */
#define	RB_MINIROOT	1 << 9		/* mini-root present in memory at boot time (unused) */
#define RB_CONFIG	1 << 10		/* was: invoke user configuration routing (-c) */
#define RB_VERBOSE	1 << 11		/* print all potentially useful info (-v) */
#define	RB_SERIAL	1 << 12		/* user serial port as console (-h) */
#define	RB_CDROM	1 << 13		/* use cdrom as root (-C) */
#define	RB_POWEROFF	1 << 14		/* if you can, turn the power off (unused) */
#define	RB_GDB		1 << 15		/* use GDB remote debugger instead of DDB (-g) */
#define	RB_MUTE		1 << 16		/* Come up with the console muted (-m) */
#define	RB_SELFTEST	1 << 17		/* don't boot to normal operation, do selftest (unused) */
#define RB_RESERVED01	1 << 18		/* reserved for internal use of boot blocks */
#define RB_RESERVED02	1 << 19		/* reserved for internal use of boot blocks */
#define	RB_PAUSE	1 << 20		/* pause after each output line during probe (-p) */
#define RB_QUIET	1 << 21		/* Don't generate output during boot1/boot2 (-q) */
//#define RB_REROOT	1 << 21		/* unmount the rootfs and mount it again (fbsd)*/
//#define RB_POWERCYCLE	1 << 22		/* Power cycle if possible (fbsd) */
//#define RB_UNUSED03	1 << 23
//#define RB_UNUSED04	1 << 24
//#define RB_UNUSED05	1 << 25
#define RB_NOINTR	1 << 26		/* Non Interruptable come up (-n) */
#define RB_DUAL		1 << 27		/* use comconsole and vidconsole (-D) */
//#define RB_PROBE	1 << 28		/* Probe multiple consoles (fbsd) */

/* temp fixup */
#define RB_VIDEO	1 << 29		/* use video console */
#define RB_MULTIPLE	1 << 29		/* use multiple consoles (-D) */
/* end temp */

#define	RB_BOOTINFO	1 << 31		/* have `struct bootinfo *' arg */

#endif
