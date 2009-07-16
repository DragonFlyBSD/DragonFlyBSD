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
#include <errno.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

extern struct config *config;
extern struct authusers authusers;
static jmp_buf timeout_alarm;
char neterr[BUF_SIZE];

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
	size_t len, pos;
	int s;
	ssize_t n;

	va_start(va, fmt);
	s = vsnprintf(cmd, sizeof(cmd) - 2, fmt, va);
	va_end(va);
	if (s == sizeof(cmd) - 2 || s < 0)
		errx(1, "Internal error: oversized command string");
	/* We *know* there are at least two more bytes available */
	strcat(cmd, "\r\n");
	len = strlen(cmd);

	if (((config->features & SECURETRANS) != 0) &&
	    ((config->features & NOSSL) == 0)) {
		while ((s = SSL_write(config->ssl, (const char*)cmd, len)) <= 0) {
			s = SSL_get_error(config->ssl, s);
			if (s != SSL_ERROR_WANT_READ &&
			    s != SSL_ERROR_WANT_WRITE)
				return (-1);
		}
	}
	else {
		pos = 0;
		while (pos < len) {
			n = write(fd, cmd + pos, len - pos);
			if (n < 0)
				return (-1);
			pos += n;
		}
	}

	return (len);
}

int
read_remote(int fd, int extbufsize, char *extbuf)
{
	ssize_t rlen = 0;
	size_t pos, len;
	char buff[BUF_SIZE];
	int done = 0, status = 0, extbufpos = 0;

	if (signal(SIGALRM, sig_alarm) == SIG_ERR) {
		snprintf(neterr, sizeof(neterr), "SIGALRM error: %s",
		    strerror(errno));
		return (1);
	}
	if (setjmp(timeout_alarm) != 0) {
		snprintf(neterr, sizeof(neterr), "Timeout reached");
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

	buff[len] = '\0';
	while (len > 0 && (buff[len - 1] == '\r' || buff[len - 1] == '\n'))
		buff[--len] = '\0';
	snprintf(neterr, sizeof(neterr), "%s", buff);
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
			syslog(LOG_NOTICE, "%s: remote delivery deferred:"
					" AUTH login not available: %s",
					it->queueid, neterr);
			return (1);
		}

		len = base64_encode(login, strlen(login), &temp);
		if (len <= 0)
			return (-1);

		send_remote_command(fd, "%s", temp);
		if (read_remote(fd, 0, NULL) != 3) {
			syslog(LOG_NOTICE, "%s: remote delivery deferred:"
					" AUTH login failed: %s", it->queueid,
					neterr);
			return (-1);
		}

		len = base64_encode(password, strlen(password), &temp);
		if (len <= 0)
			return (-1);

		send_remote_command(fd, "%s", temp);
		res = read_remote(fd, 0, NULL);
		if (res == 5) {
			syslog(LOG_NOTICE, "%s: remote delivery failed:"
					" Authentication failed: %s",
					it->queueid, neterr);
			return (-1);
		} else if (res != 2) {
			syslog(LOG_NOTICE, "%s: remote delivery failed:"
					" AUTH password failed: %s",
					it->queueid, neterr);
			return (-1);
		}
	} else {
		syslog(LOG_WARNING, "%s: non-encrypted SMTP login is disabled in config, so skipping it. ",
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
		syslog(LOG_NOTICE, "%s: remote delivery deferred: "
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
		syslog(LOG_NOTICE, "%s: remote delivery deferred: %s (%s:%s)",
			it->queueid, errmsg, host, servname);
		freeaddrinfo(res0);
		return (-1);
	}
	freeaddrinfo(res0);
	return (fd);
}

int
deliver_remote(struct qitem *it, const char **errmsg)
{
	struct authuser *a;
	char *host, line[1000];
	int fd, error = 0, do_auth = 0, res = 0;
	size_t linelen;
	/* asprintf can't take const */
	void *errmsgc = __DECONST(char **, errmsg);

	host = strrchr(it->addr, '@');
	/* Should not happen */
	if (host == NULL) {
		asprintf(errmsgc, "Internal error: badly formed address %s",
		    it->addr);
		return(-1);
	} else {
		/* Step over the @ */
		host++;
	}

	/* Smarthost support? */
	if (config->smarthost != NULL && strlen(config->smarthost) > 0) {
		syslog(LOG_INFO, "%s: using smarthost (%s:%i)",
		       it->queueid, config->smarthost, config->port);
		host = config->smarthost;
	}

	fd = open_connection(it, host);
	if (fd < 0)
		return (1);

	/* Check first reply from remote host */
	config->features |= NOSSL;
	res = read_remote(fd, 0, NULL);
	if (res != 2) {
		syslog(LOG_WARNING, "%s: Invalid initial response: %i",
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
#endif /* HAVE_CRYPTO */

	send_remote_command(fd, "EHLO %s", hostname());
	if (read_remote(fd, 0, NULL) != 2) {
		syslog(LOG_WARNING, "%s: remote delivery deferred: "
		       " EHLO failed: %s", it->queueid, neterr);
		asprintf(errmsgc, "%s did not like our EHLO:\n%s",
		    host, neterr);
		return (-1);
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
			asprintf(errmsgc, "SMTP login to %s failed", host);
			return (-1);
		}
		/* SMTP login is not available, so try without */
		else if (error > 0)
			syslog(LOG_WARNING, "%s: SMTP login not available."
					" Try without", it->queueid);
	}

#define READ_REMOTE_CHECK(c, exp)	\
	res = read_remote(fd, 0, NULL); \
	if (res == 5) { \
		syslog(LOG_ERR, "%s: remote delivery failed: " \
		       c " failed: %s", it->queueid, neterr); \
		asprintf(errmsgc, "%s did not like our " c ":\n%s", \
		    host, neterr); \
		return (-1); \
	} else if (res != exp) { \
		syslog(LOG_NOTICE, "%s: remote delivery deferred: " \
		       c " failed: %s", it->queueid, neterr); \
		return (1); \
	}

	send_remote_command(fd, "MAIL FROM:<%s>", it->sender);
	READ_REMOTE_CHECK("MAIL FROM", 2);

	send_remote_command(fd, "RCPT TO:<%s>", it->addr);
	READ_REMOTE_CHECK("RCPT TO", 2);

	send_remote_command(fd, "DATA");
	READ_REMOTE_CHECK("DATA", 3);

	if (fseek(it->queuef, it->hdrlen, SEEK_SET) != 0) {
		syslog(LOG_ERR, "%s: remote delivery deferred: cannot seek: %s",
		       it->queueid, neterr);
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
			syslog(LOG_NOTICE, "%s: remote delivery deferred: "
				"write error", it->queueid);
			error = 1;
			goto out;
		}
	}

	send_remote_command(fd, ".");
	READ_REMOTE_CHECK("final DATA", 2);

	send_remote_command(fd, "QUIT");
	if (read_remote(fd, 0, NULL) != 2)
		syslog(LOG_INFO, "%s: remote delivery succeeded but "
		       "QUIT failed: %s", it->queueid, neterr);
out:

	close(fd);
	return (error);
}

