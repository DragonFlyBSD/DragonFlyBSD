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
 * $DragonFly: src/usr.bin/objformat/objformat.c,v 1.5 2004/01/23 22:30:25 joerg Exp $
 */

#include <err.h>
#include <objformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OBJFORMAT	0
#define COMPILER	1
#define BINUTILS	2

struct command {
	const char *cmd;
	int type;
};

static struct command commands[] = {
	{"CC",		COMPILER},
	{"c++",		COMPILER},
	{"c++filt",	COMPILER},
	{"cc",		COMPILER},
	{"cpp",		COMPILER},
	{"f77",		COMPILER},
	{"g++",		COMPILER},
	{"gcc",		COMPILER},
	{"gcov",	COMPILER},
	{"addr2line",	BINUTILS},
	{"ar",		BINUTILS},
	{"as",		BINUTILS},
	{"gasp",	BINUTILS},
	{"gdb",		BINUTILS},
	{"ld",		BINUTILS},
	{"nm",		BINUTILS},
	{"objcopy",	BINUTILS},
	{"objdump",	BINUTILS},
	{"ranlib",	BINUTILS},
	{"size",	BINUTILS},
	{"strings",	BINUTILS},
	{"strip",	BINUTILS},
	{"objformat",	OBJFORMAT},
	{0, 0}
};

int
main(int argc, char **argv)
{
	struct command *cmds = commands;
	char objformat[32];
	char *path, *chunk;
	char *cmd, *newcmd = NULL;
	char *objformat_path;
	const char *env_name = NULL;
	const char *env_value;
	const char *env_default = NULL;
	const char *base_path = NULL;
	int use_objformat = 0;

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

	for (;cmds->cmd; cmds++)
		if (strcmp(cmd, cmds->cmd) == 0)
		{
			switch (cmds->type)
			{
			case COMPILER:
				env_name = "CCVER";
				env_default = "gcc2";
				base_path = "/usr/bin";
				use_objformat = 0;
				break;
			case BINUTILS:
				env_name = "BINUTILSVER";
				env_default = "gcc2";
				base_path = "/usr/libexec";
				use_objformat = 1;
				break;
			case OBJFORMAT:
				break;
			default:
				err(1, "unknown command type");
			}
			break;
		}

	/*
	 * The objformat command itself doesn't need another exec
	 */
	if (cmds->type == OBJFORMAT) {
		if (argc != 1) {
			fprintf(stderr, "Usage: objformat\n");
			exit(1);
		}

		printf("%s\n", objformat);
		exit(0);
	}

	/*
	 * make buildworld glue and CCVER overrides.
	 */
	objformat_path = getenv("OBJFORMAT_PATH");
	if (objformat_path == NULL)
		objformat_path = "";

	if ((env_value = getenv(env_name)) == NULL)
		env_value = env_default;

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
		if (use_objformat)
			asprintf(&newcmd, "%s%s/%s/%s/%s",
				chunk, base_path, env_value, objformat, cmd);
		else
			asprintf(&newcmd, "%s%s/%s/%s",
				chunk, base_path, env_value, cmd);
		if (newcmd == NULL)
			err(1, "cannot allocate memory");

		argv[0] = newcmd;
		execv(newcmd, argv);
	}
	if (use_objformat)
		err(1, "in path [%s]%s/%s/%s/%s",
			objformat_path, base_path, env_value, objformat, cmd);
	else
		err(1, "in path [%s]%s/%s/%s",
			objformat_path, base_path, env_value, cmd);
}

