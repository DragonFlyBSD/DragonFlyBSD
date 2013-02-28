/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/bin/varsym/varsym.c,v 1.4 2003/12/11 20:33:49 dillon Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/varsym.h>

static void dumpvars(char *buf, int bytes);
static int doexec(char **av);
static void usage(void);

int
main(int ac, char **av)
{
	int i;
	int mask =  VARSYM_ALL_MASK;
	int level = VARSYM_USER;
	int deleteOpt = 0;
	int verboseOpt = 1;
	int allOpt = 0;
	int execok = 0;
	int ret = 0;

	while ((i = getopt(ac, av, "adhpqsux")) != -1) {
		switch (i) {
		case 'a':
			allOpt = 1;
			break;
		case 'd':
			deleteOpt = 1;
			break;
		case 'p':
			mask = VARSYM_PROC_MASK;
			level = VARSYM_PROC;
			break;
		case 'q':
			verboseOpt = 0;
			break;
		case 's':
			mask = VARSYM_SYS_MASK;
			level = VARSYM_SYS;
			break;
		case 'u':
			mask = VARSYM_USER_MASK;
			level = VARSYM_USER;
			break;
		case 'x':
			mask = VARSYM_PROC_MASK;
			level = VARSYM_PROC;
			execok = 1;
			break;
		case 'h':
		default:
			usage();
			return(-1);
		}
	}

	if (allOpt) {
		char buf[1024];
		int marker = 0;
		int bytes;

		for (;;) {
			bytes = varsym_list(level, buf, sizeof(buf), &marker);
			if (bytes < 0)		/* error occured */
			    break;
			dumpvars(buf, bytes);
			if (marker < 0)		/* no more vars */
			    break;
		}
		if (bytes < 0) {
			fprintf(stderr, "varsym_list(): %s\n", strerror(errno));
			return 1;
		}
	}

	for ( ; optind < ac; optind++) {
		char *name = av[optind];
		char *data = strchr(name, '=');
		int error;
		char buf[MAXVARSYM_DATA];

		if (data)
			*data++ = 0;

		if (execok) {
			if (deleteOpt) {
				usage();
				exit(1);
			}
			if (data) {
				error = varsym_set(level, name, data);
				if (error)
					ret = 2;
			} else {
				error = doexec(av + optind);
			}
		} else if (deleteOpt) {
			error = varsym_set(level, name, NULL);
			if (error)
				ret = 2;
		} else if (data) {
			error = varsym_set(level, name, data);
			if (error)
				ret = 2;
		} else {
			error = varsym_get(mask, name, buf, sizeof(buf));
			if (error >= 0 && error <= (int)sizeof(buf)) {
				if (verboseOpt)
					printf("%s=", name);
				printf("%s\n", buf);
			} else {
				ret = 1;
			}
		}
		if (error < 0 && verboseOpt)
			fprintf(stderr, "%s: %s\n", name, strerror(errno));
	}

	return ret;
}

static void
dumpvars(char *buf, int bytes)
{
	int b;
	int i;
	char *vname = NULL;
	char *vdata = NULL;

	for (b = i = 0; i < bytes; ++i) {
		if (buf[i] == 0) {
			if (vname == NULL) {
				vname = buf + b;
			} else {
				vdata = buf + b;
				printf("%s=%s\n", vname, vdata);
				vname = vdata = NULL;
			}
			b = i + 1;
		}
	}
}

static int
doexec(char **av)
{
	int error;

	error = execvp(av[0], av);
	return (error);
}

static void
usage(void)
{
	fprintf(stderr, "usage: varsym [-adpqsu] [var[=data] ...]\n");
}

