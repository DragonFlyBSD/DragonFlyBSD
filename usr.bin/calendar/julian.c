/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
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
 * Reference:
 * Calendrical Calculations, The Ultimate Edition (4th Edition)
 * by Edward M. Reingold and Nachum Dershowitz
 * 2018, Cambridge University Press
 */

#include <stdbool.h>
#include <stdio.h>

#include "calendar.h"
#include "basics.h"
#include "dates.h"
#include "gregorian.h"
#include "julian.h"
#include "utils.h"

/*
 * Fixed date of the start of the Julian calendar.
 * Ref: Sec.(3.1), Eq.(3.2)
 */
static const int epoch = -1;  /* Gregorian: 0, December, 30 */

/*
 * Return true if $year is a leap year on the Julian calendar,
 * otherwise return false.
 * Ref: Sec.(3.1), Eq.(3.1)
 */
bool
julian_leap_year(int year)
{
	int i = (year > 0) ? 0 : 3;
	return (mod(year, 4) == i);
}

/*
 * Calculate the fixed date (RD) equivalent to the Julian date $date.
 * Ref: Sec.(3.1), Eq.(3.3)
 */
int
fixed_from_julian(const struct date *date)
{
	int y = (date->year >= 0) ? date->year : (date->year + 1);
	int rd = ((epoch - 1) + 365 * (y - 1) +
		  div_floor(y - 1, 4) +
		  div_floor(date->month * 367 - 362, 12));
	/* correct for the assumption that February always has 30 days */
	if (date->month <= 2)
		return rd + date->day;
	else if (julian_leap_year(date->year))
		return rd + date->day - 1;
	else
		return rd + date->day - 2;
}

/*
 * Calculate the Julian date (year, month, day) corresponding to the
 * fixed date $rd.
 * Ref: Sec.(3.1), Eq.(3.4)
 */
void
julian_from_fixed(int rd, struct date *date)
{
	int correction, pdays;

	date->year = div_floor(4 * (rd - epoch) + 1464, 1461);
	if (date->year <= 0)
		date->year--;

	struct date d = { date->year, 3, 1 };
	if (rd < fixed_from_julian(&d))
		correction = 0;
	else if (julian_leap_year(date->year))
		correction = 1;
	else
		correction = 2;

	d.month = 1;
	pdays = rd - fixed_from_julian(&d);
	date->month = div_floor(12 * (pdays + correction) + 373, 367);

	d.month = date->month;
	date->day = rd - fixed_from_julian(&d) + 1;
}

/**************************************************************************/

/*
 * Format the given fixed date $rd to '<month>/<day>' string in $buf.
 * Return the formatted string length.
 */
int
julian_format_date(char *buf, size_t size, int rd)
{
	static const char *month_names[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};
	struct date jdate;

	julian_from_fixed(rd, &jdate);
	return snprintf(buf, size, "%s/%02d",
			month_names[jdate.month - 1], jdate.day);
}

/*
 * Calculate the Julian year corresponding to the fixed date $rd.
 */
static int
julian_year_from_fixed(int rd)
{
	struct date jdate;
	julian_from_fixed(rd, &jdate);
	return jdate.year;
}

/*
 * Find days of the specified Julian year ($year), month ($month) and
 * day ($day).
 * If year $year < 0, then year is ignored.
 */
int
julian_find_days_ymd(int year, int month, int day, struct cal_day **dayp,
		     char **edp __unused)
{
	struct cal_day *dp;
	struct date date;
	int rd, year1, year2;
	int count = 0;

	year1 = julian_year_from_fixed(Options.day_begin);
	year2 = julian_year_from_fixed(Options.day_end);
	for (int y = year1; y <= year2; y++) {
		if (year >= 0 && year != y)
			continue;
		date_set(&date, y, month, day);
		rd = fixed_from_julian(&date);
		if ((dp = find_rd(rd, 0)) != NULL) {
			if (count >= CAL_MAX_REPEAT) {
				warnx("%s: too many repeats", __func__);
				return count;
			}
			dayp[count++] = dp;
		}
	}

	return count;
}

/*
 * Find days of the specified Julian day of month ($dom) of all months.
 */
int
julian_find_days_dom(int dom, struct cal_day **dayp, char **edp __unused)
{
	struct cal_day *dp;
	struct date date;
	int year1, year2;
	int rd_begin, rd_end;
	int count = 0;

	year1 = julian_year_from_fixed(Options.day_begin);
	year2 = julian_year_from_fixed(Options.day_end);
	for (int y = year1; y <= year2; y++) {
		date_set(&date, y, 1, 1);
		rd_begin = fixed_from_julian(&date);
		date.year++;
		rd_end = fixed_from_julian(&date);
		if (rd_end > Options.day_end)
			rd_end = Options.day_end;

		for (int m = 1, rd = rd_begin; rd <= rd_end; m++) {
			date_set(&date, y, m, dom);
			rd = fixed_from_julian(&date);
			if ((dp = find_rd(rd, 0)) != NULL) {
				if (count >= CAL_MAX_REPEAT) {
					warnx("%s: too many repeats",
					      __func__);
					return count;
				}
				dayp[count++] = dp;
			}
		}
	}

	return count;
}

/*
 * Find days of all days of the specified Julian month ($month).
 */
int
julian_find_days_month(int month, struct cal_day **dayp, char **edp __unused)
{
	struct cal_day *dp;
	struct date date;
	int year1, year2;
	int rd_begin, rd_end;
	int count = 0;

	year1 = julian_year_from_fixed(Options.day_begin);
	year2 = julian_year_from_fixed(Options.day_end);
	for (int y = year1; y <= year2; y++) {
		date_set(&date, y, month, 1);
		rd_begin = fixed_from_julian(&date);
		date.month++;
		if (date.month > 12)
			date_set(&date, y+1, 1, 1);
		rd_end = fixed_from_julian(&date);
		if (rd_end > Options.day_end)
			rd_end = Options.day_end;

		for (int rd = rd_begin; rd <= rd_end; rd++) {
			if ((dp = find_rd(rd, 0)) != NULL) {
				if (count >= CAL_MAX_REPEAT) {
					warnx("%s: too many repeats",
					      __func__);
					return count;
				}
				dayp[count++] = dp;
			}
		}
	}

	return count;
}


/*
 * Print the Julian calendar of the given date $rd.
 */
void
show_julian_calendar(int rd)
{
	struct date gdate, jdate;
	bool leap;

	gregorian_from_fixed(rd, &gdate);
	julian_from_fixed(rd, &jdate);
	leap = julian_leap_year(jdate.year);

	printf("Gregorian date: %d-%02d-%02d\n",
	       gdate.year, gdate.month, gdate.day);
	printf("Julian date: %d-%02d-%02d\n",
	       jdate.year, jdate.month, jdate.day);
	printf("Leap year: %s\n", leap ? "yes" : "no");
}
