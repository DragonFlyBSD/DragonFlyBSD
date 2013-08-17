/*
 * RCONFIG/SUBS.C
 * 
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#include "defs.h"

static void udp_alarm(int signo);

static __inline
int
iswhite(char c)
{
    return(c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

const char *
parse_str(char **scanp, int flags)
{
    char *base;
    char *ptr;

    for (base = *scanp; *base && iswhite(*base); ++base)
	;
    for (ptr = base; *ptr && !iswhite(*ptr); ++ptr) {
	if (flags & PAS_ALPHA) {
	    if ((*ptr >= 'a' && *ptr <= 'z') ||
		(*ptr >= 'A' && *ptr <= 'Z') ||
		*ptr == '_'
	    ) {
		continue;
	    }
	}
	if (flags & PAS_NUMERIC) {
	    if (*ptr >= '0' && *ptr <= '9')
		continue;
	}
	if ((flags & PAS_ANY) == 0)
	    return(NULL);
    }
    if (*ptr)
	*ptr++ = 0;
    *scanp = ptr;
    return(base);
}

int
udp_transact(struct sockaddr_in *sain, struct sockaddr_in *rsin, int *pfd, 
		char **bufp, int *lenp, const char *ctl, ...)
{
    va_list va;
    int fd;
    int n;
    socklen_t rsin_len = sizeof(*rsin);
    int rc;
    int nretry = 3;
    int timeout = 1;
    int on = 1;
    char buf[2048];

    if (*bufp) {
	free(*bufp);
	*bufp = NULL;
	*lenp = 0;
    }
    if ((fd = *pfd) < 0) {
	struct sockaddr_in lsin;

	lsin.sin_addr.s_addr = INADDR_ANY;
	lsin.sin_port = 0;
	lsin.sin_family = AF_INET;
	if ((fd = socket(AF_INET, SOCK_DGRAM, PF_UNSPEC)) < 0) {
	    asprintf(bufp, "udp_transaction: socket: %s", strerror(errno));
	    *lenp = strlen(*bufp);
	    return(509);
	}
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	if (bind(fd, (void *)&lsin, sizeof(lsin)) < 0) {
	    asprintf(bufp, "udp_transaction: bind: %s", strerror(errno));
	    *lenp = strlen(*bufp);
	    close(fd);
	    return(509);
	}
	*pfd = fd;
    }
retry:
    va_start(va, ctl);
    vsnprintf(buf, sizeof(buf), ctl, va);
    va_end(va);
    if (sendto(fd, buf, strlen(buf), 0, (void *)sain, sizeof(*sain)) >= 0) {
	struct sigaction nact;
	struct sigaction oact;

	bzero(&nact, sizeof(nact));
	nact.sa_handler = udp_alarm;
	nact.sa_flags = 0;
	sigaction(SIGALRM, &nact, &oact);
	alarm(timeout);
	n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (void *)rsin, &rsin_len);
	alarm(0);
	sigaction(SIGALRM, &oact, NULL);
	if (n < 0) {
	    if (errno == EINTR && --nretry > 0)
		goto retry;
	    asprintf(bufp, "udp_transaction: recvfrom: timeout");
	    *lenp = strlen(*bufp);
	    return(508);
	}
	while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n'))
		--n;
	buf[n] = 0;
	rc = strtol(buf, NULL, 10);
	*bufp = strdup(buf);
	*lenp = strlen(buf);
    } else {
	rc = 508;
	asprintf(bufp, "udp_transaction: sendto: %s", strerror(errno));
	*lenp = strlen(*bufp);
    }
    return(rc);
}

int
tcp_transact(struct sockaddr_in *sain, FILE **pfi, FILE **pfo, char **bufp,
		int *lenp, const char *ctl, ...)
{
    char buf[2048];
    va_list va;
    int rc;
    int n;

    if (*bufp) {
	free(*bufp);
	*bufp = NULL;
    }
    if (*pfi == NULL) {
	int fd;

	if ((fd = socket(AF_INET, SOCK_STREAM, PF_UNSPEC)) < 0) {
	    asprintf(bufp, "tcp_transaction: socket: %s", strerror(errno));
	    *lenp = strlen(*bufp);
	    return(509);
	}
	if (connect(fd, (void *)sain, sizeof(*sain)) < 0) {
	    asprintf(bufp, "tcp_transaction: connect: %s", strerror(errno));
	    *lenp = strlen(*bufp);
	    close(fd);
	    return(509);
	}
	*pfi = fdopen(fd, "r");
	*pfo = fdopen(dup(fd), "w");
	if (tcp_transact(sain, pfi, pfo, bufp, lenp, NULL) != 108) {
	    fclose(*pfi);
	    fclose(*pfo);
	    *pfi = *pfo = NULL;
	    if (*bufp)
		free(*bufp);
	    asprintf(bufp, "tcp_transaction: did not get HELLO from server\n");
	    *lenp = strlen(*bufp);
	    return(509);
	}
	if (*bufp) {
	    printf("%s\n", *bufp);
	    free(*bufp);
	    *bufp = NULL;
	}
    }
    if (ctl) {
	va_start(va, ctl);
	vfprintf(*pfo, ctl, va);
	va_end(va);
	fflush(*pfo);
    }
    if (fgets(buf, sizeof(buf), *pfi) != NULL) {
	rc = strtol(buf, NULL, 10);
	n = strlen(buf);
	if (rc == 201 && strstr(buf, "SIZE=") != NULL) {
	    *lenp = strtol(strstr(buf, "SIZE=") + 5, NULL, 0);
	    if (*lenp > 0)
		*bufp = malloc(*lenp);
	    for (rc = 0; *bufp && rc < *lenp; rc += n) {
		if ((n = *lenp - rc) > (int)sizeof(buf))
		    n = sizeof(buf);
		n = fread(*bufp + rc, 1, n, *pfi);
		if (n <= 0)
		    break;
	    }
	    if (rc == *lenp && fgets(buf, sizeof(buf), *pfi)) {
		if (strstr(buf, "ERROR=")) {
		    rc = strtol(strstr(buf, "ERROR=") + 6, NULL, 0);
		    if (rc == 0)
			rc = 201;
		    else
			rc = 509;
		} else {
		    rc = 509;
		}
	    } else {
		rc = 509;
	    }
	    if (rc != 201) {
		free(*bufp);
		asprintf(bufp, "tcp_transaction: download failed\n");
		*lenp = strlen(*bufp);
	    }
	} else {
	    while (n > 0 && (buf[n-1] == '\r' || buf[n-1] == '\n'))
		--n;
	    buf[n] = 0;
	    *bufp = strdup(buf);
	    *lenp = n;
	}
    } else {
	asprintf(bufp, "tcp_transaction: read: %s", strerror(errno));
	*lenp = strlen(*bufp);
	fclose(*pfi);
	fclose(*pfo);
	*pfi = *pfo = NULL;
	rc = 509;
    }
    return(rc);
}    

static
void
udp_alarm(int signo __unused)
{
    /* do nothing */
}

