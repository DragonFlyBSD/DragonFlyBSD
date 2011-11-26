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

struct udev_enumerate {
	struct udev	*udev_ctx;
	prop_array_t	ev_filt;
	prop_array_t	pa;
	int	refs;
	TAILQ_HEAD(, udev_list_entry)	list_entries;
};

struct udev_list_entry {
	struct udev	*udev_ctx;
	prop_dictionary_t	dict;
	TAILQ_ENTRY(udev_list_entry)	link;
};

struct udev_enumerate *
udev_enumerate_new(struct udev *udev_ctx)
{
	struct udev_enumerate *udev_enum;

	udev_enum = malloc(sizeof(struct udev_enumerate));

	udev_enum->refs = 1;
	udev_enum->ev_filt = NULL;
	udev_enum->pa = NULL;
	TAILQ_INIT(&udev_enum->list_entries);
	udev_enum->udev_ctx = udev_ref(udev_ctx);

	return udev_enum;
}

struct udev_enumerate *
udev_enumerate_ref(struct udev_enumerate *udev_enum)
{
	atomic_add_int(&udev_enum->refs, 1);

	return udev_enum;
}

void
udev_enumerate_unref(struct udev_enumerate *udev_enum)
{
	struct udev_list_entry	*le;
	int refcount;

	refcount = atomic_fetchadd_int(&udev_enum->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_enum->refs, 0x400); /* in destruction */
		if (udev_enum->pa != NULL)
			prop_object_release(udev_enum->pa);
		if (udev_enum->ev_filt != NULL)
			prop_object_release(udev_enum->ev_filt);

		while (!TAILQ_EMPTY(&udev_enum->list_entries)) {
			le = TAILQ_FIRST(&udev_enum->list_entries);
			TAILQ_REMOVE(&udev_enum->list_entries, le, link);
			prop_object_release(le->dict);
			free(le);
		}
		udev_unref(udev_enum->udev_ctx);
		free(udev_enum);
	}
}

struct udev *
udev_enumerate_get_udev(struct udev_enumerate *udev_enum)
{
	return udev_enum->udev_ctx;
}

int
udev_enumerate_scan_devices(struct udev_enumerate *udev_enum)
{
	prop_array_t	pa;

	if (udev_get_fd(udev_enum->udev_ctx) == -1)
		return -1;

	pa = udevd_request_devs(udev_get_fd(udev_enum->udev_ctx), udev_enum->ev_filt);
	if (pa == NULL)
		return -1;

	prop_object_retain(pa);

	if (udev_enum->pa != NULL)
		prop_object_release(udev_enum->pa);

	udev_enum->pa = pa;

	return 0;
}

struct udev_list_entry *
udev_enumerate_get_list_entry(struct udev_enumerate *udev_enum)
{
	struct udev_list_entry *le;
	prop_object_iterator_t	iter;
	prop_dictionary_t	dict;

	/* If the list is not empty, assume it was populated in an earlier call */
	if (!TAILQ_EMPTY(&udev_enum->list_entries))
		return TAILQ_FIRST(&udev_enum->list_entries);

	iter = prop_array_iterator(udev_enum->pa);
	if (iter == NULL)
		return NULL;

	while ((dict = prop_object_iterator_next(iter)) != NULL) {
		le = malloc(sizeof(struct udev_list_entry));
		if (le == NULL)
			goto out;

		prop_object_retain(dict);
		le->dict = dict;
		le->udev_ctx = udev_enum->udev_ctx;
		TAILQ_INSERT_TAIL(&udev_enum->list_entries, le, link);
	}

	le = TAILQ_FIRST(&udev_enum->list_entries);

out:
	prop_object_iterator_release(iter);
	return le;
}

prop_array_t
udev_enumerate_get_array(struct udev_enumerate *udev_enum)
{
	return udev_enum->pa;
}

struct udev_list_entry *
udev_list_entry_get_next(struct udev_list_entry *list_entry)
{
	return TAILQ_NEXT(list_entry, link);
}

prop_dictionary_t
udev_list_entry_get_dictionary(struct udev_list_entry *list_entry)
{
	return list_entry->dict;
}

struct udev_device *
udev_list_entry_get_device(struct udev_list_entry *list_entry)
{
	struct udev_device *udev_dev;

	udev_dev = udev_device_new_from_dictionary(list_entry->udev_ctx,
						   list_entry->dict);

	return udev_dev;
}

int
udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enum,
						const char *subsystem)
{
	int ret;

	ret = _udev_enumerate_filter_add_match_gen(udev_enum,
						 EVENT_FILTER_TYPE_WILDCARD,
						 0,
						 "subsystem",
						 __DECONST(char *, subsystem));

	return ret;
}

int
udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *udev_enum,
						const char *subsystem)
{
	int ret;

	ret = _udev_enumerate_filter_add_match_gen(udev_enum,
						 EVENT_FILTER_TYPE_WILDCARD,
						 1,
						 "subsystem",
						 __DECONST(char *, subsystem));

	return ret;
}

int
udev_enumerate_add_match_expr(struct udev_enumerate *udev_enum,
			      const char *key,
			      char *expr)
{
	int ret;

	ret = _udev_enumerate_filter_add_match_gen(udev_enum,
						 EVENT_FILTER_TYPE_WILDCARD,
						 0,
						 key,
						 expr);

	return ret;
}

int
udev_enumerate_add_nomatch_expr(struct udev_enumerate *udev_enum,
			        const char *key,
			        char *expr)
{
	int ret;

	ret = _udev_enumerate_filter_add_match_gen(udev_enum,
						 EVENT_FILTER_TYPE_WILDCARD,
						 1,
						 key,
						 expr);

	return ret;
}

int
udev_enumerate_add_match_regex(struct udev_enumerate *udev_enum,
			      const char *key,
			      char *expr)
{
	int ret;

	ret = _udev_enumerate_filter_add_match_gen(udev_enum,
						 EVENT_FILTER_TYPE_REGEX,
						 0,
						 key,
						 expr);

	return ret;
}

int
udev_enumerate_add_nomatch_regex(struct udev_enumerate *udev_enum,
			        const char *key,
			        char *expr)
{
	int ret;

	ret = _udev_enumerate_filter_add_match_gen(udev_enum,
						 EVENT_FILTER_TYPE_REGEX,
						 1,
						 key,
						 expr);

	return ret;
}

int
_udev_enumerate_filter_add_match_gen(struct udev_enumerate *udev_enum,
				     int type,
				     int neg,
				     const char *key,
				     char *expr)
{
	prop_array_t		pa;
	int error;

	if (udev_enum->ev_filt == NULL) {
		pa = prop_array_create_with_capacity(5);
		if (pa == NULL)
			return -1;

		udev_enum->ev_filt = pa;
	}

	error = _udev_filter_add_match_gen(udev_enum->ev_filt, type, neg, key, expr);

	return error;
}
