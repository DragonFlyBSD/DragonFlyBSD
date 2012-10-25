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

static void *service_thread(void *data);
static void *udev_thread(void *data);
static void master_reconnect(const char *mntpt);
static void udev_check_disks(void);

/*
 * Start-up the master listener daemon for the machine.
 *
 * The master listener serves as a rendezvous point in the cluster, accepting
 * connections, performing registrations and authentications, maintaining
 * the spanning tree, and keeping track of message state so disconnects can
 * be handled properly.
 *
 * Once authenticated only low-level messaging protocols (which includes
 * tracking persistent messages) are handled by this daemon.  This daemon
 * does not run the higher level quorum or locking protocols.
 *
 * This daemon can also be told to maintain connections to other nodes,
 * forming a messaging backbone, which in turn allows PFS's (if desired) to
 * simply connect to the master daemon via localhost if desired.
 * Backbones are specified via /etc/hammer2.conf.
 */
int
cmd_service(void)
{
	struct sockaddr_in lsin;
	int on;
	int lfd;

	/*
	 * Acquire socket and set options
	 */
	if ((lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "master_listen: socket(): %s\n",
			strerror(errno));
		return 1;
	}
	on = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	/*
	 * Setup listen port and try to bind.  If the bind fails we assume
	 * that a master listener process is already running and silently
	 * fail.
	 */
	bzero(&lsin, sizeof(lsin));
	lsin.sin_family = AF_INET;
	lsin.sin_addr.s_addr = INADDR_ANY;
	lsin.sin_port = htons(DMSG_LISTEN_PORT);
	if (bind(lfd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(lfd);
		if (QuietOpt == 0) {
			fprintf(stderr,
				"master listen: daemon already running\n");
		}
		return 0;
	}
	if (QuietOpt == 0)
		fprintf(stderr, "master listen: startup\n");
	listen(lfd, 50);

	/*
	 * Fork and disconnect the controlling terminal and parent process,
	 * executing the specified function as a pthread.
	 *
	 * Returns to the original process which can then continue running.
	 * In debug mode this call will create the pthread without forking
	 * and set NormalExit to 0, instead of fork.
	 */
	hammer2_demon(service_thread, (void *)(intptr_t)lfd);
	if (NormalExit)
		close(lfd);
	return 0;
}

/*
 * Master listen/accept thread.  Accept connections on the master socket,
 * starting a pthread for each one.
 */
static
void *
service_thread(void *data)
{
	struct sockaddr_in asin;
	socklen_t alen;
	pthread_t thread;
	dmsg_master_service_info_t *info;
	int lfd = (int)(intptr_t)data;
	int fd;
	int i;
	int count;
	struct statfs *mntbuf = NULL;
	struct statvfs *mntvbuf = NULL;

	/*
	 * Nobody waits for us
	 */
	setproctitle("hammer2 master listen");
	pthread_detach(pthread_self());

	/*
	 * Start up a thread to handle block device monitoring
	 */
	thread = NULL;
	pthread_create(&thread, NULL, udev_thread, NULL);

	/*
	 * Scan existing hammer2 mounts and reconnect to them using
	 * HAMMER2IOC_RECLUSTER.
	 */
	count = getmntvinfo(&mntbuf, &mntvbuf, MNT_NOWAIT);
	for (i = 0; i < count; ++i) {
		if (strcmp(mntbuf[i].f_fstypename, "hammer2") == 0)
			master_reconnect(mntbuf[i].f_mntonname);
	}

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
		fprintf(stderr, "service_thread: accept fd %d\n", fd);
		info = malloc(sizeof(*info));
		bzero(info, sizeof(*info));
		info->fd = fd;
		info->detachme = 1;
		info->dbgmsg_callback = hammer2_shell_parse;
		pthread_create(&thread, NULL, dmsg_master_service, info);
	}
	return (NULL);
}

/*
 * Monitor block devices.  Currently polls every ~10 seconds or so.
 */
static
void *
udev_thread(void *data __unused)
{
	int	fd;
	int	seq = 0;

	pthread_detach(pthread_self());

	if ((fd = open(UDEV_DEVICE_PATH, O_RDWR)) < 0) {
		fprintf(stderr, "udev_thread: unable to open \"%s\"\n",
			UDEV_DEVICE_PATH);
		pthread_exit(NULL);
	}
	udev_check_disks();
	while (ioctl(fd, UDEVWAIT, &seq) == 0) {
		udev_check_disks();
		sleep(1);
	}
	return (NULL);
}

/*
 * Retrieve the list of disk attachments and attempt to export
 * them.
 */
static
void
udev_check_disks(void)
{
	char tmpbuf[1024];
	char *buf = NULL;
	int error;
	size_t n;

	for (;;) {
		n = 0;
		error = sysctlbyname("kern.disks", NULL, &n, NULL, 0);
		if (error < 0 || n == 0)
			break;
		if (n >= sizeof(tmpbuf))
			buf = malloc(n + 1);
		else
			buf = tmpbuf;
		error = sysctlbyname("kern.disks", buf, &n, NULL, 0);
		if (error == 0) {
			buf[n] = 0;
			break;
		}
		if (buf != tmpbuf) {
			free(buf);
			buf = NULL;
		}
		if (errno != ENOMEM)
			break;
	}
	if (buf) {
		fprintf(stderr, "DISKS: %s\n", buf);
		if (buf != tmpbuf)
			free(buf);
	}
}

/*
 * Normally the mount program supplies a cluster communications
 * descriptor to the hammer2 vfs on mount, but if you kill the service
 * daemon and restart it that link will be lost.
 *
 * This procedure attempts to [re]connect to existing mounts when
 * the service daemon is started up before going into its accept
 * loop.
 *
 * NOTE: A hammer2 mount point can only accomodate one connection at a time
 *	 so this will disconnect any existing connection during the
 *	 reconnect.
 */
static
void
master_reconnect(const char *mntpt)
{
	struct hammer2_ioc_recluster recls;
	dmsg_master_service_info_t *info;
	pthread_t thread;
	int fd;
	int pipefds[2];

	fd = open(mntpt, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "reconnect %s: no access to mount\n", mntpt);
		return;
	}
	if (pipe(pipefds) < 0) {
		fprintf(stderr, "reconnect %s: pipe() failed\n", mntpt);
		close(fd);
		return;
	}
	bzero(&recls, sizeof(recls));
	recls.fd = pipefds[0];
	if (ioctl(fd, HAMMER2IOC_RECLUSTER, &recls) < 0) {
		fprintf(stderr, "reconnect %s: ioctl failed\n", mntpt);
		close(pipefds[0]);
		close(pipefds[1]);
		close(fd);
		return;
	}
	close(pipefds[0]);
	close(fd);

	info = malloc(sizeof(*info));
	bzero(info, sizeof(*info));
	info->fd = pipefds[1];
	info->detachme = 1;
	info->dbgmsg_callback = hammer2_shell_parse;
	pthread_create(&thread, NULL, dmsg_master_service, info);
}
