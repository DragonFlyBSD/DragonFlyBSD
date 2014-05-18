#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *cmd)
{
	fprintf(stderr,
	    "%s -m addr -p port [-d dst] [-i addr] [-D delay]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct ip_mreq mreq;
	struct sockaddr_in in, dst;
	struct in_addr iface;
	int s, opt, n, delay;

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;

	memset(&iface, 0, sizeof(iface));
	delay = 0;

	while ((opt = getopt(argc, argv, "m:p:d:i:D:")) != -1) {
		switch (opt) {
		case 'm':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0) {
				fprintf(stderr, "invalid -m addr %s\n", optarg);
				exit(1);
			}
			break;

		case 'p':
			in.sin_port = htons(atoi(optarg));
			dst.sin_port = in.sin_port;
			break;

		case 'd':
			if (inet_pton(AF_INET, optarg, &dst.sin_addr) <= 0) {
				fprintf(stderr, "invalid -d addr %s\n", optarg);
				exit(1);
			}
			break;

		case 'i':
			if (inet_pton(AF_INET, optarg, &iface) <= 0) {
				fprintf(stderr, "invalid -i addr %s\n", optarg);
				exit(1);
			}
			break;

		case 'D':
			delay = atoi(optarg);
			break;

		default:
			usage(argv[0]);
		}
	}

	if (in.sin_addr.s_addr == INADDR_ANY || in.sin_port == 0)
		usage(argv[0]);
	if (dst.sin_addr.s_addr == 0)
		dst.sin_addr = in.sin_addr;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		fprintf(stderr, "socket failed: %d\n", errno);
		exit(1);
	}

	if (bind(s, (const struct sockaddr *)&in, sizeof(in)) < 0) {
		fprintf(stderr, "bind failed: %d\n", errno);
		exit(1);
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr = in.sin_addr;
	mreq.imr_interface = iface;
	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
	    &mreq, sizeof(mreq)) < 0) {
		fprintf(stderr, "setsockopt IP MEMSHIP failed: %d\n", errno);
		exit(1);
	}

	if (iface.s_addr != INADDR_ANY) {
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
		    &iface, sizeof(iface)) < 0) {
			fprintf(stderr, "setsockopt IP IF failed: %d\n", errno);
			exit(1);
		}
	}

	if (delay > 0)
		sleep(delay);

	n = sendto(s, &mreq, sizeof(mreq), 0,
	    (const struct sockaddr *)&dst, sizeof(dst));
	if (n < 0) {
		fprintf(stderr, "sendto failed: %d\n", errno);
		exit(1);
	}

	exit(0);
}
