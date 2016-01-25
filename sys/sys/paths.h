/*
 * Copyright (c) 1989, 1993
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
 *	@(#)paths.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD: src/include/paths.h,v 1.9.6.4 2002/07/19 07:53:41 jmallett Exp $
 * $DragonFly: src/sys/sys/paths.h,v 1.1 2003/08/07 21:17:40 dillon Exp $
 */

#ifndef _SYS_PATHS_H_
#define	_SYS_PATHS_H_

#define	__SYS_PATH_CONSOLE	"/dev/console"
#define	__SYS_PATH_DEFTAPE	"/dev/sa0"
#define	__SYS_PATH_DEVNULL	"/dev/null"
#define	__SYS_PATH_DEVZERO	"/dev/zero"
#define	__SYS_PATH_DRUM		"/dev/drum"
#define	__SYS_PATH_KMEM		"/dev/kmem"
#define	__SYS_PATH_MEM		"/dev/mem"
#define	__SYS_PATH_TTY		"/dev/tty"

/* Provide trailing slash, since mostly used for building pathnames. */
#define	__SYS_PATH_DEV		"/dev/"

#endif
