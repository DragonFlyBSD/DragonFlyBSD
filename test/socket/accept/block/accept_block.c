#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WRITE_WAIT_TIME	8	/* unit: sec */
#define READ_WAIT_MIN	6	/* unit: sec */

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -p port\n", cmd);
	exit(1);
}

static void
connect_client(int port)
{
	struct sockaddr_in in;
	int s, dummy;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		err(1, "client socket failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	in.sin_port = htons(port);
	if (connect(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(1, "connect failed");

	sleep(WRITE_WAIT_TIME);
	write(s, &dummy, sizeof(dummy));

	pause();
	exit(0);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int serv_s, s;
	int opt, port, dummy, n, status, panic, ex;
	struct timespec stime, etime;
	pid_t pid;

	ex = 1;
	panic = 0;

	port = 0;
	while ((opt = getopt(argc, argv, "p:")) != -1) {
		char *endptr;

		switch (opt) {
		case 'p':
			port = strtol(optarg, &endptr, 0);
			if (*endptr != '\0')
				errx(1, "invalid -p");
			break;

		default:
			usage(argv[0]);
		}
	}
	if (port <= 0)
		usage(argv[0]);

	serv_s = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_s < 0)
		err(1, "socket failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = htons(port);
	if (bind(serv_s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(1, "bind failed");

	if (listen(serv_s, 0) < 0)
		err(1, "listen failed");

	pid = fork();
	if (pid < 0) {
		err(1, "fork failed");
	} else if (pid == 0) {
		connect_client(port);
		abort();
	}

	s = accept(serv_s, NULL, NULL);
	if (s < 0) {
		warn("accept failed");
		goto done;
	}
	panic = 1;

	clock_gettime(CLOCK_MONOTONIC, &stime);
	n = read(s, &dummy, sizeof(dummy));
	if (n < 0) {
		warn("read failed");
		goto done;
	} else if (n != sizeof(dummy)) {
		warnx("read size mismatch");
		goto done;
	}
	clock_gettime(CLOCK_MONOTONIC, &etime);

	if (etime.tv_sec - stime.tv_sec < READ_WAIT_MIN) {
		warnx("invalid wait time %ld", etime.tv_sec - stime.tv_sec);
		goto done;
	}

	ex = 0;
	panic = 0;
	fprintf(stderr, "passed\n");
done:
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	if (panic)
		abort();
	exit(ex);
}
