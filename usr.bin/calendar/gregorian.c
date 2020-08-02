/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2019-2020 The DragonFly Project.  All rights reserved.
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

/*
 * Rata Die (R.D.), i.e., fixed date, is 1 at midnight (00:00) local time
 * on January 1, AD 1 in the proleptic Gregorian calendar.
 */

#include <stdbool.h>

#include "basics.h"
#include "gregorian.h"
#include "utils.h"

/*
 * Fixed date of the start of the (proleptic) Gregorian calendar.
 * Ref: Sec.(2.2), Eq.(2.3)
 */
static const int epoch = 1;

/*
 * Return true if $year is a leap year on the Gregorian calendar,
 * otherwise return false.
 * Ref: Sec.(2.2), Eq.(2.16)
 */
bool
gregorian_leap_year(int year)
{
	int r4 = mod(year, 4);
	int r400 = mod(year, 400);
	return (r4 == 0 && (r400 != 100 && r400 != 200 && r400 != 300));
}

/*
 * Calculate the fixed date (RD) equivalent to the Gregorian date $date.
 * Ref: Sec.(2.2), Eq.(2.17)
 */
int
fixed_from_gregorian(const struct date *date)
{
	int rd = ((epoch - 1) + 365 * (date->year - 1) +
		  div_floor(date->year - 1, 4) -
		  div_floor(date->year - 1, 100) +
		  div_floor(date->year - 1, 400) +
		  div_floor(date->month * 367 - 362, 12));
	/* correct for the assumption that February always has 30 days */
	if (date->month <= 2)
		return rd + date->day;
	else if (gregorian_leap_year(date->year))
		return rd + date->day - 1;
	else
		return rd + date->day - 2;
}

/*
 * Calculate the fixed date of January 1 in year $year.
 * Ref: Sec.(2.2), Eq.(2.18)
 */
int
gregorian_new_year(int year)
{
	struct date date = { year, 1, 1 };
	return fixed_from_gregorian(&date);
}

/*
 * Calculate the Gregorian year corresponding to the fixed date $rd.
 * Ref: Sec.(2.2), Eq.(2.21)
 */
int
gregorian_year_from_fixed(int rd)
{
	int d0 = rd - epoch;
	int d1 = mod(d0, 146097);
	int d2 = mod(d1, 36524);
	int d3 = mod(d2, 1461);

	int n400 = div_floor(d0, 146097);
	int n100 = div_floor(d1, 36524);
	int n4 = div_floor(d2, 1461);
	int n1 = div_floor(d3, 365);

	int year = 400 * n400 + 100 * n100 + 4 * n4 + n1;
	if (n100 == 4 || n1 == 4)
		return year;
	else
		return year + 1;
}

/*
 * Number of days from Gregorian date $date1 until $date2.
 * Ref: Sec.(2.2), Eq.(2.24)
 */
int
gregorian_date_difference(const struct date *date1,
			  const struct date *date2)
{
	return fixed_from_gregorian(date2) - fixed_from_gregorian(date1);
}

/*
 * Calculate the Gregorian date (year, month, day) corresponding to the
 * fixed date $rd.
 * Ref: Sec.(2.2), Eq.(2.23)
 */
void
gregorian_from_fixed(int rd, struct date *date)
{
	int correction, pdays;

	date->year = gregorian_year_from_fixed(rd);

	struct date d = { date->year, 3, 1 };
	if (rd < fixed_from_gregorian(&d))
		correction = 0;
	else if (gregorian_leap_year(date->year))
		correction = 1;
	else
		correction = 2;

	d.month = 1;
	pdays = rd - fixed_from_gregorian(&d);
	date->month = div_floor(12 * (pdays + correction) + 373, 367);

	d.month = date->month;
	date->day = rd - fixed_from_gregorian(&d) + 1;
}
