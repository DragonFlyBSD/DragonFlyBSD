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

struct udev_monitor {
	struct udev	*udev_ctx;
	prop_array_t	ev_filt;
	int	socket;
	int	user_socket; /* maybe... one day... */
	int	refs;
};

struct udev_monitor *
udev_monitor_new(struct udev *udev_ctx)
{
	struct udev_monitor *udev_monitor;
	int ret, s;

	ret = conn_local_server(LISTEN_SOCKET_FILE, SOCK_STREAM, 0, &s);
	if (ret < 0)
		return NULL;

	udev_monitor = malloc(sizeof(struct udev_monitor));
	if (udev_monitor == NULL)
		return NULL;

	udev_monitor->refs = 1;
	udev_monitor->ev_filt = NULL;
	udev_monitor->socket = s;
	udev_monitor->user_socket = 1;
	udev_monitor->udev_ctx = udev_ref(udev_ctx);

	return udev_monitor;
}


struct udev_monitor *
udev_monitor_ref(struct udev_monitor *udev_monitor)
{
	atomic_add_int(&udev_monitor->refs, 1);

	return udev_monitor;
}

void
udev_monitor_unref(struct udev_monitor *udev_monitor)
{
	int refcount;

	refcount = atomic_fetchadd_int(&udev_monitor->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_monitor->refs, 0x400); /* in destruction */
		if (udev_monitor->ev_filt != NULL)
			prop_object_release(udev_monitor->ev_filt);

		if (udev_monitor->socket != -1)
			close(udev_monitor->socket);
		if (udev_monitor->user_socket != -1)
			close(udev_monitor->user_socket);

		udev_unref(udev_monitor->udev_ctx);
		free(udev_monitor);
	}
}

struct udev *
udev_monitor_get_udev(struct udev_monitor *udev_monitor)
{
	return udev_monitor->udev_ctx;
}

int
udev_monitor_get_fd(struct udev_monitor *udev_monitor)
{
	return udev_monitor->socket;
}

struct udev_device *
udev_monitor_receive_device(struct udev_monitor *udev_monitor)
{
	struct udev_device *udev_dev;
	prop_dictionary_t dict, evdict;
	prop_number_t	pn;
	char *xml;
	int n;

	if ((n = read_xml(udev_monitor->socket, &xml)) <= 0)
		return NULL;

	xml[n+1] = '\0';
	dict = prop_dictionary_internalize(xml);
	free(xml);
	if (dict == NULL)
		return NULL;

	pn = prop_dictionary_get(dict, "evtype");
	if (pn == NULL) {
		prop_object_release(dict);
		return NULL;
	}

	evdict = prop_dictionary_get(dict, "evdict");
	if (evdict == NULL) {
		prop_object_release(dict);
		return NULL;
	}

	udev_dev = udev_device_new_from_dictionary(udev_monitor->udev_ctx, evdict);
	if (udev_dev == NULL) {
		prop_object_release(dict);
		return NULL;
	}

	udev_device_set_action(udev_dev, prop_number_integer_value(pn));

	prop_object_release(dict);
	return udev_dev;
}

int
udev_monitor_enable_receiving(struct udev_monitor *udev_monitor)
{
	prop_dictionary_t	dict;
	char *xml;
	int n;
	/* ->socket, ->user_socket, ->ev_filt */

	dict = udevd_get_command_dict(__DECONST(char *, "monitor"));
	if (dict == NULL)
		return -1;

	/* Add event filters to message, if available */
	if (udev_monitor->ev_filt != NULL) {
		if (prop_dictionary_set(dict, "filters",
		    udev_monitor->ev_filt) == false) {
			prop_object_release(dict);
			return -1;
		}
	}

	xml = prop_dictionary_externalize(dict);
	prop_object_release(dict);
	if (xml == NULL)
		return -1;

	n = send_xml(udev_monitor->socket, xml);
	free(xml);
	if (n <= 0)
		return -1;

	return 0;
}

int
udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *udev_monitor,
						const char *subsystem,
						const char *devtype __unused)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_WILDCARD,
						 0,
						 "subsystem",
						 __DECONST(char *, subsystem));

	return ret;
}

int
udev_monitor_filter_add_match_expr(struct udev_monitor *udev_monitor,
			      	   const char *key,
			      	   char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_WILDCARD,
						 0,
						 key,
						 expr);

	return ret;
}

int
udev_monitor_filter_add_nomatch_expr(struct udev_monitor *udev_monitor,
			      	     const char *key,
			      	     char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_WILDCARD,
						 1,
						 key,
						 expr);

	return ret;
}

int
udev_monitor_filter_add_match_regex(struct udev_monitor *udev_monitor,
			      	   const char *key,
			      	   char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_REGEX,
						 0,
						 key,
						 expr);

	return ret;
}

int
udev_monitor_filter_add_nomatch_regex(struct udev_monitor *udev_monitor,
			      	     const char *key,
			      	     char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_REGEX,
						 1,
						 key,
						 expr);

	return ret;
}

int
_udev_filter_add_match_gen(prop_array_t filters,
				   int type,
				   int neg,
				   const char *key,
				   char *expr)
{
	prop_dictionary_t	dict;
	int error;

	if (key == NULL)
		return -1;
	if (expr == NULL)
		return -1;

	dict = prop_dictionary_create();
	if (dict == NULL)
		return -1;

	error = _udev_dict_set_cstr(dict, "key", __DECONST(char *, key));
	if (error != 0)
		goto error_out;
	error = _udev_dict_set_int(dict, "type", type);
	if (error != 0)
		goto error_out;
	error = _udev_dict_set_cstr(dict, "expr", expr);
	if (error != 0)
		goto error_out;

	if (neg) {
		error = _udev_dict_set_int(dict, "negative", 1);
		if (error != 0)
			goto error_out;
	}

	if (prop_array_add(filters, dict) == false)
		goto error_out;

	return 0;

error_out:
	prop_object_release(dict);
	return -1;
}

int
_udev_monitor_filter_add_match_gen(struct udev_monitor *udev_monitor,
				   int type,
				   int neg,
				   const char *key,
				   char *expr)
{
	prop_array_t		pa;
	int error;

	if (udev_monitor->ev_filt == NULL) {
		pa = prop_array_create_with_capacity(5);
		if (pa == NULL)
			return -1;

		udev_monitor->ev_filt = pa;
	}

	error = _udev_filter_add_match_gen(udev_monitor->ev_filt, type, neg, key, expr);

	return error;
}

