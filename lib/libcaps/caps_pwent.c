/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $DragonFly: src/lib/libcaps/caps_pwent.c,v 1.1 2004/03/07 23:36:44 dillon Exp $
 */

#include <sys/types.h>
#include <pwd.h>
#include <stddef.h>
#include <stdarg.h>
#include "caps_struct.h"

/*
 * Password Entry encoding and decoding
 */
#define CAPS_PW_NAME	1
#define CAPS_PW_PASSWD	2
#define CAPS_PW_UID	3
#define CAPS_PW_GID	4
#define CAPS_PW_CHANGE	5
#define CAPS_PW_CLASS	6
#define CAPS_PW_GECOS	7
#define CAPS_PW_DIR	8
#define CAPS_PW_SHELL	9
#define CAPS_PW_EXPIRE	10

#define STYPE	struct passwd

const struct caps_label caps_passwd_label[] = {
    { offsetof(STYPE, pw_name),	CAPS_IN_STRPTR_T, CAPS_PW_NAME },
    { offsetof(STYPE, pw_passwd),	CAPS_IN_STRPTR_T, CAPS_PW_PASSWD },
    { offsetof(STYPE, pw_uid),	CAPS_IN_UID_T,	  CAPS_PW_UID },
    { offsetof(STYPE, pw_gid),	CAPS_IN_GID_T,	  CAPS_PW_GID },
    { offsetof(STYPE, pw_change),	CAPS_IN_TIME_T,	  CAPS_PW_CHANGE },
    { offsetof(STYPE, pw_class), 	CAPS_IN_STRPTR_T, CAPS_PW_CLASS },
    { offsetof(STYPE, pw_gecos), 	CAPS_IN_STRPTR_T, CAPS_PW_GECOS },
    { offsetof(STYPE, pw_dir), 	CAPS_IN_STRPTR_T, CAPS_PW_DIR },
    { offsetof(STYPE, pw_shell), 	CAPS_IN_STRPTR_T, CAPS_PW_SHELL },
    { offsetof(STYPE, pw_expire),	CAPS_IN_TIME_T,   CAPS_PW_EXPIRE },
    { -1 }
};

#undef STYPE

const struct caps_struct caps_passwd_struct = {
    "passwd", caps_passwd_label 
};

