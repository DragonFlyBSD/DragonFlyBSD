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
	int s, opt, n;
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

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	if (bind(s, (const struct sockaddr *)&local_in, sizeof(local_in)) < 0)
		err(2, "bind failed");

	if (connect(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(2, "connect failed");

	n = write(s, buf, sizeof(buf));
	if (n < 0)
		err(2, "write failed");
	else if (n != (int)sizeof(buf))
		errx(2, "written truncated data %d", n);

	n = read(s, buf, sizeof(buf));
	if (n < 0)
		err(2, "read failed");
	printf("read %d, done\n", n);

	exit(0);
}
