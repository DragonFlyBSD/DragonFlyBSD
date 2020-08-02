/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1992-2009 Edwin Groothuis <edwin@FreeBSD.org>.
 * All rights reserved.
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
 * $FreeBSD: head/usr.bin/calendar/dates.c 326276 2017-11-27 15:37:16Z pfg $
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "calendar.h"
#include "basics.h"
#include "dates.h"
#include "gregorian.h"
#include "io.h"
#include "utils.h"


struct event {
	bool		 variable;  /* Whether a variable event ? */
	char		 date[32];  /* Date in Gregorian calendar */
	char		 date_user[64];  /* Date in user-chosen calendar */
	struct cal_desc *description;  /* Event description */
	char		*extra;  /* Extra data of the event */
	struct event	*next;
};

static struct cal_day *cal_days = NULL;


void
generate_dates(void)
{
	struct cal_day *dp;
	struct date date;
	int daycount, dow, year, month, day;
	int rd_month1, rd_nextmonth, rd_nextyear;

	daycount = Options.day_end - Options.day_begin + 1;
	cal_days = xcalloc((size_t)daycount, sizeof(struct cal_day));

	dow = dayofweek_from_fixed(Options.day_begin);
	gregorian_from_fixed(Options.day_begin, &date);
	year = date.year;
	month = date.month;
	day = date.day;

	date.day = 1;
	rd_month1 = fixed_from_gregorian(&date);
	if (date.month == 12) {
		date_set(&date, date.year+1, 1, 1);
		rd_nextmonth = fixed_from_gregorian(&date);
		rd_nextyear = rd_nextmonth;
	} else {
		date.month++;
		rd_nextmonth = fixed_from_gregorian(&date);
		date_set(&date, date.year+1, 1, 1);
		rd_nextyear = fixed_from_gregorian(&date);
	}

	for (int i = 0; i < daycount; i++) {
		dp = &cal_days[i];
		dp->rd = Options.day_begin + i;

		if (dp->rd == rd_nextmonth) {
			month++;
			day = 1;
			rd_month1 = rd_nextmonth;
			if (dp->rd == rd_nextyear) {
				year++;
				month = 1;
			}

			date_set(&date, year, month, day);
			if (date.month == 12) {
				date_set(&date, date.year+1, 1, 1);
				rd_nextmonth = fixed_from_gregorian(&date);
				rd_nextyear = rd_nextmonth;
			} else {
				date.month++;
				rd_nextmonth = fixed_from_gregorian(&date);
				date_set(&date, date.year+1, 1, 1);
				rd_nextyear = fixed_from_gregorian(&date);
			}
		}

		dp->year = year;
		dp->month = month;
		dp->day = dp->rd - rd_month1 + 1;
		dp->dow[0] = (dow + i) % 7;
		dp->dow[1] = (dp->rd - rd_month1) / 7 + 1;
		dp->dow[2] = -((rd_nextmonth - dp->rd - 1) / 7 + 1);
		dp->last_dom = (dp->rd == rd_nextmonth - 1);

		DPRINTF("%s: [%d] rd:%d, date:%d-%02d-%02d, dow:[%d,%d,%d]\n",
			__func__, i, dp->rd, dp->year, dp->month,
			dp->day, dp->dow[0], dp->dow[1], dp->dow[2]);
	}
}

void
free_dates(void)
{
	struct event *e;
	struct cal_day *dp = NULL;

	while ((dp = loop_dates(dp)) != NULL) {
		while ((e = dp->events) != NULL) {
			dp->events = e->next;
			free(e->extra);
			free(e);
		}
	}
	free(cal_days);
}

struct cal_day *
loop_dates(struct cal_day *dp)
{
	int daycount = Options.day_end - Options.day_begin + 1;

	if (dp == NULL)
		dp = &cal_days[0];
	else
		dp++;

	if (dp < &cal_days[0] || dp > &cal_days[daycount-1])
		return NULL;
	else
		return dp;
}


struct cal_day *
find_rd(int rd, int offset)
{
	rd += offset;
	if (rd < Options.day_begin || rd > Options.day_end)
		return NULL;

	return &cal_days[rd - Options.day_begin];
}


struct event *
event_add(struct cal_day *dp, bool day_first, bool variable,
	  struct cal_desc *desc, char *extra)
{
	struct event *e;
	struct date gdate;
	struct tm tm = { 0 };

	e = xcalloc(1, sizeof(*e));

	gregorian_from_fixed(dp->rd, &gdate);
	tm.tm_year = gdate.year - 1900;
	tm.tm_mon = gdate.month - 1;
	tm.tm_mday = gdate.day;
	strftime(e->date, sizeof(e->date),
		 (day_first ? "%e %b" : "%b %e"), &tm);
	if (Calendar->format_date != NULL) {
		(Calendar->format_date)(e->date_user, sizeof(e->date_user),
					dp->rd);
	}

	e->variable = variable;
	e->description = desc;
	if (extra != NULL && extra[0] != '\0')
		e->extra = extra;

	e->next = dp->events;
	dp->events = e;

	return (e);
}

void
event_print_all(FILE *fp)
{
	struct event *e;
	struct cal_day *dp = NULL;
	struct cal_desc *desc;
	struct cal_line *line;

	while ((dp = loop_dates(dp)) != NULL) {
		for (e = dp->events; e != NULL; e = e->next) {
			fprintf(fp, "%s%c\t", e->date, e->variable ? '*' : ' ');
			if (e->date_user[0] != '\0')
				fprintf(fp, "[%s] ", e->date_user);

			desc = e->description;
			for (line = desc->firstline; line; line = line->next) {
				fprintf(fp, "%s%s%s",
					(line == desc->firstline) ? "" : "\t\t",
					line->str,
					(line == desc->lastline) ? "" : "\n");
			}
			if (e->extra)
				fprintf(fp, " (%s)", e->extra);

			fputc('\n', fp);
			fflush(fp);
		}
	}
}
