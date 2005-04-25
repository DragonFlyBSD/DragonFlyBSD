/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/usr.sbin/dntpd/log.c,v 1.2 2005/04/25 17:42:49 dillon Exp $
 */

#include "defs.h"

static void vlogline(int level, int newline, const char *ctl, va_list va);

void
logerr(const char *ctl, ...)
{
    int saved_errno;
    va_list va;

    saved_errno = errno;
    va_start(va, ctl);
    vlogline(0, 0, ctl, va);
    vlogline(0, 1, ": %s", strerror(saved_errno));
    va_end(va);

}

void
logerrstr(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vlogline(0, 1, ctl, va);
    va_end(va);
}

/*
 * logdebug() does not add the '\n', allowing multiple calls to generate
 * a debugging log line.  The buffer is accumulated until a newline is
 * detected, then syslogged.
 */
void
_logdebug(int level, const char *ctl, ...)
{
    va_list va;

    if (level <= debug_level) {
	va_start(va, ctl);
	vlogline(level, 0, ctl, va);
	va_end(va);
    }
}

static void
vlogline(int level, int newline, const char *ctl, va_list va)
{
    static char line_build[1024];
    static int line_index;
    int priority;

    /*
     * Output to stderr directly but build the log line for syslog.
     */
    if (level <= debug_level) {
	if (debug_opt) {
	    vfprintf(stderr, ctl, va);
	    fflush(stderr);
	} else {
	    vsnprintf(line_build + line_index, sizeof(line_build) - line_index, 
		    ctl, va);
	    line_index += strlen(line_build + line_index);
	    if (line_index && line_build[line_index-1] == '\n') {
		newline = 1;
		--line_index;
	    }
	    if (newline) {
		switch(level) {
		case 0:
		case 1:
		case 2:
		    priority = LOG_NOTICE;
		    break;
		case 3:
		    priority = LOG_INFO;
		    break;
		default:
		    priority = LOG_DEBUG;
		    break;
		}
		syslog(priority, "%*.*s", line_index, line_index, line_build);
		line_index = 0;
	    }
	}
    }
}

