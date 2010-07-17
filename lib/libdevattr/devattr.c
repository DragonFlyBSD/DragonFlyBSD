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

#include <libprop/proplib.h>
#include <sys/udev.h>
#define LIBDEVATTR_INTERNAL
#include "devattr.h"

struct udev {
	int	gp_fd;
	int	monitor_fd;
	int	refs;

	void	*userdata;
};

struct udev *
udev_ref(struct udev *udev_ctx)
{
	atomic_add_int(&udev_ctx->refs, 1);

	return udev_ctx;
}

void
udev_unref(struct udev *udev_ctx)
{
	int refcount;

	refcount = atomic_fetchadd_int(&udev_ctx->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_ctx->refs, 0x400); /* in destruction */
		if (udev_ctx->gp_fd != -1)
			close (udev_ctx->gp_fd);
		if (udev_ctx->monitor_fd != -1)
			close (udev_ctx->monitor_fd);

		free(udev_ctx);
	}
}

struct udev *
udev_new(void)
{
	struct udev *udev_ctx;
	int ret, s;

	ret = conn_local_server(LISTEN_SOCKET_FILE, SOCK_STREAM, 0, &s);
	if (ret < 0)
		return NULL;

	udev_ctx = malloc(sizeof(struct udev));

	udev_ctx->refs = 1;
	udev_ctx->gp_fd = s;
	udev_ctx->monitor_fd = -1;
	udev_ctx->userdata = NULL;

	return udev_ctx;
}

const char *udev_get_dev_path(struct udev *udev_ctx __unused)
{
	return "/dev";
}

void *
udev_get_userdata(struct udev *udev_ctx)
{
	return udev_ctx->userdata;
}

void
udev_set_userdata(struct udev *udev_ctx, void *userdata)
{
	udev_ctx->userdata = userdata;
}

int
udev_get_fd(struct udev *udev_ctx)
{
	return udev_ctx->gp_fd;
}

int
send_xml(int s, char *xml)
{
	ssize_t r,n;
	size_t sz;

	sz = strlen(xml) + 1;

	r = send(s, &sz, sizeof(sz), 0);
	if (r <= 0)
		return r;

	r = 0;
	while (r < (ssize_t)sz) {
		n = send(s, xml+r, sz-r, 0);
		if (n <= 0)
			return n;
		r += n;
	}

	return r;
}

int
read_xml(int s, char **buf)
{
	char *xml;
	size_t sz;
	int n, r;

	*buf = NULL;

	n = recv(s, &sz, sizeof(sz), MSG_WAITALL);
	if ((n <= 0) || (sz > 12*1024*1024)) /* Arbitrary limit */
		return n;

	xml = malloc(sz+2);
	r = 0;
	while (r < (ssize_t)sz) {
		n = recv(s, xml+r, sz-r, MSG_WAITALL);
		if (n <= 0) {
			free(xml);
			return n;
		}
		r += n;
	}

	*buf = xml;
	return r;
}

int
_udev_dict_set_cstr(prop_dictionary_t dict, const char *key, char *str)
{
	prop_string_t	ps;

	ps = prop_string_create_cstring(str);
	if (ps == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, ps) == false) {
		prop_object_release(ps);
		return ENOMEM;
	}

	prop_object_release(ps);
	return 0;
}

int
_udev_dict_set_int(prop_dictionary_t dict, const char *key, int64_t val)
{
	prop_number_t	pn;

	pn = prop_number_create_integer(val);
	if (pn == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, pn) == false) {
		prop_object_release(pn);
		return ENOMEM;
	}

	prop_object_release(pn);
	return 0;
}

int
_udev_dict_set_uint(prop_dictionary_t dict, const char *key, uint64_t val)
{
	prop_number_t	pn;

	pn = prop_number_create_unsigned_integer(val);
	if (pn == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, pn) == false) {
		prop_object_release(pn);
		return ENOMEM;
	}

	prop_object_release(pn);
	return 0;
}

int
conn_local_server(const char *sockfile, int socktype, int nonblock __unused,
		  int *retsock)
{
	int s;
	struct sockaddr_un serv_addr;

	*retsock = -1;
	if ((s = socket(AF_UNIX, socktype, 0)) < 0)
		return -1;

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;
	strncpy(serv_addr.sun_path, sockfile, SOCKFILE_NAMELEN);
	serv_addr.sun_path[SOCKFILE_NAMELEN - 1] = '\0';

	*retsock = s;
	return connect(s, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
}

prop_dictionary_t
udevd_get_command_dict(char *command)
{
	prop_dictionary_t	dict;
	int	error;

	dict = prop_dictionary_create();
	if (dict == NULL)
		return NULL;

	if ((error = _udev_dict_set_cstr(dict, "command", command)))
		goto error_out;

	return dict;

error_out:
	prop_object_release(dict);
	return NULL;
}

prop_array_t
udevd_request_devs(int s, prop_array_t filters)
{
	prop_array_t	pa;
	prop_dictionary_t	dict;
	char *xml;

	int n;

	dict = udevd_get_command_dict(__DECONST(char *, "getdevs"));
	if (dict == NULL)
		return NULL;

	/* Add filters to message, if available */
	if (filters != NULL) {
		if (prop_dictionary_set(dict, "filters", filters) == false) {
			prop_object_release(dict);
			return NULL;
		}
	}

	xml = prop_dictionary_externalize(dict);
	prop_object_release(dict);
	if (xml == NULL)
		return NULL;

	n = send_xml(s, xml);
	free(xml);

	if (n <= 0)
		return NULL;

	if ((n = read_xml(s, &xml)) <= 0)
		return NULL;

	xml[n+1] = '\0';
	pa = prop_array_internalize(xml);
	free(xml);
	return (pa);
}
