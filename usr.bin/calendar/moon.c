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

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "basics.h"
#include "gregorian.h"
#include "moon.h"
#include "sun.h"
#include "utils.h"


/*
 * Time from new moon to new moon (i.e., a lunation).
 * Ref: Sec.(14.6), Eq.(14.44)
 */
const double mean_synodic_month = 29.530588861;


/*
 * Argument data 1 used by 'nth_new_moon()'.
 * Ref: Sec.(14.6), Table(14.3)
 */
static const struct nth_new_moon_arg1 {
	double	v;
	int	w;
	int	x;
	int	y;
	int	z;
} nth_new_moon_data1[] = {
	{ -0.40720, 0,  0, 1,  0 },
	{  0.17241, 1,  1, 0,  0 },
	{  0.01608, 0,  0, 2,  0 },
	{  0.01039, 0,  0, 0,  2 },
	{  0.00739, 1, -1, 1,  0 },
	{ -0.00514, 1,  1, 1,  0 },
	{  0.00208, 2,  2, 0,  0 },
	{ -0.00111, 0,  0, 1, -2 },
	{ -0.00057, 0,  0, 1,  2 },
	{  0.00056, 1,  1, 2,  0 },
	{ -0.00042, 0,  0, 3,  0 },
	{  0.00042, 1,  1, 0,  2 },
	{  0.00038, 1,  1, 0, -2 },
	{ -0.00024, 1, -1, 2,  0 },
	{ -0.00007, 0,  2, 1,  0 },
	{  0.00004, 0,  0, 2, -2 },
	{  0.00004, 0,  3, 0,  0 },
	{  0.00003, 0,  1, 1, -2 },
	{  0.00003, 0,  0, 2,  2 },
	{ -0.00003, 0,  1, 1,  2 },
	{  0.00003, 0, -1, 1,  2 },
	{ -0.00002, 0, -1, 1, -2 },
	{ -0.00002, 0,  1, 3,  0 },
	{  0.00002, 0,  0, 4,  0 },
};

/*
 * Argument data 2 used by 'nth_new_moon()'.
 * Ref: Sec.(14.6), Table(14.4)
 */
static const struct nth_new_moon_arg2 {
	double	i;
	double	j;
	double	l;
} nth_new_moon_data2[] = {
	{ 251.88,  0.016321, 0.000165 },
	{ 251.83, 26.651886, 0.000164 },
	{ 349.42, 36.412478, 0.000126 },
	{  84.66, 18.206239, 0.000110 },
	{ 141.74, 53.303771, 0.000062 },
	{ 207.14,  2.453732, 0.000060 },
	{ 154.84,  7.306860, 0.000056 },
	{  34.52, 27.261239, 0.000047 },
	{ 207.19,  0.121824, 0.000042 },
	{ 291.34,  1.844379, 0.000040 },
	{ 161.72, 24.198154, 0.000037 },
	{ 239.56, 25.513099, 0.000035 },
	{ 331.55,  3.592518, 0.000023 },
};

/*
 * Calculate the moment of the $n-th new moon after the new moon of
 * January 11, 1 (Gregorian), which is the first new moon after RD 0.
 * This function is centered upon January, 2000.
 * Ref: Sec.(14.6), Eq.(14.45)
 */
double
nth_new_moon(int n)
{
	int n0 = 24724;  /* Months from RD 0 until j2000 */
	int k = n - n0;  /* Months since j2000 */
	double nm = 1236.85;  /* Months per century */
	double c = k / nm;  /* Julian centuries */
	double j2000 = 0.5 + gregorian_new_year(2000);

	double coef_ap[] = { 5.09766, mean_synodic_month * nm,
			     0.00015437, -0.000000150, 0.00000000073 };
	double approx = j2000 + poly(c, coef_ap, nitems(coef_ap));

	double extra = 0.000325 * sin_deg(299.77 + 132.8475848 * c -
					  0.009173 * c*c);

	double coef_sa[] = { 2.5534, 29.10535670 * nm,
			     -0.0000014, -0.00000011 };
	double coef_la[] = { 201.5643, 385.81693528 * nm,
			     0.0107582, 0.00001238, -0.000000058 };
	double coef_ma[] = { 160.7108, 390.67050284 * nm,
			     -0.0016118, -0.00000227, 0.000000011 };
	double coef_om[] = { 124.7746, -1.56375588 * nm,
			     0.0020672, 0.00000215 };
	double solar_anomaly = poly(c, coef_sa, nitems(coef_sa));
	double lunar_anomaly = poly(c, coef_la, nitems(coef_la));
	double moon_argument = poly(c, coef_ma, nitems(coef_ma));
	double omega = poly(c, coef_om, nitems(coef_om));
	double E = 1.0 - 0.002516 * c - 0.0000074 * c*c;

	double sum_c = 0.0;
	const struct nth_new_moon_arg1 *arg1;
	for (size_t i = 0; i < nitems(nth_new_moon_data1); i++) {
		arg1 = &nth_new_moon_data1[i];
		sum_c += arg1->v * pow(E, arg1->w) * sin_deg(
				arg1->x * solar_anomaly +
				arg1->y * lunar_anomaly +
				arg1->z * moon_argument);
	}
	double correction = -0.00017 * sin_deg(omega) + sum_c;

	double additional = 0.0;
	const struct nth_new_moon_arg2 *arg2;
	for (size_t i = 0; i < nitems(nth_new_moon_data2); i++) {
		arg2 = &nth_new_moon_data2[i];
		additional += arg2->l * sin_deg(arg2->i + arg2->j * k);
	}

	double dt = approx + correction + extra + additional;
	return universal_from_dynamical(dt);
}

/*
 * Mean longitude of moon at moment given in Julian centuries $c.
 * Ref: Sec.(14.6), Eq.(14.49)
 */
static double
lunar_longitude_mean(double c)
{
	double coef[] = { 218.3164477, 481267.88123421, -0.0015786,
			  1.0 / 538841.0, -1.0 / 65194000.0 };
	return mod_f(poly(c, coef, nitems(coef)), 360);
}

/*
 * Elongation of moon (angular distance from Sun) at moment given in Julian
 * centuries $c.
 * Ref: Sec.(14.6), Eq.(14.50)
 */
static double
lunar_elongation(double c)
{
	double coef[] = { 297.8501921, 445267.1114034, -0.0018819,
			  1.0 / 545868.0, -1.0 / 113065000.0 };
	return mod_f(poly(c, coef, nitems(coef)), 360);
}

/*
 * Mean anomaly of Sun (angular distance from perihelion) at moment given in
 * Julian centuries $c.
 * Ref: Sec.(14.6), Eq.(14.51)
 */
static double
solar_anomaly(double c)
{
	double coef[] = { 357.5291092, 35999.0502909, -0.0001536,
			  1.0 / 24490000.0 };
	return mod_f(poly(c, coef, nitems(coef)), 360);
}

/*
 * Mean anomaly of moon (angular distance from perigee) at moment given in
 * Julian centuries $c.
 * Ref: Sec.(14.6), Eq.(14.52)
 */
static double
lunar_anomaly(double c)
{
	double coef[] = { 134.9633964, 477198.8675055, 0.0087414,
			  1.0 / 69699.0, -1.0 / 14712000.0 };
	return mod_f(poly(c, coef, nitems(coef)), 360);
}

/*
 * Moon's argument of latitude (the distance from the moon's node) at moment
 * given in Julian centuries $c.
 * Ref: Sec.(14.6), Eq.(14.53)
 */
static double
moon_node(double c)
{
	double coef[] = { 93.2720950, 483202.0175233, -0.0036539,
			  -1.0 / 3526000.0, 1.0 / 863310000.0 };
	return mod_f(poly(c, coef, nitems(coef)), 360);
}

/*
 * Argument data used by 'lunar_longitude()'.
 * Ref: Sec.(14.6), Table(14.5)
 */
static const struct lunar_longitude_arg {
	int	v;
	int	w;
	int	x;
	int	y;
	int	z;
} lunar_longitude_data[] = {
	{ 6288774, 0,  0,  1,  0 },
	{ 1274027, 2,  0, -1,  0 },
	{  658314, 2,  0,  0,  0 },
	{  213618, 0,  0,  2,  0 },
	{ -185116, 0,  1,  0,  0 },
	{ -114332, 0,  0,  0,  2 },
	{   58793, 2,  0, -2,  0 },
	{   57066, 2, -1, -1,  0 },
	{   53322, 2,  0,  1,  0 },
	{   45758, 2, -1,  0,  0 },
	{  -40923, 0,  1, -1,  0 },
	{  -34720, 1,  0,  0,  0 },
	{  -30383, 0,  1,  1,  0 },
	{   15327, 2,  0,  0, -2 },
	{  -12528, 0,  0,  1,  2 },
	{   10980, 0,  0,  1, -2 },
	{   10675, 4,  0, -1,  0 },
	{   10034, 0,  0,  3,  0 },
	{    8548, 4,  0, -2,  0 },
	{   -7888, 2,  1, -1,  0 },
	{   -6766, 2,  1,  0,  0 },
	{   -5163, 1,  0, -1,  0 },
	{    4987, 1,  1,  0,  0 },
	{    4036, 2, -1,  1,  0 },
	{    3994, 2,  0,  2,  0 },
	{    3861, 4,  0,  0,  0 },
	{    3665, 2,  0, -3,  0 },
	{   -2689, 0,  1, -2,  0 },
	{   -2602, 2,  0, -1,  2 },
	{    2390, 2, -1, -2,  0 },
	{   -2348, 1,  0,  1,  0 },
	{    2236, 2, -2,  0,  0 },
	{   -2120, 0,  1,  2,  0 },
	{   -2069, 0,  2,  0,  0 },
	{    2048, 2, -2, -1,  0 },
	{   -1773, 2,  0,  1, -2 },
	{   -1595, 2,  0,  0,  2 },
	{    1215, 4, -1, -1,  0 },
	{   -1110, 0,  0,  2,  2 },
	{    -892, 3,  0, -1,  0 },
	{    -810, 2,  1,  1,  0 },
	{     759, 4, -1, -2,  0 },
	{    -713, 0,  2, -1,  0 },
	{    -700, 2,  2, -1,  0 },
	{     691, 2,  1, -2,  0 },
	{     596, 2, -1,  0, -2 },
	{     549, 4,  0,  1,  0 },
	{     537, 0,  0,  4,  0 },
	{     520, 4, -1,  0,  0 },
	{    -487, 1,  0, -2,  0 },
	{    -399, 2,  1,  0, -2 },
	{    -381, 0,  0,  2, -2 },
	{     351, 1,  1,  1,  0 },
	{    -340, 3,  0, -2,  0 },
	{     330, 4,  0, -3,  0 },
	{     327, 2, -1,  2,  0 },
	{    -323, 0,  2,  1,  0 },
	{     299, 1,  1, -1,  0 },
	{     294, 2,  0,  3,  0 },
};

/*
 * Calculate the geocentric longitude of moon (in degrees) at moment $t.
 * Ref: Sec.(14.6), Eq.(14.48)
 */
double
lunar_longitude(double t)
{
	double c = julian_centuries(t);
	double nu = nutation(t);

	double L_prime = lunar_longitude_mean(c);
	double D = lunar_elongation(c);
	double M = solar_anomaly(c);
	double M_prime = lunar_anomaly(c);
	double F = moon_node(c);
	double E = 1.0 - 0.002516 * c - 0.0000074 * c*c;

	double sum = 0.0;
	const struct lunar_longitude_arg *arg;
	for (size_t i = 0; i < nitems(lunar_longitude_data); i++) {
		arg = &lunar_longitude_data[i];
		sum += arg->v * pow(E, abs(arg->x)) * sin_deg(
				arg->w * D +
				arg->x * M +
				arg->y * M_prime +
				arg->z * F);
	}
	double correction = sum / 1e6;

	double venus = sin_deg(119.75 + 131.849 * c) * 3958 / 1e6;
	double jupiter = sin_deg(53.09 + 479264.29 * c) * 318 / 1e6;
	double flat_earth = sin_deg(L_prime - F) * 1962 / 1e6;

	return mod_f((L_prime + correction + venus + jupiter +
		      flat_earth + nu), 360);
}

/*
 * Argument data used by 'lunar_latitude()'.
 * Ref: Sec.(14.6), Table(14.6)
 */
static const struct lunar_latitude_arg {
	int	v;
	int	w;
	int	x;
	int	y;
	int	z;
} lunar_latitude_data[] = {
	{ 5128122, 0,  0,  0,  1 },
	{  280602, 0,  0,  1,  1 },
	{  277693, 0,  0,  1, -1 },
	{  173237, 2,  0,  0, -1 },
	{   55413, 2,  0, -1,  1 },
	{   46271, 2,  0, -1, -1 },
	{   32573, 2,  0,  0,  1 },
	{   17198, 0,  0,  2,  1 },
	{    9266, 2,  0,  1, -1 },
	{    8822, 0,  0,  2, -1 },
	{    8216, 2, -1,  0, -1 },
	{    4324, 2,  0, -2, -1 },
	{    4200, 2,  0,  1,  1 },
	{   -3359, 2,  1,  0, -1 },
	{    2463, 2, -1, -1,  1 },
	{    2211, 2, -1,  0,  1 },
	{    2065, 2, -1, -1, -1 },
	{   -1870, 0,  1, -1, -1 },
	{    1828, 4,  0, -1, -1 },
	{   -1794, 0,  1,  0,  1 },
	{   -1749, 0,  0,  0,  3 },
	{   -1565, 0,  1, -1,  1 },
	{   -1491, 1,  0,  0,  1 },
	{   -1475, 0,  1,  1,  1 },
	{   -1410, 0,  1,  1, -1 },
	{   -1344, 0,  1,  0, -1 },
	{   -1335, 1,  0,  0, -1 },
	{    1107, 0,  0,  3,  1 },
	{    1021, 4,  0,  0, -1 },
	{     833, 4,  0, -1,  1 },
	{     777, 0,  0,  1, -3 },
	{     671, 4,  0, -2,  1 },
	{     607, 2,  0,  0, -3 },
	{     596, 2,  0,  2, -1 },
	{     491, 2, -1,  1, -1 },
	{    -451, 2,  0, -2,  1 },
	{     439, 0,  0,  3, -1 },
	{     422, 2,  0,  2,  1 },
	{     421, 2,  0, -3, -1 },
	{    -366, 2,  1, -1,  1 },
	{    -351, 2,  1,  0,  1 },
	{     331, 4,  0,  0,  1 },
	{     315, 2, -1,  1,  1 },
	{     302, 2, -2,  0, -1 },
	{    -283, 0,  0,  1,  3 },
	{    -229, 2,  1,  1, -1 },
	{     223, 1,  1,  0, -1 },
	{     223, 1,  1,  0,  1 },
	{    -220, 0,  1, -2, -1 },
	{    -220, 2,  1, -1, -1 },
	{    -185, 1,  0,  1,  1 },
	{     181, 2, -1, -2, -1 },
	{    -177, 0,  1,  2,  1 },
	{     176, 4,  0, -2, -1 },
	{     166, 4, -1, -1, -1 },
	{    -164, 1,  0,  1, -1 },
	{     132, 4,  0,  1, -1 },
	{    -119, 1,  0, -1, -1 },
	{     115, 4, -1,  0, -1 },
	{     107, 2, -2,  0,  1 },
};

/*
 * Calculate the geocentric latitude of moon (in degrees) at moment $t.
 * Lunar latitude ranges from about -6 to 6 degress.
 * Ref: Sec.(14.6), Eq.(14.63)
 */
double
lunar_latitude(double t)
{
	double c = julian_centuries(t);

	double L_prime = lunar_longitude_mean(c);
	double D = lunar_elongation(c);
	double M = solar_anomaly(c);
	double M_prime = lunar_anomaly(c);
	double F = moon_node(c);
	double E = 1.0 - 0.002516 * c - 0.0000074 * c*c;

	double sum = 0.0;
	const struct lunar_latitude_arg *arg;
	for (size_t i = 0; i < nitems(lunar_latitude_data); i++) {
		arg = &lunar_latitude_data[i];
		sum += arg->v * pow(E, abs(arg->x)) * sin_deg(
				arg->w * D +
				arg->x * M +
				arg->y * M_prime +
				arg->z * F);
	}
	double beta = sum / 1e6;

	double venus = (sin_deg(119.75 + 131.849 * c + F) +
			sin_deg(119.75 + 131.849 * c - F)) * 175 / 1e6;
	double flat_earth = (-2235 * sin_deg(L_prime) +
			     127 * sin_deg(L_prime - M_prime) -
			     115 * sin_deg(L_prime + M_prime)) / 1e6;
	double extra = sin_deg(313.45 + 481266.484 * c) * 382 / 1e6;

	return (beta + venus + flat_earth + extra);
}

/*
 * Argument data used by 'lunar_distance()'.
 * Ref: Sec.(14.6), Table(14.7)
 */
static const struct lunar_distance_arg {
	int	v;
	int	w;
	int	x;
	int	y;
	int	z;
} lunar_distance_data[] = {
	{ -20905355, 0,  0,  1,  0 },
	{  -3699111, 2,  0, -1,  0 },
	{  -2955968, 2,  0,  0,  0 },
	{   -569925, 0,  0,  2,  0 },
	{     48888, 0,  1,  0,  0 },
	{     -3149, 0,  0,  0,  2 },
	{    246158, 2,  0, -2,  0 },
	{   -152138, 2, -1, -1,  0 },
	{   -170733, 2,  0,  1,  0 },
	{   -204586, 2, -1,  0,  0 },
	{   -129620, 0,  1, -1,  0 },
	{    108743, 1,  0,  0,  0 },
	{    104755, 0,  1,  1,  0 },
	{     10321, 2,  0,  0, -2 },
	{         0, 0,  0,  1,  2 },
	{     79661, 0,  0,  1, -2 },
	{    -34782, 4,  0, -1,  0 },
	{    -23210, 0,  0,  3,  0 },
	{    -21636, 4,  0, -2,  0 },
	{     24208, 2,  1, -1,  0 },
	{     30824, 2,  1,  0,  0 },
	{     -8379, 1,  0, -1,  0 },
	{    -16675, 1,  1,  0,  0 },
	{    -12831, 2, -1,  1,  0 },
	{    -10445, 2,  0,  2,  0 },
	{    -11650, 4,  0,  0,  0 },
	{     14403, 2,  0, -3,  0 },
	{     -7003, 0,  1, -2,  0 },
	{         0, 2,  0, -1,  2 },
	{     10056, 2, -1, -2,  0 },
	{      6322, 1,  0,  1,  0 },
	{     -9884, 2, -2,  0,  0 },
	{      5751, 0,  1,  2,  0 },
	{         0, 0,  2,  0,  0 },
	{     -4950, 2, -2, -1,  0 },
	{      4130, 2,  0,  1, -2 },
	{         0, 2,  0,  0,  2 },
	{     -3958, 4, -1, -1,  0 },
	{         0, 0,  0,  2,  2 },
	{      3258, 3,  0, -1,  0 },
	{      2616, 2,  1,  1,  0 },
	{     -1897, 4, -1, -2,  0 },
	{     -2117, 0,  2, -1,  0 },
	{      2354, 2,  2, -1,  0 },
	{         0, 2,  1, -2,  0 },
	{         0, 2, -1,  0, -2 },
	{     -1423, 4,  0,  1,  0 },
	{     -1117, 0,  0,  4,  0 },
	{     -1571, 4, -1,  0,  0 },
	{     -1739, 1,  0, -2,  0 },
	{         0, 2,  1,  0, -2 },
	{     -4421, 0,  0,  2, -2 },
	{         0, 1,  1,  1,  0 },
	{         0, 3,  0, -2,  0 },
	{         0, 4,  0, -3,  0 },
	{         0, 2, -1,  2,  0 },
	{      1165, 0,  2,  1,  0 },
	{         0, 1,  1, -1,  0 },
	{         0, 2,  0,  3,  0 },
	{      8752, 2,  0, -1, -2 },
};

/*
 * Calculate the distance to moon (in meters) at moment $t.
 * Ref: Sec.(14.6), Eq.(14.65)
 */
double
lunar_distance(double t)
{
	double c = julian_centuries(t);

	double D = lunar_elongation(c);
	double M = solar_anomaly(c);
	double M_prime = lunar_anomaly(c);
	double F = moon_node(c);
	double E = 1.0 - 0.002516 * c - 0.0000074 * c*c;

	double correction = 0.0;
	const struct lunar_distance_arg *arg;
	for (size_t i = 0; i < nitems(lunar_distance_data); i++) {
		arg = &lunar_distance_data[i];
		correction += arg->v * pow(E, abs(arg->x)) * cos_deg(
				arg->w * D +
				arg->x * M +
				arg->y * M_prime +
				arg->z * F);
	}

	return 385000560.0 + correction;
}

/*
 * Calculate the altitude of moon (in degrees) above the horizon at
 * location ($latitude, $longitude) and moment $t, ignoring parallax
 * and refraction.
 * NOTE: This calculates the geocentric altitude viewed from the center
 * of the Earth.
 * Ref: Sec.(14.6), Eq.(14.64)
 */
double
lunar_altitude(double t, double latitude, double longitude)
{
	double lambda = lunar_longitude(t);
	double beta = lunar_latitude(t);
	double alpha = right_ascension(t, beta, lambda);
	double delta = declination(t, beta, lambda);
	double theta = sidereal_from_moment(t);
	double H = mod_f(theta + longitude - alpha, 360);

	double v = (sin_deg(latitude) * sin_deg(delta) +
		    cos_deg(latitude) * cos_deg(delta) * cos_deg(H));
	return mod3_f(arcsin_deg(v), -180, 180);
}

/*
 * Parallax of moon at moment $t and location ($latitude, $longitude).
 * Ref: Sec.(14.6), Eq.(14.66)
 */
static double
lunar_parallax(double t, double latitude, double longitude)
{
	double geo = lunar_altitude(t, latitude, longitude);
	double distance = lunar_distance(t);
	/* Equatorial horizontal parallax of the moon */
	double sin_pi = 6378140.0 / distance;
	return arcsin_deg(sin_pi * cos_deg(geo));
}

/*
 * Calculate the topocentric altitude of moon viewed from the Earth surface
 * at location ($latitude, $longitude) and at moment $t.
 * Ref: Sec.(14.6), Eq.(14.67)
 */
static double
lunar_altitude_topocentric(double t, double latitude, double longitude)
{
	return (lunar_altitude(t, latitude, longitude) -
		lunar_parallax(t, latitude, longitude));
}

/*
 * Calculate the observed altitude of the upper limb of moon at location
 * ($latitude, $longitude) and moment $t.
 * Ref: Sec.(14.7), Eq.(14.82)
 */
double
lunar_altitude_observed(double t, const struct location *loc)
{
	double moon_radius = 16.0 / 60.0;  /* 16 arcminutes */
	return (lunar_altitude_topocentric(t, loc->latitude, loc->longitude) +
		refraction(loc->elevation) + moon_radius);
}

/*
 * Calculate the phase of the moon, defined as the difference in
 * longitudes of the Sun and moon, at the given moment $t.
 * Ref: Sec.(14.6), Eq.(14.56)
 */
double
lunar_phase(double t)
{
	double phi = mod_f(lunar_longitude(t) - solar_longitude(t), 360);

	/*
	 * To check whether the above result conflicts with the time of
	 * new moon as calculated by the more precise 'nth_new_moon()'
	 */
	double t0 = nth_new_moon(0);
	int n = (int)lround((t - t0) / mean_synodic_month);
	double tn = nth_new_moon(n);
	double phi2 = mod_f((t - tn) / mean_synodic_month, 1) * 360;

	/* prefer the approximation based on the 'nth_new_moon()' moment */
	return (fabs(phi - phi2) > 180) ? phi2 : phi;
}

/*
 * Calculate the moment of the next time at or after the given moment $t
 * when the phase of the moon is $phi degree.
 * Ref: Sec.(14.6), Eq.(14.58)
 */
double
lunar_phase_atafter(double phi, double t)
{
	double rate = mean_synodic_month / 360.0;
	double phase = lunar_phase(t);
	double tau = t + rate * mod_f(phi - phase, 360);

	/* estimate range (within 2 days) */
	double a = (t > tau - 2) ? t : tau - 2;
	double b = tau + 2;

	return invert_angular(lunar_phase, phi, a, b);
}

/*
 * Calculate the moment of the new moon before the given moment $t.
 * Ref: Sec.(14.6), Eq.(14.46)
 */
double
new_moon_before(double t)
{
	double t0 = nth_new_moon(0);
	double phi = lunar_phase(t);
	int n = (int)lround((t - t0) / mean_synodic_month - phi / 360.0);

	int k = n - 1;
	double t1 = nth_new_moon(k);
	while (t1 < t) {
		k++;
		t1 = nth_new_moon(k);
	}

	return nth_new_moon(k-1);
}

/*
 * Calculate the moment of the new moon at or after the given moment $t.
 * Ref: Sec.(14.6), Eq.(14.47)
 */
double
new_moon_atafter(double t)
{
	double t0 = nth_new_moon(0);
	double phi = lunar_phase(t);
	int n = (int)lround((t - t0) / mean_synodic_month - phi / 360.0);

	double t1 = nth_new_moon(n);
	while (t1 < t) {
		n++;
		t1 = nth_new_moon(n);
	}

	return t1;
}

/*
 * Calculate the moment of moonrise in standard time on fixed date $rd
 * at location $loc.
 * NOTE: Return an NaN if no moonrise.
 * Ref: Sec.(14.7), Eq.(14.83)
 */
double
moonrise(int rd, const struct location *loc)
{
	double t = (double)rd - loc->zone;  /* universal time */
	bool waning = lunar_phase(t) > 180.0;
	/* lunar altitude at midnight */
	double alt = lunar_altitude_observed(t, loc);
	double offset = alt / (4.0 * (90.0 - fabs(loc->latitude)));

	/* approximate rising time */
	double t_approx = t;
	if (waning) {
		t_approx -= offset;
		if (offset > 0)
			t_approx += 1;
	} else {
		t_approx += 0.5 + offset;
	}

	/* binary search to determine the rising time */
	const double eps = 30.0 / 3600 / 24;  /* accuracy of 30 seconds */
	double a = t_approx - 6.0/24.0;
	double b = t_approx + 6.0/24.0;
	double t_rise;
	do {
		t_rise = (a + b) / 2.0;
		if (lunar_altitude_observed(t_rise, loc) > 0)
			b = t_rise;
		else
			a = t_rise;
	} while (fabs(a - b) >= eps);

	if (t_rise < t + 1) {
		t_rise += loc->zone;  /* standard time */
		/* may be just before to midnight */
		return (t_rise > (double)rd) ? t_rise : (double)rd;
	} else {
		/* no moonrise on this day */
		return NAN;
	}
}

/*
 * Calculate the moment of moonset in standard time on fixed date $rd
 * at location $loc.
 * NOTE: Return an NaN if no moonset.
 * Ref: Sec.(14.7), Eq.(14.84)
 */
double
moonset(int rd, const struct location *loc)
{
	double t = (double)rd - loc->zone;  /* universal time */
	bool waxing = lunar_phase(t) < 180.0;
	/* lunar altitude at midnight */
	double alt = lunar_altitude_observed(t, loc);
	double offset = alt / (4.0 * (90.0 - fabs(loc->latitude)));

	/* approximate setting time */
	double t_approx = t;
	if (waxing) {
		t_approx += offset;
		if (offset <= 0)
			t_approx += 1;
	} else {
		t_approx += 0.5 - offset;
	}

	/* binary search to determine the setting time */
	const double eps = 30.0 / 3600 / 24;  /* accuracy of 30 seconds */
	double a = t_approx - 6.0/24.0;
	double b = t_approx + 6.0/24.0;
	double t_set;
	do {
		t_set = (a + b) / 2.0;
		if (lunar_altitude_observed(t_set, loc) < 0)
			b = t_set;
		else
			a = t_set;
	} while (fabs(a - b) >= eps);

	if (t_set < t + 1) {
		t_set += loc->zone;  /* standard time */
		/* may be just before to midnight */
		return (t_set > (double)rd) ? t_set : (double)rd;
	} else {
		/* no moonrise on this day */
		return NAN;
	}
}

/**************************************************************************/

/*
 * Print moon information at the given moment $t (in standard time)
 * and events in the year.
 */
void
show_moon_info(double t, const struct location *loc)
{
	char buf[64];
	int rd = (int)floor(t);
	double t_u = t - loc->zone;  /* universal time */

	/*
	 * Lunar phase
	 */

	double eps = 1e-5;
	double phi = lunar_phase(t_u);
	bool waxing = (phi <= 180);
	bool crescent = (phi <= 90 || phi > 270);
	const char *phase_name;
	if (fabs(phi) < eps || fabs(phi - 360) < eps)
		phase_name = "New Moon";
	else if (fabs(phi - 90) < eps)
		phase_name = "First Quarter";
	else if (fabs(phi - 180) < eps)
		phase_name = "Full Moon";
	else if (fabs(phi - 270) < eps)
		phase_name = "Last Quarter";
	else
		phase_name = NULL;

	if (phase_name) {
		snprintf(buf, sizeof(buf), "%s", phase_name);
	} else {
		snprintf(buf, sizeof(buf), "%s %s",
			 (waxing ? "Waxing" : "Waning"),
			 (crescent ? "Crescent" : "Gibbous"));
	}
	printf("Moon phase: %.2lf° (%s)\n", phi, buf);

	/*
	 * Moon position
	 */
	double lon = lunar_longitude(t_u);
	double alt = lunar_altitude_observed(t_u, loc);
	printf("Moon position: %.4lf° (longitude), %.4lf° (altitude)\n",
	       lon, alt);

	/*
	 * Moon rise and set
	 */

	double moments[2] = { moonrise(rd, loc), moonset(rd, loc) };
	const char *names[2] = { "Moonrise", "Moonset" };
	if (!isnan(moments[0]) && !isnan(moments[1]) &&
	    moments[0] > moments[1]) {
		double t_tmp = moments[0];
		moments[0] = moments[1];
		moments[1] = t_tmp;
		const char *p = names[0];
		names[0] = names[1];
		names[1] = p;
	}
	for (size_t i = 0; i < nitems(moments); i++) {
		if (isnan(moments[i]))
			snprintf(buf, sizeof(buf), "(null)");
		else
			format_time(buf, sizeof(buf), moments[i]);
		printf("%-8s: %s\n", names[i], buf);
	}

	/*
	 * Moon phases in the year
	 */

	int year = gregorian_year_from_fixed(rd);
	struct date date = { year, 1, 1 };
	double t_begin = fixed_from_gregorian(&date) - loc->zone;
	date.year++;
	double t_end = fixed_from_gregorian(&date) - loc->zone;

	printf("\nLunar events in year %d:\n", year);
	printf("%19s   %19s   %19s   %19s\n",
	       "New Moon", "First Quarter", "Full Moon", "Last Quarter");

	double t_newmoon = t_begin;
	while ((t_newmoon = new_moon_atafter(t_newmoon)) < t_end) {
		t_newmoon += loc->zone;  /* to standard time */
		gregorian_from_fixed((int)floor(t_newmoon), &date);
		format_time(buf, sizeof(buf), t_newmoon);
		printf("%d-%02d-%02d %s",
		       date.year, date.month, date.day, buf);

		/*
		 * first quarter, full moon, last quarter
		 */
		double t_event = t_newmoon;
		int phi_events[] = { 90, 180, 270 };
		for (size_t i = 0; i < nitems(phi_events); i++) {
			t_event = lunar_phase_atafter(phi_events[i], t_event);
			t_event += loc->zone;
			gregorian_from_fixed((int)floor(t_event), &date);
			format_time(buf, sizeof(buf), t_event);
			printf("   %d-%02d-%02d %s",
			       date.year, date.month, date.day, buf);
		}
		printf("\n");

		/* go to the next new moon */
		t_newmoon += 28;
	}
}
