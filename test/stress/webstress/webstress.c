/* 
 * The software known as "DragonFly" or "DragonFly BSD" is distributed under
 * the following terms:
 * 
 * Copyright (c) 2003, 2004, 2005 The DragonFly Project.  All rights reserved.
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
 *
 * $DragonFly: src/test/stress/webstress/webstress.c,v 1.1 2005/04/05 00:13:20 dillon Exp $
 */
/*
 * webstress [-n num] [-r] [-f url_file] [-l limit_ms] [-c count] url url
 *			url...
 *
 * Fork N processes (default 8).  Each process makes a series of connections
 * to retrieve the specified URLs.  Any transaction that takes longer the
 * limit_ms (default 1000) to perform is reported.  
 *
 * If the -r option is specified the list of URLs is randomized.
 *
 * If the -f option is specified URLs are read from a file.  Multiple -f
 * options may be specified instead of or in addition to specifying additional
 * URLs on the command line.
 *
 * All URLs should begin with http:// but this is optional.  Only http is
 * supported.
 *
 * By default the program runs until you ^C it.  The -c option may be 
 * used to limit the number of loops.
 *
 * WARNING!  This can easily blow out available sockets on the client or
 * server, or blow out available ports on the client, due to sockets left
 * in TIME_WAIT.  It is recommended that net.inet.tcp.msl be lowered
 * considerably to run this test.  The test will abort if the system runs
 * out of sockets or ports.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

typedef struct urlnode {
    struct urlnode *url_next;
    struct sockaddr_in url_sin;
    char *url_host;
    char *url_path;
} *urlnode_t;

static void usage(void);
static void read_url_file(const char *path);
static void run_test(urlnode_t *array, int count);
static void add_url(const char *url);

int fork_opt = 8;
int random_opt;
int limit_opt = 1000000;
int report_interval = 100;
int loop_count = 0;
urlnode_t url_base;
urlnode_t *url_nextp = &url_base;
int url_count;

int
main(int ac, char **av)
{
    int ch;
    int i;
    urlnode_t node;
    urlnode_t *array;
    pid_t pid;

    while ((ch = getopt(ac, av, "c:f:l:n:r")) != -1) {
	printf("CH %c\n", ch);
	switch(ch) {
	case 'c':
	    loop_count = strtol(optarg, NULL, 0);
	    if (report_interval > loop_count)
		report_interval = loop_count;
	    break;
	case 'n':
	    fork_opt = strtol(optarg, NULL, 0);
	    break;
	case 'r':
	    random_opt = 1;
	    break;
	case 'f':
	    read_url_file(optarg);
	    break;
	case 'l':
	    limit_opt = strtol(optarg, NULL, 0) * 1000;
	    break;
	default:
	    usage();
	    /* not reached */
	    break;
	}
    }
    ac -= optind;
    av += optind;
    for (i = 0; i < ac; ++i)
	add_url(av[i]);
    if (url_base == NULL)
	usage();

    /*
     * Convert the list to an array
     */
    array = malloc(sizeof(urlnode_t) * url_count);
    for (i = 0, node = url_base; i < url_count; ++i, node = node->url_next) {
	array[i] = node;
    }

    /*
     * Dump the list
     */
    printf("URL LIST:\n");
    for (node = url_base; node; node = node->url_next) {
	printf("    http://%s:%d/%s\n", 
	    inet_ntoa(node->url_sin.sin_addr), 
	    ntohs(node->url_sin.sin_port),
	    node->url_path
	);
    }
    printf("Running...\n");

    /*
     * Fork children and start the test
     */
    for (i = 0; i < fork_opt; ++i) {
	if ((pid = fork()) == 0) {
	    run_test(array, url_count);
	    exit(0);
	} else if (pid == (pid_t)-1) {
	    printf("unable to fork child %d\n", i);
	    exit(1);
	}
    }
    while (wait(NULL) >= 0 || errno == EINTR)
	;
    return(0);
}

static
void 
usage(void)
{
    fprintf(stderr, 
	"%s [-n num] [-r] [-f url_file] [-l limit_ms] [-c loops] url url...\n"
	"    -n num        number of forks (8)\n"
	"    -r            randomize list (off)\n"
	"    -f url_file   read URLs from file\n"
	"    -l limit_ms   report if transaction latency >limit (1000)\n"
	"    -c loops      test loops (0 == infinite)\n"
	"\n"
 "WARNING!  This can easily blow out available sockets on the client or\n"
 "server, or blow out available ports on the client, due to sockets left\n"
 "in TIME_WAIT.  It is recommended that net.inet.tcp.msl be lowered\n"
 "considerably to run this test.  The test will abort if the system runs\n"
 "out of sockets or ports.\n",
	getprogname()
    );
    exit(1);
}

static
void
read_url_file(const char *path)
{
    char buf[1024];
    FILE *fi;
    int len;

    if ((fi = fopen(path, "r")) != NULL) {
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    if (buf[0] == '#')
		continue;
	    len = strlen(buf);
	    if (len && buf[len-1] == '\n')
		buf[len-1] = 0;
	    add_url(buf);
	}
	fclose(fi);
    } else {
	fprintf(stderr, "Unable to open %s\n", path);
	exit(1);
    }
}

static
void
add_url(const char *url)
{
    struct hostent *hen;
    const char *base;
    const char *ptr;
    char *hostname;
    urlnode_t node;
    int error;

    node = malloc(sizeof(*node));
    bzero(node, sizeof(*node));

    base = url;
    if (strncmp(url, "http://", 7) == 0)
	base += 7;
    if ((ptr = strchr(base, '/')) == NULL) {
	fprintf(stderr, "malformed URL: %s\n", base);
	free(node);
	return;
    }
    hostname = malloc(ptr - base + 1);
    bcopy(base, hostname, ptr - base);
    hostname[ptr - base] = 0;
    base = ptr + 1;
    if ((ptr = strrchr(hostname, ':')) != NULL) {
	*strrchr(hostname, ':') = 0;
	++ptr;
	node->url_sin.sin_port = htons(strtol(ptr, NULL, 0));
    } else {
	node->url_sin.sin_port = htons(80);
    }
    node->url_sin.sin_len = sizeof(node->url_sin);
    node->url_sin.sin_family = AF_INET;
    error = inet_aton(hostname, &node->url_sin.sin_addr);
    if (error < 0) {
	fprintf(stderr, "unable to parse host/ip: %s (%s)\n", 
		hostname, strerror(errno));
	free(node);
	return;
    } 
    if (error == 0) {
	if ((hen = gethostbyname(hostname)) == NULL) {
	    fprintf(stderr, "unable to resolve host: %s (%s)\n",
		hostname, hstrerror(h_errno));
	    free(node);
	    return;
	}
	bcopy(hen->h_addr, &node->url_sin.sin_addr, hen->h_length);
	node->url_sin.sin_family = hen->h_addrtype;
    }
    node->url_host = strdup(hostname);
    node->url_path = strdup(base);
    *url_nextp = node;
    url_nextp = &node->url_next;
    ++url_count;
}

static
void
run_test(urlnode_t *array, int count)
{
    struct timeval tv1;
    struct timeval tv2;
    char buf[1024];
    urlnode_t node;
    FILE *fp;
    int loops;
    int one;
    int fd;
    int us;
    int i;
    double total_time;

    total_time = 0.0;
    one = 1;

    /*
     * Make sure children's random number generators are NOT synchronized.
     */
    if (random_opt)
	srandomdev();

    for (loops = 0; loop_count == 0 || loops < loop_count; ++loops) {
	/*
	 * Random requests
	 */
	if (random_opt) {
	    for (i = count * 4; i; --i) {
		int ex1 = random() % count;
		int ex2 = random() % count;
		node = array[ex1];
		array[ex1] = array[ex2];
		array[ex2] = node;
	    }
	}

	/*
	 * Run through the array
	 */
	for (i = 0; i < count; ++i) {
	    node = array[i];

	    if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	    }
	    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	    gettimeofday(&tv1, NULL);
	    if (connect(fd, (void *)&node->url_sin, node->url_sin.sin_len) < 0) {
		gettimeofday(&tv2, NULL);
		us = (tv2.tv_sec - tv1.tv_sec) * 1000000;
		us += (int)(tv2.tv_usec - tv1.tv_usec) / 1000000;
		printf("connect_failure %6.2fms: http://%s:%d/%s\n",
		    (double)us / 1000.0, node->url_host,
		    ntohs(node->url_sin.sin_port),
		    node->url_path);
		close(fd);
		continue;
	    }
	    if ((fp = fdopen(fd, "r+")) == NULL) {
		perror("fdopen");
		exit(1);
	    }
	    fprintf(fp, "GET /%s HTTP/1.1\r\n"
			"Host: %s\r\n\r\n", 
			node->url_path,
			node->url_host);
	    fflush(fp);
	    shutdown(fileno(fp), SHUT_WR);
	    while (fgets(buf, sizeof(buf), fp) != NULL)
		;
	    fclose(fp);
	    gettimeofday(&tv2, NULL);
	    us = (tv2.tv_sec - tv1.tv_sec) * 1000000;
	    us += (int)(tv2.tv_usec - tv1.tv_usec);
	    if (us > limit_opt) {
		printf("access_time %6.2fms: http://%s:%d/%s\n",
		    (double)us / 1000.0, node->url_host,
		    ntohs(node->url_sin.sin_port),
		    node->url_path);
	    }
	    total_time += (double)us / 1000000.0;
	}
	if (report_interval && (loops + 1) % report_interval == 0) {
		printf("loop_time: %6.3fmS avg/url %6.3fmS\n", 
			total_time / (double)report_interval * 1000.0,
			total_time / (double)report_interval * 1000.0 /
			 (double)count);
		total_time = 0.0;

		/*
		 * don't let the loops variable wrap if we are running
		 * forever, it will cause weird times to be reported.
		 */
		if (loop_count == 0)
			loops = 0;
	}
    }
}

