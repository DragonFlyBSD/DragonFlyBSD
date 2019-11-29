%{
/*
 * parser.y
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: parser.y,v 1.5 2003/06/07 21:22:30 max Exp $
 * $FreeBSD: src/usr.sbin/bluetooth/hcsecd/parser.y,v 1.4 2004/09/14 20:04:33 emax Exp $
 */

#include <sys/fcntl.h>
#include <sys/queue.h>
#include <bluetooth.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "bthcid.h"

	int	yylex    (void);

static	void	free_key (link_key_p key);
static	int	hexa2int4(char *a);
static	int	hexa2int8(char *a);

extern	void			 yyerror(const char *);
extern	int			 yylineno;
extern	FILE			*yyin;

static	LIST_HEAD(, link_key)	 link_keys;

const	char			*config_file = "/etc/bluetooth/bthcid.conf";
static	link_key_p		 key = NULL;
%}

%union {
	char	*string;
}

%token <string> T_BDADDRSTRING T_HEXSTRING T_STRING
%token T_DEVICE T_BDADDR T_NAME T_KEY T_PIN T_NOKEY T_NOPIN T_JUNK

%%

config:		line
		| config line
		;

line:		T_DEVICE
			{
			key = (link_key_p) malloc(sizeof(*key));
			if (key == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"config entry");
				exit(1);
			}

			memset(key, 0, sizeof(*key));
			}
		'{' options '}'
			{
			if (get_key(&key->bdaddr, 1) != NULL) {
				syslog(LOG_ERR, "Ignoring duplicated entry " \
						"for bdaddr %s",
						bt_ntoa(&key->bdaddr, NULL));
				free_key(key);
			} else 
				LIST_INSERT_HEAD(&link_keys, key, next);

			key = NULL;
			}
		;

options:	option ';'
		| options option ';'
		;

option:		bdaddr
		| name
		| key
		| pin
		;

bdaddr:		T_BDADDR T_BDADDRSTRING
			{
			if (!bt_aton($2, &key->bdaddr)) {
				syslog(LOG_ERR, "Could not parse BD_ADDR " \
						"'%s'", $2);
				exit(1);
			}
			}
		;

name:		T_NAME T_STRING
			{
			if (key->name != NULL)
				free(key->name);

			key->name = strdup($2);
			if (key->name == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"device name");
				exit(1);
			}
			}
		;

key:		T_KEY T_HEXSTRING
			{
			int	i, len;

			if (key->key != NULL)
				free(key->key);

			key->key = (uint8_t *) malloc(HCI_KEY_SIZE);
			if (key->key == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"link key");
				exit(1);
			}

			memset(key->key, 0, HCI_KEY_SIZE);

			len = strlen($2) / 2;
			if (len > HCI_KEY_SIZE)
				len = HCI_KEY_SIZE;

			for (i = 0; i < len; i ++)
				key->key[i] = hexa2int8((char *)($2) + 2*i);
			}
		| T_KEY T_NOKEY
			{
			if (key->key != NULL)
				free(key->key);

			key->key = NULL;
			}
		;

pin:		T_PIN T_STRING
			{
			if (key->pin != NULL)
				free(key->pin);

			key->pin = strdup($2);
			if (key->pin == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"PIN code");
				exit(1);
			}
			}
		| T_PIN T_NOPIN
			{
			if (key->pin != NULL)
				free(key->pin);

			key->pin = NULL;
			}
		;

%%

/* Display parser error message */
void
yyerror(char const *message)
{
	syslog(LOG_ERR, "%s in line %d", message, yylineno);
}

/* Re-read config file */
void
read_config_file(void)
{
	if (config_file == NULL) {
		syslog(LOG_ERR, "Unknown config file name!");
		exit(1);
	}

	if ((yyin = fopen(config_file, "r")) == NULL) {
		syslog(LOG_ERR, "Could not open config file '%s'. %s (%d)",
				config_file, strerror(errno), errno);
		exit(1);
	}

	clean_config();
	if (yyparse() < 0) {
		syslog(LOG_ERR, "Could not parse config file '%s'",config_file);
		exit(1);
	}

	fclose(yyin);
	yyin = NULL;

#ifdef __config_debug__
	dump_config();
#endif
}

/* Clean config */
void
clean_config(void)
{
	link_key_p	lkey = NULL;

	while ((lkey = LIST_FIRST(&link_keys)) != NULL) {
		LIST_REMOVE(lkey, next);
		free_key(lkey);
	}
}

/* Find link key entry in the list. Return exact or default match */
link_key_p
get_key(bdaddr_p bdaddr, int exact_match)
{
	link_key_p	lkey = NULL, defkey = NULL;

	LIST_FOREACH(lkey, &link_keys, next) {
		if (memcmp(bdaddr, &lkey->bdaddr, sizeof(lkey->bdaddr)) == 0)
			break;

		if (!exact_match)
			if (memcmp(BDADDR_ANY, &lkey->bdaddr,
					sizeof(lkey->bdaddr)) == 0)
				defkey = lkey;
	}

	return ((lkey != NULL)? lkey : defkey);
}

#ifdef __config_debug__
/* Dump config */
void
dump_config(void)
{
	link_key_p	lkey = NULL;
	char		buffer[64];

	LIST_FOREACH(lkey, &link_keys, next) {
		if (lkey->key != NULL)
			snprintf(buffer, sizeof(buffer),
"0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
				lkey->key[0], lkey->key[1], lkey->key[2],
				lkey->key[3], lkey->key[4], lkey->key[5],
				lkey->key[6], lkey->key[7], lkey->key[8],
				lkey->key[9], lkey->key[10], lkey->key[11],
				lkey->key[12], lkey->key[13], lkey->key[14],
				lkey->key[15]);

		syslog(LOG_DEBUG, 
"device %s " \
"bdaddr %s " \
"pin %s " \
"key %s",
			(lkey->name != NULL)? lkey->name : "noname",
			bt_ntoa(&lkey->bdaddr, NULL),
			(lkey->pin != NULL)? lkey->pin : "nopin",
			(lkey->key != NULL)? buffer : "nokey");
	}
}
#endif

/* Read keys file */
int
read_keys_file(void)
{
	FILE		*f = NULL;
	link_key_t	*lkey = NULL;
	char		 buf[BTHCID_BUFFER_SIZE], *p = NULL, *cp = NULL;
	bdaddr_t	 bdaddr;
	int		 i, len;

	if ((f = fopen(BTHCID_KEYSFILE, "r")) == NULL) {
		if (errno == ENOENT)
			return (0);

		syslog(LOG_ERR, "Could not open keys file %s. %s (%d)\n",
				BTHCID_KEYSFILE, strerror(errno), errno);

		return (-1);
	}

	while ((p = fgets(buf, sizeof(buf), f)) != NULL) {
		if (*p == '#')
			continue;
		if ((cp = strpbrk(p, " ")) == NULL)
			continue;

		*cp++ = '\0';

		if (!bt_aton(p, &bdaddr))
			continue;

		if ((lkey = get_key(&bdaddr, 1)) == NULL)
			continue;

		if (lkey->key == NULL) {
			lkey->key = (uint8_t *) malloc(HCI_KEY_SIZE);
			if (lkey->key == NULL) {
				syslog(LOG_ERR, "Could not allocate link key");
				exit(1);
			}
		}

		memset(lkey->key, 0, HCI_KEY_SIZE);

		len = strlen(cp) / 2;
		if (len > HCI_KEY_SIZE)
			len = HCI_KEY_SIZE;

		for (i = 0; i < len; i ++)
			lkey->key[i] = hexa2int8(cp + 2*i);

		syslog(LOG_DEBUG, "Restored link key for the entry, " \
				"remote bdaddr %s, name '%s'",
				bt_ntoa(&lkey->bdaddr, NULL),
				(lkey->name != NULL)? lkey->name : "No name");
	}

	fclose(f);

	return (0);
}

/* Dump keys file */
int
dump_keys_file(void)
{
	link_key_p	lkey = NULL;
	char		tmp[PATH_MAX], buf[BTHCID_BUFFER_SIZE];
	int		f;

	snprintf(tmp, sizeof(tmp), "%s.tmp", BTHCID_KEYSFILE);
	if ((f = open(tmp, O_RDWR|O_CREAT|O_TRUNC|O_EXCL, 0600)) < 0) {
		syslog(LOG_ERR, "Could not create temp keys file %s. %s (%d)\n",
				tmp, strerror(errno), errno);
		return (-1);
	}

	LIST_FOREACH(lkey, &link_keys, next) {
		if (lkey->key == NULL)
			continue;

		snprintf(buf, sizeof(buf),
"%s %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			bt_ntoa(&lkey->bdaddr, NULL),
			lkey->key[0],  lkey->key[1],  lkey->key[2],  lkey->key[3],
			lkey->key[4],  lkey->key[5],  lkey->key[6],  lkey->key[7],
			lkey->key[8],  lkey->key[9],  lkey->key[10], lkey->key[11],
			lkey->key[12], lkey->key[13], lkey->key[14], lkey->key[15]);

		if (write(f, buf, strlen(buf)) < 0) {
			syslog(LOG_ERR, "Could not write temp keys file. " \
					"%s (%d)\n", strerror(errno), errno);
			break;
		}
	}

	close(f);

	if (rename(tmp, BTHCID_KEYSFILE) < 0) {
		syslog(LOG_ERR, "Could not rename(%s, %s). %s (%d)\n",
				tmp, BTHCID_KEYSFILE, strerror(errno), errno);
		unlink(tmp);
		return (-1);
	}

	return (0);
}

/* Free key entry */
static void
free_key(link_key_p lkey)
{
	if (lkey->name != NULL)
		free(lkey->name);
	if (lkey->key != NULL)
		free(lkey->key);
	if (lkey->pin != NULL)
		free(lkey->pin);

	memset(lkey, 0, sizeof(*lkey));
	free(lkey);
}

/* Convert hex ASCII to int4 */
static int
hexa2int4(char *a)
{
	if ('0' <= *a && *a <= '9')
		return (*a - '0');

	if ('A' <= *a && *a <= 'F')
		return (*a - 'A' + 0xa);

	if ('a' <= *a && *a <= 'f')
		return (*a - 'a' + 0xa);

	syslog(LOG_ERR, "Invalid hex character: '%c' (%#x)", *a, *a);
	exit(1);
}

/* Convert hex ASCII to int8 */
static int
hexa2int8(char *a)
{
	return ((hexa2int4(a) << 4) | hexa2int4(a + 1));
}

