/* $NetBSD: bthcid.h,v 1.3 2006/09/26 19:18:19 plunky Exp $ */
/* $DragonFly: src/usr.sbin/bthcid/bthcid.h,v 1.1 2008/01/30 14:10:19 hasso Exp $ */

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

#ifndef _BTHCID_H_
#define _BTHCID_H_	1

#include <sys/queue.h>

/* config.c */
uint8_t		*lookup_key		(bdaddr_t *, bdaddr_t *);
void		 save_key		(bdaddr_t *, bdaddr_t *, uint8_t *);
void 		 create_dict(bdaddr_t *laddr, bdaddr_t *raddr, uint8_t * key);

/* client.c */
int		 init_control		(const char *, mode_t);
int		 send_client_request	(bdaddr_t *, bdaddr_t *, int);
uint8_t		*lookup_pin		(bdaddr_t *, bdaddr_t *);
uint8_t		*lookup_pin_conf	(bdaddr_t *, bdaddr_t *);
void		process_control		(int);
void		process_client		(int, void *);
void		process_item		(void *);

/* hci.c */
int		init_hci		(bdaddr_t *);
void		process_hci		(int);
int		send_pin_code_reply	(int, struct sockaddr_bt *, bdaddr_t *, uint8_t *);

#define BTHCID_BUFFER_SIZE	512
#define BTHCID_IDENT		"bthcid"
#define BTHCID_PIDFILE		"/var/run/" BTHCID_IDENT ".pid"
#define BTHCID_KEYSFILE		"/var/db/"  BTHCID_IDENT ".keys"
#define BTHCID_KQ_EVENTS	64

struct link_key
{
	bdaddr_t		 bdaddr; /* remote device BDADDR */
	char			*name;   /* remote device name */
	uint8_t			*key;    /* link key (or NULL if no key) */
	char			*pin;    /* pin (or NULL if no pin) */
	LIST_ENTRY(link_key)	 next;   /* link to the next */
};
typedef struct link_key		link_key_t;
typedef struct link_key *	link_key_p;

extern const char		*config_file;
extern int			hci_kq;

#if __config_debug__
void		dump_config	(void);
#endif

#ifndef bdaddr_p 
#define bdaddr_p bdaddr_t *
#endif

void		read_config_file(void);
void		clean_config	(void);
link_key_p	get_key		(bdaddr_p bdaddr, int exact_match);

int		read_keys_file  (void);
int		dump_keys_file  (void);

#endif	/* _BTHCID_H_ */
