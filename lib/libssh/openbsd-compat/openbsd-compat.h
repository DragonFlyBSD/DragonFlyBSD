/*
 * Copyright (c) 1999-2003 Damien Miller.  All rights reserved.
 * Copyright (c) 2003 Ben Lindstrom. All rights reserved.
 * Copyright (c) 2002 Tim Rice.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _OPENBSD_COMPAT_H
#define _OPENBSD_COMPAT_H

#include "includes.h"

#include <sys/types.h>
#include <pwd.h>

#include <sys/socket.h>

#ifndef __DragonFly__
#include <stddef.h>  /* for wchar_t */
#endif

/* OpenBSD function replacements */
#ifndef __DragonFly__
#include "base64.h"
#include "sigact.h"
#include "readpassphrase.h"
#include "vis.h"
#endif
#include "getrrsetbyname.h"
#ifndef __DragonFly__
#include "sha1.h"
#include "sha2.h"
#include "rmd160.h"
#include "md5.h"
#endif
#include "blf.h"

#if !defined(HAVE_REALPATH) || defined(BROKEN_REALPATH)
/*
 * glibc's FORTIFY_SOURCE can redefine this and prevent us picking up the
 * compat version.
 */
# ifdef BROKEN_REALPATH
#  define realpath(x, y) _ssh_compat_realpath(x, y)
# endif

char *realpath(const char *path, char *resolved);
#endif

#ifndef HAVE_FMT_SCALED
#define	FMT_SCALED_STRSIZE	7
int	fmt_scaled(long long number, char *result);
#endif

#ifndef HAVE_SCAN_SCALED
int	scan_scaled(char *, long long *);
#endif

/* Home grown routines */
#include "bsd-misc.h"
#ifndef __DragonFly__
#include "bsd-setres_id.h"
#endif
#include "bsd-signal.h"
#ifndef __DragonFly__
#include "bsd-statvfs.h"
#include "bsd-waitpid.h"
#include "bsd-poll.h"
#endif

/*
 * Some platforms unconditionally undefine va_copy() so we define VA_COPY()
 * instead.  This is known to be the case on at least some configurations of
 * AIX with the xlc compiler.
 */
#ifndef VA_COPY
#  define VA_COPY(dest, src) va_copy(dest, src)
#endif

#ifndef HAVE_BCRYPT_PBKDF
int	bcrypt_pbkdf(const char *, size_t, const u_int8_t *, size_t,
    u_int8_t *, size_t, unsigned int);
#endif

char *xcrypt(const char *password, const char *salt);
char *shadow_pw(struct passwd *pw);

#ifndef __DragonFly__
/* rfc2553 socket API replacements */
#include "fake-rfc2553.h"
#endif

#ifndef __DragonFly__
/* Routines for a single OS platform */
#include "bsd-cygwin_util.h"
#endif

#ifndef __DragonFly__
#include "port-aix.h"
#include "port-irix.h"
#include "port-linux.h"
#include "port-solaris.h"
#endif

#include "port-net.h"
#ifndef __DragonFly__
#include "port-uw.h"
#endif

#endif /* _OPENBSD_COMPAT_H */
