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
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"


/*
 * Calculate the polynomial: c[0] + c[1] * x + ... + c[n-1] * x^(n-1)
 */
double
poly(double x, const double *coefs, size_t n)
{
	double p = 0.0;
	double t = 1.0;
	for (size_t i = 0; i < n; i++) {
		p += t * coefs[i];
		t *= x;
	}
	return p;
}

/*
 * Use bisection search to find the inverse of the given angular function
 * $f(x) at value $y (degrees) within time interval [$a, $b].
 * Ref: Sec.(1.8), Eq.(1.36)
 */
double
invert_angular(double (*f)(double), double y, double a, double b)
{
	static const double eps = 1e-6;
	double x;

	do {
		x = (a + b) / 2.0;
		if (mod_f(f(x) - y, 360) < 180.0)
			b = x;
		else
			a = x;
	} while (fabs(a-b) >= eps);

	return x;
}


/*
 * Like malloc(3) but exit if allocation fails.
 */
void *
xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (ptr == NULL)
		errx(1, "mcalloc(%zu): out of memory", size);
	return ptr;
}

/*
 * Like calloc(3) but exit if allocation fails.
 */
void *
xcalloc(size_t number, size_t size)
{
	void *ptr = calloc(number, size);
	if (ptr == NULL)
		errx(1, "xcalloc(%zu, %zu): out of memory", number, size);
	return ptr;
}

/*
 * Like realloc(3) but exit if allocation fails.
 */
void *
xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (ptr == NULL)
		errx(1, "xrealloc: out of memory (size: %zu)", size);
	return ptr;
}

/*
 * Like strdup(3) but exit if fail.
 */
char *
xstrdup(const char *str)
{
	char *p = strdup(str);
	if (p == NULL)
		errx(1, "xstrdup: out of memory (length: %zu)", strlen(str));
	return p;
}


/*
 * Linked list implementation
 */

struct node {
	char		*name;
	void		*data;
	struct node	*next;
};

/*
 * Create a new list node with the given $name and $data.
 */
struct node *
list_newnode(char *name, void *data)
{
	struct node *newp;

	newp = xcalloc(1, sizeof(*newp));
	newp->name = name;
	newp->data = data;

	return newp;
}

/*
 * Add $newp to the front of list $listp.
 */
struct node *
list_addfront(struct node *listp, struct node *newp)
{
	newp->next = listp;
	return newp;
}

/*
 * Lookup the given $name in the list $listp.
 * The $cmp function compares two names and return 0 if they equal.
 * Return the associated data with the found node, otherwise NULL.
 */
bool
list_lookup(struct node *listp, const char *name,
	    int (*cmp)(const char *, const char *), void **data_out)
{
	for ( ; listp; listp = listp->next) {
		if ((*cmp)(name, listp->name) == 0) {
			if (data_out)
				*data_out = listp->data;
			return true;
		}
	}

	return false;
}

/*
 * Free all nodes of list $listp.
 */
void
list_freeall(struct node *listp,
	     void (*free_name)(void *),
	     void (*free_data)(void *))
{
	struct node *cur;

	while (listp) {
		cur = listp;
		listp = listp->next;
		if (free_name)
			(*free_name)(cur->name);
		if (free_data)
			(*free_data)(cur->data);
		free(cur);
	}
}
