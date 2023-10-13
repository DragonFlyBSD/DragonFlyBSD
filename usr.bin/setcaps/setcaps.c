/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
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
/*
 * setcaps - Set capability restrictions on the parent process of this
 * 	     program (usually a shell).
 */
#include <sys/types.h>
#include <sys/caps.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__SYSCAP_ALLSTRINGS;

static void printallcaps(void);
static void listcaps(void);

int
main(int ac, char **av)
{
	int quietopt = 0;
	int c;
	int i;
	int j;
	int estatus = 0;
	int noargs = 1;

	while ((c = getopt(ac, av, "plqh")) != -1) {
		noargs = 0;
		switch(c) {
		case 'p':
			printallcaps();
			break;
		case 'l':
			listcaps();
			break;
		case 'q':
			quietopt = 1;
			break;
		case 'h':
		default:
			if (c != 'h')
				fprintf(stderr, "Bad option: -%c\n", c);
			fprintf(stderr, "setcaps [-p] [-l] caps...\n");
			fprintf(stderr, "    -p	- list available caps\n");
			fprintf(stderr, "    -l	- list current caps\n");
			fprintf(stderr, "    -q	- ignore unknown caps\n");
			fprintf(stderr, "  caps[:es] - set specific flags\n");
			fprintf(stderr, "  If no args, current caps are "
					"listed\n");
			if (c != 'h')
				exit(1);
			exit(0);
		}
	}
	ac -= optind;
	av += optind;

	for (j = 0; j < ac; ++j) {
		char *which;
		char *scan;
		int res;
		int flags = __SYSCAP_SELF | __SYSCAP_EXEC;
		int found = 0;

		noargs = 0;

		which = av[j];
		if ((scan = strchr(which, ':')) != NULL) {
			*scan++ = 0;
			flags = 0;
			while (*scan) {
				switch (*scan) {
				case 's':
					flags |= __SYSCAP_SELF;
					break;
				case 'e':
					flags |= __SYSCAP_EXEC;
					break;
				default:
					fprintf(stderr, "unknown flag %s:%c\n",
						which, *scan);
					break;
				}
				++scan;
			}
		}
		for (i = 0; i < __SYSCAP_COUNT; ++i) {
			const char *ptr;

			ptr = SyscapAllStrings[i / 16][i & 15];
			if (ptr == NULL || strcmp(ptr, which) != 0)
				continue;
			found = 1;
			res = syscap_set(i | __SYSCAP_INPARENT, flags, NULL, 0);
			if (res < 0) {
				fprintf(stderr, "%s: %s\n",
					which, strerror(errno));
				estatus = 1;
			} else {
				printf("%s: ", which);
				if (res & __SYSCAP_EXEC)
					printf(" on-exec");
				if (res & __SYSCAP_SELF)
					printf(" on-self");
				printf("\n");
			}
		}
		if (found == 0 && quietopt == 0) {
			printf("%s: not-found\n", which);
		}
	}
	if (noargs)
		listcaps();

	return estatus;
}

static void
printallcaps(void)
{
	const char *ptr;
	int i;

	for (i = 0; i < __SYSCAP_COUNT; ++i) {
		if ((ptr = SyscapAllStrings[i / 16][i & 15]) != NULL) {
			printf("0x%04x %s\n", i, ptr);
		}
	}
}

static void
listcaps(void)
{
	int i;

	for (i = 0; i < __SYSCAP_COUNT; ++i) {
		const char *ptr;
		int res;

		res = syscap_get(i | __SYSCAP_INPARENT, NULL, 0);
		if (res < 0)
			break;
		if (res) {
			if ((ptr = SyscapAllStrings[i / 16][i & 15]) != NULL) {
				printf("%-15s", ptr);
			} else {
				printf("0x%04x         ", res);
			}
			if (res & __SYSCAP_EXEC)
				printf(" on-exec");
			if (res & __SYSCAP_SELF)
				printf(" on-self");
			printf("\n");
		}
	}
}
