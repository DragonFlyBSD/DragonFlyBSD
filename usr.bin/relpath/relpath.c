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
 * $DragonFly: src/usr.bin/relpath/relpath.c,v 1.1 2003/09/08 23:38:44 dillon Exp $
 *
 * relpath base path
 *
 * This program returns a path for 'path' which is relative to the specified
 * base.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int ac, char **av)
{
    char *path;
    char *rbase;
    struct stat st1;
    struct stat st2;
    int error;

    if (ac != 3) {
	fprintf(stderr, "%s base path\n", av[0]);
	fprintf(stderr, "\treturns a relative path for path from base\n");
	exit(1);
    }
    if (stat(av[1], &st1) < 0) {
	fprintf(stderr, "Unable to stat %s\n", av[1]);
	exit(1);
    }
    path = strdup(av[2]);
    rbase = path + strlen(path);
    for (;;) {
	while (rbase > path && rbase[-1] != '/')
	    --rbase;
	if (rbase == path)
	    break;
	--rbase;
	*rbase = 0;
	error = stat(path, &st2);
	*rbase = '/';
	if (error == 0 && 
	    st2.st_dev == st1.st_dev &&
	    st2.st_ino == st1.st_ino
	) {
		printf("%s\n", rbase + 1);
		exit(0);
	}
    }
    fprintf(stderr, "%s is not a subdirectory of %s\n", av[2], av[1]);
    exit(1);
}

