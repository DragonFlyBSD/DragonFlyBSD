/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/types.h>
#include <sys/device.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <sys/un.h>
#include <cpu/inttypes.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include <libprop/proplib.h>
#include <sys/udev.h>
#include "udevd.h"

struct cmd_function cmd_fn[] = {
		{ .cmd = "getdevs", .fn = client_cmd_getdevs},
		{ .cmd = "monitor", .fn = client_cmd_monitor},
		{NULL, NULL}
};

static void *client_thread(void *arg);

void
handle_new_connection(int s)
{
	struct client_info *cli_info;
	struct sockaddr_un addr;
	int fd;
	socklen_t saddr_len = sizeof(struct sockaddr_un);

	fd = accept(s, (struct sockaddr *)&addr, &saddr_len);
	if (fd < 0) {
		syslog(LOG_ERR, "uh, oh, accept failed with %d", errno);
		return;
	}

	block_descriptor(fd);
	cli_info = malloc(sizeof(struct client_info));
	memset(cli_info, 0, sizeof(struct client_info));

	cli_info->fd = fd;
	pthread_create(&cli_info->tid, NULL, client_thread, (void *)cli_info);
}


static void *
client_thread(void *arg)
{
	prop_dictionary_t	dict;
	prop_string_t		ps;
	prop_object_t		po;
	struct client_info	*cli;
	char	*xml;
	int	r, n, error;

	r = pthread_detach(pthread_self());
	assert(r == 0);

	r = ignore_signal(SIGPIPE);
	if (r != 0)
		err(1, "could not ignore_signal SIGPIPE");

	cli = (struct client_info *)arg;
	for (;;) {
		n = read_xml(cli->fd, &xml);
		if (n == 0)
			goto cli_disconnect;
		else if (n < 0)
			goto error_out;

		xml[n+1] = '\0';

		dict = prop_dictionary_internalize(xml);
		free(xml);

		if (dict == NULL) {
			syslog(LOG_ERR, "internalization of received XML failed");
			goto error_out;
		}

		po = prop_dictionary_get(dict, "command");
		if (po == NULL || prop_object_type(po) != PROP_TYPE_STRING) {
			syslog(LOG_ERR, "received dictionary doesn't contain a key 'command'");
			prop_object_release(dict);
			continue;
		}

		ps = po;

		syslog(LOG_DEBUG, "Received command: %s (from fd = %d)\n", prop_string_cstring_nocopy(ps), cli->fd);
		for(n = 0; cmd_fn[n].cmd != NULL; n++) {
			if (prop_string_equals_cstring(ps, cmd_fn[n].cmd))
				break;
		}

		if (cmd_fn[n].cmd != NULL) {
			error = cmd_fn[n].fn(cli, dict);
			if (error) {
				prop_object_release(dict);
				goto error_out;
			}
		}
		prop_object_release(dict);
	}

error_out:

cli_disconnect:
	close(cli->fd);
	cli->fd = -1;
	free(cli);
	return NULL;
}

int
client_cmd_getdevs(struct client_info *cli, prop_dictionary_t cli_dict)
{
	struct pdev_array_entry *pae;
	struct udev_monitor	*udm;
	prop_object_iterator_t	iter;
	prop_dictionary_t	dict;
	prop_object_t	po;
	prop_array_t	pa;
	char *xml;
	ssize_t r;
	int filters;


	pa = NULL;
	po = prop_dictionary_get(cli_dict, "filters");
	if ((po != NULL) && prop_object_type(po) == PROP_TYPE_ARRAY) {
		pa = po;
		filters = 1;
	} else {
		filters = 0;
	}

	pae = pdev_array_entry_get_last();
	if (pae == NULL)
		return 1;

	if (filters) {
		udm = udev_monitor_init(cli, pa);
		if (udm == NULL) {
			pdev_array_entry_unref(pae);
			return 1;
		}

		pa = prop_array_create_with_capacity(10);

		iter = prop_array_iterator(pae->pdev_array);
		if (iter == NULL) {
			pdev_array_entry_unref(pae);
			udev_monitor_free(udm);
			return 1;
		}

		while ((dict = prop_object_iterator_next(iter)) != NULL) {
			if (match_event_filter(udm, dict)) {
				prop_array_add(pa, dict);
			}
		}

		prop_object_iterator_release(iter);
		udev_monitor_free(udm);
	} else {
		pa = pae->pdev_array;
	}

	xml = prop_array_externalize(pa);
	if (filters)
		prop_object_release(pa);

	pdev_array_entry_unref(pae);

	if (xml == NULL)
		return 1;

	r = send_xml(cli->fd, xml);
	if (r < 0)
		syslog(LOG_DEBUG, "error while send_xml (cmd_getdevs)\n");
	if (r == 0)
		syslog(LOG_DEBUG, "EOF while send_xml (cmd_getdevs)\n");

	free(xml);

	return 0;
}
