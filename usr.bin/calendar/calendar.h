/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
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
 * $FreeBSD: head/usr.bin/calendar/calendar.h 326025 2017-11-20 19:49:47Z pfg $
 */

#ifndef CALENDAR_H_
#define CALENDAR_H_

#include <stdbool.h>
#include <stddef.h>

#ifndef __unused
#define __unused	__attribute__((__unused__))
#endif
#ifndef __daed2
#define __dead2		__attribute__((__noreturn__))
#endif

/*
 * Maximum number of repeats of an event.  100 should be enough, which is
 * about the number of weeks in 2 years).  If you need more, then you may
 * be using this program wrong...
 */
#define CAL_MAX_REPEAT	100

#define DPRINTF(...) \
	if (Options.debug) fprintf(stderr, __VA_ARGS__)
#define DPRINTF2(...) \
	if (Options.debug >= 2) fprintf(stderr, __VA_ARGS__)
#define DPRINTF3(...) \
	if (Options.debug >= 3) fprintf(stderr, __VA_ARGS__)


struct location;
struct cal_day;

struct cal_options {
	struct location *location;
	double time;  /* [0, 1) time of now in unit of days */
	int today;  /* R.D. of today to remind events */
	int day_begin;  /* beginning of date range to remind events */
	int day_end;  /* end of date range to remind events */
	int debug;  /* debug log level (higher means more verbose) */
	bool allmode;  /* whether to process calendars for all users */
};

/* IDs of supported calendars */
enum {
	CAL_GREGORIAN,
	CAL_JULIAN,
	CAL_CHINESE,
};

struct calendar {
	int	id;  /* enum ID of the calendar */
	const char *name;
	int	(*format_date)(char *buf, size_t size, int rd);
	int	(*find_days_ymd)(int year, int month, int day,
				 struct cal_day **dayp, char **edp);
	int	(*find_days_dom)(int dom, struct cal_day **dayp, char **edp);
	int	(*find_days_month)(int month, struct cal_day **dayp,
				   char **edp);
	int	(*find_days_mdow)(int month, int dow, int index,
				  struct cal_day **dayp, char **edp);
};

extern struct cal_options Options;
extern struct calendar *Calendar;
extern const char *calendarDirs[];  /* paths to search for calendar files */

bool	set_calendar(const char *name);

#endif
