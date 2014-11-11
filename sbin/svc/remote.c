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
 * Handle remote listen/connect and parsing operations.
 */

#include "svc.h"

typedef struct SvcConnect {
	struct SvcConnect *next;
	command_t *cmd;
	char	  *label;
	pthread_t td;
	int	active;
	int	fd;
	int	rc;
	FILE	*fpr;
	FILE	*fpw;
} connect_t;

static void *remote_connect_thread(void *arg);
static void *remote_listener_thread(void *arg);
static void *remote_accepted_thread(void *arg);
static void remote_issue(connect_t *conn, command_t *cmd);
static int decode_args(connect_t *conn, char ***avp,
			const char *ptr, size_t len);

connect_t *CHead;
connect_t **CNextP = &CHead;


/*
 * Execute cmd on the running service by connecting to the service, passing-in
 * the command, and processing results.
 *
 * Called only by master process
 */
void
remote_execute(command_t *cmd, const char *label)
{
	connect_t *conn = calloc(sizeof(*conn), 1);

	conn->fd = -1;
	conn->cmd = cmd;
	conn->label = strdup(label);
	conn->active = 1;
	conn->next = *CNextP;
	*CNextP = conn;

	pthread_create(&conn->td, NULL, remote_connect_thread, conn);
}

/*
 * Threaded connect/execute
 */
static
void *
remote_connect_thread(void *arg)
{
	connect_t *conn;
	command_t *cmd;
	struct sockaddr_un sou;
	size_t len;
	char *ptr;

	conn = arg;
	cmd = conn->cmd;

	bzero(&sou, sizeof(sou));
	if ((conn->fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0) {
		sou.sun_family = AF_UNIX;
		snprintf(sou.sun_path, sizeof(sou.sun_path),
			 "%s/service.%s.sk", cmd->piddir, conn->label);
		len = strlen(sou.sun_path);
		len = offsetof(struct sockaddr_un, sun_path[len+1]);

		if (connect(conn->fd, (void *)&sou, len) < 0) {
			close(conn->fd);
			conn->fd = -1;
		}
	}
	if (conn->fd >= 0) {
		/*
		 * Issue command
		 */
		conn->fpr = fdopen(conn->fd, "r");
		conn->fpw = fdopen(dup(conn->fd), "w");
		conn->fd = -1;
		setvbuf(conn->fpr, NULL, _IOFBF, 0);
		setvbuf(conn->fpw, NULL, _IOFBF, 0);
		remote_issue(conn, cmd);
		while ((ptr = fgetln(conn->fpr, &len)) != NULL) {
			if (len == 2 && ptr[0] == '.' && ptr[1] == '\n')
				break;
			fwrite(ptr, 1, len, cmd->fp);
			fflush(cmd->fp);
		}
		conn->rc = 0;
		conn->active = 0;
		fclose(conn->fpr);
		fclose(conn->fpw);
		conn->fpr = NULL;
		conn->fpw = NULL;
	} else {
		/*
		 * Connection failed
		 */
		fprintf(cmd->fp,
			"Unable to connect to service %s\n",
			conn->label);
		conn->rc = 1;
		conn->active = 0;
		if (cmd->force_remove_files) {
			fprintf(cmd->fp,
				"Removing pid and socket files for %s\n",
				conn->label);
			remove_pid_and_socket(cmd, conn->label);
		}
	}
	return NULL;
}

/*
 * Called only by master process
 *
 * Collect status from all remote commands.
 */
int
remote_wait(void)
{
	connect_t *scan;
	int rc = 0;

	while ((scan = CHead) != NULL) {
		if (pthread_join(scan->td, NULL) < 0)
			continue;
		assert(scan->active == 0);
		rc += scan->rc;
		CHead = scan->next;

		if (scan->fpr) {
			fclose(scan->fpr);
			scan->fpr = NULL;
		}
		if (scan->fpw) {
			fclose(scan->fpw);
			scan->fpw = NULL;
		}
		if (scan->fd >= 0) {
			close(scan->fd);
			scan->fd = -1;
		}
		if (scan->label) {
			free(scan->label);
			scan->label = NULL;
		}
		scan->cmd = NULL;
		free(scan);
	}
	return rc;
}

/*
 * Create the unix domain socket and pid file for the service
 * and start a thread to accept and process connections.
 *
 * Return 0 on success, non-zero if the socket could not be created.
 */
void
remote_listener(command_t *cmd, int lfd)
{
	connect_t *conn;

	/*
	 * child, create our unix domain socket listener thread.
	 */
	conn = calloc(sizeof(*conn), 1);
	conn->fd = lfd;
	conn->cmd = cmd;
	conn->label = strdup(cmd->label);
	conn->active = 1;

	conn->next = *CNextP;
	*CNextP = conn;
	pthread_create(&conn->td, NULL, remote_listener_thread, conn);
}

static void *
remote_listener_thread(void *arg)
{
	connect_t *lconn = arg;
	connect_t *conn;
	struct sockaddr_un sou;
	socklen_t len;

	conn = calloc(sizeof(*conn), 1);
	for (;;) {
		len = sizeof(sou);
		conn->fd = accept(lconn->fd, (void *)&sou, &len);
		conn->label = strdup(lconn->label);
		if (conn->fd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		pthread_create(&conn->td, NULL, remote_accepted_thread, conn);
		conn = calloc(sizeof(*conn), 1);
	}
	free(conn);

	return NULL;
}

static void *
remote_accepted_thread(void *arg)
{
	connect_t *conn = arg;
	command_t cmd;
	char *ptr;
	size_t len;
	int rc;
	int ac;
	char **av;

	pthread_detach(conn->td);
	conn->fpr = fdopen(conn->fd, "r");
	conn->fpw = fdopen(dup(conn->fd), "w");
	conn->fd = -1;
	setvbuf(conn->fpr, NULL, _IOFBF, 0);
	setvbuf(conn->fpw, NULL, _IOFBF, 0);

	while ((ptr = fgetln(conn->fpr, &len)) != NULL) {
		ac = decode_args(conn, &av, ptr, len);
		rc = process_cmd(&cmd, conn->fpw, ac, av);
		cmd.cmdline = 0;	/* we are the remote */
		cmd.commanded = 1;	/* commanded action (vs automatic) */
		sreplace(&cmd.label, conn->label);
		if (rc == 0) {
			pthread_mutex_lock(&serial_mtx);
			rc = execute_cmd(&cmd);
			pthread_mutex_unlock(&serial_mtx);
		}
		free_cmd(&cmd);
		afree(&av);
		fwrite(".\n", 2, 1, conn->fpw);
		fflush(conn->fpw);
	}
	fclose(conn->fpr);
	fclose(conn->fpw);
	conn->fpr = NULL;
	conn->fpw = NULL;
	free(conn->label);
	free(conn);

	return NULL;
}

/*
 * Issue the command to the remote, encode the arguments.
 */
static
void
remote_issue(connect_t *conn, command_t *cmd)
{
	int i;

	for (i = 1; i < cmd->orig_ac; ++i) {
		const char *str = cmd->orig_av[i];

		if (i != 1)
			putc(' ', conn->fpw);
		while (*str) {
			if (*str == ' ' || *str == '\\' || *str == '\n')
				putc('\\', conn->fpw);
			putc(*str, conn->fpw);
			++str;
		}
	}
	putc('\n', conn->fpw);
	fflush(conn->fpw);
}

/*
 * Decode arguments
 */
static int
decode_args(connect_t *conn __unused, char ***avp, const char *ptr, size_t len)
{
	char **av;
	char *arg;
	size_t i;
	size_t j;
	int acmax;
	int ac;

	if (len && ptr[len-1] == '\n')
		--len;

	acmax = 3;	/* av[0], first arg, terminating NULL */
	for (i = 0; i < len; ++i) {
		if (ptr[i] == ' ')
			++acmax;
	}
	av = calloc(sizeof(char *), acmax);
	av[0] = NULL;
	ac = 1;

	i = 0;
	while (i < len) {
		for (j = i; j < len; ++j) {
			if (ptr[j] == ' ')
				break;
		}
		arg = malloc(j - i + 1);	/* worst case arg size */
		j = 0;
		while (i < len) {
			if (ptr[i] == ' ')
				break;
			if (ptr[i] == '\\' && i + 1 < len) {
				arg[j++] = ptr[i+1];
				i += 2;
			} else {
				arg[j++] = ptr[i];
				i += 1;
			}
		}
		arg[j] = 0;
		av[ac++] = arg;
		if (i < len && ptr[i] == ' ')
			++i;
	}
	av[ac] = NULL;

#if 0
	fprintf(conn->fpw, "DECODE ARGS: ");
	for (i = 1; i < (size_t)ac; ++i)
		fprintf(conn->fpw, " \"%s\"", av[i]);
	fprintf(conn->fpw, "\n");
	fflush(conn->fpw);
#endif

	*avp = av;
	return ac;
}
