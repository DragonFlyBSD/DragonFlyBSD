/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2019-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2020 Matt Dunwoodie <ncon@noconroy.net>
 * Copyright (c) 2023 Aaron LI <aly@aaronly.me>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/wg/if_wg.h>

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h> /* getaddrinfo(), getnameinfo() */
#include <resolv.h> /* b64_pton(), b64_ntop() */
#include <stdbool.h>
#include <stddef.h> /* ptrdiff_t */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> /* timespec_get() */

#include "ifconfig.h"

/*
 * WG_BASE64_KEY_LEN is the size of a base64 encoded WireGuard key.
 * For every 4 input (base64) bytes, 3 output bytes wil be produced.
 * The output will be padded with 0 bits, therefore we need more than
 * the regular 32 bytes of space.
 */
#define WG_BASE64_KEY_LEN	(4 * ((WG_KEY_SIZE + 2) / 3))

static struct wg_data_io wg_data;
static struct wg_interface_io *wg_interface;
static struct wg_peer_io *wg_peer;
static struct wg_aip_io *wg_aip;


static void
wg_data_init(void)
{
	if (wg_interface != NULL)
		return;

	strlcpy(wg_data.wgd_name, IfName, sizeof(wg_data.wgd_name));
	wg_data.wgd_size = sizeof(*wg_interface);
	wg_data.wgd_interface = wg_interface = calloc(1, wg_data.wgd_size);
	if (wg_interface == NULL)
		err(1, "calloc");
}

static void
wg_data_grow(size_t by)
{
	ptrdiff_t peer_offset, aip_offset;

	wg_data_init();

	peer_offset = (char *)wg_peer - (char *)wg_interface;
	aip_offset = (char *)wg_aip - (char *)wg_interface;

	wg_data.wgd_size += by;
	wg_data.wgd_interface = realloc(wg_interface, wg_data.wgd_size);
	if (wg_data.wgd_interface == NULL)
		err(1, "realloc");

	wg_interface = wg_data.wgd_interface;
	memset((char *)wg_interface + wg_data.wgd_size - by, 0, by);

	if (wg_peer != NULL)
		wg_peer = (void *)((char *)wg_interface + peer_offset);
	if (wg_aip != NULL)
		wg_aip = (void *)((char *)wg_interface + aip_offset);
}


static void
wg_callback(int s, void *arg __unused)
{
	if (ioctl(s, SIOCSWG, &wg_data) == -1)
		err(1, "%s: SIOCSWG", wg_data.wgd_name);
}

static bool wg_cb_registered;

#define WG_REGISTER_CALLBACK()					\
	if (!wg_cb_registered) {				\
		callback_register(wg_callback, NULL);		\
		wg_cb_registered = true;			\
	}


static void
wg_setkey(const char *privkey, int arg __unused, int s __unused,
	  const struct afswtch *afp __unused)
{
	wg_data_init();

	if (b64_pton(privkey, wg_interface->i_private, WG_KEY_SIZE)
	    != WG_KEY_SIZE)
		errx(1, "wgkey: invalid private key: %s", privkey);
	wg_interface->i_flags |= WG_INTERFACE_HAS_PRIVATE;

	WG_REGISTER_CALLBACK();
}

static void
wg_setport(const char *port, int arg __unused, int s __unused,
	   const struct afswtch *afp __unused)
{
	const char *errmsg = NULL;

	wg_data_init();

	wg_interface->i_port = (in_port_t)strtonum(port, 0, 65535, &errmsg);
	if (errmsg != NULL)
		errx(1, "wgport: invalid port %s: %s", port, errmsg);
	wg_interface->i_flags |= WG_INTERFACE_HAS_PORT;

	WG_REGISTER_CALLBACK();
}

static void
wg_setcookie(const char *cookie, int arg __unused, int s __unused,
	     const struct afswtch *afp __unused)
{
	const char *errmsg = NULL;

	wg_data_init();

	wg_interface->i_cookie =
		(uint32_t)strtonum(cookie, 0, UINT32_MAX, &errmsg);
	if (errmsg != NULL)
		errx(1, "wgcookie: invalid cookie %s: %s", cookie, errmsg);
	wg_interface->i_flags |= WG_INTERFACE_HAS_COOKIE;

	WG_REGISTER_CALLBACK();
}

static void
wg_unsetcookie(const char *x __unused, int arg __unused, int s __unused,
	       const struct afswtch *afp __unused)
{
	wg_data_init();

	/* Unset cookie by setting it to value 0. */
	wg_interface->i_cookie = 0;
	wg_interface->i_flags |= WG_INTERFACE_HAS_COOKIE;

	WG_REGISTER_CALLBACK();
}


static void
wg_setpeer(const char *peerkey, int arg __unused, int s __unused,
	   const struct afswtch *afp __unused)
{
	wg_data_grow(sizeof(*wg_peer));

	if (wg_aip == NULL)
		wg_peer = &wg_interface->i_peers[0];
	else
		wg_peer = (struct wg_peer_io *)wg_aip;
	wg_aip = &wg_peer->p_aips[0];

	if (b64_pton(peerkey, wg_peer->p_public, WG_KEY_SIZE) != WG_KEY_SIZE)
		errx(1, "wgpeer: invalid peer key: %s", peerkey);
	wg_peer->p_flags |= WG_PEER_HAS_PUBLIC;
	wg_interface->i_peers_count++;

	WG_REGISTER_CALLBACK();
}

static void
wg_unsetpeer(const char *peerkey, int arg, int s, const struct afswtch *afp)
{
	wg_setpeer(peerkey, arg, s, afp);
	wg_peer->p_flags |= WG_PEER_REMOVE;
}

static void
wg_unsetpeerall(const char *x __unused, int arg __unused, int s __unused,
		const struct afswtch *afp __unused)
{
	wg_data_init();

	wg_interface->i_flags |= WG_INTERFACE_REPLACE_PEERS;

	WG_REGISTER_CALLBACK();
}

static void
wg_setpeeraip(const char *aip, int arg __unused, int s __unused,
	      const struct afswtch *afp __unused)
{
	int res;

	if (wg_peer == NULL)
		errx(1, "wgaip: wgpeer not set");

	wg_data_grow(sizeof(*wg_aip));

	if ((res = inet_net_pton(AF_INET, aip, &wg_aip->a_ipv4,
				 sizeof(wg_aip->a_ipv4))) != -1) {
		wg_aip->a_af = AF_INET;
	} else if ((res = inet_net_pton(AF_INET6, aip, &wg_aip->a_ipv6,
					sizeof(wg_aip->a_ipv6))) != -1) {
		wg_aip->a_af = AF_INET6;
	} else {
		errx(1, "wgaip: bad address: %s", aip);
	}
	wg_aip->a_cidr = res;

	wg_peer->p_flags |= WG_PEER_REPLACE_AIPS;
	wg_peer->p_aips_count++;

	wg_aip++;

	WG_REGISTER_CALLBACK();
}

static void
wg_setpeerpsk(const char *psk, int arg __unused, int s __unused,
	      const struct afswtch *afp __unused)
{
	if (wg_peer == NULL)
		errx(1, "wgpsk: wgpeer not set");

	if (b64_pton(psk, wg_peer->p_psk, WG_KEY_SIZE) != WG_KEY_SIZE)
		errx(1, "wgpsk: invalid key: %s", psk);
	wg_peer->p_flags |= WG_PEER_HAS_PSK;

	WG_REGISTER_CALLBACK();
}

static void
wg_unsetpeerpsk(const char *x __unused, int arg __unused, int s __unused,
		const struct afswtch *afp __unused)
{
	if (wg_peer == NULL)
		errx(1, "-wgpsk: wgpeer not set");

	/* Unset PSK by setting it to empty. */
	memset(wg_peer->p_psk, 0, sizeof(wg_peer->p_psk));
	wg_peer->p_flags |= WG_PEER_HAS_PSK;

	WG_REGISTER_CALLBACK();
}

static void
wg_setpeerpka(const char *pka, int arg __unused, int s __unused,
	      const struct afswtch *afp __unused)
{
	const char *errmsg = NULL;

	if (wg_peer == NULL)
		errx(1, "wgpka: wgpeer not set");

	/* 43200 seconds == 12h, reasonable for a uint16_t value */
	wg_peer->p_pka = (uint16_t)strtonum(pka, 0, 43200, &errmsg);
	if (errmsg != NULL)
		errx(1, "wgpka: invalid pka %s: %s", pka, errmsg);
	wg_peer->p_flags |= WG_PEER_HAS_PKA;

	WG_REGISTER_CALLBACK();
}

static void
wg_unsetpeerpka(const char *x __unused, int arg __unused, int s __unused,
		const struct afswtch *afp __unused)
{
	if (wg_peer == NULL)
		errx(1, "wgpka: wgpeer not set");

	wg_peer->p_pka = 0;
	wg_peer->p_flags |= WG_PEER_HAS_PKA;

	WG_REGISTER_CALLBACK();
}

static void
wg_setpeerep(const char *host, const char *service, int s __unused,
	     const struct afswtch *afp __unused)
{
	struct addrinfo *ai;
	int error;

	if (wg_peer == NULL)
		errx(1, "wgendpoint: wgpeer not set");

	if ((error = getaddrinfo(host, service, NULL, &ai)) != 0)
		errx(1, "%s", gai_strerror(error));

	memcpy(&wg_peer->p_sa, ai->ai_addr, ai->ai_addrlen);
	wg_peer->p_flags |= WG_PEER_HAS_ENDPOINT;

	freeaddrinfo(ai);
	WG_REGISTER_CALLBACK();
}

static void
wg_setpeerdesc(const char *desc, int arg __unused, int s __unused,
	       const struct afswtch *afp __unused)
{
	if (wg_peer == NULL)
		errx(1, "wgdescr: wgpeer not set");

	strlcpy(wg_peer->p_description, desc, sizeof(wg_peer->p_description));
	wg_peer->p_flags |= WG_PEER_SET_DESCRIPTION;

	WG_REGISTER_CALLBACK();
}

static void
wg_unsetpeerdesc(const char *x __unused, int arg __unused, int s __unused,
		 const struct afswtch *afp __unused)
{
	if (wg_peer == NULL)
		errx(1, "-wgpsk: wgpeer not set");

	memset(wg_peer->p_description, 0, sizeof(wg_peer->p_description));
	wg_peer->p_flags |= WG_PEER_SET_DESCRIPTION;

	WG_REGISTER_CALLBACK();
}


static void
wg_status(int s)
{
	struct timespec now;
	size_t i, j, last_size;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	char key[WG_BASE64_KEY_LEN + 1];

	memset(&wg_data, 0, sizeof(wg_data));
	strlcpy(wg_data.wgd_name, IfName, sizeof(wg_data.wgd_name));

	for (;;) {
		last_size = wg_data.wgd_size;

		if (ioctl(s, SIOCGWG, &wg_data) == -1) {
			if (errno == EINVAL || errno == ENOTTY)
				goto out;
			err(1, "%s: SIOCGWG", wg_data.wgd_name);
		}

		if (last_size >= wg_data.wgd_size)
			break;

		wg_interface = calloc(1, wg_data.wgd_size);
		if (wg_interface == NULL)
			err(1, "calloc");
		free(wg_data.wgd_interface);
		wg_data.wgd_interface = wg_interface;
	}

	wg_interface = wg_data.wgd_interface;

	if (wg_interface->i_flags & WG_INTERFACE_HAS_PORT)
		printf("\twgport: %hu\n", wg_interface->i_port);
	if (wg_interface->i_flags & WG_INTERFACE_HAS_COOKIE)
		printf("\twgcookie: %u\n", wg_interface->i_cookie);
	if (wg_interface->i_flags & WG_INTERFACE_HAS_PRIVATE && printkeys) {
		b64_ntop(wg_interface->i_private, WG_KEY_SIZE,
			 key, sizeof(key));
		printf("\twgkey: %s\n", key);
	}
	if (wg_interface->i_flags & WG_INTERFACE_HAS_PUBLIC) {
		b64_ntop(wg_interface->i_public, WG_KEY_SIZE,
			 key, sizeof(key));
		printf("\twgpubkey: %s\n", key);
	}

	wg_peer = &wg_interface->i_peers[0];
	for (i = 0; i < wg_interface->i_peers_count; i++) {
		b64_ntop(wg_peer->p_public, WG_KEY_SIZE,
			 key, sizeof(key));
		printf("\twgpeer: %s\n", key);

		printf("\t\tid: %" PRIu64 "\n", wg_peer->p_id);
		if (wg_peer->p_description[0] != '\0')
			printf("\t\twgdescr: %s\n", wg_peer->p_description);
		if (wg_peer->p_flags & WG_PEER_HAS_PSK) {
			if (printkeys) {
				b64_ntop(wg_peer->p_psk, WG_KEY_SIZE,
					 key, sizeof(key));
				printf("\t\twgpsk: %s\n", key);
			} else {
				printf("\t\twgpsk: (present)\n");
			}
		}
		if ((wg_peer->p_flags & WG_PEER_HAS_PKA) && wg_peer->p_pka > 0)
			printf("\t\twgpka: %u (seconds)\n", wg_peer->p_pka);
		if (wg_peer->p_flags & WG_PEER_HAS_ENDPOINT) {
			if (getnameinfo(&wg_peer->p_sa, wg_peer->p_sa.sa_len,
					hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
					NI_NUMERICHOST | NI_NUMERICSERV) == 0)
				printf("\t\twgendpoint: %s %s\n", hbuf, sbuf);
			else
				printf("\t\twgendpoint: (unable to print)\n");
		}

		printf("\t\ttx: %" PRIu64 " (bytes), rx: %" PRIu64 " (bytes)\n",
		       wg_peer->p_txbytes, wg_peer->p_rxbytes);

		if (wg_peer->p_last_handshake.tv_sec != 0) {
			timespec_get(&now, TIME_UTC);
			printf("\t\tlast handshake: %ld seconds ago\n",
			       now.tv_sec - wg_peer->p_last_handshake.tv_sec);
		}

		for (j = 0; j < wg_peer->p_aips_count; j++) {
			wg_aip = &wg_peer->p_aips[j];
			inet_ntop(wg_aip->a_af, &wg_aip->a_addr,
				  hbuf, sizeof(hbuf));
			printf("\t\twgaip: %s/%d\n", hbuf, wg_aip->a_cidr);
		}

		wg_aip = &wg_peer->p_aips[wg_peer->p_aips_count];
		wg_peer = (struct wg_peer_io *)wg_aip;
	}

out:
	free(wg_data.wgd_interface);
}


static struct cmd wg_cmds[] = {
	DEF_CMD_ARG("wgkey",			wg_setkey),
	DEF_CMD_ARG("wgport",			wg_setport),
	DEF_CMD_ARG("wgcookie",			wg_setcookie),
	DEF_CMD("-wgcookie",		0,	wg_unsetcookie),
	DEF_CMD_ARG("wgpeer",			wg_setpeer),
	DEF_CMD_ARG("-wgpeer",			wg_unsetpeer),
	DEF_CMD("-wgpeerall",		0,	wg_unsetpeerall),
	DEF_CMD_ARG("wgaip",			wg_setpeeraip),
	DEF_CMD_ARG("wgpsk",			wg_setpeerpsk),
	DEF_CMD("-wgpsk",		0,	wg_unsetpeerpsk),
	DEF_CMD_ARG("wgpka",			wg_setpeerpka),
	DEF_CMD("-wgpka",		0,	wg_unsetpeerpka),
	DEF_CMD_ARG2("wgendpoint",		wg_setpeerep),
	DEF_CMD_ARG("wgdescr",			wg_setpeerdesc),
	DEF_CMD_ARG("wgdescription",		wg_setpeerdesc),
	DEF_CMD("-wgdescr",		0,	wg_unsetpeerdesc),
	DEF_CMD("-wgdescription",	0,	wg_unsetpeerdesc),
};

static struct afswtch af_wg = {
	.af_name		= "af_wg", /* dummy */
	.af_af			= AF_UNSPEC,
	.af_other_status	= wg_status,
};

__constructor(143)
static void
wg_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(wg_cmds); i++)
		cmd_register(&wg_cmds[i]);

	af_register(&af_wg);
}
