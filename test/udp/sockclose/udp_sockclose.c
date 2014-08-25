#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -n count\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, opt, count;

	count = 0;
	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
		case 'n':
			count = strtol(optarg, NULL, 10);
			break;

		default:
			usage(argv[0]);
		}
	}
	if (count == 0)
		usage(argv[0]);

	for (i = 0; i < count; ++i) {
		int s;

		s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
			err(2, "socket failed");
		close(s);
	}
	printf("done\n");
	exit(0);
}
