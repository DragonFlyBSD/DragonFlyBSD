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
 * $DragonFly: src/usr.sbin/dntpd/defs.h,v 1.6 2005/04/25 20:50:59 dillon Exp $
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <syslog.h>

#include "ntp.h"
#include "client.h"

#define DNTPD_VERSION	"1.0"

#define logdebug(level, ctl, varargs...)	\
	if (level <= debug_level) _logdebug(level, ctl, ##varargs);

extern int debug_opt;
extern int debug_level;
extern int min_sleep_opt;
extern int nom_sleep_opt;
extern int max_sleep_opt;
extern int log_stderr;

int udp_socket(const char *target, int port);
int udp_ntptimereq(int fd, struct timeval *rtvp, 
		   struct timeval *ltvp, struct timeval *lbtvp);

void l_fixedpt_to_tv(struct l_fixedpt *fixed, struct timeval *tvp);
void tv_subtract_micro(struct timeval *tvp, long usec);
void tv_add_micro(struct timeval *tvp, long usec);
void tv_add_offset(struct timeval *tvp, double offset);
double tv_delta_double(struct timeval *tv1, struct timeval *tv2);
void tv_to_ts(struct timeval *tv, struct timespec *ts);
void ts_to_tv(struct timespec *ts, struct timeval *tv);

void logerr(const char *ctl, ...);
void logerrstr(const char *ctl, ...);
void _logdebug(int level, const char *ctl, ...);

void sysntp_getbasetime(struct timeval *tvp);
double sysntp_correct_offset(double offset);
double sysntp_correct_course_offset(double offset);
void sysntp_correct_freq(double freq);


