/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/boot/common/rel_open.c,v 1.2 2008/09/04 17:30:52 swildner Exp $
 */

#include <stand.h>
#include <string.h>
#include "bootstrap.h"

char *DirBase;

COMMAND_SET(cd, "cd", "Change directory", command_chdir);
COMMAND_SET(optcd, "optcd",
	    "Change directory; ignore exit status", command_optchdir);

int
command_chdir(int ac, char **av)
{
	int result;

	if (ac == 1) {
		result = chdir(getenv("base"));
	} else if (ac == 2) {
		result = chdir(av[1]);
	} else {
		sprintf(command_errbuf, "usage: cd [<directory>]");
		result = CMD_ERROR;
	}
	return result;
}

int
command_optchdir(int ac, char **av)
{
	if (ac == 1) {
		chdir(getenv("base"));
	} else if (ac == 2) {
		chdir(av[1]);
	} else {
		sprintf(command_errbuf, "usage: optcd [<directory>]");
	}
	return(CMD_OK);
}

int
chdir(const char *path)
{
	struct stat st;
	char *base;
	char *p;
	char *b;
	char *s;
	char *w;
	int len;
	int dlen;
	int res;

	if (DirBase == NULL)
		DirBase = strdup("/");

	len = strlen(path);
	if (path[0] == '/') {
		base = malloc(len + 2);		/* room for trailing / */
		bcopy(path, base, len + 1);
	} else {
		while (len && path[len-1] == '/')
			--len;
		dlen = strlen(DirBase);
		base = malloc(dlen + len + 2);	/* room for trailing / */
		bcopy(DirBase, base, dlen);
		bcopy(path, base + dlen, len);
		base[dlen + len] = 0;
	}

	if (stat(base, &st) == 0 && S_ISDIR(st.st_mode)) {
		p = b = w = s = base;
		while (*s) {
			if (*s == '/') {
				if (s - b == 2 && b[0] == '.' && b[1] == '.') {
					w = p;
				} else {
					p = b;
					b = s + 1;
				}
				while (s[1] == '/')
					++s;
			}
			*w = *s;
			++w;
			++s;
		}
		if (s - b == 2 && b[0] == '.' && b[1] == '.')
			w = p;
		while (w > base && w[-1] == '/')
			--w;
		*w++ = '/';
		*w = 0;

		if (DirBase)
			free(DirBase);
		DirBase = base;
		res = CMD_OK;
	} else {
		free(base);
		sprintf(command_errbuf, "Unable to find directory");
		res = CMD_ERROR;
	}
	return (res);
}

COMMAND_SET(pwd, "pwd", "Get current directory", command_pwd);

int
command_pwd(int ac, char **av)
{
	printf("%s\n", DirBase ? DirBase : "/");
	return(0);
}

int
rel_open(const char *path, char **abspathp, int flags)
{
	int fd;
	char *ptr;

	if (DirBase == NULL)
		DirBase = strdup("/");

	if (path[0] != '/') {
		ptr = malloc(strlen(DirBase) + strlen(path) + 1);
		sprintf(ptr, "%s%s", DirBase, path);
		fd = open(ptr, flags);
		if (abspathp && fd >= 0)
			*abspathp = ptr;
		else if (abspathp)
			*abspathp = NULL;
		else
			free(ptr);
	} else {
		fd = open(path, flags);
		if (abspathp && fd >= 0)
			*abspathp = strdup(path);
		else if (abspathp)
			*abspathp = NULL;
	}
	return(fd);
}

int
rel_stat(const char *path, struct stat *st)
{
	char *ptr;
	int res;

	if (DirBase == NULL)
		DirBase = strdup("/");

	if (path[0] != '/') {
		ptr = malloc(strlen(DirBase) + strlen(path) + 1);
		sprintf(ptr, "%s%s", DirBase, path);
		res = stat(ptr, st);
		free(ptr);
	} else {
		res = stat(path, st);
	}
	return(res);
}
