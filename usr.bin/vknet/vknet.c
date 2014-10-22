/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/usr.bin/vknet/vknet.c,v 1.1 2008/05/27 23:26:38 dillon Exp $
 */
/*
 * vknet [-C] [-b local-bridge] [-B remote-bridge] [-r delay[:retries]]
 *       local-spec [user@]remote[:remote-spec]
 * vknet -S [-b local-bridge] local-spec	(server mode)
 *
 * Connect a SOCK_SEQPACKET socket or TUN device on the local host with
 * a SOCK_SEQPACKET socket or TUN device on the remote host through a SSH
 * connection.  When a TUN device is specified it may be optionally bridged.
 *
 * This program expects packetized reads and writes on the local and remote
 * sides and will re-block them over the SSH stream.
 */

#include "vknet.h"

static void vknet_blastaway(ioinfo_t ios, ioinfo_t iod);
static void *vknet_stream(void *arg);
static void vknet_connect(ioinfo_t ios,
			  const char *localSide, const char *localBridge);
static pid_t vknet_execssh(int fdin, int fdout, int compressOpt,
			   const char *remoteSide, const char *remoteBridge);
static void usage(void);

pthread_mutex_t MasterLock;

int
main(int ac, char **av)
{
	int compressOpt = 0;
	int remoteOpt = 0;
	const char *localBridge = NULL;
	const char *remoteBridge = NULL;
	const char *localSide;
	const char *remoteSide;
	char *ptr;
	int c;
	int retriesOpt = -1;
	int timeoutOpt = -1;
	pid_t sshpid = -1;
	pid_t p;
	struct ioinfo ios;
	struct ioinfo iod;

	while ((c = getopt(ac, av, "b:B:r:CS")) != -1) {
		switch (c) {
		case 'b':
			localBridge = optarg;
			break;
		case 'B':
			remoteBridge = optarg;
			break;
		case 'r':
			timeoutOpt = strtol(optarg, &ptr, 0);
			if (ptr && *ptr == ':')
				retriesOpt = strtol(ptr + 1, NULL, 0);
			break;
		case 'S':
			remoteOpt = 1;
			break;
		case 'C':
			compressOpt = 1;
			break;
		default:
			usage();
		}
	}
	av += optind;
	ac -= optind;

	/*
	 * Local and remote arguments.
	 */
	if (remoteOpt) {
		if (ac != 1)
			usage();
		localSide = av[0];
		remoteSide = NULL;
	} else {
		if (ac != 2)
			usage();
		localSide = av[0];
		remoteSide = av[1];
	}

	pthread_mutex_init(&MasterLock, NULL);

retry:
	/*
	 * Setup connections
	 */
	vknet_connect(&ios, localSide, localBridge);
	if (remoteOpt) {
		iod.fdin = 0;
		iod.fdout = 1;
	} else {
		int fds[2];

		if (pipe(fds) < 0) {
			perror("pipe");
			exit(1);
		}
		sshpid = vknet_execssh(fds[1], fds[1], compressOpt,
			      remoteSide, remoteBridge);
		close(fds[1]);
		iod.fdin = fds[0];
		iod.fdout = fds[0];
	}

	/*
	 * Blast away, timeout/retry on failure
	 */
	vknet_blastaway(&ios, &iod);

	/*
	 * Terminate child process
	 */
	if (sshpid > 0) {
		if (kill(sshpid, SIGTERM) != 0)
			perror("kill");
		while ((p = waitpid(sshpid, NULL, 0)) != sshpid) {
			if (p < 0 && errno != EINTR)
				break;
		}
		sshpid = -1;
	}

	/*
	 * Handle timeout/retries
	 */
	if (timeoutOpt >= 0 && retriesOpt != 0) {
		printf("timeout %d retries %d\n", timeoutOpt, retriesOpt);
		if (timeoutOpt > 0)
			sleep(timeoutOpt);
		if (retriesOpt > 0)
			--retriesOpt;
		goto retry;
	}
	exit(0);
}

static void
vknet_blastaway(ioinfo_t ios, ioinfo_t iod)
{
	struct streaminfo stream1;
	struct streaminfo stream2;

	pthread_mutex_lock(&MasterLock);
	stream1.fdin = ios->fdin;
	stream1.fdout = iod->fdout;
	stream1.flags = REBLOCK_OUT;
	stream1.other = &stream2;
	stream2.fdin = iod->fdin;
	stream2.fdout = ios->fdout;
	stream2.flags = REBLOCK_IN;
	stream2.other = &stream1;
	pthread_create(&stream1.thread, NULL, vknet_stream, &stream1);
	pthread_create(&stream2.thread, NULL, vknet_stream, &stream2);
	pthread_mutex_unlock(&MasterLock);
	pthread_join(stream1.thread, NULL);
	pthread_join(stream2.thread, NULL);
}

/*
 * Transfer packets between two descriptors
 */
static
void *
vknet_stream(void *arg)
{
	streaminfo_t stream = arg;
	struct blkhead head;
	u_int8_t *pkt;
	int bytes;
	int n;
	int r;

	/*
	 * Synchronize with master thread, then loop
	 */
	pthread_mutex_lock(&MasterLock);
	pthread_mutex_unlock(&MasterLock);

	pkt = malloc(MAXPKT);

	for (;;) {
		/*
		 * Input side
		 */
		if (stream->flags & REBLOCK_IN) {
			bytes = sizeof(head);
			for (n = 0; n < bytes; n += r) {
				r = read(stream->fdin, (char *)&head + n,
					 bytes - n);
				if (r <= 0)
					break;
			}
			if (n != bytes)
				break;
			if (le32toh(head.magic) != MAGIC)
				break;
			bytes = le32toh(head.bytes);
			if (bytes <= 0 || bytes > MAXPKT)
				break;
			for (n = 0; n < bytes; n += r) {
				r = read(stream->fdin, pkt + n, bytes - n);
				if (r <= 0)
					break;
			}
			if (n != bytes)
				break;
		} else {
			bytes = read(stream->fdin, pkt, MAXPKT);
			if (bytes <= 0)
				break;
		}

		/*
		 * Output side
		 */
		if (stream->flags & REBLOCK_OUT) {
			head.magic = htole32(MAGIC);
			head.bytes = htole32(bytes);
			if (write(stream->fdout, &head, sizeof(head)) != sizeof(head))
				break;
			if (write(stream->fdout, pkt, bytes) != bytes)
				break;
		} else {
			if (write(stream->fdout, pkt, bytes) != bytes)
				break;
		}
	}
	free(pkt);
	close(stream->fdin);
	close(stream->fdout);
	pthread_cancel(stream->other->thread);
	pthread_exit(NULL);
}

/*
 * vknet_connect() - Connect to local side, optionally find or bridge the tap
 *		     interface.
 */
static void
vknet_connect(ioinfo_t io, const char *localSide, const char *localBridge)
{
	struct ifreq ifr;
	struct ifaliasreq ifra;
	char *buf = NULL;
	int tap_fd;
	int tap_unit;
	int i;
	int s;
	int flags;

	tap_unit = -1;
	tap_fd = -1;

	if (strcmp(localSide, "auto") == 0) {
		for (i = 0; ; ++i) {
			asprintf(&buf, "/dev/tap%d", i);
			tap_fd = open(buf, O_RDWR | O_NONBLOCK);
			free(buf);
			if (tap_fd >= 0 || errno == ENOENT) {
				tap_unit = i;
				break;
			}
		}
	} else if (strncmp(localSide, "tap", 3) == 0) {
		asprintf(&buf, "/dev/%s", localSide);
		tap_fd = open(buf, O_RDWR | O_NONBLOCK);
		tap_unit = strtol(localSide + 3, NULL, 10);
		free(buf);
	} else if ((tap_fd = open(localSide, O_RDWR | O_NONBLOCK)) >= 0) {
		const char *ptr = localSide + strlen(localSide);
		while (ptr > localSide && ptr[-1] >= '0' && ptr[-1] <= '9')
			--ptr;
		tap_unit = strtol(ptr, NULL, 10);
	} else {
		struct sockaddr_un sunx;
		int len;

		snprintf(sunx.sun_path, sizeof(sunx.sun_path), "%s", localSide);
		len = offsetof(struct sockaddr_un,
			       sun_path[strlen(sunx.sun_path)]);
		++len;	/* include nul */
		sunx.sun_family = AF_UNIX;
		sunx.sun_len = len;

		tap_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (tap_fd >= 0)	 {
			if (connect(tap_fd, (void *)&sunx, len) < 0) {
				close(tap_fd);
				tap_fd = -1;
			}
		}
	}

	if (tap_fd < 0) {
		err(1, "Unable to connect to %s", localSide);
		/* NOT REACHED */
	}

	fcntl(tap_fd, F_SETFL, 0);
	io->fdin = tap_fd;
	io->fdout = tap_fd;

	/*
	 * If this isn't a TAP device we are done.
	 */
	if (tap_unit < 0)
		return;

	/*
	 * Bring up the TAP interface
	 */
	bzero(&ifr, sizeof(ifr));
	bzero(&ifra, sizeof(ifra));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "tap%d", tap_unit);
	snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "tap%d", tap_unit);

	s = socket(AF_INET, SOCK_DGRAM, 0);

#if 0
	/*
	 * Set the interface address if in Secure mode.
	 */
	if (SecureOpt) {
		struct sockaddr_in *in;

		in = (void *)&ifra.ifra_addr;
		in->sin_family = AF_INET;
		in->sin_len = sizeof(ifra.ifra_addr);
		in->sin_addr = NetAddress;
		in = (void *)&ifra.ifra_mask;
		in->sin_family = AF_INET;
		in->sin_len = sizeof(ifra.ifra_mask);
		in->sin_addr = NetMask;
		if (ioctl(s, SIOCAIFADDR, &ifra) < 0) {
			perror("Unable to set address on tap interface");
			exit(1);
		}
	}
#endif

	/*
	 * Turn up the interface
	 */
	flags = IFF_UP;
	if (ioctl(s, SIOCGIFFLAGS, &ifr) >= 0) {
		bzero(&ifr, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "tap%d", tap_unit);
		ifr.ifr_flags |= flags & 0xFFFF;
		ifr.ifr_flagshigh |= flags >> 16;
		if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
			perror("Unable to set IFF_UP on tap interface");
			exit(1);
		}
	}

	/*
	 * If a bridge was specified associate the tap interface with the
	 * bridge.
	 */
	if (localBridge) {
		struct ifbreq ifbr;
		struct ifdrv ifd;

		/*
		 * Create the bridge if necessary.
		 */
		bzero(&ifr, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", localBridge);
		if (ioctl(s, SIOCIFCREATE, &ifr) < 0) {
			if (errno != EEXIST) {
				perror("Unable to create bridge interface");
				exit(1);
			}
		}


		/*
		 * Add the tap interface to the bridge
		 */
		bzero(&ifbr, sizeof(ifbr));
		snprintf(ifbr.ifbr_ifsname, sizeof(ifbr.ifbr_ifsname),
			 "tap%d", tap_unit);

		bzero(&ifd, sizeof(ifd));
		snprintf(ifd.ifd_name, sizeof(ifd.ifd_name), "%s", localBridge);
		ifd.ifd_cmd = BRDGADD;
		ifd.ifd_len = sizeof(ifbr);
		ifd.ifd_data = &ifbr;

		if (ioctl(s, SIOCSDRVSPEC, &ifd) < 0) {
			if (errno != EEXIST) {
				perror("Unable to add tap ifc to bridge!");
				exit(1);
			}
		}
	}
	close(s);
}

/*
 * Connect to the remote machine with ssh and set up a stream
 */
static pid_t
vknet_execssh(int fdin, int fdout, int compressOpt, 
	      const char *remoteSide, const char *remoteBridge)
{
	char *remoteHost;
	char *remotePath;
	const char *av[24];
	int ac;
	pid_t pid;

	/*
	 * Fork / parent returns.
	 */
	if ((pid = fork()) > 0)
		return pid;
	if (pid < 0) {
		perror("fork");
		exit(1);
	}

	/*
	 * Setup stdin, stdout
	 */
	assert(fdin > 2);
	assert(fdout > 2);
	dup2(fdin, 0);
	dup2(fdout, 1);
	close(fdin);
	close(fdout);

	/*
	 * Set up arguments
	 */
	remoteHost = strdup(remoteSide);
	if ((remotePath = strchr(remoteHost, ':')) != NULL) {
		*remotePath++ = 0;
	} else {
		remotePath = strdup("/var/run/vknet");
	}
	ac = 0;
	av[ac++] = "ssh";
	if (compressOpt)
		av[ac++] = "-C";
	av[ac++] = "-x";
	av[ac++] = "-T";
	av[ac++] = "-e";
	av[ac++] = "none";
	av[ac++] = remoteHost;
	av[ac++] = "exec";
	av[ac++] = "vknet";
	av[ac++] = "-S";
	if (remoteBridge) {
		av[ac++] = "-b";
		av[ac++] = remoteBridge;
	}
	av[ac++] = remotePath;
	av[ac++] = NULL;
	execv("/usr/bin/ssh", (void *)av);
	exit(1);
}

/*
 * Misc
 */
static
void
usage(void)
{
	fprintf(stderr, 
		"vknet [-C] [-b local-bridge] [-B remote-bridge] [-r delay[:retries]]\n"
		"      local-spec [user@]remote[:remote-spec]\n"
		"vknet -S [-b local-bridge] local-spec\n"
	);
	exit(1);
}

