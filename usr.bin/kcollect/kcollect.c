/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/kcollect.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int
main(int ac __unused, char **av __unused)
{
	size_t bytes = 0;
	kcollect_t *ary;
	int i;
	int j;
	char fmt;
	uintmax_t scale __unused;
	uintmax_t value;

	sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
	if (bytes == 0) {
		fprintf(stderr, "kern.collect_data not available\n");
		exit(1);
	}
	ary = malloc(bytes);
	sysctlbyname("kern.collect_data", ary, &bytes, NULL, 0);

	for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
		if (ary[1].data[j]) {
			printf("%8.8s ", (char *)&ary[1].data[j]);
		}
	}
	printf("\n");

	for (i = 2; i * sizeof(*ary) < bytes; ++i) {
		for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
			if (ary[1].data[j] == 0)
				continue;
			value = ary[i].data[j];
			scale = KCOLLECT_GETSCALE(ary[0].data[j]);
			fmt = KCOLLECT_GETFMT(ary[0].data[j]);
			switch(fmt) {
			case '2':
				/*
				 * fractional x100
				 */
				printf("%5ju.%02ju", value / 100, value % 100);
				break;
			case 'p':
				/*
				 * Percentage fractional x100 (100% = 10000)
				 */
				printf("%4ju.%02ju%%",
					value / 100, value % 100);
				break;
			case 'm':
				/*
				 * Megabytes
				 */
				printf("%8juM", (uintmax_t)value);
				break;
			case 'c':
				/*
				 * Raw count over period (this is not total)
				 */
				printf("%8ju", (uintmax_t)value);
				break;
			case 'b':
				/*
				 * Total bytes (this is a total)
				 */
				printf("%8ju", (uintmax_t)value);
				break;
			default:
				printf("        ");
				break;
			}
			printf(" ");
		}
		printf("\n");
	}
	return 0;
}
