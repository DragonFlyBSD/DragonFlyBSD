/* $NetBSD: client.c,v 1.4 2006/09/29 20:06:11 plunky Exp $ */
/* $DragonFly: src/usr.sbin/bthcid/client.c,v 1.1 2008/01/30 14:10:19 hasso Exp $ */

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/un.h>
#include <bluetooth.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bthcid.h"

/*
 * A client is anybody who connects to our control socket to
 * receive PIN requests.
 */
struct client {
	int			fd;		/* client descriptor */
	LIST_ENTRY(client)	next;
};

/*
 * PIN cache items are made when we have sent a client pin
 * request. The event is used to expire the item.
 */
struct item {
	bdaddr_t	 laddr;			/* local device BDADDR */
	bdaddr_t	 raddr;			/* remote device BDADDR */
	uint8_t		 pin[HCI_PIN_SIZE];	/* PIN */
	int		 hci;			/* HCI socket */
	LIST_ENTRY(item) next;
};

static LIST_HEAD(,client)	client_list;
static LIST_HEAD(,item)		item_list;

#define PIN_REQUEST_TIMEOUT	30	/* Request is valid */
#define PIN_TIMEOUT		300	/* PIN is valid */

int
init_control(const char *name, mode_t mode)
{
	struct sockaddr_un	un;
	struct kevent		change;
	struct timespec		timeout = { 0, 0 };
	int			ctl;

	LIST_INIT(&client_list);
	LIST_INIT(&item_list);

	if (name == NULL)
		return 0;

	if (unlink(name) < 0 && errno != ENOENT)
		return -1;

	ctl = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (ctl < 0)
		return -1;

	memset(&un, 0, sizeof(un));
	un.sun_len = sizeof(un);
	un.sun_family = AF_LOCAL;
	strlcpy(un.sun_path, name, sizeof(un.sun_path));
	if (bind(ctl, (struct sockaddr *)&un, sizeof(un)) < 0) {
		close(ctl);
		return -1;
	}

	if (chmod(name, mode) < 0) {
		close(ctl);
		unlink(name);
		return -1;
	}

	if (listen(ctl, 10) < 0) {
		close(ctl);
		unlink(name);
		return -1;
	}

	EV_SET(&change, ctl, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(hci_kq, &change, 1, NULL, 0, &timeout) == -1) {
		close(ctl);
		unlink(name);
		return -1;
	}

	return ctl;
}

/* Process control socket event */
void
process_control(int sock)
{
	struct sockaddr_un	un;
	socklen_t		n;
	struct kevent		change;
	struct timespec		timeout = { 0, 0 };
	int			fd;
	struct client		*cl;

	n = sizeof(un);
	fd = accept(sock, (struct sockaddr *)&un, &n);
	if (fd < 0) {
		syslog(LOG_ERR, "Could not accept PIN client connection");
		return;
	}

	n = 1;
	if (ioctl(fd, FIONBIO, &n) < 0) {
		syslog(LOG_ERR, "Could not set non blocking IO for client");
		close(fd);
		return;
	}

	cl = malloc(sizeof(struct client));
	if (cl == NULL) {
		syslog(LOG_ERR, "Could not malloc client");
		close(fd);
		return;
	}

	memset(cl, 0, sizeof(struct client));
	cl->fd = fd;

	EV_SET(&change, cl->fd, EVFILT_READ, EV_ADD, 0, 0, cl);
	if (kevent(hci_kq, &change, 1, NULL, 0, &timeout) == -1) {
		syslog(LOG_ERR, "Could not add client event");
		free(cl);
		close(fd);
		return;
	}

	syslog(LOG_DEBUG, "New Client");
	LIST_INSERT_HEAD(&client_list, cl, next);
}

/* Process client response packet */
void
process_client(int sock, void *arg)
{
	bthcid_pin_response_t	 rp;
	struct sockaddr_bt	 sa;
	struct client		*cl = arg;
	struct item		*item;
	struct kevent		change;
	struct timespec		timeout = { 0, 0 };
	int			 n;

	n = recv(sock, &rp, sizeof(rp), 0);
	if (n != sizeof(rp)) {
		if (n != 0)
			syslog(LOG_ERR, "Bad Client");

		close(sock);
		LIST_REMOVE(cl, next);
		free(cl);

		syslog(LOG_DEBUG, "Client Closed");
		return;
	}

	syslog(LOG_DEBUG, "Received PIN for %s", bt_ntoa(&rp.raddr, NULL));

	LIST_FOREACH(item, &item_list, next) {
		if (bdaddr_same(&rp.laddr, &item->laddr) == 0
		    || bdaddr_same(&rp.raddr, &item->raddr) == 0)
			continue;

		EV_SET(&change, sock, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
		kevent(hci_kq, &change, 1, NULL, 0, &timeout);
		if (item->hci != -1) {
			memset(&sa, 0, sizeof(sa));
			sa.bt_len = sizeof(sa);
			sa.bt_family = AF_BLUETOOTH;
			bdaddr_copy(&sa.bt_bdaddr, &item->laddr);

			send_pin_code_reply(item->hci, &sa, &item->raddr, rp.pin);
			LIST_REMOVE(item, next);
			free(item);
			return;
		}
		goto newpin;
	}

	item = malloc(sizeof(struct item));
	if (item == NULL) {
		syslog(LOG_ERR, "Item allocation failed");
		return;
	}

	memset(item, 0, sizeof(struct item));
	bdaddr_copy(&item->laddr, &rp.laddr);
	bdaddr_copy(&item->raddr, &rp.raddr);
	LIST_INSERT_HEAD(&item_list, item, next);

newpin:
	syslog(LOG_DEBUG, "Caching PIN for %s", bt_ntoa(&rp.raddr, NULL));

	memcpy(item->pin, rp.pin, HCI_PIN_SIZE);
	item->hci = -1;

	EV_SET(&change, sock, EVFILT_TIMER, EV_ADD, 0, PIN_TIMEOUT * 1000, NULL);
	if (kevent(hci_kq, &change, 1, NULL, 0, &timeout) == -1) {
		syslog(LOG_ERR, "Cannot add event timer for item");
		LIST_REMOVE(item, next);
		free(item);
	}
}

/* Send PIN request to client */
int
send_client_request(bdaddr_t *laddr, bdaddr_t *raddr, int hci)
{
	bthcid_pin_request_t	 cp;
	struct client		*cl;
	struct item		*item;
	struct kevent		change;
	struct timespec		timeout = { 0, 0 };
	int			n = 0;

	memset(&cp, 0, sizeof(cp));
	bdaddr_copy(&cp.laddr, laddr);
	bdaddr_copy(&cp.raddr, raddr);
	cp.time = PIN_REQUEST_TIMEOUT;

	LIST_FOREACH(cl, &client_list, next) {
		if (send(cl->fd, &cp, sizeof(cp), 0) != sizeof(cp))
			syslog(LOG_ERR, "send PIN request failed");
		else
			n++;
	}

	if (n == 0)
		return 0;

	syslog(LOG_DEBUG, "Sent PIN requests to %d client%s.",
				n, (n == 1 ? "" : "s"));

	item = malloc(sizeof(struct item));
	if (item == NULL) {
		syslog(LOG_ERR, "Cannot allocate PIN request item");
		return 0;
	}

	memset(item, 0, sizeof(struct item));
	bdaddr_copy(&item->laddr, laddr);
	bdaddr_copy(&item->raddr, raddr);
	item->hci = hci;
	EV_SET(&change, item->hci, EVFILT_TIMER, EV_ADD, 0, cp.time * 1000, item);
	if (kevent(hci_kq, &change, 1, NULL, 0, &timeout) == -1) {
		syslog(LOG_ERR, "Cannot add request timer");
		free(item);
		return 0;
	}

	LIST_INSERT_HEAD(&item_list, item, next);
	return 1;
}

/* Process item event (by expiring it) */
void
process_item(void *arg)
{
	struct item *item = arg;
	struct kevent change;
	struct timespec timeout = { 0, 0 };

	syslog(LOG_DEBUG, "PIN for %s expired", bt_ntoa(&item->raddr, NULL));
	LIST_REMOVE(item, next);
	EV_SET(&change, item->hci, EVFILT_TIMER, EV_DELETE, 0, 0, 0);
	kevent(hci_kq, &change, 1, NULL, 0, &timeout);
	free(item);
}

/* lookup PIN in item cache */
uint8_t *
lookup_pin(bdaddr_t *laddr, bdaddr_t *raddr)
{
	static uint8_t pin[HCI_PIN_SIZE];
	struct item *item;
	struct kevent change;
	struct timespec timeout = { 0, 0 };

	LIST_FOREACH(item, &item_list, next) {
		if (bdaddr_same(raddr, &item->raddr) == 0)
			continue;

		if (bdaddr_same(laddr, &item->laddr) == 0
		    && bdaddr_any(&item->laddr) == 0)
			continue;

		if (item->hci >= 0)
			break;

		syslog(LOG_DEBUG, "Matched PIN from cache");
		memcpy(pin, item->pin, sizeof(pin));

		LIST_REMOVE(item, next);
		EV_SET(&change, item->hci, EVFILT_TIMER, EV_DELETE, 0, 0, 0);
		kevent(hci_kq, &change, 1, NULL, 0, &timeout);
		free(item);

		return pin;
	}

	return NULL;
}
