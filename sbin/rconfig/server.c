/*
 * RCONFIG/SERVER.C
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

static void server_connection(int fd);
static void service_packet_loop(int fd);
static void server_chld_exit(int signo);
static int nconnects;

void
doServer(void)
{
    tag_t tag;

    /*
     * Listen on one or more UDP and TCP addresses, fork for each one.
     */
    signal(SIGCHLD, SIG_IGN);
    for (tag = AddrBase; tag; tag = tag->next) {
	struct sockaddr_in sain;
	const char *host;
	int lfd;
	int fd;
	int on = 1;

	bzero(&sain, sizeof(sain));
	if (tag->name == NULL) {
	    sain.sin_addr.s_addr = INADDR_ANY;
	    host = "<any>";
	} else {
	    if (inet_aton(tag->name, &sain.sin_addr) == 0) {
		struct hostent *hp;
		if ((hp = gethostbyname2(tag->name, AF_INET)) == NULL) {
		    fprintf(stderr, "Unable to resolve %s\n", tag->name);
		    exit(1);
		}
		bcopy(hp->h_addr_list[0], &sain.sin_addr, hp->h_length);
		host = strdup(hp->h_name);
		endhostent();
	    } else {
		host = strdup(tag->name);
	    }
	}
	sain.sin_port = htons(257);
	sain.sin_len = sizeof(sain);
	sain.sin_family = AF_INET;
	fflush(stdout);
	if (fork() == 0) {
	    if ((lfd = socket(AF_INET, SOCK_STREAM, PF_UNSPEC)) < 0) {
		fprintf(stderr, "%s: socket: %s\n", host, strerror(errno));
		exit(1);
	    }
	    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	    if (bind(lfd, (void *)&sain, sizeof(sain)) < 0) {
		fprintf(stderr, "%s: bind: %s\n", host, strerror(errno));
		exit(1);
	    }
	    if (listen(lfd, 20) < 0) {
		fprintf(stderr, "%s: listen: %s\n", host, strerror(errno));
		exit(1);
	    }
	    signal(SIGCHLD, server_chld_exit);
	    for (;;) {
		socklen_t slen = sizeof(sain);
		fd = accept(lfd, (void *)&sain, &slen);
		if (fd < 0) {
		    if (errno != EINTR)
			break;
		    continue;
		}
		++nconnects; /* XXX sigblock/sigsetmask */
		if (fork() == 0) {
		    close(lfd);
		    server_connection(fd);
		    exit(0);
		}
		close(fd);
	    }
	    exit(0);
	}
	if (fork() == 0) {
	    if ((lfd = socket(AF_INET, SOCK_DGRAM, PF_UNSPEC)) < 0) {
		fprintf(stderr, "%s: socket: %s\n", host, strerror(errno));
		exit(1);
	    }
	    if (bind(lfd, (void *)&sain, sizeof(sain)) < 0) {
		fprintf(stderr, "%s: bind: %s\n", host, strerror(errno));
		exit(1);
	    }
	    service_packet_loop(lfd);
	    exit(1);
	}
    }
    while (wait(NULL) > 0 || errno != EINTR)
	;
}

static
void
server_chld_exit(int signo __unused)
{
    while (wait3(NULL, WNOHANG, NULL) > 0)
	--nconnects;
}

static
void
server_connection(int fd)
{
    FILE *fi;
    FILE *fo;
    char buf[256];
    char *scan;
    const char *cmd;
    const char *name;

    fi = fdopen(fd, "r");
    fo = fdopen(dup(fd), "w");

    if (gethostname(buf, sizeof(buf)) == 0) {
	fprintf(fo, "108 HELLO SERVER=%s\r\n", buf);
    } else {
	fprintf(fo, "108 HELLO\r\n");
    }
    fflush(fo);

    while (fgets(buf, sizeof(buf), fi) != NULL) {
	scan = buf;
	cmd = parse_str(&scan, PAS_ALPHA);
	if (cmd == NULL) {
	    fprintf(fo, "502 Illegal Command String\r\n");
	} else if (strcasecmp(cmd, "VAR") == 0) {
	    fprintf(fo, "100 OK\r\n");
	} else if (strcasecmp(cmd, "TAG") == 0) {
	    if ((name = parse_str(&scan, PAS_ALPHA|PAS_NUMERIC)) == NULL) {
		fprintf(fo, "401 Illegal Tag\r\n");
	    } else {
		char *path = NULL;
		FILE *fp;
		asprintf(&path, "%s/%s.sh", TagDir, name);
		if ((fp = fopen(path, "r")) == NULL) {
		    fprintf(fo, "402 '%s' Not Found\r\n", name);
		} else {
		    size_t bytes;
		    size_t n;
		    int error = 0;

		    fseek(fp, 0L, 2);
		    bytes = (size_t)ftell(fp);
		    fseek(fp, 0L, 0);
		    fprintf(fo, "201 SIZE=%d\r\n", (int)bytes);
		    while (bytes > 0) {
			n = (bytes > sizeof(buf)) ? sizeof(buf) : bytes;
			n = fread(buf, 1, n, fp);
			if (n <= 0) {
			    error = 1;
			    break;
			}
			if (fwrite(buf, 1, n, fo) != n) {
			    error = 1;
			    break;
			}
			bytes -= n;
		    }
		    fclose(fp);
		    if (bytes > 0 && ferror(fo) == 0) {
			bzero(buf, sizeof(buf));
			while (bytes > 0) {
			    n = (bytes > sizeof(buf)) ? sizeof(buf) : bytes;
			    if (fwrite(buf, 1, n, fo) != n)
				break;
			    bytes -= n;
			}
		    }
		    fprintf(fo, "202 ERROR=%d\r\n", error); 
		}
		free(path);
	    }
	} else if (strcasecmp(cmd, "IDLE") == 0) {
	    if ((name = parse_str(&scan, PAS_ANY)) == NULL) {
		fprintf(fo, "401 Illegal String\r\n");
	    } else {
		fprintf(fo, "109 %s\r\n", name);
	    }
	} else if (strcasecmp(cmd, "QUIT") == 0) {
	    fprintf(fo, "409 Bye!\r\n");
	    break;
	} else {
	    fprintf(fo, "501 Unknown Command\r\n");
	}
	fflush(fo);
    }
    fclose(fi);
    fclose(fo);
}

/*
 * UDP packet loop.  For now just handle one request per packet.  Note that
 * since the protocol is designed to be used in a broadcast environment,
 * we only respond when we have something to contribute.
 */
static
void
service_packet_loop(int fd)
{
    struct sockaddr_in sain;
    char ibuf[256+1];
    char obuf[256+1];
    socklen_t sain_len;
    int n;
    char *scan;
    const char *cmd;
    const char *name;

    for (;;) {
	sain_len = sizeof(sain);
	n = recvfrom(fd, ibuf, sizeof(ibuf) - 1, 0, (void *)&sain, &sain_len);
	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	ibuf[n] = 0;
	n = 0;
	scan = ibuf;
	cmd = parse_str(&scan, PAS_ALPHA);
	if (cmd == NULL) {
	    ;
	} else if (strcasecmp(cmd, "TAG") == 0) {
	    if ((name = parse_str(&scan, PAS_ALPHA|PAS_NUMERIC)) != NULL) {
		char *path = NULL;
		struct stat st;
		asprintf(&path, "%s/%s.sh", TagDir, name);
		if (stat(path, &st) == 0) {
		    snprintf(obuf, sizeof(obuf), "101 TAG=%s\r\n", name);
		    n = strlen(obuf);
		}
		free(path);
	    }
	}
	if (n)
	    sendto(fd, obuf, n, 0, (void *)&sain, sain_len);
    }
}

