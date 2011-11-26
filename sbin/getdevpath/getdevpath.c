/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fstab.h>

static void usage(void);

int
main(int ac, char **av)
{
	int flags = 0;
	int i;
	int ch;
	int quietopt = 0;
	int error;
	char *path;

	while ((ch = getopt(ac, av, "r")) != -1) {
		switch(ch) {
		case 'r':
			flags |= GETDEVPATH_RAWDEV;
			break;
		case 'q':
			quietopt = 1;
			break;
		default:
			usage();
			/* NOT REACHED */
			break;
		}
	}
	ac -= optind;
	av += optind;
	error = 0;
	for (i = 0; i < ac; ++i) {
		path = getdevpath(av[i], flags);
		if (quietopt) {
			if (path) {
				printf("%s\n", path);
			} else {
				error = 1;
				printf("\n");
			}
		} else {
			if (path) {
				printf("%-20s %s\n", av[i], path);
			} else {
				printf("%-20s <unknown>\n", av[i]);
				error = 1;
			}
		}
	}
	exit (error);
}

static void
usage(void)
{
	fprintf(stderr, "usage: getdevpath [-qr] devname ...\n");
	exit(1);
}
