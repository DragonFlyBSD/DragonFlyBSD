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

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -p port\n", cmd);
	exit(1);
}

static void
connect_client(int port, int ctrl_s)
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

	/* connect is done */
	write(ctrl_s, &dummy, sizeof(dummy));

	pause();
	exit(0);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int serv_s, s, ctrl_s[2];
	int opt, port, dummy, n, status, panic, ex;
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

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, ctrl_s) < 0)
		err(1, "socketpair failed");

	serv_s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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
		close(ctrl_s[0]);
		connect_client(port, ctrl_s[1]);
		abort();
	}

	/* Wait for the connect */
	read(ctrl_s[0], &dummy, sizeof(dummy));

	s = accept(serv_s, NULL, NULL);
	if (s < 0) {
		warn("accept failed");
		goto done;
	}
	panic = 1;

	n = read(s, &dummy, sizeof(dummy));
	if (n < 0) {
		int error = errno;

		if (error != EAGAIN) {
			warnx("invaid errno %d\n", error);
			goto done;
		}
	} else {
		warnx("read works");
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
