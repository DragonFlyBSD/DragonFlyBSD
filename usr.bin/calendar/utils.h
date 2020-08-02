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

#ifndef UTILS_H_
#define UTILS_H_

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifndef nitems
#define nitems(x)	(sizeof(x) / sizeof((x)[0]))
#endif


/*
 * Return true if string $s1 starts with the string $s2.
 */
static inline bool
string_startswith(const char *s1, const char *s2)
{
	return (s1 && s2 && strncmp(s1, s2, strlen(s2)) == 0);
}

/*
 * Count the number of character $ch in string $s.
 */
static inline size_t
count_char(const char *s, int ch)
{
	size_t count = 0;

	for ( ; *s; s++) {
		if (*s == ch)
			count++;
	}

	return count;
}

/*
 * Trim the leading whitespaces of the given string $s
 * and return the trimed string.
 */
static inline char *
triml(char *s)
{
	while (isspace((unsigned char)*s))
		s++;

	return s;
}

/*
 * Trim the trailing whitespaces of the given string $s
 * and return the trimed string.
 */
static inline char *
trimr(char *s)
{
	size_t l = strlen(s);

	while (l > 0 && isspace((unsigned char) s[l-1]))
		l--;
	s[l] = '\0';

	return s;
}


/*
 * Swap the values of two integers.
 */
static inline void
swap(int *a, int *b)
{
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

/*
 * Divide integer $x by integer $y, rounding towards minus infinity.
 */
static inline int
div_floor(int x, int y)
{
	int q = x / y;
	int r = x % y;
	if ((r != 0) && ((r < 0) != (y < 0)))
		q--;
	return q;
}

/*
 * Calculate the remainder of $x divided by $y; the result has the same
 * sign as $y.
 * Ref: Sec.(1.7), Eq.(1.17)
 */
static inline int
mod(int x, int y)
{
	return x - y * div_floor(x, y);
}

static inline double
mod_f(double x, double y)
{
	return x - y * floor(x / y);
}

/*
 * Return the value of ($x % $y) with $y instead of 0, i.e., with value
 * range being [1, $y].
 */
static inline int
mod1(int x, int y)
{
	return y + mod(x, -y);
}

/*
 * Calculate the interval modulus of $x, i.e., shifted into the range
 * [$a, $b).  Return $x if $a = $b.
 * Ref: Sec.(1.7), Eq.(1.24)
 */
static inline int
mod3(int x, int a, int b)
{
	if (a == b)
		return x;
	else
		return a + mod(x - a, b - a);
}

static inline double
mod3_f(double x, double a, double b)
{
	static const double eps = 1e-6;

	if (fabs(a - b) < eps)
		return x;
	else
		return a + mod_f(x - a, b - a);
}


/*
 * Calculate the sine value of degree $deg.
 */
static inline double
sin_deg(double deg)
{
	return sin(M_PI * deg / 180.0);
}

/*
 * Calculate the cosine value of degree $deg.
 */
static inline double
cos_deg(double deg)
{
	return cos(M_PI * deg / 180.0);
}

/*
 * Calculate the tangent value of degree $deg.
 */
static inline double
tan_deg(double deg)
{
	return tan(M_PI * deg / 180.0);
}

/*
 * Calculate the arc sine value (in degrees) of $x.
 */
static inline double
arcsin_deg(double x)
{
	return asin(x) * 180.0 / M_PI;
}

/*
 * Calculate the arc cosine value (in degrees) of $x.
 */
static inline double
arccos_deg(double x)
{
	return acos(x) * 180.0 / M_PI;
}

/*
 * Calculate the arc tangent value (in degrees from 0 to 360) of $y / $x.
 * Error if $x and $y are both zero.
 */
static inline double
arctan_deg(double y, double x)
{
	errno = 0;
	double v = atan2(y, x);
	if (errno == EDOM)
		errx(10, "%s(%g, %g) invalid!", __func__, y, x);
	return mod_f(v * 180.0 / M_PI, 360);
}

/*
 * Convert angle in (degree, arcminute, arcsecond) to degree.
 */
static inline double
angle2deg(int deg, int min, double sec)
{
	return deg + min/60.0 + sec/3600.0;
}


double	poly(double x, const double *coefs, size_t n);
double	invert_angular(double (*f)(double), double y, double a, double b);

void *	xmalloc(size_t size);
void *	xcalloc(size_t number, size_t size);
void *	xrealloc(void *ptr, size_t size);
char *	xstrdup(const char *str);

struct node;
struct node *	list_newnode(char *name, void *data);
struct node *	list_addfront(struct node *listp, struct node *newp);
bool		list_lookup(struct node *listp, const char *name,
			    int (*cmp)(const char *, const char *),
			    void **data_out);
void		list_freeall(struct node *listp, void (*free_name)(void *),
			     void (*free_data)(void *));

#endif
