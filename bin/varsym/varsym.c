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
 * $DragonFly: src/bin/varsym/varsym.c,v 1.1 2003/11/05 23:29:37 dillon Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/varsym.h>

int
main(int ac, char **av)
{
    int i;
    int mask =  VARSYM_ALL_MASK;
    int level = VARSYM_USER;
    int nary = 0;
    int mary = 8;
    int deleteOpt = 0;
    int verboseOpt = 1;
    char **ary;

    ary = malloc(sizeof(char *) * mary);
    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (*ptr != '-') {
	    ary[nary++] = ptr;
	    if (nary == mary) {
		mary *= 2;
		ary = realloc(ary, sizeof(char *) * mary);
	    }
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'q':
	    verboseOpt = 0;
	    break;
	case 'd':
	    deleteOpt = 1;
	    break;
	case 's':
	    mask = VARSYM_SYS_MASK;
	    level = VARSYM_SYS;
	    break;
	case 'u':
	    mask = VARSYM_USER_MASK;
	    level = VARSYM_USER;
	    break;
	case 'p':
	    mask = VARSYM_PROC_MASK;
	    level = VARSYM_PROC;
	    break;
	}
    }
    for (i = 0; i < nary; ++i) {
	char *name = ary[i];
	char *data = strchr(name, '=');
	int error;
	char buf[MAXVARSYM_DATA];

	if (data)
	    *data++ = 0;

	if (deleteOpt) {
	    error = varsym_set(level, name, NULL);
	} else if (data) {
	    error = varsym_set(level, name, data);
	} else {
	    error = varsym_get(mask, name, buf, sizeof(buf));
	    if (error >= 0 && error <= (int)sizeof(buf)) {
		if (verboseOpt)
		    printf("%s=", name);
		printf("%s\n", buf);
	    }
	}
	if (error < 0)
	    fprintf(stderr, "%s: %s\n", name, strerror(errno));
    }
    return(0);
}

