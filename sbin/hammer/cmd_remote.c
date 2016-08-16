/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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

#include "hammer.h"

#define REMOTE_MAXARGS	256

/*
 * Execute SSH_ORIGINAL_COMMAND if it matches
 */
void
hammer_cmd_sshremote(const char *cmd, const char *target)
{
	char *env, *str, *dup;
	const char *av[REMOTE_MAXARGS + 1];
	int ac;

	if ((env = getenv("SSH_ORIGINAL_COMMAND")) == NULL) {
		fprintf(stderr, "SSH_ORIGINAL_COMMAND env missing\n");
		exit(1);
	}
	dup = env = strdup(env);
	av[0] = "hammer";
	av[1] = "-R";
	av[2] = cmd;
	av[3] = "-T";
	av[4] = target;
	ac = 5;

	str = strsep(&env, " \t\r\n");
	if (str == NULL) {
		fprintf(stderr, "hammer-remote: null command\n");
		exit(1);
	}
	if (strstr(str, "hammer") == NULL) {
		fprintf(stderr, "hammer-remote: Command not 'hammer'\n");
		exit(1);
	}

	while (ac < REMOTE_MAXARGS) {
		av[ac] = strsep(&env, " \t\r\n");
		++ac;
		if (av[ac - 1] == NULL)
			break;
	}
	free(dup);
	execv("/sbin/hammer", (void *)av);
	fprintf(stderr, "hammer-remote: execv failed\n");
	exit(1);
}
