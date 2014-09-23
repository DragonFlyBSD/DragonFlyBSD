#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 ip4 -p port [-r]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int s, opt, n, reuseport;
	uint8_t buf[18];

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	reuseport = 0;

	while ((opt = getopt(argc, argv, "4:p:r")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0)
				usage(argv[0]);
			break;

		case 'p':
			in.sin_port = strtol(optarg, NULL, 10);
			in.sin_port = htons(in.sin_port);
			break;

		case 'r':
			reuseport = 1;
			break;

		default:
			usage(argv[0]);
		}
	}

	if (in.sin_addr.s_addr == INADDR_ANY || in.sin_port == 0)
		usage(argv[0]);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	if (reuseport) {
		if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT,
		    &reuseport, sizeof(reuseport)) < 0)
			err(2, "setsockopt SO_REUSEPORT failed");
	}

	n = sendto(s, buf, sizeof(buf), 0,
	    (const struct sockaddr *)&in, sizeof(in));
	if (n < 0)
		err(2, "sendto failed");
	else if (n != (int)sizeof(buf))
		errx(2, "sent truncated data %d", n);

	n = read(s, buf, sizeof(buf));
	if (n < 0)
		err(2, "read failed");
	printf("read %d, done\n", n);

	exit(0);
}
