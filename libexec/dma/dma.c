/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon 'corecode' Schubert <corecode@fs.ei.tum.de>.
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
 * $DragonFly: src/libexec/dma/dma.c,v 1.5 2008/09/30 17:47:21 swildner Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"



static void deliver(struct qitem *);

struct aliases aliases = LIST_HEAD_INITIALIZER(aliases);
struct strlist tmpfs = SLIST_HEAD_INITIALIZER(tmpfs);
struct virtusers virtusers = LIST_HEAD_INITIALIZER(virtusers);
struct authusers authusers = LIST_HEAD_INITIALIZER(authusers);
static int daemonize = 1;
struct config *config;
static const char *username;
static uid_t uid;


const char *
hostname(void)
{
	static char name[MAXHOSTNAMELEN+1];
	int initialized = 0;
	FILE *fp;
	size_t len;

	if (initialized)
		return (name);

	if (config->mailname != NULL && config->mailname[0] != '\0') {
		snprintf(name, sizeof(name), "%s", config->mailname);
		initialized = 1;
		return (name);
	}
	if (config->mailnamefile != NULL && config->mailnamefile[0] != '\0') {
		fp = fopen(config->mailnamefile, "r");
		if (fp != NULL) {
			if (fgets(name, sizeof(name), fp) != NULL) {
				len = strlen(name);
				while (len > 0 &&
				    (name[len - 1] == '\r' ||
				     name[len - 1] == '\n'))
					name[--len] = '\0';
				if (name[0] != '\0') {
					initialized = 1;
					return (name);
				}
			}
			fclose(fp);
		}
	}
	if (gethostname(name, sizeof(name)) != 0)
		strcpy(name, "(unknown hostname)");
	initialized = 1;
	return name;
}

static const char *
check_username(const char *name, uid_t ckuid)
{
	struct passwd *pwd;

	if (name == NULL)
		return (NULL);
	pwd = getpwnam(name);
	if (pwd == NULL || pwd->pw_uid != ckuid)
		return (NULL);
	return (name);
}

static void
set_username(void)
{
	struct passwd *pwd;
	char *u = NULL;

	uid = getuid();
	username = check_username(getlogin(), uid);
	if (username != NULL)
		return;
	username = check_username(getenv("LOGNAME"), uid);
	if (username != NULL)
		return;
	username = check_username(getenv("USER"), uid);
	if (username != NULL)
		return;
	pwd = getpwuid(uid);
	if (pwd != NULL && pwd->pw_name != NULL && pwd->pw_name[0] != '\0' &&
	    (u = strdup(pwd->pw_name)) != NULL) {
		username = check_username(u, uid);
		if (username != NULL)
			return;
		else
			free(u);
	}
	asprintf(__DECONST(void *, &username), "%ld", (long)uid);
	if (username != NULL)
		return;
	username = "unknown-or-invalid-username";
}

static char *
set_from(const char *osender)
{
	struct virtuser *v;
	char *sender;

	if ((config->features & VIRTUAL) != 0) {
		SLIST_FOREACH(v, &virtusers, next) {
			if (strcmp(v->login, username) == 0) {
				sender = strdup(v->address);
				if (sender == NULL)
					return(NULL);
				goto out;
			}
		}
	}

	if (osender) {
		sender = strdup(osender);
		if (sender == NULL)
			return (NULL);
	} else {
		if (asprintf(&sender, "%s@%s", username, hostname()) <= 0)
			return (NULL);
	}

	if (strchr(sender, '\n') != NULL) {
		errno = EINVAL;
		return (NULL);
	}

out:
	return (sender);
}

static int
read_aliases(void)
{
	yyin = fopen(config->aliases, "r");
	if (yyin == NULL)
		return (0);	/* not fatal */
	if (yyparse())
		return (-1);	/* fatal error, probably malloc() */
	fclose(yyin);
	return (0);
}

int
add_recp(struct queue *queue, const char *str, const char *sender, int expand)
{
	struct qitem *it, *tit;
	struct stritem *sit;
	struct alias *al;
	struct passwd *pw;
	char *host;
	int aliased = 0;

	it = calloc(1, sizeof(*it));
	if (it == NULL)
		return (-1);
	it->addr = strdup(str);
	if (it->addr == NULL)
		return (-1);

	it->sender = sender;
	host = strrchr(it->addr, '@');
	if (host != NULL &&
	    (strcmp(host + 1, hostname()) == 0 ||
	     strcmp(host + 1, "localhost") == 0)) {
		*host = 0;
	}
	LIST_FOREACH(tit, &queue->queue, next) {
		/* weed out duplicate dests */
		if (strcmp(tit->addr, it->addr) == 0) {
			free(it->addr);
			free(it);
			return (0);
		}
	}
	LIST_INSERT_HEAD(&queue->queue, it, next);
	if (strrchr(it->addr, '@') == NULL) {
		it->remote = 0;
		if (expand) {
			LIST_FOREACH(al, &aliases, next) {
				if (strcmp(al->alias, it->addr) != 0)
					continue;
				SLIST_FOREACH(sit, &al->dests, next) {
					if (add_recp(queue, sit->str, sender, 1) != 0)
						return (-1);
				}
				aliased = 1;
			}
			if (aliased) {
				LIST_REMOVE(it, next);
			} else {
				/* Local destination, check */
				pw = getpwnam(it->addr);
				if (pw == NULL)
					goto out;
				/* XXX read .forward */
				endpwent();
			}
		}
	} else {
		it->remote = 1;
	}

	return (0);

out:
	free(it->addr);
	free(it);
	return (-1);
}

static void
deltmp(void)
{
	struct stritem *t;

	SLIST_FOREACH(t, &tmpfs, next) {
		unlink(t->str);
	}
}

int
open_locked(const char *fname, int flags, ...)
{
	int mode = 0;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}

#ifndef O_EXLOCK
	int fd, save_errno;

	fd = open(fname, flags, mode);
	if (fd < 0)
		return(fd);
	if (flock(fd, LOCK_EX|((flags & O_NONBLOCK)? LOCK_NB: 0)) < 0) {
		save_errno = errno;
		close(fd);
		errno = save_errno;
		return(-1);
	}
	return(fd);
#else
	return(open(fname, flags|O_EXLOCK, mode));
#endif
}

static char *
rfc822date(void)
{
	static char str[50];
	size_t error;
	time_t now;

	now = time(NULL);
	error = strftime(str, sizeof(str), "%a, %d %b %Y %T %z",
		       localtime(&now));
	if (error == 0)
		strcpy(str, "(date fail)");
	return (str);
}

static int
strprefixcmp(const char *str, const char *prefix)
{
	return (strncasecmp(str, prefix, strlen(prefix)));
}

static int
readmail(struct queue *queue, const char *sender, int nodot)
{
	char line[1000];	/* by RFC2822 */
	size_t linelen;
	int error;
	int had_headers = 0;
	int had_from = 0;
	int had_messagid = 0;
	int had_date = 0;

	error = snprintf(line, sizeof(line),
		"Received: from %s (uid %d)\n"
		"\t(envelope-from %s)\n"
		"\tid %"PRIxMAX"\n"
		"\tby %s (%s)\n"
		"\t%s\n",
		username, uid,
		sender,
		queue->id,
		hostname(), VERSION,
		rfc822date());
	if (error < 0 || (size_t)error >= sizeof(line))
		return (-1);
	if (write(queue->mailfd, line, error) != error)
		return (-1);

	while (!feof(stdin)) {
		if (fgets(line, sizeof(line), stdin) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			errno = EINVAL;		/* XXX mark permanent errors */
			return (-1);
		}
		if (!had_headers) {
			if (strprefixcmp(line, "Date:") == 0)
				had_date = 1;
			else if (strprefixcmp(line, "Message-Id:") == 0)
				had_messagid = 1;
			else if (strprefixcmp(line, "From:") == 0)
				had_from = 1;
		}
		if (strcmp(line, "\n") == 0 && !had_headers) {
			had_headers = 1;
			while (!had_date || !had_messagid || !had_from) {
				if (!had_date) {
					had_date = 1;
					snprintf(line, sizeof(line), "Date: %s\n", rfc822date());
				} else if (!had_messagid) {
					/* XXX better msgid, assign earlier and log? */
					had_messagid = 1;
					snprintf(line, sizeof(line), "Message-Id: <%"PRIxMAX"@%s>\n",
						 queue->id, hostname());
				} else if (!had_from) {
					had_from = 1;
					snprintf(line, sizeof(line), "From: <%s>\n", sender);
				}
				if ((size_t)write(queue->mailfd, line, strlen(line)) != strlen(line))
					return (-1);
			}
			strcpy(line, "\n");
		}
		if (!nodot && linelen == 2 && line[0] == '.')
			break;
		if ((size_t)write(queue->mailfd, line, linelen) != linelen)
			return (-1);
	}
	if (fsync(queue->mailfd) != 0)
		return (-1);
	return (0);
}

static struct qitem *
go_background(struct queue *queue)
{
	struct sigaction sa;
	struct qitem *it;
	pid_t pid;

	if (daemonize && daemon(0, 0) != 0) {
		syslog(LOG_ERR, "can not daemonize: %m");
		exit(1);
	}
	daemonize = 0;

	bzero(&sa, sizeof(sa));
	sa.sa_flags = SA_NOCLDWAIT;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	LIST_FOREACH(it, &queue->queue, next) {
		/* No need to fork for the last dest */
		if (LIST_NEXT(it, next) == NULL)
			return (it);

		pid = fork();
		switch (pid) {
		case -1:
			syslog(LOG_ERR, "can not fork: %m");
			exit(1);
			break;

		case 0:
			/*
			 * Child:
			 *
			 * return and deliver mail
			 */
			return (it);

		default:
			/*
			 * Parent:
			 *
			 * fork next child
			 */
			break;
		}
	}

	syslog(LOG_CRIT, "reached dead code");
	exit(1);
}

static void
bounce(struct qitem *it, const char *reason)
{
	struct queue bounceq;
	struct qitem *bit;
	char line[1000];
	size_t pos;
	int error;

	/* Don't bounce bounced mails */
	if (it->sender[0] == 0) {
		syslog(LOG_INFO, "%s: can not bounce a bounce message, discarding",
		       it->queueid);
		exit(1);
	}

	syslog(LOG_ERR, "%s: delivery failed, bouncing",
	       it->queueid);

	LIST_INIT(&bounceq.queue);
	if (add_recp(&bounceq, it->sender, "", 1) != 0)
		goto fail;
	if (newspoolf(&bounceq, "") != 0)
		goto fail;
	bit = LIST_FIRST(&bounceq.queue);
	error = fprintf(bit->mailf,
		"Received: from MAILER-DAEMON\n"
		"\tid %"PRIxMAX"\n"
		"\tby %s (%s)\n"
		"\t%s\n"
		"X-Original-To: <%s>\n"
		"From: MAILER-DAEMON <>\n"
		"To: %s\n"
		"Subject: Mail delivery failed\n"
		"Message-Id: <%"PRIxMAX"@%s>\n"
		"Date: %s\n"
		"\n"
		"This is the %s at %s.\n"
		"\n"
		"There was an error delivering your mail to <%s>.\n"
		"\n"
		"%s\n"
		"\n"
		"%s\n"
		"\n",
		bounceq.id,
		hostname(), VERSION,
		rfc822date(),
		it->addr,
		it->sender,
		bounceq.id, hostname(),
		rfc822date(),
		VERSION, hostname(),
		it->addr,
		reason,
		config->features & FULLBOUNCE ?
		    "Original message follows." :
		    "Message headers follow.");
	if (error < 0)
		goto fail;
	if (fflush(bit->mailf) != 0)
		goto fail;

	if (fseek(it->mailf, it->hdrlen, SEEK_SET) != 0)
		goto fail;
	if (config->features & FULLBOUNCE) {
		while ((pos = fread(line, 1, sizeof(line), it->mailf)) > 0) {
			if ((size_t)write(bounceq.mailfd, line, pos) != pos)
				goto fail;
		}
	} else {
		while (!feof(it->mailf)) {
			if (fgets(line, sizeof(line), it->mailf) == NULL)
				break;
			if (line[0] == '\n')
				break;
			if ((size_t)write(bounceq.mailfd, line, strlen(line)) != strlen(line))
				goto fail;
		}
	}
	if (fsync(bounceq.mailfd) != 0)
		goto fail;
	if (linkspool(&bounceq) != 0)
		goto fail;
	/* bounce is safe */

	delqueue(it);

	bit = go_background(&bounceq);
	deliver(bit);
	/* NOTREACHED */

fail:
	syslog(LOG_CRIT, "%s: error creating bounce: %m", it->queueid);
	delqueue(it);
	exit(1);
}

static void
deliver(struct qitem *it)
{
	int error;
	unsigned int backoff = MIN_RETRY;
	const char *errmsg = "unknown bounce reason";
	struct timeval now;
	struct stat st;

	syslog(LOG_INFO, "%s: mail from=<%s> to=<%s>",
	       it->queueid, it->sender, it->addr);

retry:
	syslog(LOG_INFO, "%s: trying delivery",
	       it->queueid);

	if (it->remote)
		error = deliver_remote(it, &errmsg);
	else
		error = deliver_local(it, &errmsg);

	switch (error) {
	case 0:
		delqueue(it);
		syslog(LOG_INFO, "%s: delivery successful",
		       it->queueid);
		exit(0);

	case 1:
		if (stat(it->queuefn, &st) != 0) {
			syslog(LOG_ERR, "%s: lost queue file `%s'",
			       it->queueid, it->queuefn);
			exit(1);
		}
		if (gettimeofday(&now, NULL) == 0 &&
		    (now.tv_sec - st.st_mtimespec.tv_sec > MAX_TIMEOUT)) {
			asprintf(__DECONST(void *, &errmsg),
				 "Could not deliver for the last %d seconds. Giving up.",
				 MAX_TIMEOUT);
			goto bounce;
		}
		sleep(backoff);
		backoff *= 2;
		if (backoff > MAX_RETRY)
			backoff = MAX_RETRY;
		goto retry;

	case -1:
	default:
		break;
	}

bounce:
	bounce(it, errmsg);
	/* NOTREACHED */
}

static void
run_queue(struct queue *queue)
{
	struct qitem *it;

	if (LIST_EMPTY(&queue->queue))
		return;

	it = go_background(queue);
	deliver(it);
	/* NOTREACHED */
}

static void
show_queue(struct queue *queue)
{
	struct qitem *it;

	if (LIST_EMPTY(&queue->queue)) {
		printf("Mail queue is empty\n");
		return;
	}

	LIST_FOREACH(it, &queue->queue, next) {
		printf("ID\t: %s%s\n"
		       "From\t: %s\n"
		       "To\t: %s\n"
		       "--\n",
		       it->queueid,
		       it->locked ? "*" : "",
		       it->sender, it->addr);
	}
}

/*
 * TODO:
 *
 * - alias processing
 * - use group permissions
 * - proper sysexit codes
 */

int
main(int argc, char **argv)
{
	char *sender = NULL;
	const char *tag = "dma";
	struct qitem *it;
	struct queue queue;
	struct queue lqueue;
	int i, ch;
	int nodot = 0, doqueue = 0, showq = 0;

	atexit(deltmp);
	LIST_INIT(&queue.queue);

	if (strcmp(argv[0], "mailq") == 0) {
		argv++; argc--;
		showq = 1;
		if (argc != 0)
			errx(1, "invalid arguments");
		goto skipopts;
	}

	opterr = 0;
	while ((ch = getopt(argc, argv, "A:b:B:C:d:Df:F:h:iL:N:no:O:q:r:R:UV:vX:")) != -1) {
		switch (ch) {
		case 'A':
			/* -AX is being ignored, except for -A{c,m} */
			if (optarg[0] == 'c' || optarg[0] == 'm') {
				break;
			}
			/* else FALLTRHOUGH */
		case 'b':
			/* -bX is being ignored, except for -bp */
			if (optarg[0] == 'p') {
				showq = 1;
				break;
			}
			/* else FALLTRHOUGH */
		case 'D':
			daemonize = 0;
			break;
		case 'L':
			tag = optarg;
			break;
		case 'f':
		case 'r':
			sender = optarg;
			break;

		case 'o':
			/* -oX is being ignored, except for -oi */
			if (optarg[0] != 'i')
				break;
			/* else FALLTRHOUGH */
		case 'O':
			break;
		case 'i':
			nodot = 1;
			break;

		case 'q':
			doqueue = 1;
			break;

		/* Ignored options */
		case 'B':
		case 'C':
		case 'd':
		case 'F':
		case 'h':
		case 'N':
		case 'n':
		case 'R':
		case 'U':
		case 'V':
		case 'v':
		case 'X':
			break;

		default:
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;
	opterr = 1;

	if (argc != 0 && (showq || doqueue))
		errx(1, "sending mail and queue operations are mutually exclusive");

	if (showq + doqueue > 1)
		errx(1, "conflicting queue operations");

skipopts:
	openlog(tag, LOG_PID, LOG_MAIL);
	set_username();

	/* XXX fork root here */

	config = calloc(1, sizeof(*config));
	if (config == NULL)
		err(1, NULL);

	if (parse_conf(CONF_PATH) < 0) {
		free(config);
		err(1, "can not read config file");
	}

	if (config->features & VIRTUAL)
		if (parse_virtuser(config->virtualpath) < 0)
			err(1, "can not read virtual user file `%s'",
				config->virtualpath);

	if (parse_authfile(config->authpath) < 0)
		err(1, "can not read SMTP authentication file");

	if (showq) {
		load_queue(&lqueue, 1);
		show_queue(&lqueue);
		return (0);
	}

	if (doqueue) {
		load_queue(&lqueue, 0);
		run_queue(&lqueue);
		return (0);
	}

	if (read_aliases() != 0)
		err(1, "can not read aliases file `%s'", config->aliases);

	if ((sender = set_from(sender)) == NULL)
		err(1, NULL);

	for (i = 0; i < argc; i++) {
		if (add_recp(&queue, argv[i], sender, 1) != 0)
			errx(1, "invalid recipient `%s'", argv[i]);
	}

	if (LIST_EMPTY(&queue.queue))
		errx(1, "no recipients");

	if (newspoolf(&queue, sender) != 0)
		err(1, "can not create temp file");

	if (readmail(&queue, sender, nodot) != 0)
		err(1, "can not read mail");

	if (linkspool(&queue) != 0)
		err(1, "can not create spools");

	/* From here on the mail is safe. */

	if (config->features & DEFER)
		return (0);

	it = go_background(&queue);
	deliver(it);

	/* NOTREACHED */
	return (0);
}
