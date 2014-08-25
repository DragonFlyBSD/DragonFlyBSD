#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -m addr -p port -i addr "
	    "-r remote_ip4 -P remote_port\n", cmd);
	exit(1);
}

static int
create_sock(const struct sockaddr_in *in, const struct in_addr *iface)
{
	struct ip_mreq mreq;
	int s, on;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	on = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0)
		err(2, "setsockopt SO_REUSEPORT failed");

	if (bind(s, (const struct sockaddr *)in, sizeof(*in)) < 0)
		err(2, "bind failed");

	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr = in->sin_addr;
	mreq.imr_interface = *iface;
	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
	    &mreq, sizeof(mreq)) < 0)
		err(2, "setsockopt IP_ADD_MEMBERSHIP failed");

	return s;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in, remote_in;
	struct in_addr iface;
	int s1, s2, opt, n;
	uint8_t buf[18];

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	memset(&remote_in, 0, sizeof(remote_in));
	remote_in.sin_family = AF_INET;

	memset(&iface, 0, sizeof(iface));

	while ((opt = getopt(argc, argv, "P:i:m:p:r:")) != -1) {
		switch (opt) {
		case 'P':
			remote_in.sin_port = strtol(optarg, NULL, 10);
			remote_in.sin_port = htons(remote_in.sin_port);
			break;

		case 'i':
			if (inet_pton(AF_INET, optarg, &iface) <= 0)
				usage(argv[0]);
			break;

		case 'm':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0)
				usage(argv[0]);
			break;

		case 'p':
			in.sin_port = strtol(optarg, NULL, 10);
			in.sin_port = htons(in.sin_port);
			break;

		case 'r':
			if (inet_pton(AF_INET, optarg,
			    &remote_in.sin_addr) <= 0)
				usage(argv[0]);
			break;

		default:
			usage(argv[0]);
		}
	}

	if (in.sin_addr.s_addr == INADDR_ANY || in.sin_port == 0 ||
	    iface.s_addr == INADDR_ANY ||
	    remote_in.sin_addr.s_addr == INADDR_ANY ||
	    remote_in.sin_port == 0)
		usage(argv[0]);

	s1 = create_sock(&in, &iface);
	s2 = create_sock(&in, &iface);

	if (connect(s2, (const struct sockaddr *)&remote_in,
	    sizeof(remote_in)) < 0)
		err(2, "connect failed");

	n = read(s1, buf, sizeof(buf));
	if (n < 0)
		err(2, "read 1 failed");
	fprintf(stderr, "read 1 got %d\n", n);

	n = read(s2, buf, sizeof(buf));
	if (n < 0)
		err(2, "read 2 failed");
	fprintf(stderr, "read 2 got %d\n", n);

	exit(0);
}
