/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthias Schmidt <matthias@dragonflybsd.org>, University of Marburg,
 * Germany.
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
 *
 * $DragonFly: src/libexec/dma/conf.c,v 1.2 2008/02/04 10:11:41 matthias Exp $
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>

#include "dma.h"

#define DP	": \t\n"
#define EQS	" \t\n"

extern struct virtusers virtusers;
extern struct authusers authusers;

/*
 * Remove trailing \n's
 */
void
trim_line(char *line)
{
	size_t linelen;
	char *p;

	p = line;

	if ((p = strchr(line, '\n')))
		*p = (char)0;

	/* Escape leading dot in every case */
	linelen = strlen(line);
	if (line[0] == '.') {
		if ((linelen + 2) > 1000) {
			syslog(LOG_CRIT, "Cannot escape leading dot.  Buffer overflow");
			exit(1);
		}
		memmove((line + 1), line, (linelen + 1));
		line[0] = '.';
	}
}

/*
 * Add a virtual user entry to the list of virtual users
 */
static void
add_virtuser(char *login, char *address)
{
	struct virtuser *v;

	v = malloc(sizeof(struct virtuser));
	v->login = strdup(login);
	v->address = strdup(address);
	SLIST_INSERT_HEAD(&virtusers, v, next);
}

/*
 * Read the virtual user table
 */
int
parse_virtuser(const char *path)
{
	FILE *v;
	char *word;
	char *data;
	char line[2048];

	v = fopen(path, "r");
	if (v == NULL)
		return (-1);

	while (!feof(v)) {
		fgets(line, sizeof(line), v);
		/* We hit a comment */
		if (strchr(line, '#'))
			*strchr(line, '#') = 0;
		if ((word = strtok(line, DP)) != NULL) {
			data = strtok(NULL, DP);
			if (data != NULL) {
				add_virtuser(word, data);
			}
		}
	}

	fclose(v);
	return (0);
}

/*
 * Add entry to the SMTP auth user list
 */
static void
add_smtp_auth_user(char *userstring, char *password)
{
	struct authuser *a;
	char *temp;

	a = malloc(sizeof(struct virtuser));
	a->password= strdup(password);

	temp = strrchr(userstring, '|');
	if (temp == NULL)
		errx(1, "auth.conf file in wrong format");

	a->host = strdup(temp+1);
	a->login = strdup(strtok(userstring, "|"));
	if (a->login == NULL)
		errx(1, "auth.conf file in wrong format");

	SLIST_INSERT_HEAD(&authusers, a, next);
}

/*
 * Read the SMTP authentication config file
 */
int
parse_authfile(const char *path)
{
	FILE *a;
	char *word;
	char *data;
	char line[2048];

	a = fopen(path, "r");
	if (a == NULL)
		return (1);

	while (!feof(a)) {
		fgets(line, sizeof(line), a);
		/* We hit a comment */
		if (strchr(line, '#'))
			*strchr(line, '#') = 0;
		if ((word = strtok(line, DP)) != NULL) {
			data = strtok(NULL, DP);
			if (data != NULL) {
				add_smtp_auth_user(word, data);
			}
		}
	}

	fclose(a);
	return (0);
}

/*
 * XXX TODO
 * Check if the user supplied a value.  If not, fill in default
 * Check for bad things[TM]
 */
int
parse_conf(const char *config_path, struct config *config)
{
	char *word;
	char *data;
	FILE *conf;
	char line[2048];

	conf = fopen(config_path, "r");
	if (conf == NULL)
		return (-1);

	/* Reset features */
	config->features = 0;

	while (!feof(conf)) {
		fgets(line, sizeof(line), conf);
		/* We hit a comment */
		if (strchr(line, '#'))
			*strchr(line, '#') = 0;
		if ((word = strtok(line, EQS)) != NULL) {
			data = strtok(NULL, EQS);
			if (strcmp(word, "SMARTHOST") == 0) {
				if (data != NULL)
					config->smarthost = strdup(data);
			}
			else if (strcmp(word, "PORT") == 0) {
				if (data != NULL)
					config->port = atoi(strdup(data));
			}
			else if (strcmp(word, "ALIASES") == 0) {
				if (data != NULL)
					config->aliases = strdup(data);
			}
			else if (strcmp(word, "SPOOLDIR") == 0) {
				if (data != NULL)
					config->spooldir = strdup(data);
			}
			else if (strcmp(word, "VIRTPATH") == 0) {
				if (data != NULL)
					config->virtualpath = strdup(data);
			}
			else if (strcmp(word, "AUTHPATH") == 0) {
				if (data != NULL)
					config->authpath= strdup(data);
			}
			else if (strcmp(word, "CERTFILE") == 0) {
				if (data != NULL)
					config->certfile = strdup(data);
			}
			else if (strcmp(word, "VIRTUAL") == 0)
				config->features |= VIRTUAL;
			else if (strcmp(word, "STARTTLS") == 0)
				config->features |= STARTTLS;
			else if (strcmp(word, "SECURETRANSFER") == 0)
				config->features |= SECURETRANS;
			else if (strcmp(word, "DEFER") == 0)
				config->features |= DEFER;
			else if (strcmp(word, "INSECURE") == 0)
				config->features |= INSECURE;
		}
	}

	fclose(conf);
	return (0);
}

