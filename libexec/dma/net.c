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
 * $DragonFly: src/libexec/dma/net.c,v 1.2 2008/02/03 11:06:17 matthias Exp $
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
	    ((config->features & TLSINIT) == 0)) {
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

static int
read_remote_command(int fd, char *buff)
{
	ssize_t len;

	if (signal(SIGALRM, sig_alarm) == SIG_ERR) {
		syslog(LOG_ERR, "SIGALRM error: %m");
	}
	if (setjmp(timeout_alarm) != 0) {
		syslog(LOG_ERR, "Timeout reached");
		return (1);
	}
	alarm(CON_TIMEOUT);

	/*
	 * According to RFC 821 a reply can consists of multiple lines, so
	 * so read until the 4th char of the reply code is != '-'
	 */
	if (((config->features & SECURETRANS) != 0) &&
	    ((config->features & TLSINIT) == 0))
		do {
			len = SSL_read(config->ssl, buff, BUF_SIZE);
		} while (len > 3 && buff[3] == '-');
	else
		do {
			len = read(fd, buff, BUF_SIZE);
		} while (len > 3 && buff[3] == '-');

	alarm(0);

	return (0);
}

int
check_for_smtp_error(int fd, char *buff)
{
	if (read_remote_command(fd, buff) < 0)
		return (-1);

	/* We received a 5XX reply thus an error happend */
	if (strncmp(buff, "5", 1) == 0) {
		syslog(LOG_ERR, "SMTP error : %s", buff);
		return (-1);
	}
	return (0);
}

/*
 * Handle SMTP authentication
 *
 * XXX TODO: give me AUTH CRAM-MD5
 */
static int
smtp_login(struct qitem *it, int fd, char *login, char* password)
{
	char buf[2048];
	char *temp;
	int len;

	/* Send AUTH command according to RFC 2554 */
	send_remote_command(fd, "AUTH LOGIN");
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " AUTH login not available: %m", it->queueid);
		return (1);
	}

	len = base64_encode(login, strlen(login), &temp);
	if (len <= 0)
		return (-1);

	send_remote_command(fd, "%s", temp);
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " AUTH login failed: %m", it->queueid);
		return (-1);
	}

	len = base64_encode(password, strlen(password), &temp);
	if (len <= 0)
		return (-1);

	send_remote_command(fd, "%s", temp);
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " AUTH password failed: %m", it->queueid);
		return (-1);
	}

	return (0);
}

static int
open_connection(struct qitem *it, const char *host)
{
#ifdef HAVE_INET6
	struct addrinfo hints, *res, *res0;
	char servname[128];
	const char *errmsg = NULL;
#else
	struct hostent *hn;
	struct sockaddr_in addr;
#endif
	int fd, error = 0, port;

	if (config->port != 0)
		port = config->port;
	else
		port = SMTP_PORT;

#ifdef HAVE_INET6
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
#else
	memset(&addr, 0, sizeof(addr));
	fd = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;

	addr.sin_port = htons(port);
	error = inet_pton(AF_INET, host, &addr.sin_addr);
	if (error < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: "
			"address conversion failed: %m", it->queueid);
		return (1);
	}
	hn = gethostbyname(host);
	if (hn == NULL) {
		syslog(LOG_ERR, "%s: remote delivery deferred: cannot resolve "
			"hostname (%s) %m", it->queueid, host);
		return (-1);
	} else {
		memcpy(&addr.sin_addr, hn->h_addr, sizeof(struct in_addr));
		if (hn->h_length != 4)
			return (-1);
	}

	error = connect(fd, (struct sockaddr *) &addr, sizeof(addr));
	if (error < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: "
		       "connection failed : %m", it->queueid);
		return (-1);
	}
#endif
	return (fd);
}

int
deliver_remote(struct qitem *it, const char **errmsg)
{
	struct authuser *a;
	char *host, buf[2048], line[1000];
	int fd, error = 0, do_auth = 0;
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
		return (1);

#ifdef HAVE_CRYPTO
	if ((config->features & SECURETRANS) != 0) {
		error = smtp_init_crypto(it, fd, config->features);
		if (error >= 0)
			syslog(LOG_INFO, "%s: SSL initialization sucessful",
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
		if (check_for_smtp_error(fd, buf) < 0) {
			syslog(LOG_ERR, "%s: remote delivery deferred: "
			       " EHLO failed: %m", it->queueid);
			return (-1);
		}
	}
#endif /* HAVE_CRYPTO */
	if (((config->features & SECURETRANS) == 0)) {
		send_remote_command(fd, "EHLO %s", hostname());
		if (check_for_smtp_error(fd, buf) < 0) {
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
		syslog(LOG_INFO, "%s: Use SMTP authentication", it->queueid);
		error = smtp_login(it, fd, a->login, a->password);
		if (error < 0) {
			syslog(LOG_ERR, "%s: remote delivery failed:"
				" SMTP login failed: %m", it->queueid);
			return (-1);
		}
		/* SMTP login is not available, so try without */
		else if (error > 0)
			syslog(LOG_ERR, "%s: SMTP login not available. Try without",
				it->queueid);
	}

	send_remote_command(fd, "MAIL FROM:<%s>", it->sender);
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " MAIL FROM failed: %m", it->queueid);
		return (1);
	}

	/* XXX TODO:
	 * Iterate over all recepients and open only one connection
	 */
	send_remote_command(fd, "RCPT TO:<%s>", it->addr);
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " RCPT TO failed: %m", it->queueid);
		return (1);
	}

	send_remote_command(fd, "DATA");
	if (check_for_smtp_error(fd, buf) < 0) {
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
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: %m",
		       it->queueid);
		return (1);
	}

	send_remote_command(fd, "QUIT");
	if (check_for_smtp_error(fd, buf) < 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: "
		       "QUIT failed: %m", it->queueid);
		return (1);
	}
out:

	close(fd);
	return (error);
}

