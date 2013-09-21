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
 * $FreeBSD: src/include/paths.h,v 1.30 2010/02/16 19:39:50 imp Exp $
 */

#ifndef _PATHS_H_
#define	_PATHS_H_

#include <sys/cdefs.h>
#include <sys/paths.h>	/* dev paths */

/* Default search path. */
#define	_PATH_DEFPATH \
	"/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin:/usr/pkg/bin:/usr/pkg/sbin"

/* All standard utilities path. */
#define	_PATH_STDPATH \
	"/usr/bin:/bin:/usr/sbin:/sbin:"

#define _PATH_DEVTAB_PATHS \
	"/usr/local/etc:/etc:/etc/defaults"

#define	_PATH_AUTHCONF	"/etc/auth.conf"
#define	_PATH_BSHELL	"/bin/sh"
#define	_PATH_CONSOLE	__SYS_PATH_CONSOLE
#define	_PATH_CP	"/bin/cp"
#define	_PATH_CSHELL	"/bin/csh"
#define	_PATH_CSMAPPER	"/usr/share/i18n/csmapper"
#define	_PATH_DEFTAPE	__SYS_PATH_DEFTAPE
#define	_PATH_DEVDB	"/var/run/dev.db"
#define	_PATH_DEVNULL	__SYS_PATH_DEVNULL
#define	_PATH_DEVZERO	__SYS_PATH_DEVZERO
#define	_PATH_DRUM	__SYS_PATH_DRUM
#define	_PATH_ESDB	"/usr/share/i18n/esdb"
#define	_PATH_ETC	"/etc"
#define	_PATH_FTPUSERS	"/etc/ftpusers"
#define	_PATH_I18NMODULE	"/usr/lib/i18n"
#define	_PATH_KMEM	__SYS_PATH_KMEM
#define	_PATH_LIBMAP_CONF	"/etc/libmap.conf"
#define	_PATH_LOCALE	"/usr/share/locale"
#define	_PATH_LOGIN	"/usr/bin/login"
#define	_PATH_MAILDIR	"/var/mail"
#define	_PATH_MAN	"/usr/share/man"
#define	_PATH_MEM	__SYS_PATH_MEM
#define	_PATH_NOLOGIN	"/var/run/nologin"
#define	_PATH_RCP	"/bin/rcp"
#define	_PATH_RLOGIN	"/usr/bin/rlogin"
#define	_PATH_RM	"/bin/rm"
#define	_PATH_RSH	"/usr/bin/rsh"
#define	_PATH_SENDMAIL	"/usr/sbin/sendmail"
#define	_PATH_SHELLS	"/etc/shells"
#define	_PATH_TTY	__SYS_PATH_TTY
#define	_PATH_UNIX	"don't use _PATH_UNIX"
#define	_PATH_VI	"/usr/bin/vi"
#define	_PATH_WALL	"/usr/bin/wall"

/* Provide trailing slash, since mostly used for building pathnames. */
#define	_PATH_DEV	__SYS_PATH_DEV
#define	_PATH_TMP	"/tmp/"
#define	_PATH_VARDB	"/var/db/"
#define	_PATH_VAREMPTY	"/var/empty/"
#define	_PATH_VARRUN	"/var/run/"
#define	_PATH_VARTMP	"/var/tmp/"
#define	_PATH_YP	"/var/yp/"
#define	_PATH_UUCPLOCK	"/var/spool/lock/"

/* How to get the correct name of the kernel. */
__BEGIN_DECLS
const char *getbootfile(void);
__END_DECLS

#endif /* !_PATHS_H_ */
