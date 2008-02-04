/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon 'corecode' Schubert <corecode@fs.ei.tum.de> and
 * Matthias Schmidt <matthias@dragonflybsd.org>.
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
 * $DragonFly: src/libexec/dma/dma.h,v 1.3 2008/02/04 10:11:41 matthias Exp $
 */

#ifndef DMA_H
#define DMA_H

#ifdef HAVE_CRYPTO
#include <openssl/ssl.h>
#endif /* HAVE_CRYPTO */

#include <sys/queue.h>
#include <stdint.h>
#include <stdio.h>


#define VERSION	"DragonFly Mail Agent 1.0"

#define BUF_SIZE	2048
#define MIN_RETRY	300		/* 5 minutes */
#define MAX_RETRY	(3*60*60)	/* retry at least every 3 hours */
#define MAX_TIMEOUT	(5*24*60*60)	/* give up after 5 days */
#define PATH_MAX	1024		/* Max path len */
#define CONF_PATH	"/etc/dma/dma.conf"	/* /etc/dma/dma.conf */
#define	SMTP_PORT	25		/* default SMTP port */
#define CON_TIMEOUT	120		/* Connection timeout */

#define VIRTUAL		0x1		/* Support for address rewrites */
#define STARTTLS	0x2		/* StartTLS support */
#define SECURETRANS	0x4		/* SSL/TLS in general */
#define TLSINIT		0x8		/* Flag for TLS init phase */
#define DEFER		0x10		/* Defer mails */
#define INSECURE	0x20		/* Allow plain login w/o encryption */

struct stritem {
	SLIST_ENTRY(stritem) next;
	char *str;
};
SLIST_HEAD(strlist, stritem);

struct alias {
	LIST_ENTRY(alias) next;
	char *alias;
	struct strlist dests;
};
LIST_HEAD(aliases, alias);

struct qitem {
	LIST_ENTRY(qitem) next;
	const char *sender;
	char *addr;
	char *queuefn;
	char *queueid;
	FILE *queuef;
	off_t hdrlen;
	int remote;
};
LIST_HEAD(queueh, qitem);

struct queue {
	struct queueh queue;
	uintmax_t id;
	int mailfd;
	char *tmpf;
};

struct config {
	char *smarthost;
	int port;
	char *aliases;
	char *spooldir;
	char *virtualpath;
	char *authpath;
	char *certfile;
	int features;
#ifdef HAVE_CRYPTO
	SSL *ssl;
#endif /* HAVE_CRYPTO */
};


struct virtuser {
	SLIST_ENTRY(virtuser) next;
	char *login;
	char *address;
};
SLIST_HEAD(virtusers, virtuser);

struct authuser {
	SLIST_ENTRY(authuser) next;
	char *login;
	char *password;
	char *host;
};
SLIST_HEAD(authusers, authuser);

extern struct aliases aliases;

/* aliases_parse.y */
extern int yyparse(void);
extern FILE *yyin;

/* conf.c */
extern void trim_line(char *);
extern int parse_conf(const char *, struct config *);
extern int parse_virtuser(const char *);
extern int parse_authfile(const char *);

/* crypto.c */
#ifdef HAVE_CRYPTO
extern int smtp_init_crypto(struct qitem *, int, int);
#endif /* HAVE_CRYPTO */

/* net.c */
extern int check_for_smtp_error(int, char *);
extern ssize_t send_remote_command(int, const char*, ...);
extern int deliver_remote(struct qitem *, const char **);

/* base64.c */
extern int base64_encode(const void *, int, char **);

/* dma.c */
extern char * hostname(void);
#endif
