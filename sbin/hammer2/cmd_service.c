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
#include <machine/atomic.h>

struct hammer2_media_config {
	hammer2_volconf_t	copy_run;
	hammer2_volconf_t	copy_pend;
	pthread_t		thread;
	pthread_cond_t		cond;
	int			ctl;
	int			fd;
	int			pipefd[2];      /* signal stop */
	dmsg_iocom_t		iocom;
	pthread_t		iocom_thread;
	enum { H2MC_STOPPED, H2MC_CONNECT, H2MC_RUNNING } state;
};

typedef struct hammer2_media_config hammer2_media_config_t;

#define H2CONFCTL_STOP          0x00000001
#define H2CONFCTL_UPDATE        0x00000002

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

struct autoconn {
	TAILQ_ENTRY(autoconn) entry;
	char	*host;
	int	stage;
	int	stopme;
	int	pipefd[2];	/* {read,write} */
	enum { AUTOCONN_INACTIVE, AUTOCONN_ACTIVE } state;
	pthread_t thread;
};

#define WS " \r\n"

TAILQ_HEAD(, diskcon) diskconq = TAILQ_HEAD_INITIALIZER(diskconq);
static pthread_mutex_t diskmtx;
static pthread_mutex_t confmtx;

static void *service_thread(void *data);
static void *udev_thread(void *data);
static void *autoconn_thread(void *data);
static void master_reconnect(const char *mntpt);
static void disk_reconnect(const char *disk);
static void disk_disconnect(void *handle);
static void udev_check_disks(void);
static void hammer2_usrmsg_handler(dmsg_msg_t *msg, int unmanaged);
static void hammer2_node_handler(void **opaque, struct dmsg_msg *msg, int op);
static void *hammer2_volconf_thread(void *info);
static void hammer2_volconf_signal(dmsg_iocom_t *iocom);
static void hammer2_volconf_start(hammer2_media_config_t *conf,
			const char *hostname);
static void hammer2_volconf_stop(hammer2_media_config_t *conf);


static void xdisk_reconnect(struct service_node_opaque *info);
static void xdisk_disconnect(void *handle);
static void *xdisk_attach_tmpthread(void *data);

/*
 * Start-up the master listener daemon for the machine.  This daemon runs
 * a UDP discovery protocol, a TCP rendezvous, and scans certain files
 * and directories for work.
 *
 * --
 *
 * The only purpose for the UDP discovery protocol is to determine what
 * other IPs on the LAN are running the hammer2 service daemon.  DNS is not
 * required to operate, but hostnames (if assigned) must be unique.  If
 * no hostname is assigned the host's IP is used as the name.  This name
 * is broadcast along with the mtime of the originator's private key.
 *
 * Receiving hammer2 service daemons which are able to match the label against
 * /etc/hammer2/remote/<label>.pub will initiate a persistent connection
 * to the target.  Removal of the file will cause a disconnection.  A failed
 * public key negotiation stops further connection attempts until either the
 * file is updated or the remote mtime is updated.
 *
 * Generally speaking this results in a web of connections, typically a
 * combination of point-to-point for the more important links and relayed
 * (spanning tree) for less important or filtered links.
 *
 * --
 *
 * The TCP listener serves as a rendezvous point in the cluster, accepting
 * connections, performing registrations and authentications, maintaining
 * the spanning tree, and keeping track of message state so disconnects can
 * be handled properly.
 *
 * Once authenticated only low-level messaging protocols (which includes
 * tracking persistent messages) are handled by this daemon.  This daemon
 * does not run the higher level quorum or locking protocols.
 *
 * --
 *
 * The file /etc/hammer2/autoconn, if it exists, contains a list of targets
 * to connect to (which do not have to be on the local lan).  This list will
 * be retried until a connection can be established.  The file is not usually
 * needed for linkages local to the LAN.
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
	 * Start thread to manage /etc/hammer2/autoconn
	 */
	thread = NULL;
	pthread_create(&thread, NULL, autoconn_thread, NULL);

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
		info->usrmsg_callback = hammer2_usrmsg_handler;
		info->node_handler = hammer2_node_handler;
		info->label = strdup("client");
		pthread_create(&thread, NULL, dmsg_master_service, info);
	}
	return (NULL);
}

/*
 * Handle/Monitor the dmsg stream.  If unmanaged is set we are responsible
 * for responding for the message, otherwise if it is not set libdmsg has
 * already done some preprocessing and will respond to the message for us
 * when we return.
 *
 * We primarily monitor for VOLCONFs
 */
static
void
hammer2_usrmsg_handler(dmsg_msg_t *msg, int unmanaged)
{
	dmsg_state_t *state;
	hammer2_media_config_t *conf;
	dmsg_lnk_hammer2_volconf_t *msgconf;
	int i;

	/*
	 * Only process messages which are part of a LNK_CONN stream
	 */
	state = msg->state;
	if (state == NULL ||
	    (state->rxcmd & DMSGF_BASECMDMASK) != DMSG_LNK_CONN) {
		hammer2_shell_parse(msg, unmanaged);
		return;
	}

	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_DELETE:
	case DMSG_LNK_CONN | DMSGF_DELETE:
	case DMSG_LNK_ERROR | DMSGF_DELETE:
		/*
		 * Deleting connection, clean out all volume configs
		 */
		if (state->media == NULL || state->media->usrhandle == NULL)
			break;
		conf = state->media->usrhandle;
		fprintf(stderr, "Shutting down media spans\n");
		for (i = 0; i < HAMMER2_COPYID_COUNT; ++i) {
			if (conf[i].thread) {
				conf[i].ctl = H2CONFCTL_STOP;
				pthread_cond_signal(&conf[i].cond);
			}
		}
		for (i = 0; i < HAMMER2_COPYID_COUNT; ++i) {
			if (conf[i].thread) {
				pthread_join(conf[i].thread, NULL);
				conf->thread = NULL;
				pthread_cond_destroy(&conf[i].cond);
			}
		}
		state->media->usrhandle = NULL;
		free(conf);
		break;
	case DMSG_LNK_HAMMER2_VOLCONF:
		/*
		 * One-way volume-configuration message is transmitted
		 * over the open LNK_CONN transaction.
		 */
		fprintf(stderr, "RECEIVED VOLCONF\n");

		if ((conf = state->media->usrhandle) == NULL) {
			conf = malloc(sizeof(*conf) * HAMMER2_COPYID_COUNT);
			bzero(conf, sizeof(*conf) * HAMMER2_COPYID_COUNT);
			state->media->usrhandle = conf;
		}
		msgconf = H2_LNK_VOLCONF(msg);

		if (msgconf->index < 0 ||
		    msgconf->index >= HAMMER2_COPYID_COUNT) {
			fprintf(stderr,
				"VOLCONF: ILLEGAL INDEX %d\n",
				msgconf->index);
			break;
		}
		if (msgconf->copy.path[sizeof(msgconf->copy.path) - 1] != 0 ||
		    msgconf->copy.path[0] == 0) {
			fprintf(stderr,
				"VOLCONF: ILLEGAL PATH %d\n",
				msgconf->index);
			break;
		}
		conf += msgconf->index;
		pthread_mutex_lock(&confmtx);
		conf->copy_pend = msgconf->copy;
		conf->ctl |= H2CONFCTL_UPDATE;
		pthread_mutex_unlock(&confmtx);
		if (conf->thread == NULL) {
			fprintf(stderr, "VOLCONF THREAD STARTED\n");
			pthread_cond_init(&conf->cond, NULL);
			pthread_create(&conf->thread, NULL,
				       hammer2_volconf_thread, (void *)conf);
		}
		pthread_cond_signal(&conf->cond);
		break;
	default:
		if (unmanaged)
			dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
}

static void *
hammer2_volconf_thread(void *info)
{
	hammer2_media_config_t *conf = info;

	pthread_mutex_lock(&confmtx);
	while ((conf->ctl & H2CONFCTL_STOP) == 0) {
		if (conf->ctl & H2CONFCTL_UPDATE) {
			fprintf(stderr, "VOLCONF UPDATE\n");
			conf->ctl &= ~H2CONFCTL_UPDATE;
			if (bcmp(&conf->copy_run, &conf->copy_pend,
				 sizeof(conf->copy_run)) == 0) {
				fprintf(stderr, "VOLCONF: no changes\n");
				continue;
			}
			/*
			 * XXX TODO - auto reconnect on lookup failure or
			 *		connect failure or stream failure.
			 */

			pthread_mutex_unlock(&confmtx);
			hammer2_volconf_stop(conf);
			conf->copy_run = conf->copy_pend;
			if (conf->copy_run.copyid != 0 &&
			    strncmp(conf->copy_run.path, "span:", 5) == 0) {
				hammer2_volconf_start(conf,
						      conf->copy_run.path + 5);
			}
			pthread_mutex_lock(&confmtx);
			fprintf(stderr, "VOLCONF UPDATE DONE state %d\n", conf->state);
		}
		if (conf->state == H2MC_CONNECT) {
			hammer2_volconf_start(conf, conf->copy_run.path + 5);
			pthread_mutex_unlock(&confmtx);
			sleep(5);
			pthread_mutex_lock(&confmtx);
		} else {
			pthread_cond_wait(&conf->cond, &confmtx);
		}
	}
	pthread_mutex_unlock(&confmtx);
	hammer2_volconf_stop(conf);
	return(NULL);
}

static
void
hammer2_volconf_start(hammer2_media_config_t *conf, const char *hostname)
{
	dmsg_master_service_info_t *info;

	switch(conf->state) {
	case H2MC_STOPPED:
	case H2MC_CONNECT:
		conf->fd = dmsg_connect(hostname);
		if (conf->fd < 0) {
			fprintf(stderr, "Unable to connect to %s\n", hostname);
			conf->state = H2MC_CONNECT;
		} else if (pipe(conf->pipefd) < 0) {
			close(conf->fd);
			fprintf(stderr, "pipe() failed during volconf\n");
			conf->state = H2MC_CONNECT;
		} else {
			fprintf(stderr, "VOLCONF CONNECT\n");
			info = malloc(sizeof(*info));
			bzero(info, sizeof(*info));
			info->fd = conf->fd;
			info->altfd = conf->pipefd[0];
			info->altmsg_callback = hammer2_volconf_signal;
			info->detachme = 0;
			conf->state = H2MC_RUNNING;
			pthread_create(&conf->iocom_thread, NULL,
				       dmsg_master_service, info);
		}
		break;
	case H2MC_RUNNING:
		break;
	}
}

static
void
hammer2_volconf_stop(hammer2_media_config_t *conf)
{
	switch(conf->state) {
	case H2MC_STOPPED:
		break;
	case H2MC_CONNECT:
		conf->state = H2MC_STOPPED;
		break;
	case H2MC_RUNNING:
		close(conf->pipefd[1]);
		conf->pipefd[1] = -1;
		pthread_join(conf->iocom_thread, NULL);
		conf->iocom_thread = NULL;
		conf->state = H2MC_STOPPED;
		break;
	}
}

static
void
hammer2_volconf_signal(dmsg_iocom_t *iocom)
{
	atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
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
hammer2_node_handler(void **opaquep, struct dmsg_msg *msg, int op)
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
		info->attached = 1;
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

static void *autoconn_connect_thread(void *data);
static void autoconn_disconnect_signal(dmsg_iocom_t *iocom);

static
void *
autoconn_thread(void *data __unused)
{
	TAILQ_HEAD(, autoconn) autolist;
	struct autoconn *ac;
	struct autoconn *next;
	pthread_t thread;
	struct stat st;
	time_t	t;
	time_t	lmod;
	int	found_last;
	FILE	*fp;
	char	buf[256];

	TAILQ_INIT(&autolist);
	found_last = 0;
	lmod = 0;

	pthread_detach(pthread_self());
	for (;;) {
		/*
		 * Polling interval
		 */
		sleep(5);

		/*
		 * Poll the file.  Loop up if the synchronized state (lmod)
		 * has not changed.
		 */
		if (stat(HAMMER2_DEFAULT_DIR "/autoconn", &st) == 0) {
			if (lmod == st.st_mtime)
				continue;
			fp = fopen(HAMMER2_DEFAULT_DIR "/autoconn", "r");
			if (fp == NULL)
				continue;
		} else {
			if (lmod == 0)
				continue;
			fp = NULL;
		}

		/*
		 * Wait at least 5 seconds after the file is created or
		 * removed.
		 *
		 * Do not update the synchronized state.
		 */
		if (fp == NULL && found_last) {
			found_last = 0;
			continue;
		} else if (fp && found_last == 0) {
			fclose(fp);
			found_last = 1;
			continue;
		}

		/*
		 * Don't scan the file until the time progresses past the
		 * file's mtime, so we can validate that the file was not
		 * further modified during our scan.
		 *
		 * Do not update the synchronized state.
		 */
		time(&t);
		if (fp) {
			if (t == st.st_mtime) {
				fclose(fp);
				continue;
			}
			t = st.st_mtime;
		} else {
			t = 0;
		}

		/*
		 * Set staging to disconnect, then scan the file.
		 */
		TAILQ_FOREACH(ac, &autolist, entry)
			ac->stage = 0;
		while (fp && fgets(buf, sizeof(buf), fp) != NULL) {
			char *host;

			if ((host = strtok(buf, " \t\r\n")) == NULL ||
			    host[0] == '#') {
				continue;
			}
			TAILQ_FOREACH(ac, &autolist, entry) {
				if (strcmp(host, ac->host) == 0)
					break;
			}
			if (ac == NULL) {
				ac = malloc(sizeof(*ac));
				bzero(ac, sizeof(*ac));
				ac->host = strdup(host);
				ac->state = AUTOCONN_INACTIVE;
				TAILQ_INSERT_TAIL(&autolist, ac, entry);
			}
			ac->stage = 1;
		}

		/*
		 * Ignore the scan (and retry again) if the file was
		 * modified during the scan.
		 *
		 * Do not update the synchronized state.
		 */
		if (fp) {
			if (fstat(fileno(fp), &st) < 0) {
				fclose(fp);
				continue;
			}
			fclose(fp);
			if (t != st.st_mtime)
				continue;
		}

		/*
		 * Update the synchronized state and reconfigure the
		 * connect list as needed.
		 */
		lmod = t;
		next = TAILQ_FIRST(&autolist);
		while ((ac = next) != NULL) {
			next = TAILQ_NEXT(ac, entry);

			/*
			 * Staging, initiate
			 */
			if (ac->stage && ac->state == AUTOCONN_INACTIVE) {
				if (pipe(ac->pipefd) == 0) {
					ac->stopme = 0;
					ac->state = AUTOCONN_ACTIVE;
					thread = NULL;
					pthread_create(&thread, NULL,
						       autoconn_connect_thread,
						       ac);
				}
			}

			/*
			 * Unstaging, stop active connection.
			 *
			 * We write to the pipe which causes the iocom_core
			 * to call autoconn_disconnect_signal().
			 */
			if (ac->stage == 0 &&
			    ac->state == AUTOCONN_ACTIVE) {
				if (ac->stopme == 0) {
					char dummy = 0;
					ac->stopme = 1;
					write(ac->pipefd[1], &dummy, 1);
				}
			}

			/*
			 * Unstaging, delete inactive connection.
			 */
			if (ac->stage == 0 &&
			    ac->state == AUTOCONN_INACTIVE) {
				TAILQ_REMOVE(&autolist, ac, entry);
				free(ac->host);
				free(ac);
				continue;
			}
		}
		sleep(5);
	}
	return(NULL);
}

static
void *
autoconn_connect_thread(void *data)
{
	dmsg_master_service_info_t *info;
	struct autoconn *ac;
	void *res;
	int fd;

	ac = data;
	pthread_detach(pthread_self());

	while (ac->stopme == 0) {
		fd = dmsg_connect(ac->host);
		if (fd < 0) {
			fprintf(stderr, "autoconn: Connect failure: %s\n",
				ac->host);
			sleep(5);
			continue;
		}
		fprintf(stderr, "autoconn: Connect %s\n", ac->host);

		info = malloc(sizeof(*info));
		bzero(info, sizeof(*info));
		info->fd = fd;
		info->altfd = ac->pipefd[0];
		info->altmsg_callback = autoconn_disconnect_signal;
		info->detachme = 0;
		info->noclosealt = 1;
		pthread_create(&ac->thread, NULL, dmsg_master_service, info);
		pthread_join(ac->thread, &res);
	}
	close(ac->pipefd[0]);
	ac->state = AUTOCONN_INACTIVE;
	/* auto structure can be ripped out here */
	return(NULL);
}

static
void
autoconn_disconnect_signal(dmsg_iocom_t *iocom)
{
	fprintf(stderr, "autoconn: Shutting down socket\n");
	atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
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
	info->usrmsg_callback = hammer2_usrmsg_handler;
	info->node_handler = hammer2_node_handler;
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
	info->usrmsg_callback = hammer2_usrmsg_handler;
	info->node_handler = hammer2_node_handler;
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
	info->usrmsg_callback = hammer2_usrmsg_handler;
	info->node_handler = hammer2_node_handler;
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
	bzero(xaioc, sizeof(*xaioc));
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
