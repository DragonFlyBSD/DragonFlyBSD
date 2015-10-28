#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define READ_BLOCK_TIME		5	/* unit: sec */

static void
sig_alarm(int sig __unused)
{
#define PANIC_STRING	"read blocks\n"

	write(2, PANIC_STRING, strlen(PANIC_STRING));
	abort();
}

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -p port\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	struct itimerval it;
	int s, n, error, port, opt;
	char buf;

	port = 0;
	while ((opt = getopt(argc, argv, "p:")) != -1) {
		char *endptr;

		switch (opt) {
		case 'p':
			port = strtol(optarg, &endptr, 0);
			if (*endptr != '\0')
				fprintf(stderr, "invalid -p argument\n");
			break;

		default:
			usage(argv[0]);
		}
	}
	if (port <= 0)
		usage(argv[0]);

	s = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (s < 0)
		err(1, "socket failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = htons(port);
	if (bind(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(1, "bind %d failed", port);

	memset(&it, 0, sizeof(it));
	it.it_value.tv_sec = READ_BLOCK_TIME;
	if (signal(SIGALRM, sig_alarm) == SIG_ERR)
		err(1, "signal failed");
	if (setitimer(ITIMER_REAL, &it, NULL) < 0)
		err(1, "setitimer failed");

	n = read(s, &buf, 1);
	if (n < 0) {
		error = errno;
		if (error != EAGAIN) {
			fprintf(stderr, "invalid errno %d\n", error);
			abort();
		}
	} else {
		fprintf(stderr, "read works\n");
		abort();
	}

	fprintf(stderr, "passed\n");
	exit(0);
}
