/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#include "hammer2.h"

static void helper_master_listen(void);
static void *helper_master_accept(void *data);
static void *helper_master_service(void *data);

/*
 * The first hammer2 helper will also fork off a daemon to listen on
 * and accept connections from the machine interconnect socket.  This
 * helper operates across all HAMMER2 mounts.
 *
 * An additional independent multi-threaded helper daemon is run for
 * each HAMMER2 PFS mount.  This helper connects to the master helper
 * and registers the PFSID for each mount, allowing the master helper
 * to forward accepted descriptors to the per-PFS helpers after handling
 * authentication and accepting the PFSID.
 *
 * The per-mount helper daemon will then install relay pipe descriptors
 * into the kernel VFS so the HAMMER2 filesystem can issue requests / accept
 * commands as needed.  Note that the HAMMER2 filesystem will also track
 * the cache state and will generally be able to bypass talking to the helper
 * threads when local media is available and determined to contain the
 * required data.
 *
 * WARNING!  Except for sel_path, we avoid accessing the filesystem.  In
 *	     a fully remote root mount scenario the administrative root
 *	     will be mounted before the helper is started up.
 */
int
cmd_helper(const char *sel_path)
{
	int ecode = 0;
	int fd;

	/*
	 * Install the master server if it is not already running.
	 */
	helper_master_listen();

	/*
	 * Acquire a handle for ioctls, which will also extract the PFSID
	 * for the mounted PFS.  If sel_path is NULL we just start the
	 * master listener and do not go any further.
	 */
	if (sel_path == NULL)
		return(0);
	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);

	/*
	 * Connect to the master to register the PFSID and start the
	 * per-PFS helper if we succeed, otherwise a helper is already
	 * running and registered.
	 */

	return ecode;
}

static
void
helper_master_listen(void)
{
	struct sockaddr_in lsin;
	int on;
	int lfd;

	/*
	 * Acquire socket and set options
	 */
	if ((lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "helper_master_listen: socket(): %s\n",
			strerror(errno));
		return;
	}
	on = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	/*
	 * Setup listen port and try to bind.  If the bind fails we assume
	 * that a master listener process is already running.
	 */
	bzero(&lsin, sizeof(lsin));
	lsin.sin_addr.s_addr = INADDR_ANY;
	lsin.sin_port = htons(HAMMER2_LISTEN_PORT);
	if (bind(lfd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(lfd);
		return;
	}
	listen(lfd, 50);

	/*
	 * Fork and disconnect the controlling terminal and parent process,
	 * executing the specified function as a pthread.
	 *
	 * Returns to the original process which can then continue running.
	 * In debug mode this call will create the pthread without forking
	 * and set NormalExit to 0.
	 */
	hammer2_disconnect(helper_master_accept, (void *)(intptr_t)lfd);
	if (NormalExit)
		close(lfd);
}

/*
 * pthread to accept connections on the master socket
 */
static
void *
helper_master_accept(void *data)
{
	struct sockaddr_in asin;
	socklen_t alen;
	pthread_t thread;
	int lfd = (int)(intptr_t)data;
	int fd;

	/*
	 * Nobody waits for us
	 */
	setproctitle("hammer2 master listen");
	pthread_detach(pthread_self());

	/*
	 * Accept connections and create pthreads to handle them after
	 * validating the IP.
	 */
	for (;;) {
		alen = sizeof(asin);
		fd = accept(lfd, (struct sockaddr *)&asin, &alen);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		thread = NULL;
		pthread_create(&thread, NULL,
			       helper_master_service, (void *)(intptr_t)fd);
	}
	return (NULL);
}

/*
 * pthread for each connection
 */
static
void *
helper_master_service(void *data)
{
	char buf[256];
	ssize_t len;
	int fd = (int)(intptr_t)data;

	while ((len = read(fd, buf, sizeof(buf))) > 0) {
		write(fd, buf, len);
	}
	close(fd);

	return (NULL);
}
