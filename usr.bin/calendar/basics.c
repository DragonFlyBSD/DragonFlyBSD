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

#include <err.h>
#include <math.h>
#include <stdio.h>

#include "basics.h"
#include "gregorian.h"
#include "utils.h"

/*
 * Determine the day of week of the fixed date $rd.
 * Ref: Sec.(1.12), Eq.(1.60)
 */
int
dayofweek_from_fixed(int rd)
{
	/* NOTE: R.D. 1 is Monday */
	return mod(rd, 7);
}

/*
 * Calculate the fixed date of the day-of-week $dow on or before
 * the fixed date $rd.
 * Ref: Sec.(1.12), Eq.(1.62)
 */
int
kday_onbefore(int dow, int rd)
{
	return rd - dayofweek_from_fixed(rd - dow);
}

/*
 * Calculate the fixed date of the day-of-week $dow after
 * the fixed date $rd.
 * Ref: Sec.(1.12), Eq.(1.68)
 */
int
kday_after(int dow, int rd)
{
	return kday_onbefore(dow, rd + 7);
}

/*
 * Calculate the fixed date of the day-of-week $dow nearest to
 * the fixed date $rd.
 * Ref: Sec.(1.12), Eq.(1.66)
 */
int
kday_nearest(int dow, int rd)
{
	return kday_onbefore(dow, rd + 3);
}

/*
 * Calculate the $n-th occurrence of a given day-of-week $dow counting
 * from either after (if $n > 0) or before (if $n < 0) the given $date.
 * Ref: Sec.(2.5), Eq.(2.33)
 */
int
nth_kday(int n, int dow, struct date *date)
{
	if (n == 0)
		errx(1, "%s: invalid n = 0!", __func__);

	int rd = fixed_from_gregorian(date);
	int kday;
	if (n > 0)
		kday = kday_onbefore(dow, rd - 1);  /* Sec.(1.12), Eq.(1.67) */
	else
		kday = kday_onbefore(dow, rd + 7);  /* Sec.(1.12), Eq.(1.68) */
	return 7 * n + kday;
}

/*
 * Calculate the ephemeris correction (fraction of day) required for
 * converting between Universal Time and Dynamical Time at moment $t.
 * Ref: Sec.(14.2), Eq.(14.15)
 */
double
ephemeris_correction(double t)
{
	int rd = (int)floor(t);
	int year = gregorian_year_from_fixed(rd);
	int y2000 = year - 2000;
	int y1700 = year - 1700;
	int y1600 = year - 1600;
	double y1820 = (year - 1820) / 100.0;
	double y1000 = (year - 1000) / 100.0;
	double y0    = year / 100.0;

	double coef2006[] = { 62.92, 0.32217, 0.005589 };
	double coef1987[] = { 63.86, 0.3345, -0.060374, 0.0017275,
			      0.000651814, 0.00002373599 };
	double coef1900[] = { -0.00002, 0.000297, 0.025184, -0.181133,
			      0.553040, -0.861938, 0.677066, -0.212591 };
	double coef1800[] = { -0.000009, 0.003844, 0.083563, 0.865736,
			      4.867575, 15.845535, 31.332267, 38.291999,
			      28.316289, 11.636204, 2.043794 };
	double coef1700[] = { 8.118780842, -0.005092142,
			      0.003336121, -0.0000266484 };
	double coef1600[] = { 120.0, -0.9808, -0.01532, 0.000140272128 };
	double coef500[]  = { 1574.2, -556.01, 71.23472, 0.319781,
			      -0.8503463, -0.005050998, 0.0083572073 };
	double coef0[]    = { 10583.6, -1014.41, 33.78311, -5.952053,
			      -0.1798452, 0.022174192, 0.0090316521 };

	double c_other = (-20.0 + 32.0 * y1820 * y1820) / 86400.0;
	struct date date1 = { 1900, 1, 1 };
	struct date date2 = { year, 7, 1 };
	double c = gregorian_date_difference(&date1, &date2) / 36525.0;

	if (year > 2150) {
		return c_other;
	} else if (year >= 2051) {
		return c_other + 0.5628 * (2150 - year) / 86400.0;
	} else if (year >= 2006) {
		return poly(y2000, coef2006, nitems(coef2006)) / 86400.0;
	} else if (year >= 1987) {
		return poly(y2000, coef1987, nitems(coef1987)) / 86400.0;
	} else if (year >= 1900) {
		return poly(c, coef1900, nitems(coef1900));
	} else if (year >= 1800) {
		return poly(c, coef1800, nitems(coef1800));
	} else if (year >= 1700) {
		return poly(y1700, coef1700, nitems(coef1700)) / 86400.0;
	} else if (year >= 1600) {
		return poly(y1600, coef1600, nitems(coef1600)) / 86400.0;
	} else if (year >= 500) {
		return poly(y1000, coef500, nitems(coef500)) / 86400.0;
	} else if (year > -500) {
		return poly(y0, coef0, nitems(coef0)) / 86400.0;
	} else {
		return c_other;
	}
}

/*
 * Convert from Universal Time (UT) to Dynamical Time (DT).
 * Ref: Sec.(14.2), Eq.(14.16)
 */
double
dynamical_from_universal(double t)
{
	return t + ephemeris_correction(t);
}

/*
 * Convert from dynamical time to universal time.
 * Ref: Sec.(14.2), Eq.(14.17)
 */
double
universal_from_dynamical(double t)
{
	return t - ephemeris_correction(t);
}

/*
 * Calculate the number (and fraction) of uniform-length centuries
 * (36525 days) since noon on January 1, 2000 (Gregorian) at moment $t.
 * Ref: Sec.(14.2), Eq.(14.18)
 */
double
julian_centuries(double t)
{
	double dt = dynamical_from_universal(t);
	double j2000 = 0.5 + gregorian_new_year(2000);
	return (dt - j2000) / 36525.0;
}

/*
 * Calculate the mean sidereal time of day expressed as hour angle
 * at moment $t.
 * Ref: Sec.(14.3), Eq.(14.27)
 */
double
sidereal_from_moment(double t)
{
	double j2000 = 0.5 + gregorian_new_year(2000);
	int century_days = 36525;
	double c = (t - j2000) / century_days;
	double coef[] = { 280.46061837, 360.98564736629 * century_days,
			  0.000387933, -1.0 / 38710000.0 };
	return mod_f(poly(c, coef, nitems(coef)), 360);
}

/*
 * Ref: Sec.(14.3), Eq.(14.20)
 */
double
equation_of_time(double t)
{
	double c = julian_centuries(t);
	double epsilon = obliquity(t);
	double y = pow(tan_deg(epsilon/2), 2);

	double lambda = 280.46645 + 36000.76983 * c + 0.0003032 * c*c;
	double anomaly = (357.52910 + 35999.05030 * c -
			  0.0001559 * c*c - 0.00000048 * c*c*c);
	double eccentricity = (0.016708617 - 0.000042037 * c -
			       0.0000001236 * c*c);

	double equation = (y * sin_deg(2*lambda) -
			   2 * eccentricity * sin_deg(anomaly) +
			   4 * eccentricity * y * sin_deg(anomaly) * cos_deg(2*lambda) -
			   1.25 * eccentricity*eccentricity * sin_deg(2*anomaly) -
			   0.5 * y*y * sin_deg(4*lambda)) / (2*M_PI);

	double vmax = 0.5;  /* i.e., 12 hours */
	if (fabs(equation) < vmax)
		return equation;
	else
		return (equation > 0) ? vmax : -vmax;
}

/*
 * Calculate the sundial time from local time $t at location of
 * longitude $longitude.
 * Ref: Sec.(14.3), Eq.(14.21)
 */
double
apparent_from_local(double t, double longitude)
{
	double ut = t - longitude / 360.0;  /* local time -> universal time */
	return t + equation_of_time(ut);
}

/*
 * Calculate the local time from sundial time $t at location of
 * longitude $longitude.
 * Ref: Sec.(14.3), Eq.(14.22)
 */
double
local_from_apparent(double t, double longitude)
{
	double ut = t - longitude / 360.0;  /* local time -> universal time */
	return t - equation_of_time(ut);
}

/*
 * Calculate the obliquity of ecliptic at moment $t
 * Ref: Sec.(14.4), Eq.(14.28)
 */
double
obliquity(double t)
{
	double c = julian_centuries(t);
	double coef[] = {
		0.0,
		angle2deg(0, 0, -46.8150),
		angle2deg(0, 0, -0.00059),
		angle2deg(0, 0, 0.001813),
	};
	double correction = poly(c, coef, nitems(coef));
	return angle2deg(23, 26, 21.448) + correction;
}

/*
 * Calculate the declination at moment $t of an object at latitude $beta
 * and longitude $lambda.
 * Ref: Sec.(14.4), Eq.(14.29)
 */
double
declination(double t, double beta, double lambda)
{
	double epsilon = obliquity(t);
	return arcsin_deg(sin_deg(beta) * cos_deg(epsilon) +
			  cos_deg(beta) * sin_deg(epsilon) * sin_deg(lambda));
}

/*
 * Calculate the right ascension at moment $t of an object at latitude $beta
 * and longitude $lambda.
 * Ref: Sec.(14.4), Eq.(14.30)
 */
double
right_ascension(double t, double beta, double lambda)
{
	double epsilon = obliquity(t);
	double x = cos_deg(lambda);
	double y = (sin_deg(lambda) * cos_deg(epsilon) -
		    tan_deg(beta) * sin_deg(epsilon));
	return arctan_deg(y, x);
}

/*
 * Calculate the refraction angle (in degrees) at a location of elevation
 * $elevation.
 * Ref: Sec.(14.7), Eq.(14.75)
 */
double
refraction(double elevation)
{
	double h = (elevation > 0) ? elevation : 0.0;
	double radius = 6.372e6;  /* Earth radius in meters */
	/* depression of visible horizon */
	double dip = arccos_deg(radius / (radius + h));
	/* depression contributed by an elevation of $h */
	double dip2 = (19.0/3600.0) * sqrt(h);
	/* average effect of refraction */
	double avg = 34.0 / 60.0;

	return avg + dip + dip2;
}

/*
 * Determine the day of year of the fixed date $rd.
 */
int
dayofyear_from_fixed(int rd)
{
	int year = gregorian_year_from_fixed(rd);
	struct date date = { year - 1, 12, 31 };

	return rd - fixed_from_gregorian(&date);
}

/*
 * Format the given time $t to 'HH:MM:SS' style.
 */
int
format_time(char *buf, size_t size, double t)
{
	int hh, mm, ss, i;

	t -= floor(t);
	i = (int)round(t * 24*60*60);

	hh = i / (60*60);
	i %= 60*60;
	mm = i / 60;
	ss = i % 60;

	return snprintf(buf, size, "%02d:%02d:%02d", hh, mm, ss);
}

/*
 * Format the given timezone (in fraction of days) $zone to '[+-]HH:MM' style.
 */
int
format_zone(char *buf, size_t size, double zone)
{
	bool positive;
	int hh, mm, i;

	positive = (zone >= 0);
	i = (int)round(fabs(zone) * 24*60);
	hh = i / 60;
	mm = i % 60;

	return snprintf(buf, size, "%c%02d:%02d",
			(positive ? '+' : '-'), hh, mm);
}

/*
 * Format the location to style: 'dd°mm'ss" [NS], dd°mm'ss" [EW], mm.m m'
 */
int
format_location(char *buf, size_t size, const struct location *loc)
{
	int d1, d2, m1, m2, s1, s2, i;
	bool north, east;

	north = (loc->latitude >= 0);
	i = (int)round(fabs(loc->latitude) * 60*60);
	d1 = i / (60*60);
	i %= 60*60;
	m1 = i / 60;
	s1 = i % 60;

	east = (loc->longitude >= 0);
	i = (int)round(fabs(loc->longitude) * 60*60);
	d2 = i / (60*60);
	i %= 60*60;
	m2 = i / 60;
	s2 = i % 60;

	return snprintf(buf, size, "%d°%d'%d\" %c, %d°%d'%d\" %c, %.1lf m",
			d1, m1, s1, (north ? 'N' : 'S'),
			d2, m2, s2, (east ? 'E' : 'W'),
			loc->elevation);
}
