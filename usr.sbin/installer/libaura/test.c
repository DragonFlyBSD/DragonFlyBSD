/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Chris Pressey <cpressey@catseye.mine.nu>.
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

/*
 * test.c
 * Test some libaura functions.
 * $Id: test.c,v 1.4 2005/01/06 23:50:17 cpressey Exp $
 */

#include <stdio.h>

#include "dict.h"
#include "fspred.h"

int main(int argc, char **argv)
{
	struct aura_dict *d;
	char k[256], v[256];
	void *rv, *rk;
	size_t rv_len, rk_len;

	d = aura_dict_new(1, AURA_DICT_SORTED_LIST);
	/* d = aura_dict_new(23, AURA_DICT_HASH); */

	while (!feof(stdin)) {
		printf("key> ");
		fgets(k, 255, stdin);
		if (strlen(k) > 0)
			k[strlen(k) - 1] = '\0';
		if (k[0] == '?') {
			printf("%s %s a file\n", &k[1],
			    is_file("%s", &k[1]) ? "IS" : "IS NOT");
		} else if (strcmp(k, "@list") == 0) {
			/* List all values in dictionary. */
			aura_dict_rewind(d);
			while (!aura_dict_eof(d)) {
				aura_dict_get_current_key(d, &rk, &rk_len),
				aura_dict_fetch(d, rk, rk_len, &rv, &rv_len);
				printf("+ %s -> %s\n", (char *)rk, (char *)rv);
				aura_dict_next(d);
			}
		} else {
			printf("value> ");
			fgets(v, 255, stdin);
			if (strlen(v) > 0)
				v[strlen(v) - 1] = '\0';
			aura_dict_fetch(d, k, strlen(k) + 1, &rv, &rv_len);
			if (rv == NULL) {
				printf("*NOT FOUND*\n");
			} else {
				printf("%s -> %s\n", k, (char *)rv);
			}
			aura_dict_store(d, k, strlen(k) + 1, v, strlen(v) + 1);
		}
	}

	aura_dict_free(d);

	return(0);
}
