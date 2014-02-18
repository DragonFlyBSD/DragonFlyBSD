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
#include "udevd.h"

int debugopt = 0;

static int udevfd;
static int hangup_ongoing = 0;
static struct pollfd fds[NFDS];

extern pthread_mutex_t	monitor_lock;
extern TAILQ_HEAD(udev_monitor_list_head, udev_monitor)	udev_monitor_list;
extern TAILQ_HEAD(pdev_array_list_head, pdev_array_entry)	pdev_array_list;

static void usage(void);

int match_dev_dict(prop_dictionary_t, prop_dictionary_t);
prop_dictionary_t find_dev_dict(int64_t, prop_dictionary_t, int *);

void udev_read_event(int);
prop_array_t udev_getdevs(int);

static
void
usage(void)
{
	fprintf(stderr, "usage: udevd [-d]\n");
	exit(1);
}

int
match_dev_dict(prop_dictionary_t dict, prop_dictionary_t match_dict)
{
	prop_number_t	pn, pn2;
	prop_string_t	ps, ps2;

	if (dict == NULL)
		return 0;

	if ((ps = prop_dictionary_get(dict, "name")) == NULL)
		return 0;
	if ((ps2 = prop_dictionary_get(match_dict, "name")) == NULL)
		return 0;
	if (!prop_string_equals(ps, ps2))
		return 0;

	if ((pn = prop_dictionary_get(dict, "devnum")) == NULL)
		return 0;
	if ((pn2 = prop_dictionary_get(match_dict, "devnum")) == NULL)
		return 0;
	if (!prop_number_equals(pn, pn2))
		return 0;

	if ((pn = prop_dictionary_get(dict, "kptr")) == NULL)
		return 0;
	if ((pn2 = prop_dictionary_get(match_dict, "kptr")) == NULL)
		return 0;
	if (!prop_number_equals(pn, pn2))
		return 0;

	return 1;
}

prop_dictionary_t
find_dev_dict(int64_t generation, prop_dictionary_t match_dict, int *idx)
{
	struct pdev_array_entry	*pae;
	prop_array_t		pa;
	prop_object_iterator_t	iter;
	prop_dictionary_t	dict;
	int i = 0;

	if (generation == -1)
		pae = pdev_array_entry_get_last();
	else
		pae = pdev_array_entry_get(generation);

	if (pae == NULL)
		return NULL;

	pa = pae->pdev_array;

	iter = prop_array_iterator(pa);
	if (iter == NULL) {
		pdev_array_entry_unref(pae);
		return NULL;
	}

	while ((dict = prop_object_iterator_next(iter)) != NULL) {
		if (match_dev_dict(dict, match_dict))
			break;
		++i;
	}

	prop_object_iterator_release(iter);

	if (idx != NULL)
		*idx = i;

	pdev_array_entry_unref(pae);
	return dict;
}

void
udev_read_event(int fd)
{
	struct pdev_array_entry	*pae;
	prop_dictionary_t	dict, evdict, devdict;
	prop_number_t		pn;
	prop_string_t		ps;
	prop_object_t		po;
	prop_array_t		pa;
	char	*xml;
	int	n, idx, evtype;
	size_t	sz;

	sz = 4096 * 1024;

	xml = malloc(sz); /* 4 MB */
again:
	if ((n = read(fd, xml, sz)) <= 0) {
		if (errno == ENOMEM) {
			sz <<= 2;
			if ((xml = realloc(xml, sz)) == NULL) {
				syslog(LOG_ERR, "could not realloc xml memory");
				return;
			}
			goto again;
		}
		free(xml);
		return;
	}

	dict = prop_dictionary_internalize(xml);
	free(xml);
	if (dict == NULL) {
		syslog(LOG_ERR, "internalization of xml failed");
		return;
	}

	pn = prop_dictionary_get(dict, "evtype");
	if (pn == NULL) {
		syslog(LOG_ERR, "read_event: no key evtype");
		goto out;
	}

	evtype = prop_number_integer_value(pn);

	evdict = prop_dictionary_get(dict, "evdict");
	if (evdict == NULL) {
		syslog(LOG_ERR, "read_event: no key evdict");
		goto out;
	}

	switch (evtype) {
	case UDEV_EVENT_ATTACH:
		monitor_queue_event(dict);
		pae = pdev_array_entry_get_last();
		pa = prop_array_copy(pae->pdev_array);
		pdev_array_entry_unref(pae);
		if (pa == NULL)
			goto out;
		prop_array_add(pa, evdict);
		pdev_array_entry_insert(pa);
		break;

	case UDEV_EVENT_DETACH:
		monitor_queue_event(dict);
		if ((devdict = find_dev_dict(-1, evdict, &idx)) == NULL)
			goto out;
		pae = pdev_array_entry_get_last();
		pa = prop_array_copy(pae->pdev_array);
		pdev_array_entry_unref(pae);
		if (pa == NULL)
			goto out;
		prop_array_remove(pa, idx);
		pdev_array_entry_insert(pa);
		break;

	case UDEV_EV_KEY_UPDATE:
		if ((devdict = find_dev_dict(-1, evdict, NULL)) == NULL)
			goto out;
		if ((ps = prop_dictionary_get(evdict, "key")) == NULL)
			goto out;
		if ((po = prop_dictionary_get(evdict, "value")) == NULL)
			goto out;
		/* prop_object_retain(po); */ /* not necessary afaik */
		prop_dictionary_set(devdict, prop_string_cstring_nocopy(ps), po);
		break;

	case UDEV_EV_KEY_REMOVE:
		if ((devdict = find_dev_dict(-1, evdict, NULL)) == NULL)
			goto out;
		if ((ps = prop_dictionary_get(evdict, "key")) == NULL)
			goto out;
		prop_dictionary_remove(devdict, prop_string_cstring_nocopy(ps));
		break;

	default:
		syslog(LOG_ERR, "read_event: unknown evtype %d", evtype);
	}

out:
	prop_object_release(dict);
	return;
}

prop_array_t
udev_getdevs(int devfd)
{
	prop_dictionary_t	pd, rpd;
	prop_string_t		ps;
	prop_array_t		pa;

	pd = prop_dictionary_create();
	if (pd == NULL) {
		err(1, "prop_dictionary_create()");
	}

	ps = prop_string_create_cstring("getdevs");
	if (ps == NULL) {
		prop_object_release(pd);
		err(1, "prop_string_create_cstring()");
	}

	if (prop_dictionary_set(pd, "command", ps) == false) {
		prop_object_release(ps);
		prop_object_release(pd);
		err(1, "prop_dictionary_set()");
	}

	prop_object_release(ps);

	/* Send dictionary to kernel space */
	if (prop_dictionary_sendrecv_ioctl(pd, devfd, UDEVPROP, &rpd) != 0)
		err(1, "prop_array_recv_ioctl()");

	prop_object_release(pd);

	pa = prop_dictionary_get(rpd, "array");
	if (pa == NULL)
		goto out;
	prop_object_retain(pa);

out:
	prop_object_release(rpd);
	return pa;
}

static void
killed(int sig __unused)
{
	syslog(LOG_ERR, "udevd stopped");
	unlink("/var/run/udevd.pid");
	pdev_array_clean();
	exit(0);
}

static void
hangup(int sig __unused)
{
	FILE *pidf;
	int s;

	syslog(LOG_ERR, "udevd hangup+resume");

	pidf = fopen("/var/run/udevd.pid", "w");
	if (pidf != NULL) {
		fprintf(pidf, "%ld\n", (long)getpid());
		fclose(pidf);
	}

	hangup_ongoing = 1;
	close(fds[UDEV_SOCKET_FD_IDX].fd);
	pdev_array_clean();
	s = init_local_server(LISTEN_SOCKET_FILE, SOCK_STREAM, 0);
	if (s < 0)
		err(1, "init_local_server");

	fds[UDEV_SOCKET_FD_IDX].fd = s;
	pdev_array_entry_insert(udev_getdevs(udevfd));
	hangup_ongoing = 0;
}

int
ignore_signal(int signum)
{
	struct sigaction act;
	int ret;

	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	ret = sigaction(signum, &act, NULL);
	return ret;
}

static int
set_signal(int signum, sig_t sig_func)
{
	struct sigaction act;
	int ret;

	act.sa_handler = sig_func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	ret = sigaction(signum, &act, NULL);
	return ret;
}

int main(int argc, char *argv[])
{
	int error __unused, i, r, s;
	FILE *pidf;
	int ch = 0;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch(ch) {
		case 'd':
			debugopt = 1;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;

	TAILQ_INIT(&pdev_array_list);
	TAILQ_INIT(&udev_monitor_list);

	r = ignore_signal(SIGPIPE);
	if (r != 0)
		err(1, "could not ignore_signal SIGPIPE");

	r = pthread_mutex_init(&(monitor_lock), NULL);
	if (r != 0)
		err(1, "could not allocate a pthread_mutex");

	if ((udevfd = open(UDEV_DEVICE_PATH, O_RDWR | O_NONBLOCK)) == -1)
		err(1, "%s", UDEV_DEVICE_PATH);
	unblock_descriptor(udevfd);

	s = init_local_server(LISTEN_SOCKET_FILE, SOCK_STREAM, 0);
	if (s < 0)
		err(1, "init_local_server");

	pidf = fopen("/var/run/udevd.pid", "w");
#if 0
	if (pidf == NULL)
		err(1, "pidfile");
#endif

	set_signal(SIGTERM, killed);
	set_signal(SIGHUP, hangup);

	if (debugopt == 0)
		if (daemon(0, 0) == -1)
			err(1, "daemon");

	if (pidf != NULL) {
		fprintf(pidf, "%ld\n", (long)getpid());
		fclose(pidf);
	}

	syslog(LOG_ERR, "udevd started");

	pdev_array_entry_insert(udev_getdevs(udevfd));

	memset(fds, 0 , sizeof(fds));
	fds[UDEV_DEVICE_FD_IDX].fd = udevfd;
	fds[UDEV_DEVICE_FD_IDX].events = POLLIN;
	fds[UDEV_SOCKET_FD_IDX].fd = s;
	fds[UDEV_SOCKET_FD_IDX].events = POLLIN | POLLPRI;

	for (;;) {
		r = poll(fds, NFDS, -1);
		if (r < 0) {
			if (hangup_ongoing == 0) {
				if (errno == EINTR) {
					usleep(5000);
					continue;
				} else {
					err(1, "polling...");
				}
			} else {
				usleep(20000); /* 20 ms */
				continue;
			}
		}

		for (i = 0; (i < NFDS) && (r > 0); i++) {
			if (fds[i].revents == 0)
				continue;

			--r;
			switch (i) {
			case UDEV_DEVICE_FD_IDX:
				udev_read_event(udevfd);
				break;
			case UDEV_SOCKET_FD_IDX:
				handle_new_connection(s);
				break;
			default:
				break;
			}
		}
	}

	syslog(LOG_ERR, "udevd is exiting normally");
	return 0;
}
