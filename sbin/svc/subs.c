/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
/*
 * Support routines
 */

#include "svc.h"

void
sfree(char **strp)
{
	if (*strp)
		free(*strp);
}

void
sreplace(char **strp, const char *orig)
{
	if (*strp) {
		free(*strp);
		*strp = NULL;
	}
	if (orig)
		*strp = strdup(orig);
}

void
sdup(char **strp)
{
	if (*strp)
		*strp = strdup(*strp);
}

void
afree(char ***aryp)
{
	char **ary = *aryp;
	int i;

	if (ary) {
		for (i = 0; ary[i]; ++i)
			free(ary[i]);
		free(ary);
	}
	*aryp = NULL;
}

void
adup(char ***aryp)
{
	char **ary = *aryp;
	char **nary;
	int i;

	if (ary) {
		for (i = 0; ary[i]; ++i)
			;
		nary = calloc(sizeof(char *), i + 1);
		bcopy(ary, nary, sizeof(char *) * (i + 1));
		for (i = 0; nary[i]; ++i)
			nary[i] = strdup(nary[i]);
		*aryp = nary;
	}
}

/*
 * Sets up the pidfile and unix domain socket.  We do not yet know the
 * pid to store in the pidfile.
 */
int
setup_pid_and_socket(command_t *cmd, int *lfdp, int *pfdp)
{
	struct sockaddr_un sou;
	size_t len;
	char *pidfile;

	/*
	 * Create and test the pidfile.
	 */
	asprintf(&pidfile, "%s/service.%s.pid", cmd->piddir, cmd->label);
	*lfdp = -1;
	*pfdp = open(pidfile, O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0644);
	if (*pfdp < 0) {
		if (errno == EWOULDBLOCK) {
			fprintf(cmd->fp, "Cannot init, %s is already active\n",
				cmd->label);
		} else {
			fprintf(cmd->fp,
				"Cannot init, unable to create \"%s\": %s\n",
				cmd->label,
				strerror(errno));
		}
		free(pidfile);
		return 1;
	}
	ftruncate(*pfdp, 0);

	/*
	 * Create the unix-domain socket.
	 */
	bzero(&sou, sizeof(sou));
	if ((*lfdp = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0) {
		sou.sun_family = AF_UNIX;
		snprintf(sou.sun_path, sizeof(sou.sun_path),
			 "%s/service.%s.sk", cmd->piddir, cmd->label);
		len = strlen(sou.sun_path);
		len = offsetof(struct sockaddr_un, sun_path[len+1]);

		/* remove stale file before trying to bind */
		remove(sou.sun_path);

		if (bind(*lfdp, (void *)&sou, len) < 0) {
			fprintf(cmd->fp, "Unable to bind \"%s\"\n",
				sou.sun_path);
			close(*lfdp);
			*lfdp = -1;
		} else if (listen(*lfdp, 32) < 0) {
			fprintf(cmd->fp, "Unable to listen on \"%s\"\n",
				sou.sun_path);
			close(*lfdp);
			*lfdp = -1;
		}
	} else {
		fprintf(cmd->fp, "Unable to create unix-domain socket\n");
	}
	if (*lfdp >= 0) {
		return 0;
	} else {
		close(*pfdp);
		*pfdp = -1;

		return 1;
	}
}

void
remove_pid_and_socket(command_t *cmd, const char *label)
{
	char *path;

	asprintf(&path, "%s/service.%s.pid", cmd->piddir, label);
	remove(path);
	asprintf(&path, "%s/service.%s.sk", cmd->piddir, label);
	remove(path);
}
