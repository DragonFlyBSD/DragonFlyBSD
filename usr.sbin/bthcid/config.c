/* $NetBSD: config.c,v 1.4 2007/01/25 20:33:41 plunky Exp $ */
/* $DragonFly: src/usr.sbin/bthcid/config.c,v 1.1 2008/01/30 14:10:19 hasso Exp $ */

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

#include <sys/time.h>
#include <bluetooth.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bthcid.h"

#if 0
static const char *key_file = "/var/db/bthcid.keys";
static const char *new_key_file = "/var/db/bthcid.keys.new";
#endif

/*
 * Look up key in keys file. We store a dictionary for each
 * remote address, and inside that we have a data object for
 * each local address containing the key.
 */
uint8_t *
lookup_key(bdaddr_t *laddr, bdaddr_t *raddr)
{
	link_key_p	key = NULL;

	syslog(LOG_DEBUG, "Got Link_Key_Request event from '%s', " \
			"remote bdaddr %s", bt_ntoa(laddr, NULL),
			bt_ntoa(raddr, NULL));

	if ((key = get_key(raddr, 0)) != NULL) {
		syslog(LOG_DEBUG, "Found matching entry, " \
				"remote bdaddr %s, name '%s', link key %s",
				bt_ntoa(&key->bdaddr, NULL),
				(key->name != NULL)? key->name : "No name",
				(key->key != NULL)? "exists" : "doesn't exist");
		return key->key;
	}

	syslog(LOG_DEBUG, "Could not find link key for remote bdaddr %s",
			bt_ntoa(raddr, NULL));
	return NULL;
}

/*
 * Look up pin in keys file. We store a dictionary for each
 * remote address, and inside that we have a data object for
 * each local address containing the pin.
 */
uint8_t *
lookup_pin_conf(bdaddr_t *laddr, bdaddr_t *raddr)
{
	link_key_p	key = NULL;

	syslog(LOG_DEBUG, "Got Link_Pin_Request event from '%s', " \
			"remote bdaddr %s", bt_ntoa(laddr, NULL),
			bt_ntoa(raddr, NULL));

	if ((key = get_key(raddr, 0)) != NULL) {
		syslog(LOG_DEBUG, "Found matching entry, " \
				"remote bdaddr %s, name '%s', pin %s",
				bt_ntoa(&key->bdaddr, NULL),
				(key->name != NULL)? key->name : "No name",
				(key->pin != NULL)? "exists" : "doesn't exist");
		return key->pin;
	}

	syslog(LOG_DEBUG, "Could not find link key for remote bdaddr %s",
			bt_ntoa(raddr, NULL));
	return NULL;
}


void
save_key(bdaddr_t *laddr, bdaddr_t *raddr, uint8_t * key)
{
	link_key_p	lkey = NULL;

	syslog(LOG_DEBUG, "Got Link_Key_Notification event from '%s', " \
			"remote bdaddr %s", bt_ntoa(laddr, NULL),
			bt_ntoa(raddr, NULL));

	if ((lkey = get_key(raddr, 1)) == NULL) {
		syslog(LOG_ERR, "Could not find entry for remote bdaddr %s",
				bt_ntoa(raddr, NULL));
		return;
	}
	
	syslog(LOG_DEBUG, "Updating link key for the entry, " \
			"remote bdaddr %s, name '%s', link key %s",
			bt_ntoa(&lkey->bdaddr, NULL),
			(lkey->name != NULL)? lkey->name : "No name",
			(lkey->key != NULL)? "exists" : "doesn't exist");

	if (lkey->key == NULL) {
		lkey->key = (uint8_t *) malloc(HCI_KEY_SIZE);
		if (lkey->key == NULL) {
			syslog(LOG_ERR, "Could not allocate link key");
			exit(1);
		}
	}

	memcpy(lkey->key, key, HCI_KEY_SIZE);

	dump_keys_file();
	read_config_file();
	read_keys_file();
	
	return;
}

