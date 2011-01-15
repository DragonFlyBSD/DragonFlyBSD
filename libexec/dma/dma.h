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
 */

#ifndef DMA_H
#define DMA_H

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <netdb.h>

#define VERSION	"DragonFly Mail Agent"

#define BUF_SIZE	2048
#define MIN_RETRY	300		/* 5 minutes */
#define MIN_RETRY_LOCAL	30		/* 30 seconds */
#define MAX_RETRY	(3*60*60)	/* retry at least every 3 hours */
#define RETRY_JITTER	10
#define MAX_TIMEOUT	(5*24*60*60)	/* give up after 5 days */
#ifndef PATH_MAX
#define PATH_MAX	1024		/* Max path len */
#endif
#define	SMTP_PORT	25		/* Default SMTP port */
#define CON_TIMEOUT	120		/* Connection timeout */

#define VIRTUAL		0x001		/* Support for address rewrites */
#define STARTTLS	0x002		/* StartTLS support */
#define SECURETRANS	0x004		/* SSL/TLS in general */
#define NOSSL		0x008		/* Do not use SSL */
#define DEFER		0x010		/* Defer mails */
#define INSECURE	0x020		/* Allow plain login w/o encryption */
#define FULLBOUNCE	0x040		/* Bounce the full message */

#ifndef CONF_PATH
#define CONF_PATH	"/etc/dma/dma.conf"	/* Default path to dma.conf */
#endif

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
	char *mailfn;
	char *queueid;
	FILE *queuef;
	FILE *mailf;
	int remote;
};
LIST_HEAD(queueh, qitem);

struct queue {
	struct queueh queue;
	char *id;
	FILE *mailf;
	char *tmpf;
	const char *sender;
};

struct config {
	const char *smarthost;
	int port;
	const char *aliases;
	const char *spooldir;
	const char *virtualpath;
	const char *authpath;
	const char *certfile;
	int features;
	const char *mailname;
	const char *mailnamefile;

	/* XXX does not belong into config */
	SSL *ssl;
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


struct mx_hostentry {
	char		host[MAXDNAME];
	char		addr[INET6_ADDRSTRLEN];
	int		pref;
	struct addrinfo	ai;
	struct sockaddr_storage	sa;
};


/* global variables */
extern struct aliases aliases;
extern struct config config;
extern struct strlist tmpfs;
extern struct virtusers virtusers;
extern struct authusers authusers;
extern const char *username;
extern const char *logident_base;

extern char neterr[BUF_SIZE];

/* aliases_parse.y */
int yyparse(void);
extern FILE *yyin;

/* conf.c */
void trim_line(char *);
void parse_conf(const char *);
void parse_virtuser(const char *);
void parse_authfile(const char *);

/* crypto.c */
void hmac_md5(unsigned char *, int, unsigned char *, int, caddr_t);
int smtp_auth_md5(int, char *, char *);
int smtp_init_crypto(int, int);

/* dns.c */
int dns_get_mx_list(const char *, int, struct mx_hostentry **, int);

/* net.c */
char *ssl_errstr(void);
int read_remote(int, int, char *);
ssize_t send_remote_command(int, const char*, ...) __printflike(2, 3);
int deliver_remote(struct qitem *, const char **);

/* base64.c */
int base64_encode(const void *, int, char **);
int base64_decode(const char *, void *);

/* dma.c */
int add_recp(struct queue *, const char *, int);
void run_queue(struct queue *);

/* spool.c */
int newspoolf(struct queue *);
int linkspool(struct queue *);
int load_queue(struct queue *);
void delqueue(struct qitem *);
int acquirespool(struct qitem *);
void dropspool(struct queue *, struct qitem *);

/* local.c */
int deliver_local(struct qitem *, const char **errmsg);

/* mail.c */
void bounce(struct qitem *, const char *);
int readmail(struct queue *, int, int);

/* util.c */
const char *hostname(void);
void setlogident(const char *, ...) __printf0like(1, 2);
void errlog(int, const char *, ...) __printf0like(2, 3);
void errlogx(int, const char *, ...) __printf0like(2, 3);
void set_username(void);
void deltmp(void);
int open_locked(const char *, int, ...);
char *rfc822date(void);
int strprefixcmp(const char *, const char *);

#endif
