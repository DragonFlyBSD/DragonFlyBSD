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

#include <sys/queue.h>
#include <libprop/proplib.h>

#define UDEV_DEVICE_PATH	"/dev/udev"
#define	LOCAL_BACKLOG		5

#define	UDEV_DEVICE_FD_IDX	0
#define	UDEV_SOCKET_FD_IDX	1
#define	NFDS			2


struct pdev_array_entry {
	int32_t		refs;
	int64_t		generation;
	prop_array_t	pdev_array;
	TAILQ_ENTRY(pdev_array_entry)	link;
};

struct event_filter {
	int id;
	int neg;
	int type;
	char *key;
	char *wildcard_match;
	regex_t regex_match;

	TAILQ_ENTRY(event_filter)	link;
};

struct udev_monitor;

struct client_info {
	pthread_t	tid;
	int	fd;
	struct udev_monitor *udm;
	struct event_filter ev_filt;
};

struct udev_monitor_event {
	prop_dictionary_t	ev_dict;
	TAILQ_ENTRY(udev_monitor_event)	link;
};

struct udev_monitor {
	pthread_mutex_t		q_lock;
	pthread_cond_t		cond;
	struct client_info	*cli;
	TAILQ_HEAD(, event_filter)	ev_filt;
	TAILQ_HEAD(, udev_monitor_event)	ev_queue;
	TAILQ_ENTRY(udev_monitor)	link;
};

struct cmd_function {
	const char *cmd;
	int  (*fn)(struct client_info *, prop_dictionary_t);
};

/* From udevd_socket.c */
int	init_local_server(const char *sockfile, int socktype, int nonblock);
int	block_descriptor(int s);
int	unblock_descriptor(int s);
int	read_xml(int s, char **buf);
int	send_xml(int s, char *xml);

/* From udevd_pdev.c */
void	pdev_array_entry_ref(struct pdev_array_entry *pae);
void	pdev_array_entry_unref(struct pdev_array_entry *pae);
void	pdev_array_entry_insert(prop_array_t pa);
void	pdev_array_clean(void);
struct pdev_array_entry *pdev_array_entry_get(int64_t generation);
struct pdev_array_entry *pdev_array_entry_get_last(void);

/* From udevd_client.c */
void	handle_new_connection(int s);
int	client_cmd_getdevs(struct client_info *cli, prop_dictionary_t dict);


/* From udevd_monitor.c */
void	monitor_queue_event(prop_dictionary_t ev_dict);
int	client_cmd_monitor(struct client_info *cli, prop_dictionary_t dict);
int	match_event_filter(struct udev_monitor *udm, prop_dictionary_t ev_dict);
struct udev_monitor *udev_monitor_init(struct client_info *cli, prop_array_t filters);
void	udev_monitor_free(struct udev_monitor *udm);

/* From udevd.c */
int	ignore_signal(int signum);
