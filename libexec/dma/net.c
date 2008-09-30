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
 * $DragonFly: src/libexec/dma/net.c,v 1.9 2008/09/30 17:47:21 swildner Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef HAVE_CRYPTO
#include <openssl/ssl.h>
#endif /* HAVE_CRYPTO */

#include <err.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

extern struct config *config;
extern struct authusers authusers;
static jmp_buf timeout_alarm;

static void
sig_alarm(int signo __unused)
{
	longjmp(timeout_alarm, 1);
}

ssize_t
send_remote_command(int fd, const char* fmt, ...)
{
	va_list va;
	char cmd[4096];
	ssize_t len = 0;

	va_start(va, fmt);
	vsprintf(cmd, fmt, va);

	if (((config->features & SECURETRANS) != 0) &&
	    ((config->features & NOSSL) == 0)) {
		len = SSL_write(config->ssl, (const char*)cmd, strlen(cmd));
		SSL_write(config->ssl, "\r\n", 2);
	}
	else {
		len = write(fd, cmd, strlen(cmd));
		write(fd, "\r\n", 2);
	}
	va_end(va);

	return (len+2);
}

int
read_remote(int fd, int extbufsize, char *extbuf)
{
	ssize_t rlen = 0;
	size_t pos, len;
	char buff[BUF_SIZE];
	int done = 0, status = 0, extbufpos = 0;

	if (signal(SIGALRM, sig_alarm) == SIG_ERR) {
		syslog(LOG_ERR, "SIGALRM error: %m");
	}
	if (setjmp(timeout_alarm) != 0) {
		syslog(LOG_ERR, "Timeout reached");
		return (1);
	}
	alarm(CON_TIMEOUT);

	/*
	 * Remote reading code from femail.c written by Henning Brauer of
	 * OpenBSD and released under a BSD style license.
	 */
	for (len = pos = 0; !done; ) {
		rlen = 0;
		if (pos == 0 ||
		    (pos > 0 && memchr(buff + pos, '\n', len - pos) == NULL)) {
			memmove(buff, buff + pos, len - pos);
			len -= pos;
			pos = 0;
			if (((config->features & SECURETRANS) != 0) &&
			    (config->features & NOSSL) == 0) {
				if ((rlen = SSL_read(config->ssl, buff + len,
				    sizeof(buff) - len)) == -1)
					err(1, "read");
			} else {
				if ((rlen = read(fd, buff + len,
				    sizeof(buff) - len)) == -1)
					err(1, "read");
			}
			len += rlen;
		}
		/*
		 * If there is an external buffer with a size bigger than zero
		 * and as long as there is space in the external buffer and
		 * there are new characters read from the mailserver
		 * copy them to the external buffer
		 */
		if (extbufpos <= (extbufsize - 1) && rlen && extbufsize > 0 
		    && extbuf != NULL) {
			/* do not write over the bounds of the buffer */
			if(extbufpos + rlen > (extbufsize - 1)) {
				rlen = extbufsize - extbufpos;
			}
			memcpy(extbuf + extbufpos, buff + len - rlen, rlen);
			extbufpos += rlen;
		}
		for (; pos < len && buff[pos] >= '0' && buff[pos] <= '9'; pos++)
			; /* Do nothing */

		if (pos == len)
			return (0);

		if (buff[pos] == ' ')
			done = 1;
		else if (buff[pos] != '-')
			syslog(LOG_ERR, "invalid syntax in reply from server");

		/* skip up to \n */
		for (; pos < len && buff[pos - 1] != '\n'; pos++)
			; /* Do nothing */

	}
	alarm(0);

	status = atoi(buff);
	return (status/100);
}

/*
 * Handle SMTP authentication
 */
static int
smtp_login(struct qitem *it, int fd, char *login, char* password)
{
	char *temp;
	int len, res = 0;

#ifdef HAVE_CRYPTO
	res = smtp_auth_md5(it, fd, login, password);
	if (res == 0) {
		return (0);
	} else if (res == -2) {
	/*
	 * If the return code is -2, then then the login attempt failed, 
	 * do not try other login mechanisms
	 */
		return (-1);
	}
#endif /* HAVE_CRYPTO */

	if ((config->features & INSECURE) != 0) {
		/* Send AUTH command according to RFC 2554 */
		send_remote_command(fd, "AUTH LOGIN");
		if (read_remote(fd, 0, NULL) != 3) {
			syslog(LOG_ERR, "%s: remote delivery deferred:"
					" AUTH login not available: %m", it->queueid);
			return (1);
		}

		len = base64_encode(login, strlen(login), &temp);
		if (len <= 0)
			return (-1);

		send_remote_command(fd, "%s", temp);
		if (read_remote(fd, 0, NULL) != 3) {
			syslog(LOG_ERR, "%s: remote delivery deferred:"
					" AUTH login failed: %m", it->queueid);
			return (-1);
		}

		len = base64_encode(password, strlen(password), &temp);
		if (len <= 0)
			return (-1);

		send_remote_command(fd, "%s", temp);
		res = read_remote(fd, 0, NULL);
		if (res == 5) {
			syslog(LOG_ERR, "%s: remote delivery failed:"
					" Authentication failed: %m", it->queueid);
			return (-1);
		} else if (res != 2) {
			syslog(LOG_ERR, "%s: remote delivery failed:"
					" AUTH password failed: %m", it->queueid);
			return (-1);
		}
	} else {
		syslog(LOG_ERR, "%s: non-encrypted SMTP login is disabled in config, so skipping it. ",
				it->queueid);
		return (1);
	}

	return (0);
}

static int
open_connection(struct qitem *it, const char *host)
{
	struct addrinfo hints, *res, *res0;
	char servname[128];
	const char *errmsg = NULL;
	int fd, error = 0, port;

	if (config->port != 0)
		port = config->port;
	else
		port = SMTP_PORT;

	/* FIXME get MX record of host */
	/* Shamelessly taken from getaddrinfo(3) */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	snprintf(servname, sizeof(servname), "%d", port);
	error = getaddrinfo(host, servname, &hints, &res0);
	if (error) {
		syslog(LOG_ERR, "%s: remote delivery deferred: "
		       "%s: %m", it->queueid, gai_strerror(error));
		return (-1);
	}
	fd = -1;
	for (res = res0; res; res = res->ai_next) {
		fd=socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) {
			errmsg = "socket failed";
			continue;
		}
		if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
			errmsg = "connect failed";
			close(fd);
			fd = -1;
			continue;
		}
		break;
	}
	if (fd < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: %s (%s:%s)",
			it->queueid, errmsg, host, servname);
		freeaddrinfo(res0);
		return (-1);
	}
	freeaddrinfo(res0);
	return (fd);
}

static void
copy_qitem(struct qitem *target, struct qitem *it) {
	target->sender = it->sender;
	target->addr = it->addr;
	target->pipeuser = it->pipeuser;
	target->queuefn = it->queuefn;
	target->queueid = it->queueid;
	target->queuef = it->queuef;
	target->hdrlen = it->hdrlen;
	target->local = 0;
}

int
deliver_remote(struct qitem *it, const char **errmsg, struct queue **queue)
{
	struct authuser *a;
	char *host, line[1000];
	int fd, error = 0, do_auth = 0, res = 0;
	size_t linelen;

	host = strrchr(it->addr, '@');
	/* Should not happen */
	if (host == NULL)
		return(-1);
	else
		/* Step over the @ */
		host++;

	/* Smarthost support? */
	if (config->smarthost != NULL && strlen(config->smarthost) > 0) {
		syslog(LOG_INFO, "%s: using smarthost (%s)",
		       it->queueid, config->smarthost);
		host = config->smarthost;
	}

	fd = open_connection(it, host);
	if (fd < 0)
		return(1);

	/* Check first reply from remote host */
	config->features |= NOSSL;
	res = read_remote(fd, 0, NULL);
	if (res != 2) {
		syslog(LOG_INFO, "%s: Invalid initial response: %i",
			it->queueid, res);
		return(1);
	}
	config->features &= ~NOSSL;

#ifdef HAVE_CRYPTO
	if ((config->features & SECURETRANS) != 0) {
		error = smtp_init_crypto(it, fd, config->features);
		if (error >= 0)
			syslog(LOG_INFO, "%s: SSL initialization successful",
				it->queueid);
		else
			goto out;
	}

	/*
	 * If the user doesn't want STARTTLS, but SSL encryption, we
	 * have to enable SSL first, then send EHLO
	 */
	if (((config->features & STARTTLS) == 0) &&
	    ((config->features & SECURETRANS) != 0)) {
		send_remote_command(fd, "EHLO %s", hostname());
		if (read_remote(fd, 0, NULL) != 2) {
			syslog(LOG_ERR, "%s: remote delivery deferred: "
			       " EHLO failed: %m", it->queueid);
			return (-1);
		}
	}
#endif /* HAVE_CRYPTO */
	if (((config->features & SECURETRANS) == 0)) {
		send_remote_command(fd, "EHLO %s", hostname());
		if (read_remote(fd, 0, NULL) != 2) {
			syslog(LOG_ERR, "%s: remote delivery deferred: "
			       " EHLO failed: %m", it->queueid);
			return (-1);
		}
	}

	/*
	 * Use SMTP authentication if the user defined an entry for the remote
	 * or smarthost
	 */
	SLIST_FOREACH(a, &authusers, next) {
		if (strcmp(a->host, host) == 0) {
			do_auth = 1;
			break;
		}
	}

	if (do_auth == 1) {
		/*
		 * Check if the user wants plain text login without using
		 * encryption.
		 */
		syslog(LOG_INFO, "%s: Use SMTP authentication",
				it->queueid);
		error = smtp_login(it, fd, a->login, a->password);
		if (error < 0) {
			syslog(LOG_ERR, "%s: remote delivery failed:"
					" SMTP login failed: %m", it->queueid);
			return(-1);
		}
		/* SMTP login is not available, so try without */
		else if (error > 0)
			syslog(LOG_ERR, "%s: SMTP login not available."
					" Try without", it->queueid);
	}

	send_remote_command(fd, "MAIL FROM:<%s>", it->sender);
	if (read_remote(fd, 0, NULL) != 2) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " MAIL FROM failed: %m", it->queueid);
		return(1);
	}

	if (queue == NULL) {
	/* without given queue send only to one receipient */
		send_remote_command(fd, "RCPT TO:<%s>", it->addr);
		if (read_remote(fd, 0, NULL) != 2) {
			syslog(LOG_ERR, "%s: remote delivery deferred:"
					" RCPT TO failed: %m", it->queueid);
			return(1);
		}
	} else {
	/* Iterate over all recepients and open only one connection */
/*
 * we need 3 queues:
 * 	-delivered addresses
 *	-temporary errors
 *	-permanent errors
 * these 3 queues are given in **queues (+the first queue with all the qitems to send)
 *
 */

		struct qitem *tit;
		int rcpt_success = 0;
		struct stat st;
		LIST_FOREACH(tit, &queue[0]->queue, next) {
			struct qitem *qit;
			qit = calloc(1, sizeof(struct qitem));
			copy_qitem(qit, tit);

			if (stat(tit->queuefn, &st) != 0) {
				syslog(LOG_ERR, "%s: lost queue file `%s'",
						tit->queueid, tit->queuefn);
				/* drop qitem and mark it as successfully sent */
				LIST_INSERT_HEAD(&queue[1]->queue, qit, next);
				continue;
			}

			send_remote_command(fd, "RCPT TO:<%s>", tit->addr);
			switch (read_remote(fd, 0, NULL)) {
			case 2: /* everythings fine, receipient accepted */
				/* add item to temporary queue, these items will be deleted in deliver_smarthost */
				rcpt_success = 1;
				LIST_INSERT_HEAD(&queue[1]->queue, qit, next);
				syslog(LOG_INFO, "%s: mail from=<%s> to=<%s>",
						tit->queueid, tit->sender, tit->addr);
				break;
			case 4: /* temporary error, try again later */
				/* add item to a temporary queue, these items will be tried again */
				LIST_INSERT_HEAD(&queue[2]->queue, qit, next);
				syslog(LOG_INFO, "%s: mail from=<%s> to=<%s>",
						tit->queueid, tit->sender, tit->addr);
				syslog(LOG_ERR, "%s: remote delivery deferred:"
						" RCPT TO failed: %m", tit->queueid);
				break;
			case 5: /* permanent error, bounce */
				/* add item to a queue, which will be returned to deliver_smarthost */
				LIST_INSERT_HEAD(&queue[3]->queue, qit, next);
				syslog(LOG_INFO, "%s: mail from=<%s> to=<%s>",
						tit->queueid, tit->sender, tit->addr);
				syslog(LOG_ERR, "%s: remote delivery failed:"
						" RCPT TO failed: %m", tit->queueid);
				break;
			}
		}
		/* if there was no successful RCPT TO, return _WITHOUT_ error */
		if (!rcpt_success)
			goto out;
	} 

	send_remote_command(fd, "DATA");
	if (read_remote(fd, 0, NULL) != 3) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " DATA failed: %m", it->queueid);
		return (1);
	}

	if (fseek(it->queuef, it->hdrlen, SEEK_SET) != 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: cannot seek: %m",
		       it->queueid);
		return (1);
	}

	while (!feof(it->queuef)) {
		if (fgets(line, sizeof(line), it->queuef) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			syslog(LOG_CRIT, "%s: remote delivery failed:"
				"corrupted queue file", it->queueid);
			*errmsg = "corrupted queue file";
			error = -1;
			goto out;
		}

		/* Remove trailing \n's and escape leading dots */
		trim_line(line);

		/*
		 * If the first character is a dot, we escape it so the line
		 * length increases
		*/
		if (line[0] == '.')
			linelen++;

		if (send_remote_command(fd, "%s", line) != (ssize_t)linelen+1) {
			syslog(LOG_ERR, "%s: remote delivery deferred: "
				"write error", it->queueid);
			error = 1;
			goto out;
		}
	}

	send_remote_command(fd, ".");
	if (read_remote(fd, 0, NULL) != 2) {
		syslog(LOG_ERR, "%s: remote delivery deferred: %m",
		       it->queueid);
		return (1);
	}

	send_remote_command(fd, "QUIT");
	if (read_remote(fd, 0, NULL) != 2) {
		syslog(LOG_ERR, "%s: remote delivery deferred: "
		       "QUIT failed: %m", it->queueid);
		return (1);
	}
out:

	close(fd);
	return (error);
}

