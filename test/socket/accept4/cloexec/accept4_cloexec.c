#include <sys/types.h>
#include <sys/socket.h>
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

#define CHECKFD_CMD	"checkfd" 
#define CHECKFD_PATH	"/usr/local/bin/" CHECKFD_CMD

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
	int s;

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	in.sin_port = htons(port);

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		err(1, "client socket failed");
	if (connect(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(1, "connect failed");

	pause();
	exit(0);
}

int
main(int argc, char *argv[])
{
	pid_t pid;
	struct sockaddr_in in;
	int serv_s, s;
	int opt, port, status, ecode, error;

	port = 0;
	while ((opt = getopt(argc, argv, "p:")) != -1) {
		char *endptr;

		switch (opt) {
		case 'p':
			port = strtol(optarg, &endptr, 0);
			if (*endptr != '\0')
				errx(1, "invalid -p %s", optarg);
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
		/* NEVER REACHED */
	}

	s = accept4(serv_s, NULL, NULL, SOCK_CLOEXEC);
	if (s < 0)
		error = errno;
	else
		error = 0;

	/* Kill connect_client */
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);

	if (error)
		errc(1, error, "accept4 failed");

	pid = fork();
	if (pid < 0) {
		err(1, "fork failed");
	} else if (pid == 0) {
		char fd[8];

		snprintf(fd, sizeof(fd), "%d", s);
		execl(CHECKFD_PATH, CHECKFD_CMD, fd, NULL);
	}

	if (waitpid(pid, &status, 0) < 0)
		err(1, "waitpid failed");
	if (!WIFEXITED(status))
		errx(1, "not exited");
	ecode = WEXITSTATUS(status);
	if (ecode != 0) {
		warnx("exit code %d", ecode);
		abort();
	}

	fprintf(stderr, "passed\n");
	exit(0);
}
