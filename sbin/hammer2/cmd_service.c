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

#include <sys/xdiskioctl.h>

struct diskcon {
	TAILQ_ENTRY(diskcon) entry;
	char	*disk;
};

struct service_node_opaque {
	char	cl_label[64];
	char	fs_label[64];
	dmsg_media_block_t block;
	int	attached;
	int	servicing;
	int	servicefd;
};

#define WS " \r\n"

TAILQ_HEAD(, diskcon) diskconq = TAILQ_HEAD_INITIALIZER(diskconq);
pthread_mutex_t diskmtx;

static void *service_thread(void *data);
static void *udev_thread(void *data);
static void master_reconnect(const char *mntpt);
static void disk_reconnect(const char *disk);
static void disk_disconnect(void *handle);
static void udev_check_disks(void);
static void service_node_handler(void **opaque, struct dmsg_msg *msg, int op);

static void xdisk_reconnect(struct service_node_opaque *info);
static void xdisk_disconnect(void *handle);
static void *xdisk_attach_tmpthread(void *data);

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

	dmsg_node_handler = service_node_handler;

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
		info->label = strdup("client");
		pthread_create(&thread, NULL, dmsg_master_service, info);
	}
	return (NULL);
}

/*
 * Node discovery code on received SPANs (or loss of SPANs).  This code
 * is used to track the availability of remote block devices and install
 * or deinstall them using the xdisk driver (/dev/xdisk).
 *
 * An installed xdisk creates /dev/xa%d and /dev/serno/<blah> based on
 * the data handed to it.  When opened, a virtual circuit is forged and
 * maintained to the block device server via DMSG.  Temporary failures
 * stall the device until successfully reconnected or explicitly destroyed.
 */
static
void
service_node_handler(void **opaquep, struct dmsg_msg *msg, int op)
{
	struct service_node_opaque *info = *opaquep;

	switch(op) {
	case DMSG_NODEOP_ADD:
		if (msg->any.lnk_span.peer_type != DMSG_PEER_BLOCK)
			break;
		if (msg->any.lnk_span.pfs_type != DMSG_PFSTYPE_SERVER)
			break;
		if (info == NULL) {
			info = malloc(sizeof(*info));
			bzero(info, sizeof(*info));
			*opaquep = info;
		}
		snprintf(info->cl_label, sizeof(info->cl_label),
			 "%s", msg->any.lnk_span.cl_label);
		snprintf(info->fs_label, sizeof(info->fs_label),
			 "%s", msg->any.lnk_span.fs_label);
		info->block = msg->any.lnk_span.media.block;
		fprintf(stderr, "NODE ADD %s serno %s\n",
			info->cl_label, info->fs_label);
		xdisk_reconnect(info);
		break;
	case DMSG_NODEOP_DEL:
		if (info) {
			fprintf(stderr, "NODE DEL %s serno %s\n",
				info->cl_label, info->fs_label);
			pthread_mutex_lock(&diskmtx);
			*opaquep = NULL;
			info->attached = 0;
			if (info->servicing == 0)
				free(info);
			else
				shutdown(info->servicefd, SHUT_RDWR);/*XXX*/
			pthread_mutex_unlock(&diskmtx);
		}
		break;
	default:
		break;
	}
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
	char *disk;
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
		for (disk = strtok(buf, WS); disk; disk = strtok(NULL, WS)) {
			disk_reconnect(disk);
		}
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
	info->label = strdup("hammer2");
	pthread_create(&thread, NULL, dmsg_master_service, info);
}

/*
 * Reconnect a physical disk service to the mesh.
 */
static
void
disk_reconnect(const char *disk)
{
	struct disk_ioc_recluster recls;
	struct diskcon *dc;
	dmsg_master_service_info_t *info;
	pthread_t thread;
	int fd;
	int pipefds[2];
	char *path;

	/*
	 * Urm, this will auto-create mdX+1, just ignore for now.
	 * This mechanic needs to be fixed.  It might actually be nice
	 * to be able to export md disks.
	 */
	if (strncmp(disk, "md", 2) == 0)
		return;
	if (strncmp(disk, "xa", 2) == 0)
		return;

	/*
	 * Check if already connected
	 */
	pthread_mutex_lock(&diskmtx);
	TAILQ_FOREACH(dc, &diskconq, entry) {
		if (strcmp(dc->disk, disk) == 0)
			break;
	}
	pthread_mutex_unlock(&diskmtx);
	if (dc)
		return;

	/*
	 * Not already connected, create a connection to the kernel
	 * disk driver.
	 */
	asprintf(&path, "/dev/%s", disk);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "reconnect %s: no access to disk\n", disk);
		free(path);
		return;
	}
	free(path);
	if (pipe(pipefds) < 0) {
		fprintf(stderr, "reconnect %s: pipe() failed\n", disk);
		close(fd);
		return;
	}
	bzero(&recls, sizeof(recls));
	recls.fd = pipefds[0];
	if (ioctl(fd, DIOCRECLUSTER, &recls) < 0) {
		fprintf(stderr, "reconnect %s: ioctl failed\n", disk);
		close(pipefds[0]);
		close(pipefds[1]);
		close(fd);
		return;
	}
	close(pipefds[0]);
	close(fd);

	dc = malloc(sizeof(*dc));
	dc->disk = strdup(disk);
	pthread_mutex_lock(&diskmtx);
	TAILQ_INSERT_TAIL(&diskconq, dc, entry);
	pthread_mutex_unlock(&diskmtx);

	info = malloc(sizeof(*info));
	bzero(info, sizeof(*info));
	info->fd = pipefds[1];
	info->detachme = 1;
	info->dbgmsg_callback = hammer2_shell_parse;
	info->exit_callback = disk_disconnect;
	info->handle = dc;
	info->label = strdup(dc->disk);
	pthread_create(&thread, NULL, dmsg_master_service, info);
}

static
void
disk_disconnect(void *handle)
{
	struct diskcon *dc = handle;

	fprintf(stderr, "DISK_DISCONNECT %s\n", dc->disk);

	pthread_mutex_lock(&diskmtx);
	TAILQ_REMOVE(&diskconq, dc, entry);
	pthread_mutex_unlock(&diskmtx);
	free(dc->disk);
	free(dc);
}

/*
 * [re]connect a remote disk service to the local system via /dev/xdisk.
 */
static
void
xdisk_reconnect(struct service_node_opaque *xdisk)
{
	struct xdisk_attach_ioctl *xaioc;
	dmsg_master_service_info_t *info;
	pthread_t thread;
	int pipefds[2];

	if (pipe(pipefds) < 0) {
		fprintf(stderr, "reconnect %s: pipe() failed\n",
			xdisk->cl_label);
		return;
	}

	info = malloc(sizeof(*info));
	bzero(info, sizeof(*info));
	info->fd = pipefds[1];
	info->detachme = 1;
	info->dbgmsg_callback = hammer2_shell_parse;
	info->exit_callback = xdisk_disconnect;
	info->handle = xdisk;
	xdisk->servicing = 1;
	xdisk->servicefd = info->fd;
	info->label = strdup(xdisk->cl_label);
	pthread_create(&thread, NULL, dmsg_master_service, info);

	/*
	 * We have to run the attach in its own pthread because it will
	 * synchronously interact with the messaging subsystem over the
	 * pipe.  If we do it here we will deadlock.
	 */
	xaioc = malloc(sizeof(*xaioc));
	bzero(xaioc, sizeof(xaioc));
	snprintf(xaioc->cl_label, sizeof(xaioc->cl_label),
		 "%s", xdisk->cl_label);
	snprintf(xaioc->fs_label, sizeof(xaioc->fs_label),
		 "X-%s", xdisk->fs_label);
	xaioc->bytes = xdisk->block.bytes;
	xaioc->blksize = xdisk->block.blksize;
	xaioc->fd = pipefds[0];

	pthread_create(&thread, NULL, xdisk_attach_tmpthread, xaioc);
}

static
void *
xdisk_attach_tmpthread(void *data)
{
	struct xdisk_attach_ioctl *xaioc = data;
	int fd;

	pthread_detach(pthread_self());

	fd = open("/dev/xdisk", O_RDWR, 0600);
	if (fd < 0) {
		fprintf(stderr, "xdisk_reconnect: Unable to open /dev/xdisk\n");
	}
	if (ioctl(fd, XDISKIOCATTACH, xaioc) < 0) {
		fprintf(stderr, "reconnect %s: xdisk attach failed\n",
			xaioc->cl_label);
	}
	close(xaioc->fd);
	close(fd);
	return (NULL);
}

static
void
xdisk_disconnect(void *handle)
{
	struct service_node_opaque *info = handle;

	assert(info->servicing == 1);

	pthread_mutex_lock(&diskmtx);
	info->servicing = 0;
	if (info->attached == 0)
		free(info);
	pthread_mutex_unlock(&diskmtx);
}
