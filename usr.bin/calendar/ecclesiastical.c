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

#include <math.h>
#include <stdbool.h>

#include "calendar.h"
#include "basics.h"
#include "ecclesiastical.h"
#include "gregorian.h"
#include "julian.h"
#include "utils.h"

/*
 * Calculate the fixed date (RD) of Orthodox Easter in Gregorian year $g_year.
 * Ref: Sec.(9.1), Eq.(9.1)
 */
int
orthodox_easter(int g_year)
{
	/* Age of moon for April 5 */
	int shifted_epact = mod(14 + 11 * mod(g_year, 19), 30);

	/* Day after full moon on/after March 21 */
	int j_year = (g_year > 0) ? g_year : (g_year - 1);
	struct date april19 = { j_year, 4, 19 };
	int paschal_moon = fixed_from_julian(&april19) - shifted_epact;

	return kday_after(SUNDAY, paschal_moon);
}

/*
 * Calculate the fixed date (RD) of Gregorian Easter (used by Catholic and
 * Protestant churches) in Gregorian year $g_year.
 * Ref: Sec.(9.2), Eq.(9.3)
 */
int
easter(int g_year)
{
	int century = div_floor(g_year, 100) + 1;
	int y_mod19 = mod(g_year, 19);
	int n = (14 + 11 * y_mod19 - (int)floor(century * 0.75) +
		 div_floor(5 + 8 * century, 25));
	int shifted_epact = mod(n, 30);
	if (shifted_epact == 0 || (shifted_epact == 1 && y_mod19 > 10))
		shifted_epact++;

	struct date april19 = { g_year, 4, 19 };
	int paschal_moon = fixed_from_gregorian(&april19) - shifted_epact;

	return kday_after(SUNDAY, paschal_moon);
}

/*
 * Calculate the fixed date (RD) of Advent Sunday (the 4th Sunday
 * before Christmas, equivalent to the Sunday closest to November 30)
 * in Gregorian year $g_year.
 * Ref: Sec.(2.5), Eq.(2.42)
 */
int
advent(int g_year)
{
	struct date date = { g_year, 11, 30 };
	int rd;

	if (Calendar->id == CAL_JULIAN)
		rd = fixed_from_julian(&date);
	else
		rd = fixed_from_gregorian(&date);

	return kday_nearest(SUNDAY, rd);
}
