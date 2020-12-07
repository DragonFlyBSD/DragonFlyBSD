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
	fprintf(stderr, "%s -p port -d dst (-i addr [-m] | -I iface) "
	    "[-P bind_port]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in dst, local_in;
	struct in_addr iface, mcast_if;
	int s, opt, n, loop = 0, use_mreq = 0, iface_idx;
	uint8_t buf[18];
	socklen_t mcast_if_len;
	char mcast_if_str[INET_ADDRSTRLEN];

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;

	memset(&local_in, 0, sizeof(local_in));
	local_in.sin_family = AF_INET;

	memset(&iface, 0, sizeof(iface));
	iface_idx = -1;

	while ((opt = getopt(argc, argv, "I:P:d:i:mp:")) != -1) {
		switch (opt) {
		case 'I':
			iface_idx = if_nametoindex(optarg);
			break;

		case 'P':
			local_in.sin_port = strtol(optarg, NULL, 10);
			local_in.sin_port = htons(local_in.sin_port);
			break;

		case 'd':
			if (inet_pton(AF_INET, optarg, &dst.sin_addr) <= 0)
				usage(argv[0]);
			break;

		case 'i':
			if (inet_pton(AF_INET, optarg, &iface) <= 0)
				usage(argv[0]);
			break;

		case 'm':
			use_mreq = 1;
			break;

		case 'p':
			dst.sin_port = strtol(optarg, NULL, 10);
			dst.sin_port = htons(dst.sin_port);
			break;

		default:
			usage(argv[0]);
		}
	}

	if ((iface.s_addr == INADDR_ANY && iface_idx < 0) ||
	    dst.sin_addr.s_addr == INADDR_ANY || dst.sin_port == 0)
		usage(argv[0]);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	if (iface_idx >= 0) {
		struct ip_mreqn mreqn;

		fprintf(stderr, "ip_mreqn mcast_if ifindex %d\n", iface_idx);
		memset(&mreqn, 0, sizeof(mreqn));
		mreqn.imr_address = iface;
		mreqn.imr_ifindex = iface_idx;
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
		    &mreqn, sizeof(mreqn)) < 0)
			err(2, "setsockopt IP_MULTICAST_IF ip_mreqn failed");
	} else if (use_mreq) {
		struct ip_mreq mreq;

		fprintf(stderr, "ip_mreq mcast_if\n");
		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_interface = iface;
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
		    &mreq, sizeof(mreq)) < 0)
			err(2, "setsockopt IP_MULTICAST_IF ip_mreq failed");
	} else {
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
		    &iface, sizeof(iface)) < 0)
			err(2, "setsockopt IP_MULTICAST_IF inaddr failed");
	}

	mcast_if_len = sizeof(mcast_if);
	if (getsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
	    &mcast_if, &mcast_if_len) < 0)
		err(2, "getsockopt IP_MULTICAST_IF failed");
	fprintf(stderr, "ifindex %s\n", inet_ntop(AF_INET, &mcast_if,
	    mcast_if_str, sizeof(mcast_if_str)));

	if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP,
	    &loop, sizeof(loop)) < 0)
		err(2, "setsockopt IP_MULTICAST_LOOP failed");

	if (local_in.sin_port != 0) {
		local_in.sin_addr = iface;
		if (bind(s, (const struct sockaddr *)&local_in,
		    sizeof(local_in)) < 0)
			err(2, "bind failed");
	}

	n = sendto(s, buf, sizeof(buf), 0,
	    (const struct sockaddr *)&dst, sizeof(dst));
	if (n < 0)
		err(2, "sendto failed");
	else if (n < (int)sizeof(buf))
		errx(2, "sent truncated data %d", n);

	exit(0);
}
