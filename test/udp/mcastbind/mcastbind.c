#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/if.h>
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
	fprintf(stderr, "%s -m addr -p port (-i addr | -I iface) [-a]\n", cmd);
	exit(1);
}

static int
create_sock(const struct sockaddr_in *in0, const struct in_addr *iface,
    int iface_idx, int bind_any)
{
	struct sockaddr_in in;
	int s, on;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	on = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0)
		err(2, "setsockopt SO_REUSEPORT failed");

	in = *in0;
	if (bind_any)
		in.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(2, "bind failed");

	if (iface_idx < 0) {
		struct ip_mreq mreq;

		fprintf(stderr, "ip_mreq add_member\n");
		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr = in0->sin_addr;
		mreq.imr_interface = *iface;
		if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		    &mreq, sizeof(mreq)) < 0)
			err(2, "setsockopt IP_ADD_MEMBERSHIP ip_mreq failed");
	} else {
		struct ip_mreqn mreqn;

		fprintf(stderr, "ip_mreqn add_member, ifindex %d\n", iface_idx);
		memset(&mreqn, 0, sizeof(mreqn));
		mreqn.imr_multiaddr = in0->sin_addr;
		mreqn.imr_address = *iface;
		mreqn.imr_ifindex = iface_idx;
		if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		    &mreqn, sizeof(mreqn)) < 0)
			err(2, "setsockopt IP_ADD_MEMBERSHIP ip_mreqn failed");
	}

	return s;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	struct in_addr iface;
	int s1, s2, opt, n, bind_any, iface_idx;
	uint8_t buf[18];

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	memset(&iface, 0, sizeof(iface));
	bind_any = 0;
	iface_idx = -1;

	while ((opt = getopt(argc, argv, "I:ai:m:p:")) != -1) {
		switch (opt) {
		case 'I':
			iface_idx = if_nametoindex(optarg);
			break;

		case 'a':
			bind_any = 1;
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

		default:
			usage(argv[0]);
		}
	}

	if (in.sin_addr.s_addr == INADDR_ANY || in.sin_port == 0 ||
	    (iface.s_addr == INADDR_ANY && iface_idx < 0))
		usage(argv[0]);

	s1 = create_sock(&in, &iface, iface_idx, bind_any);
	s2 = create_sock(&in, &iface, iface_idx, bind_any);

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
