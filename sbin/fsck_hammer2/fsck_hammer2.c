/*
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsck_hammer2.h"

int DebugOpt;
int ForceOpt;
int VerboseOpt;
int QuietOpt;
int CountEmpty;
int ScanBest;
int ScanPFS;
int NumPFSNames;
char **PFSNames;

static void
init_pfs_names(const char *names)
{
	char *name, *p;
	int siz = 32;

	PFSNames = calloc(siz, sizeof(char *));
	p = strdup(names);

	while ((name = p) != NULL) {
		p = strchr(p, ',');
		if (p)
			*p++ = 0;
		if (NumPFSNames > siz - 1) {
			siz *= 2;
			PFSNames = realloc(PFSNames, siz * sizeof(char*));
		}
		if (strlen(name))
			PFSNames[NumPFSNames++] = name;
	}

	if (DebugOpt) {
		int i;
		for (i = 0; i < NumPFSNames; i++)
			printf("PFSNames[%d]=\"%s\"\n", i, PFSNames[i]);
	}
}

static void
cleanup_pfs_names(void)
{
	int i;

	for (i = 0; i < NumPFSNames; i++)
		free(PFSNames[i]);
	free(PFSNames);
}

static void
usage(void)
{
	fprintf(stderr, "fsck_hammer2 [-f] [-v] [-q] [-e] [-b] [-p] "
	    "[-l pfs_name] special\n");
	exit(1);
}

int
main(int ac, char **av)
{
	int ch;

	while ((ch = getopt(ac, av, "dfvqebpl:")) != -1) {
		switch(ch) {
		case 'd':
			DebugOpt = 1;
			break;
		case 'f':
			ForceOpt = 1;
			break;
		case 'v':
			if (QuietOpt)
				--QuietOpt;
			else
				++VerboseOpt;
			break;
		case 'q':
			if (VerboseOpt)
				--VerboseOpt;
			else
				++QuietOpt;
			break;
		case 'e':
			CountEmpty = 1;
			break;
		case 'b':
			ScanBest = 1;
			break;
		case 'p':
			ScanPFS = 1;
			break;
		case 'l':
			init_pfs_names(optarg);
			break;
		default:
			usage();
			/* not reached */
			break;
		}
	}

	ac -= optind;
	av += optind;
	if (ac < 1) {
		usage();
		/* not reached */
	}

	if (test_hammer2(av[0]) == -1)
		exit(1);

	if (NumPFSNames)
		cleanup_pfs_names();

	return 0;
}
