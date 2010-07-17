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
#include <assert.h>

#include <ctype.h>
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

#define MONITOR_LOCK()		pthread_mutex_lock(&monitor_lock)
#define MONITOR_UNLOCK()	pthread_mutex_unlock(&monitor_lock)

static int _parse_filter_prop(struct udev_monitor *udm, prop_array_t pa);
static int match_filter(struct event_filter *evf, prop_dictionary_t dict);

static int WildCaseCmp(const char *w, const char *s);
static int wildCaseCmp(const char **mary, int d, const char *w, const char *s);

TAILQ_HEAD(udev_monitor_list_head, udev_monitor)	udev_monitor_list;
pthread_mutex_t	monitor_lock;


void
monitor_queue_event(prop_dictionary_t ev_dict)
{
	struct udev_monitor		*udm;
	struct udev_monitor_event	*udm_ev;

	MONITOR_LOCK();

	TAILQ_FOREACH(udm, &udev_monitor_list, link) {
		udm_ev = malloc(sizeof(struct udev_monitor_event));
		if (udm_ev == NULL)
			continue;

		prop_object_retain(ev_dict);
		udm_ev->ev_dict = ev_dict;

		if (match_event_filter(udm,
		    prop_dictionary_get(udm_ev->ev_dict, "evdict")) == 0) {
			prop_object_release(ev_dict);
			free(udm_ev);
			continue;
		}

		pthread_mutex_lock(&udm->q_lock);
		TAILQ_INSERT_TAIL(&udm->ev_queue, udm_ev, link);
		pthread_cond_signal(&udm->cond);
		pthread_mutex_unlock(&udm->q_lock);
	}

	MONITOR_UNLOCK();
}

struct udev_monitor *
udev_monitor_init(struct client_info *cli, prop_array_t filters)
{
	struct udev_monitor *udm;
	int error;

	udm = malloc(sizeof(struct udev_monitor));
	if (udm == NULL)
		return NULL;

	TAILQ_INIT(&udm->ev_queue);
	TAILQ_INIT(&udm->ev_filt);

	pthread_mutex_init(&udm->q_lock, NULL);
	pthread_cond_init(&udm->cond, NULL);
	udm->cli = cli;

	if (filters != NULL) {
		error = _parse_filter_prop(udm, filters);
		/* XXX: ignore error for now */
	}

	return udm;
}

void
udev_monitor_free(struct udev_monitor *udm)
{
	struct event_filter	*evf;
	struct udev_monitor_event	*udm_ev;

	pthread_mutex_lock(&udm->q_lock);

	while ((udm_ev = TAILQ_FIRST(&udm->ev_queue)) != NULL) {
		prop_object_release(udm_ev->ev_dict);
		udm_ev->ev_dict = NULL;
		TAILQ_REMOVE(&udm->ev_queue, udm_ev, link);
		free(udm_ev);
	}

	while ((evf = TAILQ_FIRST(&udm->ev_filt)) != NULL) {
		TAILQ_REMOVE(&udm->ev_filt, evf, link);
		free(evf->key);
		if (evf->type == EVENT_FILTER_TYPE_WILDCARD)
			free(evf->wildcard_match);
		else if (evf->type == EVENT_FILTER_TYPE_REGEX)
			regfree(&evf->regex_match);
		free(evf);
	}

	pthread_mutex_unlock(&udm->q_lock);
	free(udm);
}

int
client_cmd_monitor(struct client_info *cli, prop_dictionary_t dict)
{
	prop_array_t	pa;
	prop_object_t	po;
	struct udev_monitor	*udm;
	struct udev_monitor_event	*udm_ev;
	struct timespec abstime;
	struct pollfd fds[1];
	char *xml;
	ssize_t r;
	int ret, ok, dummy;

	pa = NULL;
	po = prop_dictionary_get(dict, "filters");
	if ((po != NULL) && prop_object_type(po) == PROP_TYPE_ARRAY) {
		pa = po;
	}

	udm = udev_monitor_init(cli, pa);
	if (udm == NULL)
		return 1;

	ok = 1;
	fds[0].fd = cli->fd;
	fds[0].events = POLLRDNORM;

	MONITOR_LOCK();
	TAILQ_INSERT_TAIL(&udev_monitor_list, udm, link);
	MONITOR_UNLOCK();

	pthread_mutex_lock(&udm->q_lock);
	while (ok) {
		clock_gettime(CLOCK_REALTIME,&abstime);
		abstime.tv_sec += 2;
		ret = pthread_cond_timedwait(&udm->cond, &udm->q_lock, &abstime);

		if (ret == EINVAL) {
			syslog(LOG_ERR, "pthread_cond_timedwait error: EINVAL");
			goto end_nofree;
		}

		if ((ret = poll(fds, 1, 0)) > 0) {
			ret = recv(fds[0].fd, &dummy, sizeof(dummy), MSG_DONTWAIT);
			if ((ret == 0) || ((ret < 0) && (errno != EAGAIN)))
				goto end_nofree;
		}

		udm_ev = TAILQ_FIRST(&udm->ev_queue);
		if (udm_ev == NULL)
			continue;

		assert(udm_ev->ev_dict != NULL);
		xml = prop_dictionary_externalize(udm_ev->ev_dict);
		if (xml == NULL)
			continue;

		prop_object_release(udm_ev->ev_dict);
		udm_ev->ev_dict = NULL;
		TAILQ_REMOVE(&udm->ev_queue, udm_ev, link);
		free(udm_ev);

		r = send_xml(cli->fd, xml);
		if (r <= 0)
			goto end;

		free(xml);
		continue;
end:
		free(xml);
end_nofree:
		pthread_mutex_unlock(&udm->q_lock);
		close(cli->fd);
		ok = 0;
		
	}

	MONITOR_LOCK();
	TAILQ_REMOVE(&udev_monitor_list, udm, link);
	MONITOR_UNLOCK();

	udev_monitor_free(udm);

	return 1;
}

static int
_parse_filter_prop(struct udev_monitor *udm, prop_array_t pa)
{
	prop_string_t	ps;
	prop_number_t	pn;
	prop_object_iterator_t	iter;
	prop_dictionary_t	dict;
	struct event_filter *evf;
	int error;

	iter = prop_array_iterator(pa);
	if (iter == NULL)
		return -1;

	while ((dict = prop_object_iterator_next(iter)) != NULL) {
		evf = malloc(sizeof(struct event_filter));
		bzero(evf, sizeof(struct event_filter));
		if (evf == NULL)
			goto error_alloc;

		ps = prop_dictionary_get(dict, "key");
		if (ps == NULL)
			goto error_out;
		evf->key = prop_string_cstring(ps);
		if (evf->key == NULL)
			goto error_out;

		pn = prop_dictionary_get(dict, "type");
		if (pn == NULL)
			goto error_out_ps;

		ps = prop_dictionary_get(dict, "expr");
		if (ps == NULL)
			goto error_out_ps;

		if (prop_dictionary_get(dict, "negative"))
			evf->neg = 1;
		else
			evf->neg = 0;

		evf->type = prop_number_integer_value(pn);
		switch (evf->type) {
		case EVENT_FILTER_TYPE_WILDCARD:
			evf->wildcard_match = prop_string_cstring(ps);
			if (evf->wildcard_match == NULL)
				goto error_out_ps;
			break;

		case EVENT_FILTER_TYPE_REGEX:
			error = regcomp(&evf->regex_match, prop_string_cstring_nocopy(ps), REG_ICASE | REG_NOSUB);
			if (error)
				goto error_out_ps;
			break;

		default:
			goto error_out_ps;
		}

		pthread_mutex_lock(&udm->q_lock);
		TAILQ_INSERT_TAIL(&udm->ev_filt, evf, link);
		pthread_mutex_unlock(&udm->q_lock);

	}

	prop_object_iterator_release(iter);
	return 0;

error_out_ps:
	free(evf->key);
error_out:
	free(evf);
error_alloc:
	prop_object_iterator_release(iter);
	return -1;
}

/*
Event filter format:
<array>
<dictionary>
<key>key</key>
<value>(e.g. kptr, devnum, ...)</value>
<key>type</key>
<value>(e.g. wildcard or regex)</value>
<key>expr</key>
<value>(regex)</value>
</dictionary>
... repeat ...
</array>
*/

static int
match_filter(struct event_filter *evf, prop_dictionary_t ev_dict)
{
	prop_object_t		po;
	prop_string_t		ps;
	prop_number_t		pn;
	char *str;
	char buf[128];
	int ret;

	if (ev_dict == NULL)
		return 0;

	prop_object_retain(ev_dict);

	if ((po = prop_dictionary_get(ev_dict, evf->key)) == NULL)
		goto no_match;

	if (prop_object_type(po) == PROP_TYPE_STRING) {
		ps = po;
		str = __DECONST(char *, prop_string_cstring_nocopy(ps));
	} else if (prop_object_type(po) == PROP_TYPE_NUMBER) {
		pn = po;
		if (prop_number_unsigned(pn)) {
			snprintf(buf, sizeof(buf), "%" PRIu64, prop_number_unsigned_integer_value(pn));
		} else {
			snprintf(buf, sizeof(buf), "%" PRIi64, prop_number_integer_value(pn));
		}
		str = buf;
	} else {
		syslog(LOG_DEBUG, "Unexpected type in match_filter: %d\n", prop_object_type(po));
		/* Unexpected type */
		goto no_match;
	}

	switch (evf->type) {
	case EVENT_FILTER_TYPE_WILDCARD:
		ret = WildCaseCmp(evf->wildcard_match, str);

		if (ret != 0)
			goto no_match;

		break;
	case EVENT_FILTER_TYPE_REGEX:
		ret = regexec(&evf->regex_match, str, 0, NULL, 0);

		if (ret != 0)
			goto no_match;
		break;
	default:
		goto no_match;
	}

	prop_object_release(ev_dict);
	return 1;

no_match:
	prop_object_release(ev_dict);
	return 0;
}

int
match_event_filter(struct udev_monitor *udm, prop_dictionary_t ev_dict)
{
	struct event_filter *evf;
	int all_negative = 1;

	pthread_mutex_lock(&udm->q_lock);

	if (TAILQ_EMPTY(&udm->ev_filt))
		return 1;

	TAILQ_FOREACH(evf, &udm->ev_filt, link) {
		//printf("match_event_filter 3\n");
		if (evf->neg == 0)
			all_negative = 0;

		if (match_filter(evf, ev_dict)) {
			pthread_mutex_unlock(&udm->q_lock);
			return (1 ^ evf->neg); /* return 1; or 0 for 'nomatch' hit */
		}
		//printf("match_event_filter 5\n");
	}

	pthread_mutex_unlock(&udm->q_lock);
	return (all_negative == 1)?1:0;
}

static int
WildCaseCmp(const char *w, const char *s)
{
    int i;
    int c;
    int slen = strlen(s);
    const char **mary;

    for (i = c = 0; w[i]; ++i) {
	if (w[i] == '*')
	    ++c;
    }
    mary = malloc(sizeof(char *) * (c + 1));
    if (mary == NULL)
	     return -1;

    for (i = 0; i < c; ++i)
	mary[i] = s + slen;
    i = wildCaseCmp(mary, 0, w, s);
    free(mary);
    return(i);
}

/*
 * WildCaseCmp() - compare wild string to sane string, case insensitive
 *
 *	Returns 0 on success, -1 on failure.
 */
static int
wildCaseCmp(const char **mary, int d, const char *w, const char *s)
{
    int i;

    /*
     * skip fixed portion
     */
    for (;;) {
	switch(*w) {
	case '*':
	    /*
	     * optimize terminator
	     */
	    if (w[1] == 0)
		return(0);
	    if (w[1] != '?' && w[1] != '*') {
		/*
		 * optimize * followed by non-wild
		 */
		for (i = 0; s + i < mary[d]; ++i) {
		    if (s[i] == w[1] && wildCaseCmp(mary, d + 1, w + 1, s + i) == 0)
			return(0);
		}
	    } else {
		/*
		 * less-optimal
		 */
		for (i = 0; s + i < mary[d]; ++i) {
		    if (wildCaseCmp(mary, d + 1, w + 1, s + i) == 0)
			return(0);
		}
	    }
	    mary[d] = s;
	    return(-1);
	case '?':
	    if (*s == 0)
		return(-1);
	    ++w;
	    ++s;
	    break;
	default:
	    if (*w != *s) {
		if (tolower(*w) != tolower(*s))
		    return(-1);
	    }
	    if (*w == 0)	/* terminator */
		return(0);
	    ++w;
	    ++s;
	    break;
	}
    }
    /* not reached */
    return(-1);
}
