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
	fprintf(stderr, "%s -p port -d dst -i addr [-P bind_port]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in dst, local_in;
	struct in_addr iface;
	int s, opt, n, loop = 0;
	uint8_t buf[18];

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;

	memset(&local_in, 0, sizeof(local_in));
	local_in.sin_family = AF_INET;

	memset(&iface, 0, sizeof(iface));

	while ((opt = getopt(argc, argv, "P:d:i:p:")) != -1) {
		switch (opt) {
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

		case 'p':
			dst.sin_port = strtol(optarg, NULL, 10);
			dst.sin_port = htons(dst.sin_port);
			break;

		default:
			usage(argv[0]);
		}
	}

	if (iface.s_addr == INADDR_ANY ||
	    dst.sin_addr.s_addr == INADDR_ANY || dst.sin_port == 0)
		usage(argv[0]);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
	    &iface, sizeof(iface)) < 0)
		err(2, "setsockopt IP_MULTICAST_IF failed");

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
