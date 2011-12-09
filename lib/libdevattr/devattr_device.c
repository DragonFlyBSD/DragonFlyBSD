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
#include <sys/stat.h>
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

struct udev_device {
	struct udev	*udev_ctx;
	prop_dictionary_t	dict;
	int	ev_type;
	int	refs;
};

struct udev_device *
udev_device_new_from_dictionary(struct udev *udev_ctx, prop_dictionary_t dict)
{
	struct udev_device *udev_dev;

	udev_dev = malloc(sizeof(struct udev_device));
	if (udev_dev == NULL)
		return NULL;

	udev_dev->refs = 1;
	udev_dev->ev_type = UDEV_EVENT_NONE;

	if (dict != NULL)
		prop_object_retain(dict);

	udev_dev->dict = dict;
	udev_dev->udev_ctx = udev_ref(udev_ctx);

	return udev_dev;
}

struct udev_device *
udev_device_ref(struct udev_device *udev_device)
{
	atomic_add_int(&udev_device->refs, 1);

	return udev_device;
}

void
udev_device_unref(struct udev_device *udev_device)
{
	int refcount;

	refcount = atomic_fetchadd_int(&udev_device->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_device->refs, 0x400); /* in destruction */
		if (udev_device->dict != NULL)
			prop_object_release(udev_device->dict);

		udev_unref(udev_device->udev_ctx);
		free(udev_device);
	}
}

void
udev_device_set_action(struct udev_device *udev_device, int action)
{
	udev_device->ev_type = action;
}

const char *
udev_device_get_action(struct udev_device *udev_device)
{
	const char *action;

	switch (udev_device->ev_type) {
	case UDEV_EVENT_ATTACH:
		action = "add";
		break;

	case UDEV_EVENT_DETACH:
		action = "remove";
		break;

	default:
		action = "none";
		break;
	}

	return action;
}

dev_t
udev_device_get_devnum(struct udev_device *udev_device)
{
	prop_number_t pn;
	dev_t devnum;

	if (udev_device->dict == NULL)
		return 0;

	pn = prop_dictionary_get(udev_device->dict, "devnum");
	if (pn == NULL)
		return 0;

	devnum = prop_number_unsigned_integer_value(pn);

	return devnum;
}

uint64_t
udev_device_get_kptr(struct udev_device *udev_device)
{
	prop_number_t pn;
	uint64_t kptr;

	if (udev_device->dict == NULL)
		return 0;

	pn = prop_dictionary_get(udev_device->dict, "kptr");
	if (pn == NULL)
		return 0;

	kptr = prop_number_unsigned_integer_value(pn);

	return kptr;
}

int32_t
udev_device_get_major(struct udev_device *udev_device)
{
	prop_number_t pn;
	int32_t major;

	if (udev_device->dict == NULL)
		return 0;

	pn = prop_dictionary_get(udev_device->dict, "major");
	if (pn == NULL)
		return 0;

	major = (int32_t)prop_number_integer_value(pn);

	return major;
}

int32_t
udev_device_get_minor(struct udev_device *udev_device)
{
	prop_number_t pn;
	int32_t minor;

	if (udev_device->dict == NULL)
		return 0;

	pn = prop_dictionary_get(udev_device->dict, "minor");
	if (pn == NULL)
		return 0;

	minor = (int32_t)prop_number_integer_value(pn);

	return minor;
}

const char *
udev_device_get_devnode(struct udev_device *udev_device)
{
	dev_t devnum;

	devnum = udev_device_get_devnum(udev_device);
	if (devnum == 0)
		return 0;

	return devname(devnum, S_IFCHR);
}

const char *
udev_device_get_property_value(struct udev_device *udev_device,
				const char *key)
{
	prop_object_t	po;
	prop_number_t	pn;
	prop_string_t	ps;
	static char buf[128]; /* XXX: might cause trouble */
	const char *str = NULL;

	if (udev_device->dict == NULL)
		return NULL;

	if ((po = prop_dictionary_get(udev_device->dict, key)) == NULL)
		return NULL;

	if (prop_object_type(po) == PROP_TYPE_STRING) {
		ps = po;
		str = __DECONST(char *, prop_string_cstring_nocopy(ps));
	} else if (prop_object_type(po) == PROP_TYPE_NUMBER) {
		pn = po;
		if (prop_number_unsigned(pn)) {
			snprintf(buf, sizeof(buf), "%" PRIu64,
			    prop_number_unsigned_integer_value(pn));
		} else {
			snprintf(buf, sizeof(buf), "%" PRIi64,
			    prop_number_integer_value(pn));
		}
		str = buf;
	}
	return str;
}

const char *
udev_device_get_subsystem(struct udev_device *udev_device)
{
	return udev_device_get_property_value(udev_device, "subsystem");
}

const char *
udev_device_get_driver(struct udev_device *udev_device)
{
	return udev_device_get_property_value(udev_device, "driver");
}

prop_dictionary_t
udev_device_get_dictionary(struct udev_device *udev_device)
{
	return udev_device->dict;
}

struct udev *
udev_device_get_udev(struct udev_device *udev_device)
{
	return udev_device->udev_ctx;
}

