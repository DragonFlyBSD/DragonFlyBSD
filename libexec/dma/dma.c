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
 * $DragonFly: src/libexec/dma/dma.c,v 1.4 2008/09/16 17:57:22 matthias Exp $
 */

#include <sys/ipc.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_CRYPTO
#include <openssl/ssl.h>
#endif /* HAVE_CRYPTO */

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
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



static void deliver(struct qitem *, int);
static void deliver_smarthost(struct queue *, int);
static int add_recp(struct queue *, const char *, const char *, int);

struct aliases aliases = LIST_HEAD_INITIALIZER(aliases);
static struct strlist tmpfs = SLIST_HEAD_INITIALIZER(tmpfs);
struct virtusers virtusers = LIST_HEAD_INITIALIZER(virtusers);
struct authusers authusers = LIST_HEAD_INITIALIZER(authusers);
static int daemonize = 1;
struct config *config;
int controlsocket_df, clientsocket_df, controlsocket_wl, clientsocket_wl, semkey;

static void *
release_children()
{
	struct sembuf sema;
	int null = 0;

	/* 
	 * Try to decrement semaphore as we start communicating with
	 * write_to_local_user() 
	 */
	sema.sem_num = SEM_WL;
	sema.sem_op = -1;
	sema.sem_flg = 0;
	if (semop(semkey, &sema, 1) == -1) {
		err(1, "semaphore decrement failed");
	}

	/*
	 * write_to_local_user() will exit and kill dotforwardhandler(), too
	 * if the corresponding semaphore is zero
	 * otherwise nothing happens
	 */
	write(controlsocket_wl, &null, sizeof(null));

	/* 
	 * Increment semaphore as we stop communicating with 
	 * write_to_local_user()
	 */
	sema.sem_op = 1;
	if (semop(semkey, &sema, 1) == -1) {
		err(1, "semaphore decrement failed");
	}
}

char *
hostname(void)
{
	static char name[MAXHOSTNAMELEN+1];

	if (gethostname(name, sizeof(name)) != 0)
		strcpy(name, "(unknown hostname)");

	return name;
}

static char *
set_from(const char *osender)
{
	struct virtuser *v;
	char *sender;

	if ((config->features & VIRTUAL) != 0) {
		SLIST_FOREACH(v, &virtusers, next) {
			if (strcmp(v->login, getlogin()) == 0) {
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
			return(NULL);
	} else {
		if (asprintf(&sender, "%s@%s", getlogin(), hostname()) <= 0)
			return(NULL);
	}

	if (strchr(sender, '\n') != NULL) {
		errno = EINVAL;
		return(NULL);
	}

out:
	return(sender);
}

static int
read_aliases(void)
{
	yyin = fopen(config->aliases, "r");
	if (yyin == NULL)
		return(0);	/* not fatal */
	if (yyparse())
		return(-1);	/* fatal error, probably malloc() */
	fclose(yyin);
	return(0);
}

static int
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
		return(-1);
	it->addr = strdup(str);
	if (it->addr == NULL)
		return(-1);

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
			return(0);
		}
	}
	LIST_INSERT_HEAD(&queue->queue, it, next);
	if (strrchr(it->addr, '@') == NULL) {
		/* local = 1 means its a username or mailbox */
		it->local = 1;
		/* only search for aliases and .forward if asked for */
		/* needed to have the possibility to add an mailbox directly */
		if (expand) {
			/* first check /etc/aliases */
			LIST_FOREACH(al, &aliases, next) {
				if (strcmp(al->alias, it->addr) != 0)
					continue;
				SLIST_FOREACH(sit, &al->dests, next) {
					if (add_recp(queue, sit->str,
					    sender, 1) != 0)
						return(-1);
				}
				aliased = 1;
			}
			if (aliased) {
				LIST_REMOVE(it, next);
			} else {
				/* then check .forward of user */
				fd_set rfds;
				int ret;
				uint8_t len, type;
				struct sembuf sema;
				/* is the username valid */
				pw = getpwnam(it->addr);
				endpwent();
				if (pw == NULL)
					goto out;

				/*
				 * Try to decrement semaphore as we start
				 * communicating with dotforwardhandler()
				 */
				sema.sem_num = SEM_DF;
				sema.sem_op = -1;
				sema.sem_flg = 0;
				if (semop(semkey, &sema, 1) == -1) {
					err(1, "semaphore decrement failed");
				}

				/* write username to dotforwardhandler */
				len = strlen(it->addr);
				write(controlsocket_df, &len, sizeof(len));
				write(controlsocket_df, it->addr, len);
				FD_ZERO(&rfds);
				FD_SET(controlsocket_df, &rfds);

				/* wait for incoming redirects and pipes */
				while (ret =select(controlsocket_df + 1,
				    &rfds, NULL, NULL, NULL)) {
					/*
					 * Receive back list of mailboxnames
					 * and/or emailadresses
					 */
					if (ret == -1) {
						/*
						 * increment semaphore because
						 * we stopped communicating
						 * with dotforwardhandler()
						 */
						sema.sem_op = 1;
						semop(semkey, &sema, 1);
						return(-1);
					}
					/* read type of .forward entry */
					read(controlsocket_df, &type, 1);
					if (type & ENDOFDOTFORWARD) {
						/* end of .forward */
						/*
						 * If there are redirects, then
						 * we do not need the original
						 * qitem any longer
						 */
						if (aliased) {
							LIST_REMOVE(it, next);
						}
						break;
					} else if (type & ISMAILBOX) {
						/* redirect -> user/emailaddress */
						/*
						 * FIXME shall there be the possibility to use
						 * usernames instead of mailboxes?
						 */
						char *username;
						read(controlsocket_df, &len, sizeof(len));
						username = calloc(1, len + 1);
						read(controlsocket_df, username, len);
						/*
						 * Do not further expand since
						 * its remote or local mailbox
						 */
						if (add_recp(queue, username, sender, 0) != 0) {
							aliased = 1;
						}
					} else if (type & ISPIPE) {
						/* redirect to a pipe */
						/*
						 * Create new qitem and save
						 * information in it
						 */
						struct qitem *pit;
						pit = calloc(1, sizeof(*pit));
						if (pit == NULL) {
							/*
							 * Increment semaphore
							 * because we stopped
							 * communicating with
							 * dotforwardhandler()
							 */
							sema.sem_op = 1;
							semop(semkey, &sema, 1);
							return(-1);
						}
						LIST_INSERT_HEAD(&queue->queue, pit, next);
						/*
						 * Save username to qitem,
						 * because its overwritten by
						 * pipe command
						 */
						pit->pipeuser = strdup(it->addr);
						pit->sender = sender;
						/* local = 2 means redirect to pipe */
						pit->local = 2;
						read(controlsocket_df, &len, sizeof(len));
						pit->addr = realloc(pit->addr, len + 1);
						memset(pit->addr, 0, len + 1);
						read(controlsocket_df, pit->addr, len);
						aliased = 1;
					}
				}
				/*
				 * Increment semaphore because we stopped
				 * communicating with dotforwardhandler()
				 */
				sema.sem_op = 1;
				semop(semkey, &sema, 1);
			}
		}
	} else {
		it->local = 0;
	}

	return(0);

out:
	free(it->addr);
	free(it);
	return(-1);
}

static void
deltmp(void)
{
	struct stritem *t;

	SLIST_FOREACH(t, &tmpfs, next) {
		unlink(t->str);
	}
}

static int
gentempf(struct queue *queue)
{
	char fn[PATH_MAX+1];
	struct stritem *t;
	int fd;

	if (snprintf(fn, sizeof(fn), "%s/%s", config->spooldir, "tmp_XXXXXXXXXX") <= 0)
		return(-1);
	fd = mkstemp(fn);
	if (fd < 0)
		return(-1);
	queue->mailfd = fd;

	queue->tmpf = strdup(fn);
	if (queue->tmpf == NULL) {
		unlink(fn);
		return(-1);
	}
	t = malloc(sizeof(*t));
	if (t != NULL) {
		t->str = queue->tmpf;
		SLIST_INSERT_HEAD(&tmpfs, t, next);
	}
	return(0);
}

/*
 * spool file format:
 *
 * envelope-from
 * queue-id1 envelope-to1
 * queue-id2 envelope-to2
 * ...
 * <empty line>
 * mail data
 *
 * queue ids are unique, formed from the inode of the spool file
 * and a unique identifier.
 */
static int
preparespool(struct queue *queue, const char *sender)
{
	char line[1000];	/* by RFC2822 */
	struct stat st;
	int error;
	struct qitem *it;
	FILE *queuef;
	off_t hdrlen;

	error = snprintf(line, sizeof(line), "%s\n", sender);
	if (error < 0 || (size_t)error >= sizeof(line)) {
		errno = E2BIG;
		return(-1);
	}
	if (write(queue->mailfd, line, error) != error)
		return(-1);

	queuef = fdopen(queue->mailfd, "r+");
	if (queuef == NULL)
		return(-1);

	/*
	 * Assign queue id to each dest.
	 */
	if (fstat(queue->mailfd, &st) != 0)
		return(-1);
	queue->id = st.st_ino;
	LIST_FOREACH(it, &queue->queue, next) {
		if (asprintf(&it->queueid, "%"PRIxMAX".%"PRIxPTR,
			     queue->id, (uintptr_t)it) <= 0)
			return(-1);
		if (asprintf(&it->queuefn, "%s/%s",
			     config->spooldir, it->queueid) <= 0)
			return(-1);
		/* File may already exist */
		if (stat(it->queuefn, &st) == 0) {
			warn("Spoolfile already exists: %s", it->queuefn);
			return(-1);
		}
		/* Reset errno to avoid confusion */
		errno = 0;
		it->queuef = queuef;
		error = snprintf(line, sizeof(line), "%s %s\n",
			       it->queueid, it->addr);
		if (error < 0 || (size_t)error >= sizeof(line))
			return(-1);
		if (write(queue->mailfd, line, error) != error)
			return(-1);
	}
	line[0] = '\n';
	if (write(queue->mailfd, line, 1) != 1)
		return(-1);

	hdrlen = lseek(queue->mailfd, 0, SEEK_CUR);
	LIST_FOREACH(it, &queue->queue, next) {
		it->hdrlen = hdrlen;
	}
	return(0);
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
	return(str);
}

static int
readmail(struct queue *queue, const char *sender, int nodot)
{
	char line[1000];	/* by RFC2822 */
	size_t linelen;
	int error;

	error = snprintf(line, sizeof(line), "\
Received: from %s (uid %d)\n\
\t(envelope-from %s)\n\
\tid %"PRIxMAX"\n\
\tby %s (%s)\n\
\t%s\n",
		getlogin(), getuid(),
		sender,
		queue->id,
		hostname(), VERSION,
		rfc822date());
	if (error < 0 || (size_t)error >= sizeof(line))
		return(-1);
	if (write(queue->mailfd, line, error) != error)
		return(-1);

	while (!feof(stdin)) {
		if (fgets(line, sizeof(line), stdin) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			errno = EINVAL;		/* XXX mark permanent errors */
			return(-1);
		}
		if (!nodot && linelen == 2 && line[0] == '.')
			break;
		if ((size_t)write(queue->mailfd, line, linelen) != linelen)
			return(-1);
	}
	if (fsync(queue->mailfd) != 0)
		return(-1);
	return(0);
}

static int
linkspool(struct queue *queue)
{
	struct qitem *it;

	/*
	 * Only if it is not a pipe delivery
	 * pipe deliveries are only tried once so there
	 * is no need for a spool-file, they use the
	 * original tempfile
	 */

	LIST_FOREACH(it, &queue->queue, next) {
		/* 
		 * There shall be no files for pipe deliveries since not all
		 * information is saved in the header, so pipe delivery is
		 * tried once and forgotten thereafter.
		 */
		if (it->local == 2) 
			continue;
		if (link(queue->tmpf, it->queuefn) != 0)
			goto delfiles;
	}
	return(0);

delfiles:
	LIST_FOREACH(it, &queue->queue, next) {
		/*
		 * There are no files for pipe delivery, so they can't be
		 * deleted.
		 */
		if (it->local == 2) 
			continue;
		unlink(it->queuefn);
	}
	return(-1);
}

static void
go_background(struct queue *queue, int leavesemaphore)
{
	struct sigaction sa;
	struct qitem *it;
	pid_t pid;
	int seen_remote_address = 0;

	if (daemonize && daemon(0, 0) != 0) {
		syslog(LOG_ERR, "[go_background] can not daemonize: %m");
		exit(1);
	}
	daemonize = 0;
	bzero(&sa, sizeof(sa));
	sa.sa_flags = SA_NOCLDWAIT;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);


	LIST_FOREACH(it, &queue->queue, next) {
		/* 
		 * If smarthost is enabled, the address is remote
		 * set smarthost delivery flag, otherwise deliver it 'normal'.
		 */
		if (config->smarthost != NULL && strlen(config->smarthost) > 0
		    && it->local == 0
		   ) {
			seen_remote_address = 1;
			/*
			 * if it is not the last entry, continue
			 * (if it is the last, start delivery in parent
			 */
			if (LIST_NEXT(it, next) != NULL) {
				continue;
			}
		} else {
			/*
			 * If item is local, we do not need it in the list any
			 * more, so delete it.
			 */
			LIST_REMOVE(it, next);
		}
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

			if (config->smarthost == NULL || strlen(config->smarthost) == 0 || it->local)
				if (LIST_NEXT(it, next) == NULL && !seen_remote_address)
					/* if there is no smarthost-delivery and we are the last item */
					deliver(it, leavesemaphore);
				else 
					deliver(it, 0);
			else
				_exit(0);

		default:
			/*
			 * Parent:
			 *
			 * fork next child
			 */
			/*
			 * If it is the last loop and there were remote
			 * addresses, start smarthost delivery.
			 * No need to doublecheck if smarthost is
			 * activated in config file.
			 */
			if (LIST_NEXT(it, next) == NULL) {
				if (seen_remote_address) {
					deliver_smarthost(queue, leavesemaphore);
				} else {
					_exit(0);
				}
			} 
			break;
		}
	}

	syslog(LOG_CRIT, "reached dead code");
	exit(1);
}

static void
bounce(struct qitem *it, const char *reason, int leavesemaphore)
{
	struct queue bounceq;
	struct qitem *bit;
	char line[1000];
	int error;
	struct sembuf sema;

	/* Don't bounce bounced mails */
	if (it->sender[0] == 0) {
		/*
		 * If we are the last bounce, then decrement semaphore
		 * and release children.
		 */
		if (leavesemaphore) {
			/* semaphore-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = -1;
			sema.sem_flg = IPC_NOWAIT;
			if (semop(semkey, &sema, 1) == -1) {
				err(1, "[deliver] semaphore decrement failed");
			}
			/* release child processes */
			release_children();
		}
		syslog(LOG_CRIT, "%s: delivery panic: can't bounce a bounce",
			it->queueid);
		exit(1);
	}

	syslog(LOG_ERR, "%s: delivery failed, bouncing",
	       it->queueid);

	LIST_INIT(&bounceq.queue);
	if (add_recp(&bounceq, it->sender, "", 1) != 0)
		goto fail;
	if (gentempf(&bounceq) != 0)
		goto fail;
	if (preparespool(&bounceq, "") != 0)
		goto fail;

	bit = LIST_FIRST(&bounceq.queue);
	error = fprintf(bit->queuef, "\
Received: from MAILER-DAEMON\n\
\tid %"PRIxMAX"\n\
\tby %s (%s)\n\
\t%s\n\
X-Original-To: <%s>\n\
From: MAILER-DAEMON <>\n\
To: %s\n\
Subject: Mail delivery failed\n\
Message-Id: <%"PRIxMAX"@%s>\n\
Date: %s\n\
\n\
This is the %s at %s.\n\
\n\
There was an error delivering your mail to <%s>.\n\
\n\
%s\n\
\n\
Message headers follow.\n\
\n\
",
		bounceq.id,
		hostname(), VERSION,
		rfc822date(),
		it->addr,
		it->sender,
		bounceq.id, hostname(),
		rfc822date(),
		VERSION, hostname(),
		it->addr,
		reason);
	if (error < 0)
		goto fail;
	if (fflush(bit->queuef) != 0)
		goto fail;

	if (fseek(it->queuef, it->hdrlen, SEEK_SET) != 0)
		goto fail;
	while (!feof(it->queuef)) {
		if (fgets(line, sizeof(line), it->queuef) == NULL)
			break;
		if (line[0] == '\n')
			break;
		write(bounceq.mailfd, line, strlen(line));
	}
	if (fsync(bounceq.mailfd) != 0)
		goto fail;
	if (linkspool(&bounceq) != 0)
		goto fail;
	/* bounce is safe */

	unlink(it->queuefn);
	fclose(it->queuef);

	go_background(&bounceq, leavesemaphore);
	/* NOTREACHED */

fail:
	/*
	 * If we are the last bounce, then decrement semaphore
	 * and release children.
	 */
	if (leavesemaphore) {
		/* semaphore-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
		sema.sem_num = SEM_SIGHUP;
		sema.sem_op = -1;
		sema.sem_flg = IPC_NOWAIT;
		if (semop(semkey, &sema, 1) == -1) {
			err(1, "[deliver] semaphore decrement failed");
		}
		/* release child processes */
		release_children();
	}
	syslog(LOG_CRIT, "%s: error creating bounce: %m", it->queueid);
	unlink(it->queuefn);
	exit(1);
}

static int
deliver_local(struct qitem *it, const char **errmsg)
{
	char line[1000];
	char fn[PATH_MAX+1];
	int len;
	uint8_t mode = 0, fail = 0;
	size_t linelen;
	time_t now = time(NULL);
	char *username;
	struct sembuf sema;


	/*
	 * Try to decrement semaphore as we start communicating with
	 * write_to_local_user()
	 */
	sema.sem_num = SEM_WL;
	sema.sem_op = -1;
	sema.sem_flg = 0;
	if (semop(semkey, &sema, 1) == -1) {
		err(1, "semaphore decrement failed");
	}


	/* Tell write_to_local_user() the username to drop the privileges */
	if (it->local == 1) { /* mailbox delivery */
		username = it->addr;
	} else if (it->local == 2) { /* pipe delivery */
		username = it->pipeuser;
	}
	len = strlen(username);
	write(controlsocket_wl, &len, sizeof(len));
	write(controlsocket_wl, username, len);
	read(controlsocket_wl, &fail, sizeof(fail));
	if (fail) {
		syslog(LOG_ERR,
		 	"%s: local delivery deferred: can not fork and drop privileges `%s': %m",
			it->queueid, username);
		/*
		 * Increment semaphore because we stopped communicating with
		 * write_to_local_user().
		 */
		sema.sem_op = 1;
		semop(semkey, &sema, 1);
		return(1);
	}


	/* Tell write_to_local_user() the delivery mode (write to mailbox || pipe) */
	if (it->local == 1) { /* mailbox delivery */
		mode = ISMAILBOX;
		len = snprintf(fn, sizeof(fn), "%s/%s", _PATH_MAILDIR, it->addr);
		if (len < 0 || (size_t)len >= sizeof(fn)) {
			syslog(LOG_ERR, "%s: local delivery deferred: %m",
					it->queueid);
			/*
			 * Increment semaphore because we stopped communicating
			 * with write_to_local_user().
			 */
			sema.sem_op = 1;
			semop(semkey, &sema, 1);
			return(1);
		}
	} else if (it->local == 2) { /* pipe delivery */
		mode = ISPIPE;
		strncpy(fn, it->addr, sizeof(fn));
		len = strlen(fn);
	}
	write(controlsocket_wl, &len, sizeof(len));
	write(controlsocket_wl, fn, len);
	write(controlsocket_wl, &mode, sizeof(mode));
	read(controlsocket_wl, &fail, sizeof(fail));
	if (fail) {
		errno = fail;
		syslog(LOG_ERR,
			"%s: local delivery deferred: can not (p)open `%s': %m",
			it->queueid, it->addr);
		/*
		 * Increment semaphore because we stopped communicating
		 * with write_to_local_user().
		 */
		sema.sem_op = 1;
		semop(semkey, &sema, 1);
		return(1);
	}


	/* Prepare transfer of mail-data */
	if (fseek(it->queuef, it->hdrlen, SEEK_SET) != 0) {
		syslog(LOG_ERR, "%s: local delivery deferred: can not seek: %m",
		       it->queueid);
		/*
		 * Increment semaphore because we stopped communicating
		 * with write_to_local_user().
		 */
		sema.sem_op = 1;
		semop(semkey, &sema, 1);
		return(1);
	}


	/* Send first header line. */
	linelen = snprintf(line, sizeof(line), "From %s\t%s", it->sender, ctime(&now));
	if (linelen < 0 || (size_t)linelen >= sizeof(line)) {
		syslog(LOG_ERR, "%s: local delivery deferred: can not write header: %m",
		       it->queueid);
		/*
		 * Increment semaphore because we stopped communicating
		 * with write_to_local_user().
		 */
		sema.sem_op = 1;
		semop(semkey, &sema, 1);
		return(1);
	}

	write(controlsocket_wl, &linelen, sizeof(linelen));
	write(controlsocket_wl, line, linelen);

	read(controlsocket_wl, &fail, sizeof(fail));
	if (fail) {
		goto wrerror;
	}


	/* Read mail data and transfer it to write_to_local_user(). */
	while (!feof(it->queuef)) {
		if (fgets(line, sizeof(line), it->queuef) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			syslog(LOG_CRIT,
				"%s: local delivery failed: corrupted queue file",
				it->queueid);
			*errmsg = "corrupted queue file";
			len = -1;
			/* break receive and write loop at write_to_local_user() */
			linelen = 0;
			write(controlsocket_wl, &linelen, sizeof(linelen)); 
			/* and send error state */
			linelen = 1;
			write(controlsocket_wl, &linelen, sizeof(linelen)); 
			goto chop;
		}

		if (strncmp(line, "From ", 5) == 0) {
			const char *gt = ">";
			size_t sizeofchar = 1;

			write(controlsocket_wl, &sizeofchar, sizeof(sizeofchar));
			write(controlsocket_wl, gt, 1);
			read(controlsocket_wl, &fail, sizeof(fail));
			if (fail) {
				goto wrerror;
			}
		}
		write(controlsocket_wl, &linelen, sizeof(linelen)); 
		write(controlsocket_wl, line, linelen); 
		read(controlsocket_wl, &fail, sizeof(fail));
		if (fail) {
			goto wrerror;
		}
	}

	/* Send final linebreak */
	line[0] = '\n';
	linelen = 1;
	write(controlsocket_wl, &linelen, sizeof(linelen)); 
	write(controlsocket_wl, line, linelen); 
	read(controlsocket_wl, &fail, sizeof(fail));
	if (fail) {
		goto wrerror;
	}


	/* break receive and write loop in write_to_local_user() */
	linelen = 0;
	/* send '0' twice, because above we send '0' '1' in case of error */
	write(controlsocket_wl, &linelen, sizeof(linelen)); 
	write(controlsocket_wl, &linelen, sizeof(linelen)); 
	read(controlsocket_wl, &fail, sizeof(fail));
	if (fail) {
		goto wrerror;
	}


	/*
	 * Increment semaphore because we stopped communicating
	 * with write_to_local_user().
	 */
	sema.sem_op = 1;
	semop(semkey, &sema, 1);
	return(0);

wrerror:
	errno = fail;
	syslog(LOG_ERR, "%s: local delivery failed: write error: %m",
	       it->queueid);
	len = 1;
chop:
	read(controlsocket_wl, &fail, sizeof(fail));
	if (fail == 2) {
		syslog(LOG_WARNING, "%s: error recovering mbox `%s': %m",
			it->queueid, fn);
	}
	/*
	 * Increment semaphore because we stopped communicating
	 * with write_to_local_user().
	 */
	sema.sem_op = 1;
	semop(semkey, &sema, 1);
	return(len);
}

static void
deliver(struct qitem *it, int leavesemaphore)
{
	int error;
	unsigned int backoff = MIN_RETRY;
	const char *errmsg = "unknown bounce reason";
	struct timeval now;
	struct stat st;
	struct sembuf sema;

	if (it->local == 2) {
		syslog(LOG_INFO, "%s: mail from=<%s> to=<%s> command=<%s>",
				it->queueid, it->sender, it->pipeuser, it->addr);
	} else {
		syslog(LOG_INFO, "%s: mail from=<%s> to=<%s>",
				it->queueid, it->sender, it->addr);
	}

retry:
	syslog(LOG_INFO, "%s: trying delivery",
	       it->queueid);

	/*
	 * Only increment semaphore, if we are not the last bounce
	 * because there is still a incremented semaphore from
	 * the bounced delivery
	 */
	if (!leavesemaphore) {
		/*
		 * Increment semaphore for each mail we try to deliver.
		 * When completing the transmit, the semaphore is decremented.
		 * If the semaphore is zero the other childs know that they
		 * can terminate.
		 */
		sema.sem_num = SEM_SIGHUP;
		sema.sem_op = 1;
		sema.sem_flg = 0;
		if (semop(semkey, &sema, 1) == -1) {
			err(1, "[deliver] semaphore increment failed");
		}
	}
	if (it->local) {
		error = deliver_local(it, &errmsg);
	} else {
		error = deliver_remote(it, &errmsg, NULL);
	}

	switch (error) {
	case 0:
		/* semaphore-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
		sema.sem_num = SEM_SIGHUP;
		sema.sem_op = -1;
		sema.sem_flg = IPC_NOWAIT;
		if (semop(semkey, &sema, 1) == -1) {
			err(1, "[deliver] semaphore decrement failed");
		}
		/* release child processes */
		release_children();
		/* Do not try to delete the spool file: pipe mode */
		if (it->local != 2) 
			unlink(it->queuefn);
		syslog(LOG_INFO, "%s: delivery successful",
		       it->queueid);
		exit(0);

	case 1:
		/* pipe delivery only tries once, then gives up */
		if (it->local == 2) {
			/* decrement-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = -1;
			sema.sem_flg = IPC_NOWAIT;
			if (semop(semkey, &sema, 1) == -1) {
				err(1, "[deliver] semaphore decrement failed");
			}
			/* release child processes */
			release_children();
			syslog(LOG_ERR, "%s: delivery to pipe `%s' failed, giving up",
			       it->queueid, it->addr);
			exit(1);
		}
		if (stat(it->queuefn, &st) != 0) {
			/* semaphore-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = -1;
			sema.sem_flg = IPC_NOWAIT;
			if (semop(semkey, &sema, 1) == -1) {
				err(1, "[deliver] semaphore decrement failed");
			}
			/* release child processes */
			release_children();
			syslog(LOG_ERR, "%s: lost queue file `%s'",
			       it->queueid, it->queuefn);
			exit(1);
		}
		if (gettimeofday(&now, NULL) == 0 &&
		    (now.tv_sec - st.st_mtimespec.tv_sec > MAX_TIMEOUT)) {
			char *msg;

			if (asprintf(&msg,
			    "Could not deliver for the last %d seconds. Giving up.",
			    MAX_TIMEOUT) > 0)
				errmsg = msg;
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
	bounce(it, errmsg, 1);
	/* NOTREACHED */
}

/*
 * deliver_smarthost() is similar to deliver(), but has some differences:
 * -deliver_smarthost() works with a queue
 * -each entry in this queue has a corresponding file in the spooldir
 * -if the mail is sent correctly to a address, delete the corresponding file,
 *  	even if there were errors with other addresses
 * -so deliver_remote must tell deliver_smarthost to which addresses it has
 *  successfully sent the mail
 *  -this can be done with 3 queues:
 *   -one queue for sent mails
 *   -one queue for 4xx addresses (tempfail)
 *   -one queue for 5xx addresses (permfail)
 *  -the sent mails are deleted
 *  -the 4xx are tried again
 *  -the 5xx are bounced
 */

static void
deliver_smarthost(struct queue *queue, int leavesemaphore)
{
	int error, bounces = 0;
	unsigned int backoff = MIN_RETRY;
	const char *errmsg = "unknown bounce reason";
	struct timeval now;
	struct stat st;
	struct sembuf sema;
	struct qitem *it, *tit;
	struct queue *queues[4], *bouncequeue, successqueue, tempfailqueue,
		permfailqueue;

	/*
	 * only increment semaphore, if we are not the last bounce
	 * because there is still a incremented semaphore from
	 * the bounced delivery
	 */
	if (!leavesemaphore) {
		/*
		 * Increment semaphore for each mail we try to deliver.
		 * When completing the transmit, the semaphore is decremented.
		 * If the semaphore is zero the other childs know that they
		 * can terminate.
		 */
		sema.sem_num = SEM_SIGHUP;
		sema.sem_op = 1;
		sema.sem_flg = 0;
		if (semop(semkey, &sema, 1) == -1) {
			err(1, "[deliver] semaphore increment failed");
		}
	}

	queues[0] = queue;
	queues[1] = &successqueue;
	queues[2] = &tempfailqueue;
	queues[3] = &permfailqueue;

retry:
	/* initialise 3 empty queues and link it in queues[] */
	LIST_INIT(&queues[1]->queue); /* successful sent items */
	LIST_INIT(&queues[2]->queue); /* temporary error items */
	LIST_INIT(&queues[3]->queue); /* permanent error items */

	it = LIST_FIRST(&queues[0]->queue);

	syslog(LOG_INFO, "%s: trying delivery",
	       it->queueid);

	/* if queuefile of first qitem is gone, the mail can't be sended out */
	if (stat(it->queuefn, &st) != 0) {
			syslog(LOG_ERR, "%s: lost queue file `%s'",
			       it->queueid, it->queuefn);
			/* semaphore-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = -1;
			sema.sem_flg = IPC_NOWAIT;
			if (semop(semkey, &sema, 1) == -1) {
				err(1, "[deliver] semaphore decrement failed");
			}
			release_children();
			exit(1);
	}

	error = deliver_remote(it, &errmsg, queues);

	/* if there was an error, do nothing with the other 3 queues! */
	if (error == 0) {

		/*
		 * If there are permanent errors, bounce items in permanent
		 * error queue.
		 */
		if (!LIST_EMPTY(&queues[3]->queue)) {
			bounces = 1;
			pid_t pid;
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
					 * Tell which queue to bounce and set 
					 * errmsg.  Child will exit as soon as
					 * all childs for bounces are spawned.
					 * So no need to set up a signal handler.
					 */
					bouncequeue = queues[3];
					errmsg = "smarthost sent permanent error (5xx)";
					goto bounce;

				default:
					/*
					 * Parent:
					 *
					 * continue with stuff
					 */
					break;
			}
		}

		/* delete successfully sent items */
		if (!LIST_EMPTY(&queues[1]->queue)) {
			LIST_FOREACH(tit, &queues[1]->queue, next) {
				unlink(tit->queuefn);
				LIST_REMOVE(tit, next);
				syslog(LOG_INFO, "%s: delivery successful",
						tit->queueid);
			}
		}
	}

	/* If the temporary error queue is empty and there was no error, finish */
	if (LIST_EMPTY(&queues[2]->queue) && error == 0) {
		/* only decrement semaphore if there were no bounces! */
		if (!bounces) {
			/* semaphore-- (MUST NOT BLOCK BECAUSE ITS POSITIVE) */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = -1;
			sema.sem_flg = IPC_NOWAIT;
			if (semop(semkey, &sema, 1) == -1) {
				err(1, "[deliver] semaphore decrement failed");
			}
			/* release child processes */
			release_children();
		}
		exit(0);

		/* if there are remaining items, set up retry timer */
	} else {

		/*
		 * if there was an error, do not touch queues[0]!
		 * and try to deliver all items again
		 */

		if (!error) {
			/* wipe out old queue */
			if (!LIST_EMPTY(&queues[0]->queue)) {
				LIST_FOREACH(tit, &queues[0]->queue, next) {
					unlink(tit->queuefn);
					LIST_REMOVE(tit, next);
				}
				LIST_INIT(&queues[0]->queue);
			}
			/* link temporary error queue to queues[0] */
			queues[0] = &tempfailqueue;
			/* and link queues[2] to wiped out queue */
			queues[2] = queue;
		}

		if (gettimeofday(&now, NULL) == 0 &&
		    (now.tv_sec - st.st_mtimespec.tv_sec > MAX_TIMEOUT)) {
			char *msg;

			if (asprintf(&msg,
				"Could not deliver for the last %d seconds. Giving up.",
				MAX_TIMEOUT) > 0) {
				errmsg = msg;
			}
			/* bounce remaining items which have temporary errors */
			bouncequeue = queues[2];
			goto bounce;
		}
		sleep(backoff);
		backoff *= 2;
		if (backoff > MAX_RETRY)
			backoff = MAX_RETRY;
		goto retry;
	}

bounce:
	LIST_FOREACH(tit, &bouncequeue->queue, next) {
		struct sigaction sa;
		pid_t pid;
		bzero(&sa, sizeof(sa));
		sa.sa_flags = SA_NOCLDWAIT;
		sa.sa_handler = SIG_IGN;
		sigaction(SIGCHLD, &sa, NULL);

		/* fork is needed, because bounce() does not return */
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
				 * bounce mail
				 */

				LIST_REMOVE(tit, next);
				if (LIST_NEXT(tit, next) == NULL) {
					/*
					 * For the last bounce, do not increment
					 * the semaphore when delivering the
					 * bounce.
					 */
					bounce(tit, errmsg, 1);
				} else {
					bounce(tit, errmsg, 0);
				}
				/* NOTREACHED */

			default:
				/*
				 * Parent:
				 */
				break;
		}

	}
	/* last parent shall exit, too */
	_exit(0);
 	/* NOTREACHED */
}

static void
load_queue(struct queue *queue)
{
	struct stat st;
	struct qitem *it;
	//struct queue queue, itmqueue;
	struct queue itmqueue;
	DIR *spooldir;
	struct dirent *de;
	char line[1000];
	char *fn;
	FILE *queuef;
	char *sender;
	char *addr;
	char *queueid;
	char *queuefn;
	off_t hdrlen;
	int fd;

	LIST_INIT(&queue->queue);

	spooldir = opendir(config->spooldir);
	if (spooldir == NULL)
		err(1, "reading queue");

	while ((de = readdir(spooldir)) != NULL) {
		sender = NULL;
		queuef = NULL;
		queueid = NULL;
		queuefn = NULL;
		fn = NULL;
		LIST_INIT(&itmqueue.queue);

		/* ignore temp files */
		if (strncmp(de->d_name, "tmp_", 4) == 0 ||
		    de->d_type != DT_REG)
			continue;
		if (asprintf(&queuefn, "%s/%s", config->spooldir, de->d_name) < 0)
			goto fail;
		fd = open(queuefn, O_RDONLY|O_EXLOCK|O_NONBLOCK);
		if (fd < 0) {
			/* Ignore locked files */
			if (errno == EWOULDBLOCK)
				continue;
			goto skip_item;
		}

		queuef = fdopen(fd, "r");
		if (queuef == NULL)
			goto skip_item;
		if (fgets(line, sizeof(line), queuef) == NULL ||
		    line[0] == 0)
			goto skip_item;
		line[strlen(line) - 1] = 0;	/* chop newline */
		sender = strdup(line);
		if (sender == NULL)
			goto skip_item;

		for (;;) {
			if (fgets(line, sizeof(line), queuef) == NULL ||
			    line[0] == 0)
				goto skip_item;
			if (line[0] == '\n')
				break;
			line[strlen(line) - 1] = 0;
			queueid = strdup(line);
			if (queueid == NULL)
				goto skip_item;
			addr = strchr(queueid, ' ');
			if (addr == NULL)
				goto skip_item;
			*addr++ = 0;
			if (fn != NULL)
				free(fn);
			if (asprintf(&fn, "%s/%s", config->spooldir, queueid) < 0)
				goto skip_item;
			/* Item has already been delivered? */
			if (stat(fn, &st) != 0)
				continue;
			if (add_recp(&itmqueue, addr, sender, 0) != 0)
				goto skip_item;
			it = LIST_FIRST(&itmqueue.queue);
			it->queuef = queuef;
			it->queueid = queueid;
			it->queuefn = fn;
			fn = NULL;
		}
		if (LIST_EMPTY(&itmqueue.queue)) {
			warnx("queue file without items: `%s'", queuefn);
			goto skip_item2;
		}
		hdrlen = ftell(queuef);
		while ((it = LIST_FIRST(&itmqueue.queue)) != NULL) {
			it->hdrlen = hdrlen;
			LIST_REMOVE(it, next);
			LIST_INSERT_HEAD(&queue->queue, it, next);
		}
		continue;

skip_item:
		warn("reading queue: `%s'", queuefn);
skip_item2:
		if (sender != NULL)
			free(sender);
		if (queuefn != NULL)
			free(queuefn);
		if (fn != NULL)
			free(fn);
		if (queueid != NULL)
			free(queueid);
		close(fd);
	}
	closedir(spooldir);
	return;

fail:
	err(1, "reading queue");
}

static void
run_queue(struct queue *queue)
{
	struct qitem *it;
	if (LIST_EMPTY(&queue->queue))
		return;

	go_background(queue, 0);
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
		printf("\
ID\t: %s\n\
From\t: %s\n\
To\t: %s\n--\n", it->queueid, it->sender, it->addr);
	}
}

/*
 * TODO:
 *
 * - alias processing
 * - use group permissions
 * - proper sysexit codes
 */

static int
parseandexecute(int argc, char **argv)
{
	char *sender = NULL;
	char tag[255];
	struct qitem *it;
	struct queue queue;
	struct queue lqueue;
	int i, ch;
	int nodot = 0, doqueue = 0, showq = 0;
	uint8_t null = 0, recipient_add_success = 0;

	atexit(deltmp);
	LIST_INIT(&queue.queue);
	snprintf(tag, 254, "dma");

	opterr = 0;
	while ((ch = getopt(argc, argv, "A:b:Df:iL:o:O:q:r:")) != -1) {
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
			if (optarg != NULL)
				snprintf(tag, 254, "%s", optarg);
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

		default:
			release_children();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;
	opterr = 1;

	openlog(tag, LOG_PID | LOG_PERROR, LOG_MAIL);

	config = malloc(sizeof(struct config));
	if (config == NULL)
		errx(1, "Cannot allocate enough memory");

	memset(config, 0, sizeof(struct config));
	if (parse_conf(CONF_PATH, config) < 0) {
		free(config);
		release_children();
		errx(1, "reading config file");
	}

	if (config->features & VIRTUAL)
		if (parse_virtuser(config->virtualpath) < 0) {
			release_children();
			errx(1, "error reading virtual user file: %s",
				config->virtualpath);
		}

	if (parse_authfile(config->authpath) < 0) {
		release_children();
		err(1, "reading SMTP authentication file");
	}

	if (showq) {
		if (argc != 0)
			errx(1, "sending mail and displaying queue is"
				" mutually exclusive");
		load_queue(&lqueue);
		show_queue(&lqueue);
		return(0);
	}

	if (doqueue) {
		if (argc != 0)
			errx(1, "sending mail and queue pickup is mutually exclusive");
		load_queue(&lqueue);
		run_queue(&lqueue);
		return(0);
	}

	if (read_aliases() != 0) {
		release_children();
		err(1, "reading aliases");
	}

	if ((sender = set_from(sender)) == NULL) {
		release_children();
		err(1, "setting from address");
	}

	if (gentempf(&queue) != 0) {
		release_children();
		err(1, "create temp file");
	}

	for (i = 0; i < argc; i++) {
		if (add_recp(&queue, argv[i], sender, 1) != 0) {
			release_children();
			errx(1, "invalid recipient `%s'\n", argv[i]);
		}
	}

	if (LIST_EMPTY(&queue.queue)) {
		release_children();
		errx(1, "no recipients");
	}

	if (preparespool(&queue, sender) != 0) {
		release_children();
		err(1, "creating spools (1)");
	}

	if (readmail(&queue, sender, nodot) != 0) {
		release_children();
		err(1, "reading mail");
	}

	if (linkspool(&queue) != 0) {
		release_children();
		err(1, "creating spools (2)");
	}

	/* From here on the mail is safe. */

	if (config->features & DEFER)
		return(0);

	go_background(&queue, 0);

	/* NOTREACHED */

	return(0);
}

/*
 * dotforwardhandler() waits for incoming username
 * for each username, the .forward file is read and parsed
 * earch entry is given back to add_recp which communicates
 * with dotforwardhandler()
 */
static int
dotforwardhandler()
{
	pid_t pid;
	fd_set rfds;
	int ret;
	uint8_t stmt, namelength;

	FD_ZERO(&rfds);
	FD_SET(clientsocket_df, &rfds);

	/* wait for incoming usernames */
	ret = select(clientsocket_df + 1, &rfds, NULL, NULL, NULL);
	if (ret == -1) {
		return(-1);
	}
	while (read(clientsocket_df, &namelength, sizeof(namelength))) {
		char *username;
		struct passwd *userentry;
		if (namelength == 0) {
			/* there will be no more usernames, we can terminate */
			break;
		}
		/* read username and get homedir */
		username = calloc(1, namelength + 1);
		read(clientsocket_df, username, namelength);
		userentry = getpwnam(username);
		endpwent();

		pid = fork();
		if (pid == 0) { /* child */
			FILE *forward;
			char *dotforward;
			/* drop privileges to user */
			if (chdir("/"))
				return(-1);
			if (initgroups(username, userentry->pw_gid))
				return(-1);
			if (setgid(userentry->pw_gid))
				return(-1);
			if (setuid(userentry->pw_uid))
				return(-1);

			/* read ~/.forward */
			dotforward = strdup(userentry->pw_dir);
			forward = fopen(strcat(dotforward, "/.forward"), "r");
			if (forward == NULL) { /* no dotforward */
				stmt = ENDOFDOTFORWARD;
				write(clientsocket_df, &stmt, 1);
				continue;
			}


			/* parse ~/.forward */
			while (!feof(forward)) { /* each line in ~/.forward */
				char *target = NULL;
				/* 255 Bytes should be enough for a pipe and a emailaddress */
				uint8_t len;
				char line[2048];
				memset(line, 0, 2048);
				fgets(line, sizeof(line), forward);
				/* FIXME allow comments? */
				if ((target = strtok(line, "\t\n")) != NULL)
				if (strncmp(target, "|", 1) == 0) {
					/* if first char is a '|', the line is a pipe */
					stmt = ISPIPE;
					write(clientsocket_df, &stmt, 1);
					len = strlen(target);
					/* remove the '|' */
					len--;
					/* send result back to add_recp */
					write(clientsocket_df, &len, sizeof(len));
					write(clientsocket_df, target + 1, len);
				} else {
					/* if first char is not a '|', the line is a mailbox */
					stmt = ISMAILBOX;
					write(clientsocket_df, &stmt, 1);
					len = strlen(target);
					/* send result back to add_recp */
					write(clientsocket_df, &len, sizeof(len));
					write(clientsocket_df, target, len);
				}
			}
			stmt = ENDOFDOTFORWARD;
			/* send end of .forward to add_recp */
			write(clientsocket_df, &stmt, 1);
			_exit(0);
		} else if (pid < 0) { /* fork failed */
			return(1);
		} else { /* parent */
			/* parent waits while child is processing .forward */
			waitpid(-1, NULL, 0);
		}
	}
}

/*
 * write_to_local_user() writes to a mailbox or
 * to a pipe in a user context and communicates with deliver_local()
 */
static int
write_to_local_user() {
	pid_t pid;
	int length;
	size_t linelen;

	/* wait for incoming targets */
	while (read(clientsocket_wl, &length, sizeof(length))) {
		char *target;
		uint8_t mode, fail = 0;
		char fn[PATH_MAX+1];
		char line[1000];
		int mbox;
		off_t mboxlen;
		FILE *pipe;
		int error;
		pid_t pid;
		struct passwd *userentry;

		target = calloc(1, length + 1);
		if (length == 0) {
			struct sembuf sema;
			int retval;
			/* check if semaphore is '0' */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = 0;
			sema.sem_flg = IPC_NOWAIT;
			retval = semop(semkey, &sema, 1);
			if (retval == 0 || errno == EINVAL) {
				/*
				 * if semaphore is '0' then the last mail is sent
				 * and there is no need for a write_to_local_user()
				 * so we can exit
				 *
				 * if errno is EINVAL, then someone has removed the semaphore, so we shall exit, too
				 */
				break;
			} else {
				continue;
			}
		}
		/* read username and get uid/gid */
		read(clientsocket_wl, target, length);

		userentry = getpwnam(target);
		endpwent();

		pid = fork();
		if (pid == 0) { /* child */
			/* drop privileges to user and tell if there is something wrong */
			if (chdir("/")) {
				fail = errno;
				write(clientsocket_wl, &fail, sizeof(fail));
				fail = 0;
				write(clientsocket_wl, &fail, sizeof(fail));
				free(target);
				_exit(1);
			}
			if (initgroups(target, userentry->pw_gid)) {
				fail = errno;
				write(clientsocket_wl, &fail, sizeof(fail));
				fail = 0;
				write(clientsocket_wl, &fail, sizeof(fail));
				free(target);
				_exit(1);
			}
			if (setgid(userentry->pw_gid)) {
				fail = errno;
				write(clientsocket_wl, &fail, sizeof(fail));
				fail = 0;
				write(clientsocket_wl, &fail, sizeof(fail));
				free(target);
				_exit(1);
			}
			if (setuid(userentry->pw_uid)) {
				fail = errno;
				write(clientsocket_wl, &fail, sizeof(fail));
				fail = 0;
				write(clientsocket_wl, &fail, sizeof(fail));
				free(target);
				_exit(1);
			}
			/* and go on with execution outside of if () */
		} else if (pid < 0) { /* fork failed */
			fail = errno;
			write(clientsocket_wl, &fail, sizeof(fail));
			fail = 0;
			write(clientsocket_wl, &fail, sizeof(fail));
			free(target);
			_exit(1);
		} else { /* parent */
			struct sembuf sema;
			int retval;
			/* wait for child to finish and continue loop */
			waitpid(-1, NULL, 0);
			/* check if semaphore is '0' */
			sema.sem_num = SEM_SIGHUP;
			sema.sem_op = 0;
			sema.sem_flg = IPC_NOWAIT;
			retval = semop(semkey, &sema, 1);
			if (retval == 0 || errno == EINVAL) {
				/*
				 * if semaphore is '0' then the last mail is sent
				 * and there is no need for a write_to_local_user()
				 * so we can exit
				 *
				 * if errno is EINVAL, then someone has removed the semaphore, so we shall exit, too
				 */
				break;
			} else if (errno != EAGAIN) {
				err(1, "[write_to_local_user] semop_op = 0 failed");
			}
			continue;
		}
		/* child code again here */
		/* send ack, we are ready to go on with mode and target */
		write(clientsocket_wl, &fail, sizeof(fail));

		read(clientsocket_wl, &length, sizeof(length));
		target = realloc(target, length + 1);
		memset(target, 0, length + 1);
		read(clientsocket_wl, target, length);
		read(clientsocket_wl, &mode, sizeof(mode));
		if (mode & ISMAILBOX) {
			/* if mode is mailbox, open mailbox */
			/* mailx removes users mailspool file if empty, so open with O_CREAT */
			mbox = open(target, O_WRONLY | O_EXLOCK | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
			if (mbox < 0) {
				fail = errno;
				write(clientsocket_wl, &fail, sizeof(fail));
				fail = 0;
				write(clientsocket_wl, &fail, sizeof(fail));
				_exit(1);
			}
			mboxlen = lseek(mbox, 0, SEEK_CUR);
		} else if (mode & ISPIPE) {
			/* if mode is mailbox, popen pipe */
			fflush(NULL);
			if ((pipe = popen(target, "w")) == NULL) {
				fail = errno;
				write(clientsocket_wl, &fail, sizeof(fail));
				fail = 0;
				write(clientsocket_wl, &fail, sizeof(fail));
				_exit(1);
			}
		}
		/* send ack, we are ready to receive mail contents */
		write(clientsocket_wl, &fail, sizeof(fail));
		
		/* write to file/pipe loop */
		while (read(clientsocket_wl, &linelen, sizeof(linelen))) {
			if (linelen == 0) {
				read(clientsocket_wl, &linelen, sizeof(linelen)); 
				if (linelen == 0) {
					break;
				} else {
					/* if linelen != 0, then there is a error on sender side */
					goto chop;
				}
			}
			/* receive line */
			read(clientsocket_wl, line, linelen);

			/* write line to target */
			if (mode & ISMAILBOX) { /* mailbox delivery */
				if ((size_t)write(mbox, line, linelen) != linelen) {
					goto failure;
				}
			} else if (mode & ISPIPE) { /* pipe delivery */
				if (fwrite(line, 1, linelen, pipe) != linelen) {
					goto failure;
				}
			}
			/* send ack */
			write(clientsocket_wl, &fail, sizeof(fail));
		}

		/* close target after succesfully written last line */
		if (mode & ISMAILBOX) { /* mailbox delivery */
			close(mbox);
		} else if (mode & ISPIPE) { /* pipe delivery */
			pclose(pipe);
		}
		/* send ack and exit */
		write(clientsocket_wl, &fail, sizeof(fail));
		_exit(0);
failure:
		fail = errno;
		write(clientsocket_wl, &fail, sizeof(fail));
chop:
		fail = 0;
		/* reset mailbox if there was something wrong */
		if (mode & ISMAILBOX && ftruncate(mbox, mboxlen) != 0) {
			fail = 2;
		}
		write(clientsocket_wl, &fail, sizeof(fail));
		if (mode & ISMAILBOX) { /* mailbox delivery */
			close(mbox);
		} else if (mode & ISPIPE) { /* pipe delivery */
			pclose(pipe);
		}
		_exit(1);
	}
	uint8_t null = 0;
	/* release dotforwardhandler out of loop */
	write(controlsocket_df, &null, sizeof(null));
	/* we do not need the semaphores any more */
	semctl(semkey, 0, IPC_RMID, 0);
	_exit(0);
}

int
main(int argc, char **argv)
{
	pid_t pid;
	int sockets1[2], sockets2[2];
	struct sembuf sema;
	struct ipc_perm semperm;

	if (geteuid() != 0) {
		fprintf(stderr, "This executable must be set setuid root!\n");
		return(-1);
	}

	/* create socketpair for dotforwardhandler() communication */
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, sockets1) != 0) {
		err(1,"Socketpair1 creation failed!\n");
	}
	/* df is short for DotForwardhandler */
	controlsocket_df = sockets1[0];
	clientsocket_df = sockets1[1];

	/* create socketpair for write_to_local_user() communication */
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, sockets2) != 0) {
		err(1,"Socketpair2 creation failed!\n");
	}
	/* wl is short for Write_to_Local_user */
	controlsocket_wl = sockets2[0];
	clientsocket_wl = sockets2[1];

	/*
	 * create semaphores: 
	 * 	-one for exclusive dotforwardhandler communication
	 * 	-one for exclusive write_to_local_user communication
	 * 	-another for signaling that the queue is completely processed
	 */
	semkey = semget(IPC_PRIVATE, 3, IPC_CREAT | IPC_EXCL | 0660);
	if (semkey == -1) {
		err(1,"[main] Creating semaphores failed");
	}

	/* adjust privileges of semaphores */
	struct passwd *pw;
	if ((pw = getpwnam("nobody")) == NULL)
		err(1, "Can't get uid of user 'nobody'");
	endpwent();

	struct group *grp;
	if ((grp = getgrnam("mail")) == NULL)
		err(1, "Can't get gid of group 'mail'");
	endgrent();

	semperm.uid = pw->pw_uid;
	semperm.gid = grp->gr_gid;
	semperm.mode = 0660;
	if (semctl(semkey, SEM_DF, IPC_SET, &semperm) == -1) {
		err(1, "[main] semctl(SEM_DF)");
	}
	if (semctl(semkey, SEM_WL, IPC_SET, &semperm) == -1) {
		err(1, "[main] semctl(SEM_WL)");
	}
	if (semctl(semkey, SEM_SIGHUP, IPC_SET, &semperm) == -1) {
		err(1, "[main] semctl(SEM_SIGHUP)");
	}

	sema.sem_num = SEM_DF;
	sema.sem_op = 1;
	sema.sem_flg = 0;
	if (semop(semkey, &sema, 1) == -1) {
		err(1, "[main] increment semaphore SEM_DF");
	}

	sema.sem_num = SEM_WL;
	sema.sem_op = 1;
	sema.sem_flg = 0;
	if (semop(semkey, &sema, 1) == -1) {
		err(1, "[main] increment semaphore SEM_WL");
	}

	pid = fork();
	if (pid == 0) { /* part _WITH_ root privileges */
		/* fork another process which goes into background */
		if (daemonize && daemon(0, 0) != 0) {
			syslog(LOG_ERR, "[main] can not daemonize: %m");
			exit(1);
		}
		pid = fork();
		/* both processes are running simultaneousily */
		if (pid == 0) { /* child */
			/* this process handles .forward read requests */
			dotforwardhandler();
			_exit(0);
		} else if (pid < 0) {
			err(1, "[main] Fork failed!\n");
			return(-1);
		} else { /* parent */
			/* this process writes to mailboxes if needed */
			write_to_local_user();
			_exit(0);
		}
	} else if (pid < 0) {
		err(1, "Fork failed!\n");
		return(-1);
	} else { /* part _WITHOUT_ root privileges */
		/* drop privileges */
		/* FIXME to user mail? */
		chdir("/");
		if (initgroups("nobody", pw->pw_gid) != 0)
			err(1, "initgroups");
#if 0		
		if (setgid(grp->gr_gid) != 0) /* set to group 'mail' */
#else
		/* FIXME */
		if (setgid(6) != 0) /* set to group 'mail' */
#endif
			err(1, "setgid");
		if (setuid(pw->pw_uid) != 0) /* set to user 'nobody' */
			err(1, "setuid");

		/* parse command line and execute main mua code */
		parseandexecute(argc, argv);

		/* release child processes */
		release_children();
	}

	return(0);
}

