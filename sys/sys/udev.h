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
#ifndef _SYS_UDEV_H_
#define	_SYS_UDEV_H_

#include <sys/ioccom.h>

#if defined(_KERNEL)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_CONF_H_
#include <sys/conf.h>
#endif

int	udev_dict_set_cstr(cdev_t dev, const char *key, char *str);
int	udev_dict_set_int(cdev_t dev, const char *key, int64_t val);
int	udev_dict_set_uint(cdev_t dev, const char *key, uint64_t val);
int	udev_dict_delete_key(cdev_t dev, const char *key);

int	udev_event_attach(cdev_t dev, char *name, int alias);
int	udev_event_detach(cdev_t dev, char *name, int alias);

#endif /* _KERNEL */

#define UDEVPROP _IOWR('U', 0xBA, struct plistref)
#define UDEV_EVENT_NONE		0x00
#define UDEV_EVENT_ATTACH	0x01
#define	UDEV_EVENT_DETACH	0x02

#define	UDEV_EV_KEY_UPDATE	0x11
#define	UDEV_EV_KEY_REMOVE	0x12

#define EVENT_FILTER_TYPE_WILDCARD	0
#define EVENT_FILTER_TYPE_REGEX		1

#define	LISTEN_SOCKET_FILE	"/tmp/udevd.socket"
#define SOCKFILE_NAMELEN	strlen(LISTEN_SOCKET_FILE)+1

struct udev_event {
	int	ev_type;
	prop_dictionary_t	ev_dict;
};

#endif /* _SYS_DSCHED_H_ */
