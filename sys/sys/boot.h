/*-
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
 * $FreeBSD: head/sys/sys/boot.h 263005 2014-03-11 10:13:06Z royger $
 */

#ifndef _SYS_BOOT_H_
#define _SYS_BOOT_H_

/*
 * Return a 'boothowto' value corresponding to the kernel arguments in
 * (kargs) and any relevant environment variables.
 */
static struct
{
	const char	*ev;
        const char 	flag;
	int		mask;
} howto_names[] = {
        {"boot_askname", 	'a',	RB_ASKNAME},
        {"boot_single",		's',	RB_SINGLE},
/*
        {NULL,			'-',	RB_NOSYNC},
        {NULL,			'-',	RB_HALT},
        {NULL,			'-',	RB_INITNAME},
*/
        {"boot_dfltroot",	'r',	RB_DFLTROOT},
        {"boot_ddb",		'd',	RB_KDB},
/*
        {NULL, 			'-',	RB_RDONLY},
        {NULL, 			'-',	RB_DUMP},
        {NULL, 			'-',	RB_MINIROOT},
        {NULL,			'c',	RB_CONFIG},
*/
        {"boot_verbose",	'v',	RB_VERBOSE},
        {"boot_serial",		'h',	RB_SERIAL},
        {"boot_cdrom",		'C',	RB_CDROM},
/*
        {NULL,			'-',	RB_POWEROFF},
*/
        {"boot_gdb",		'g',	RB_GDB},
        {"boot_mute",		'm',	RB_MUTE},
/*
        {NULL,			'-',	RB_SELFTEST},
        {NULL,			'-',	RB_RESERVED01},
        {NULL,			'-',	RB_RESERVED02},
*/
        {"boot_pause",		'p',	RB_PAUSE},
/*
        {NULL,			'q',	RB_QUIET},
        {NULL,			'n',	RB_NOINTR},
*/
        {"boot_multicons",	'D',	RB_DUAL},
        {"boot_vidcons",	'-',	RB_VIDEO},
        {"boot_multicons", 	'M',	RB_MULTIPLE},
/*
        {NULL,			'-',	RB_BOOTINFO},
*/
	{NULL,			'-',	0}

};

#endif /* !_SYS_BOOT_H_ */
