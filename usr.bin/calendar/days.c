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
 */

#include <assert.h>
#include <err.h>
#include <math.h>
#include <stddef.h>

#include "calendar.h"
#include "basics.h"
#include "chinese.h"
#include "dates.h"
#include "days.h"
#include "ecclesiastical.h"
#include "gregorian.h"
#include "moon.h"
#include "nnames.h"
#include "parsedata.h"
#include "sun.h"
#include "utils.h"

static int	find_days_yearly(int sday_id, int offset,
				 struct cal_day **dayp, char **edp);
static int	find_days_moon(int sday_id, int offset,
			       struct cal_day **dayp, char **edp);

static int	find_days_easter(int, struct cal_day **, char **);
static int	find_days_paskha(int, struct cal_day **, char **);
static int	find_days_advent(int, struct cal_day **, char **);
static int	find_days_cny(int, struct cal_day **, char **);
static int	find_days_cqingming(int, struct cal_day **, char **);
static int	find_days_cjieqi(int, struct cal_day **, char **);
static int	find_days_marequinox(int, struct cal_day **, char **);
static int	find_days_sepequinox(int, struct cal_day **, char **);
static int	find_days_junsolstice(int, struct cal_day **, char **);
static int	find_days_decsolstice(int, struct cal_day **, char **);
static int	find_days_newmoon(int, struct cal_day **, char **);
static int	find_days_fullmoon(int, struct cal_day **, char **);

#define SPECIALDAY_INIT0 \
	{ SD_NONE, NULL, 0, NULL, 0, NULL }
#define SPECIALDAY_INIT(id, name, func) \
	{ (id), name, sizeof(name)-1, NULL, 0, func }
struct specialday specialdays[] = {
	SPECIALDAY_INIT(SD_EASTER, "Easter", &find_days_easter),
	SPECIALDAY_INIT(SD_PASKHA, "Paskha", &find_days_paskha),
	SPECIALDAY_INIT(SD_ADVENT, "Advent", &find_days_advent),
	SPECIALDAY_INIT(SD_CNY, "ChineseNewYear", &find_days_cny),
	SPECIALDAY_INIT(SD_CQINGMING, "ChineseQingming", &find_days_cqingming),
	SPECIALDAY_INIT(SD_CJIEQI, "ChineseJieqi", &find_days_cjieqi),
	SPECIALDAY_INIT(SD_MAREQUINOX, "MarEquinox", &find_days_marequinox),
	SPECIALDAY_INIT(SD_SEPEQUINOX, "SepEquinox", &find_days_sepequinox),
	SPECIALDAY_INIT(SD_JUNSOLSTICE, "JunSolstice", &find_days_junsolstice),
	SPECIALDAY_INIT(SD_DECSOLSTICE, "DecSolstice", &find_days_decsolstice),
	SPECIALDAY_INIT(SD_NEWMOON, "NewMoon", &find_days_newmoon),
	SPECIALDAY_INIT(SD_FULLMOON, "FullMoon", &find_days_fullmoon),
	SPECIALDAY_INIT0,
};


static int
find_days_easter(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_EASTER, offset, dayp, edp);
}

static int
find_days_paskha(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_PASKHA, offset, dayp, edp);
}

static int
find_days_advent(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_ADVENT, offset, dayp, edp);
}

static int
find_days_cny(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_CNY, offset, dayp, edp);
}

static int
find_days_cqingming(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_CQINGMING, offset, dayp, edp);
}

static int
find_days_marequinox(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_MAREQUINOX, offset, dayp, edp);
}

static int
find_days_sepequinox(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_SEPEQUINOX, offset, dayp, edp);
}

static int
find_days_junsolstice(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_JUNSOLSTICE, offset, dayp, edp);
}

static int
find_days_decsolstice(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_yearly(SD_DECSOLSTICE, offset, dayp, edp);
}

/*
 * Find days of the yearly special day specified by $sday_id.
 */
static int
find_days_yearly(int sday_id, int offset, struct cal_day **dayp, char **edp)
{
	struct cal_day *dp;
	struct date date;
	double t, longitude;
	char buf[32];
	int rd, approx, month;
	int year1, year2;
	int count = 0;

	year1 = gregorian_year_from_fixed(Options.day_begin);
	year2 = gregorian_year_from_fixed(Options.day_end);
	for (int y = year1; y <= year2; y++) {
		t = NAN;

		switch (sday_id) {
		case SD_EASTER:
			rd = easter(y);
			break;
		case SD_PASKHA:
			rd = orthodox_easter(y);
			break;
		case SD_ADVENT:
			rd = advent(y);
			break;
		case SD_CNY:
			rd = chinese_new_year(y);
			break;
		case SD_CQINGMING:
			rd = chinese_qingming(y);
			break;
		case SD_MAREQUINOX:
		case SD_JUNSOLSTICE:
		case SD_SEPEQUINOX:
		case SD_DECSOLSTICE:
			if (sday_id == SD_MAREQUINOX) {
				month = 3;
				longitude = 0.0;
			} else if (sday_id == SD_JUNSOLSTICE) {
				month = 6;
				longitude = 90.0;
			} else if (sday_id == SD_SEPEQUINOX) {
				month = 9;
				longitude = 180.0;
			} else {
				month = 12;
				longitude = 270.0;
			}
			date_set(&date, y, month, 1);
			approx = fixed_from_gregorian(&date);
			t = solar_longitude_atafter(longitude, approx);
			t += Options.location->zone;  /* to standard time */
			rd = floor(t);
			break;
		default:
			errx(1, "%s: unknown special day: %d",
			     __func__, sday_id);
		}

		if ((dp = find_rd(rd, offset)) != NULL) {
			if (count >= CAL_MAX_REPEAT) {
				warnx("%s: too many repeats", __func__);
				return count;
			}
			if (!isnan(t)) {
				format_time(buf, sizeof(buf), t);
				edp[count] = xstrdup(buf);
			}
			dayp[count++] = dp;
		}
	}

	return count;
}

/*
 * Find days of the 24 Chinese Jiéqì (节气)
 */
static int
find_days_cjieqi(int offset, struct cal_day **dayp, char **edp)
{
	const struct chinese_jieqi *jq;
	struct cal_day *dp;
	struct date date;
	char buf[32];
	int year1, year2;
	int rd, rd_begin, rd_end;
	int count = 0;

	year1 = gregorian_year_from_fixed(Options.day_begin);
	year2 = gregorian_year_from_fixed(Options.day_end);
	for (int y = year1; y <= year2; y++) {
		date_set(&date, y, 1, 1);
		rd_begin = fixed_from_gregorian(&date);
		date.year++;
		rd_end = fixed_from_gregorian(&date);
		if (rd_end > Options.day_end)
			rd_end = Options.day_end;

		for (rd = rd_begin; rd <= rd_end; rd += 14) {
			rd = chinese_jieqi_onafter(rd, C_JIEQI_ALL, &jq);
			if (rd > rd_end)
				break;

			if ((dp = find_rd(rd, offset)) != NULL) {
				if (count >= CAL_MAX_REPEAT) {
					warnx("%s: too many repeats",
					      __func__);
					return count;
				}
				snprintf(buf, sizeof(buf), "%s, %s",
					 jq->name, jq->zhname);
				edp[count] = xstrdup(buf);
				dayp[count++] = dp;
			}
		}
	}

	return count;
}

static int
find_days_newmoon(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_moon(SD_NEWMOON, offset, dayp, edp);
}

static int
find_days_fullmoon(int offset, struct cal_day **dayp, char **edp)
{
	return find_days_moon(SD_FULLMOON, offset, dayp, edp);
}

/*
 * Find days of the moon events specified by $sday_id.
 */
static int
find_days_moon(int sday_id, int offset, struct cal_day **dayp, char **edp)
{
	struct cal_day *dp;
	struct date date;
	double t, t_begin, t_end;
	char buf[32];
	int year1, year2;
	int count = 0;

	year1 = gregorian_year_from_fixed(Options.day_begin);
	year2 = gregorian_year_from_fixed(Options.day_end);
	for (int y = year1; y <= year2; y++) {
		date_set(&date, y, 1, 1);
		t_begin = fixed_from_gregorian(&date) - Options.location->zone;
		date.year++;
		t_end = fixed_from_gregorian(&date) - Options.location->zone;
		if (t_end > Options.day_end + 1 - Options.location->zone)
			t_end = Options.day_end + 1 - Options.location->zone;
				/* NOTE: '+1' to include the ending day */

		for (t = t_begin; t <= t_end; ) {
			switch (sday_id) {
			case SD_NEWMOON:
				t = new_moon_atafter(t);
				break;
			case SD_FULLMOON:
				t = lunar_phase_atafter(180, t);
				break;
			default:
				errx(1, "%s: unknown moon event: %d",
				     __func__, sday_id);
			}

			if (t > t_end)
				break;

			t += Options.location->zone;  /* to standard time */
			if ((dp = find_rd(floor(t), offset)) != NULL) {
				if (count >= CAL_MAX_REPEAT) {
					warnx("%s: too many repeats",
					      __func__);
					return count;
				}
				format_time(buf, sizeof(buf), t);
				edp[count] = xstrdup(buf);
				dayp[count++] = dp;
			}
		}
	}

	return count;
}

/**************************************************************************/

/*
 * Find days of the specified year ($year), month ($month) and day ($day).
 * If year $year < 0, then year is ignored.
 */
int
find_days_ymd(int year, int month, int day,
	      struct cal_day **dayp, char **edp __unused)
{
	struct cal_day *dp = NULL;
	int count = 0;

	while ((dp = loop_dates(dp)) != NULL) {
		if (year >= 0 && year != dp->year)
			continue;
		if ((dp->month == month && dp->day == day) ||
		    /* day of zero means the last day of previous month */
		    (day == 0 && dp->last_dom && month == dp->month % 12 + 1)) {
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
 * Find days of the specified day of month ($dom) of all months.
 */
int
find_days_dom(int dom, struct cal_day **dayp, char **edp __unused)
{
	struct cal_day *dp = NULL;
	int count = 0;

	while ((dp = loop_dates(dp)) != NULL) {
		if (dp->day == dom ||
		    /* day of zero means the last day of previous month */
		    (dom == 0 && dp->last_dom)) {
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
 * Find days of all days of the specified month ($month).
 */
int
find_days_month(int month, struct cal_day **dayp, char **edp __unused)
{
	struct cal_day *dp = NULL;
	int count = 0;

	while ((dp = loop_dates(dp)) != NULL) {
		if (dp->month == month) {
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
 * If $index == 0, find days of every day-of-week ($dow) of the specified month
 * ($month).  Otherwise, find days of the indexed day-of-week of the month.
 * If month $month < 0, then find days in every month.
 */
int
find_days_mdow(int month, int dow, int index,
	       struct cal_day **dayp, char **edp __unused)
{
	struct cal_day *dp = NULL;
	int count = 0;

	while ((dp = loop_dates(dp)) != NULL) {
		if (month >= 0 && month != dp->month)
			continue;
		if (dp->dow[0] == dow) {
			if (index != 0 &&
			    (index != dp->dow[1] && index != dp->dow[2])) {
				/* Not the indexed day-of-week of month */
				continue;
			}
			if (count >= CAL_MAX_REPEAT) {
				warnx("%s: too many repeats", __func__);
				return count;
			}
			dayp[count++] = dp;
		}
	}

	return count;
}
