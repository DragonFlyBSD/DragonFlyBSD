/*
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#include "flame.h"

typedef struct elm {
	struct elm *next;
	uint64_t	hv;
	long		ticks;
	const char	*id;
	size_t		idlen;
} elm_t;

#define MAXELM	256
#define HSIZE	(1024*1024)
#define HMASK	(HSIZE - 1)

static elm_t *elm_lookup(const char *s);
static void flame_process_dump(void);

long total_ticks;

void
flame_process_loop(void)
{
	char buf[4096];
	char *s;
	elm_t *elm;
	/*elm_t *elms[MAXELM];*/
	int n;

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
		n = 0;

		s = strtok(buf, " \t\n\r");
		if (s == NULL || strchr(s, '/') == NULL)
			continue;
		while ((s = strtok(NULL, " \t\n\r")) != NULL) {
			elm = elm_lookup(s);
			/*elms[n] = elm;*/
			++elm->ticks;

			if (n == MAXELM)
				break;
			++n;
		}
		++total_ticks;
		/* more processing later */
	}
	flame_process_dump();
}

static elm_t *elm_hash_array[HSIZE];

static elm_t *
elm_lookup(const char *s)
{
	size_t len;
	uint64_t hv;
	elm_t **scanp;
	elm_t *scan;

	if (s[0] == '0' && s[1] == 'x') {
		hv = strtoul(s, NULL, 0);
		if (hv < 0x8000000000000000LU)
			s = "__userland__";
	}

	hv = 0;
	for (len = 0; s[len] && s[len] != '+' && s[len] != '('; ++len)
		hv = hv * 13 ^ (uint8_t)s[len];

	scanp = &elm_hash_array[hv % HSIZE];
	while ((scan = *scanp) != NULL) {
		if (scan->hv == hv && len == scan->idlen &&
		    bcmp(s, scan->id, len) == 0) {
			return scan;
		}
		scanp = &scan->next;
	}
	scan = malloc(sizeof(elm_t));
	bzero(scan, sizeof(*scan));
	*scanp = scan;
	scan->hv = hv;
	scan->id = strdup(s);
	scan->idlen = len;

	return scan;
}

static void
flame_process_dump(void)
{
	elm_t *elm;
	int i;

	for (i = 0; i < HSIZE; ++i) {
		for (elm = elm_hash_array[i]; elm; elm = elm->next) {
			printf("%-6ld %6.3f%% %*.*s\n",
				elm->ticks,
				(double)elm->ticks * 100.0 /
				 (double)total_ticks,
				(int)elm->idlen, (int)elm->idlen, elm->id);
		}
	}
}
