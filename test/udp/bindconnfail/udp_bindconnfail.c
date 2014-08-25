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
	fprintf(stderr, "%s -4 ip4 -p port [-b ip4] -P bind_port\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in, local_in;
	int s1, s2, opt, n, on = 1;
	uint8_t buf[18];

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	memset(&local_in, 0, sizeof(local_in));
	local_in.sin_family = AF_INET;

	while ((opt = getopt(argc, argv, "4:P:b:p:")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0)
				usage(argv[0]);
			break;

		case 'P':
			local_in.sin_port = strtol(optarg, NULL, 10);
			local_in.sin_port = htons(local_in.sin_port);
			break;

		case 'b':
			if (inet_pton(AF_INET, optarg, &local_in.sin_addr) <= 0)
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
	    local_in.sin_port == 0)
		usage(argv[0]);

	s1 = socket(AF_INET, SOCK_DGRAM, 0);
	if (s1 < 0)
		err(2, "socket 1 failed");

	if (setsockopt(s1, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0)
		err(2, "setsockopt 1 SO_REUSEPORT failed");

	if (bind(s1, (const struct sockaddr *)&local_in, sizeof(local_in)) < 0)
		err(2, "bind 1 failed");

	if (connect(s1, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(2, "connect 1 failed");

	s2 = socket(AF_INET, SOCK_DGRAM, 0);
	if (s2 < 0)
		err(2, "socket 2 failed");

	if (setsockopt(s2, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0)
		err(2, "setsockopt 2 SO_REUSEPORT failed");

	if (bind(s2, (const struct sockaddr *)&local_in, sizeof(local_in)) < 0)
		err(2, "bind 2 failed");

	if (connect(s2, (const struct sockaddr *)&in, sizeof(in)) == 0)
		errx(2, "connect 2 succeeded");

	close(s1);

	n = sendto(s2, buf, sizeof(buf), 0,
	    (const struct sockaddr *)&in, sizeof(in));
	if (n < 0)
		err(2, "sendto failed");
	else if (n != (int)sizeof(buf))
		errx(2, "sent truncated data %d", n);

	n = read(s2, buf, sizeof(buf));
	if (n < 0)
		err(2, "read failed");
	printf("read %d, done\n", n);

	exit(0);
}
