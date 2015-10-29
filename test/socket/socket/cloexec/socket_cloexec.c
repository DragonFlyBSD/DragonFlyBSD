#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECKFD_CMD	"checkfd"
#define CHECKFD_PATH	"/usr/local/bin/" CHECKFD_CMD

int
main(void)
{
	pid_t pid;
	int s, status, ecode;

	s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0)
		err(1, "socket failed");

	pid = fork();
	if (pid < 0) {
		err(1, "fork failed");
	} else if (pid == 0) {
		char fd[8];

		snprintf(fd, sizeof(fd), "%d", s);
		if (execl(CHECKFD_PATH, CHECKFD_CMD, fd, NULL) < 0)
			err(3, "execl failed");
	}

	if (waitpid(pid, &status, 0) < 0)
		err(1, "waitpid failed");

	if (!WIFEXITED(status))
		errx(1, CHECKFD_CMD " did not exit");

	ecode = WEXITSTATUS(status);
	if (ecode != 0) {
		warnx("exit code %d", ecode);
		abort();
	}

	fprintf(stderr, "passed\n");
	exit(0);
}
