/*-
 * Copyright (c) 1998, Peter Wemm <peter@netplex.com.au>
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
 * $FreeBSD: src/usr.bin/objformat/objformat.c,v 1.6 1998/10/24 02:01:30 jdp Exp $
 * $DragonFly: src/usr.bin/objformat/objformat.c,v 1.3 2004/01/16 07:45:22 dillon Exp $
 */

#include <err.h>
#include <objformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	char objformat[32];
	char *path, *chunk;
	char *cmd, *newcmd = NULL;
	char *objformat_path;
	char *gccver;
	char *dirprefix;
	char *dirpostfix;

	if (getobjformat(objformat, sizeof objformat, &argc, argv) == -1)
		errx(1, "Invalid object format");

	/*
	 * Get the last path elemenet of the program name being executed
	 */
	cmd = strrchr(argv[0], '/');
	if (cmd != NULL)
		cmd++;
	else
		cmd = argv[0];

	/*
	 * Directory prefix
	 */
	if (strcmp(cmd, "c++") == 0 ||
		   strcmp(cmd, "cc") == 0 ||
		   strcmp(cmd, "cpp") == 0 ||
		   strcmp(cmd, "f77") == 0 ||
		   strcmp(cmd, "g++") == 0 ||
		   strcmp(cmd, "gcc") == 0 ||
		   strcmp(cmd, "gcov") == 0
	) {
		dirprefix = "/usr/bin";
		dirpostfix = "";
	} else {
		dirprefix = "/usr/libexec";
		asprintf(&dirpostfix, "/%s", objformat);
	}

	/*
	 * The objformat command itself doesn't need another exec
	 */
	if (strcmp(cmd, "objformat") == 0) {
		if (argc != 1) {
			fprintf(stderr, "Usage: objformat\n");
			exit(1);
		}
		printf("%s\n", objformat);
		exit(0);
	}

	/*
	 * make buildweorld glue and GCCVER overrides.
	 */
	objformat_path = getenv("OBJFORMAT_PATH");
	if (objformat_path == NULL)
		objformat_path = "";
	if ((gccver = getenv("GCCVER")) == NULL) {
		gccver = "gcc2";
	} else if (gccver[0] >= '0' && gccver[0] <= '9') {
	    asprintf(&gccver, "gcc%s", gccver);
	}
	path = strdup(objformat_path);

	setenv("OBJFORMAT", objformat, 1);

	/*
	 * objformat_path could be sequence of colon-separated paths.
	 */
	while ((chunk = strsep(&path, ":")) != NULL) {
		if (newcmd != NULL) {
			free(newcmd);
			newcmd = NULL;
		}
		asprintf(&newcmd, "%s%s/%s%s/%s",
			chunk, dirprefix, gccver, dirpostfix, cmd);
		if (newcmd == NULL)
			err(1, "cannot allocate memory");

		argv[0] = newcmd;
		execv(newcmd, argv);
	}
	err(1, "in path [%s]%s/%s%s/%s",
		objformat_path, dirprefix, gccver, dirpostfix, cmd);
}

