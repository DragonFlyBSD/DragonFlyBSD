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
 */

#ifndef BASICS_H_
#define BASICS_H_

#include <stddef.h>

struct date {
	int	year;
	int	month;
	int	day;
};

static inline void
date_set(struct date *date, int y, int m, int d)
{
	date->year = y;
	date->month = m;
	date->day = d;
}

struct location {
	double	latitude;	/* degree */
	double	longitude;	/* degree */
	double	elevation;	/* meter */
	double	zone;		/* time offset (in days) from UTC */
};

enum dayofweek {
	SUNDAY = 0,
	MONDAY,
	TUESDAY,
	WEDNESDAY,
	THURSDAY,
	FRIDAY,
	SATURDAY,
};

int	dayofweek_from_fixed(int rd);
int	kday_after(int dow, int rd);
int	kday_nearest(int dow, int rd);
int	kday_onbefore(int dow, int rd);
int	nth_kday(int n, int dow, struct date *date);
int	dayofyear_from_fixed(int rd);

double	julian_centuries(double t);
double	sidereal_from_moment(double t);

double	ephemeris_correction(double t);
double	universal_from_dynamical(double t);
double	dynamical_from_universal(double t);

double	equation_of_time(double t);
double	apparent_from_local(double t, double longitude);
double	local_from_apparent(double t, double longitude);

double	obliquity(double t);
double	declination(double t, double beta, double lambda);
double	right_ascension(double t, double beta, double lambda);

double	refraction(double elevation);

int	format_time(char *buf, size_t size, double t);
int	format_zone(char *buf, size_t size, double zone);
int	format_location(char *buf, size_t size, const struct location *loc);

#endif
