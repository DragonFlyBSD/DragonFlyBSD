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
 */

/*
 * vknet [-cdU] [-b bridgeN] [-p socket_path] [-t tapN] [address/cidrbits]
 *
 * Create a named unix-domain socket which userland vkernels can open
 * to gain access to a local network.  All connections to the socket
 * are bridged together and the local network can also be bridged onto
 * a TAP interface by specifying the -t option.
 */

#include "vknetd.h"

static ioinfo_t vknet_tap(const char *tapName, const char *bridgeName);
static int vknet_listener(const char *pathName);
static void vknet_acceptor(int net_fd);
static void *vknet_io(void *arg);
static int vknet_connect(const char *pathName);
static void vknet_monitor(int net_fd);
static void usage(void) __dead2;
static void writepid(void);
static void cleanup(int);

pthread_mutex_t BridgeMutex;

static int DebugOpt = 0;
static int SetAddrOpt = 0;
static const char *pidfile = "/var/run/vknetd.pid";

int SecureOpt = 1;
struct in_addr NetAddress;
struct in_addr NetMask;

int
main(int ac, char **av)
{
	const char *pathName = "/var/run/vknet";
	const char *tapName = "auto";
	const char *bridgeName = NULL;
	int net_fd;
	int connectOpt = 0;
	int c;
	ioinfo_t tap_info;
	pthread_t dummy_td;

	while ((c = getopt(ac, av, "b:cdp:i:t:U")) != -1) {
		switch (c) {
		case 'U':
			SecureOpt = 0;
			break;
		case 'b':
			bridgeName = optarg;
			break;
		case 'd':
			DebugOpt = 1;
			break;
		case 'p':
			pathName = optarg;
			break;
		case 'i':
			pidfile = optarg;
			break;
		case 't':
			tapName = optarg;
			break;
		case 'c':
			connectOpt = 1;
			break;
		default:
			usage();
		}
	}
	av += optind;
	ac -= optind;
	if (ac)
		SetAddrOpt = 1;

	/*
	 * Ignore SIGPIPE to prevent write() races against disconnecting
	 * clients from killing vknetd.  Should be inherited by all I/O
	 * threads.
	 */
	signal(SIGPIPE, SIG_IGN);

	/*
	 * Special connect/debug mode
	 */
	if (connectOpt) {
		net_fd = vknet_connect(pathName);
		if (net_fd < 0) {
			perror("connect");
			exit(1);
		}
		vknet_monitor(net_fd);
		exit(0);
	}

	/*
	 * In secure mode (the default), a network address/mask must be
	 * specified.  e.g. 10.1.0.0/16.  Any traffic going out the TAP
	 * interface will be filtered.
	 *
	 * If non-secure mode the network address/mask is optional.
	 */
	if (SecureOpt || SetAddrOpt) {
		char *str;
		int masklen;
		u_int32_t mask;

		if (ac == 0 || strchr(av[0], '/') == NULL)
			usage();
		str = strdup(av[0]);
		if (inet_pton(AF_INET, strtok(str, "/"), &NetAddress) <= 0)
			usage();
		masklen = strtoul(strtok(NULL, "/"), NULL, 10);
		mask = (1 << (32 - masklen)) - 1;
		NetMask.s_addr = htonl(~mask);
	}

	/*
	 * Normal operation, create the tap/bridge and listener.  This
	 * part is not threaded.
	 */
	mac_init();

	if ((tap_info = vknet_tap(tapName, bridgeName)) == NULL) {
		perror("tap: ");
		exit(1);
	}
	if ((net_fd = vknet_listener(pathName)) < 0) {
		perror("listener: ");
		exit(1);
	}

	/*
	 * Now make us a demon and start the threads going.
	 */
	if (DebugOpt == 0)
		daemon(1, 0);

	writepid();

	signal(SIGINT, cleanup);
	signal(SIGHUP, cleanup);
	signal(SIGTERM, cleanup);
	if (tap_info && tap_info->fd >= 0) {
		int pid = getpid();
		ioctl(tap_info->fd, FIOSETOWN, &pid);
	}

	pthread_mutex_init(&BridgeMutex, NULL);
	pthread_create(&dummy_td, NULL, vknet_io, tap_info);
	vknet_acceptor(net_fd);

	exit(0);
}

#define TAPDEV_MINOR(x) ((int)((x) & 0xffff00ff))

static ioinfo_t
vknet_tap(const char *tapName, const char *bridgeName)
{
	struct ifreq ifr;
	struct ifaliasreq ifra;
	struct stat st;
	char *buf = NULL;
	int tap_fd;
	int tap_unit;
	int s;
	int flags;
	ioinfo_t info;

	if (strcmp(tapName, "auto") == 0) {
		tap_fd = open("/dev/tap", O_RDWR);
	} else if (strncmp(tapName, "tap", 3) == 0) {
		asprintf(&buf, "/dev/%s", tapName);
		tap_fd = open(buf, O_RDWR | O_NONBLOCK);
		free(buf);
	} else {
		tap_fd = open(tapName, O_RDWR | O_NONBLOCK);
	}
	if (tap_fd < 0)
		return(NULL);

	/*
	 * Figure out the tap unit number
	 */
	if (fstat(tap_fd, &st) < 0) {
		close(tap_fd);
		return(NULL);
	}
	tap_unit = TAPDEV_MINOR(st.st_rdev);

	/*
	 * Output the tap interface before detaching for any script
	 * that might be using vknetd.
	 */
	printf("/dev/tap%d\n", tap_unit);
	fflush(stdout);

	/*
	 * Setup for ioctls
	 */
	fcntl(tap_fd, F_SETFL, 0);
	bzero(&ifr, sizeof(ifr));
	bzero(&ifra, sizeof(ifra));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "tap%d", tap_unit);
	snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "tap%d", tap_unit);

	s = socket(AF_INET, SOCK_DGRAM, 0);

	/*
	 * Set the interface address if in Secure mode.
	 */
	if (SetAddrOpt) {
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

	if (bridgeName) {
		struct ifbreq ifbr;
		struct ifdrv ifd;

		/*
		 * Create the bridge if necessary.
		 */
		bzero(&ifr, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", bridgeName);
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
		snprintf(ifd.ifd_name, sizeof(ifd.ifd_name), "%s", bridgeName);
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
	info = malloc(sizeof(*info));
	bzero(info, sizeof(*info));
	info->fd = tap_fd;
	info->istap = 1;
	return(info);
}

#undef TAPDEV_MINOR

static int
vknet_listener(const char *pathName)
{
	struct sockaddr_un sunx;
	int net_fd;
	int len;
	gid_t gid;
	struct group *grp;

	/*
	 * Group access to our named unix domain socket.
	 */
	if ((grp = getgrnam("vknet")) == NULL) {
		fprintf(stderr, "The 'vknet' group must exist\n");
		exit(1);
	}
	gid = grp->gr_gid;
	endgrent();

	/*
	 * Socket setup
	 */
	snprintf(sunx.sun_path, sizeof(sunx.sun_path), "%s", pathName);
	len = offsetof(struct sockaddr_un, sun_path[strlen(sunx.sun_path)]);
	++len;	/* include nul */
	sunx.sun_family = AF_UNIX;
	sunx.sun_len = len;

	net_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (net_fd < 0)
		return(-1);
	remove(pathName);
	if (bind(net_fd, (void *)&sunx, len) < 0) {
		close(net_fd);
		return(-1);
	}
	if (listen(net_fd, 1024) < 0) {
		close(net_fd);
		return(-1);
	}
	if (chown(pathName, (uid_t)-1, gid) < 0) {
		close(net_fd);
		return(-1);
	}
	if (chmod(pathName, 0660) < 0) {
		close(net_fd);
		return(-1);
	}
	return(net_fd);
}

static void
vknet_acceptor(int net_fd)
{
	struct sockaddr_un sunx;
	pthread_t dummy_td;
	int sunx_len;
	int rfd;
	ioinfo_t info;

	for (;;) {
		sunx_len = sizeof(sunx);
		rfd = accept(net_fd, (void *)&sunx, &sunx_len);
		if (rfd < 0)
			break;
		info = malloc(sizeof(*info));
		bzero(info, sizeof(*info));
		info->fd = rfd;
		info->istap = 0;
		pthread_create(&dummy_td, NULL, vknet_io, info);
	}
}

/*
 * This I/O thread implements the core of the bridging code.
 */
static void *
vknet_io(void *arg)
{
	ioinfo_t info = arg;
	bridge_t bridge;
	u_int8_t *pkt;
	int bytes;

	pthread_detach(pthread_self());

	/*
	 * Assign as a bridge slot using our thread id.
	 */
	pthread_mutex_lock(&BridgeMutex);
	bridge = bridge_add(info);
	pthread_mutex_unlock(&BridgeMutex);

	/*
	 * Read packet loop.  Writing is handled by the bridge code.
	 */
	pkt = malloc(MAXPKT);
	while ((bytes = read(info->fd, pkt, MAXPKT)) > 0) {
		pthread_mutex_lock(&BridgeMutex);
		bridge_packet(bridge, pkt, bytes);
		pthread_mutex_unlock(&BridgeMutex);
	}

	/*
	 * Cleanup
	 */
	pthread_mutex_lock(&BridgeMutex);
	bridge_del(bridge);
	pthread_mutex_unlock(&BridgeMutex);

	close(info->fd);
	free(pkt);
	pthread_exit(NULL);
}

/*
 * Debugging
 */
static int
vknet_connect(const char *pathName)
{
	struct sockaddr_un sunx;
	int len;
	int net_fd;

	snprintf(sunx.sun_path, sizeof(sunx.sun_path), "%s", pathName);
	len = offsetof(struct sockaddr_un, sun_path[strlen(sunx.sun_path)]);
	++len;	/* include nul */
	sunx.sun_family = AF_UNIX;
	sunx.sun_len = len;

	net_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (net_fd < 0)
		return(-1);
	if (connect(net_fd, (void *)&sunx, len) < 0) {
		close(net_fd);
		return(-1);
	}
	return(net_fd);
}

static void
vknet_monitor(int net_fd)
{
	u_int8_t *pkt;
	int bytes;
	int i;

	pkt = malloc(MAXPKT);
	while ((bytes = read(net_fd, pkt, MAXPKT)) > 0) {
		printf("%02x:%02x:%02x:%02x:%02x:%02x <- "
		       "%02x:%02x:%02x:%02x:%02x:%02x",
		       pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5],
		       pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[11]);
		for (i = 12; i < bytes; ++i) {
			if (((i - 12) & 15) == 0) {
				printf("\n\t");
			}
			printf(" %02x", pkt[i]);
		}
		printf("\n");
	}
	free(pkt);
}

/*
 * Misc
 */
static void
writepid(void)
{
	FILE *pf;

	if ((pf = fopen(pidfile, "w+")) == NULL)
		errx(1, "Failed to create pidfile %s", pidfile);

	if ((fprintf(pf, "%d\n", getpid())) < 1)
		err(1, "fprintf");

	fclose(pf);
}

static void
cleanup(int __unused sig)
{
	if (pidfile)
		unlink(pidfile);
}

static void
usage(void)
{
	fprintf(stderr,
		"usage: vknet [-cdU] [-b bridgeN] [-p socket_path]\n"
		"             [-i pidfile] [-t tapN] [address/cidrbits]\n"
		"\n"
		"address/cidrbits must be specified in default secure mode.\n");
	exit(1);
}

