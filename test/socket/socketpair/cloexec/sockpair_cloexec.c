#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

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
	int s[2], status, ecode;

	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, s) < 0)
		err(1, "socketpair failed");

	pid = fork();
	if (pid < 0) {
		err(1, "fork failed");
	} else if (pid == 0) {
		char fd1[8], fd2[8];

		snprintf(fd1, sizeof(fd1), "%d", s[0]);
		snprintf(fd2, sizeof(fd2), "%d", s[1]);
		if (execl(CHECKFD_PATH, CHECKFD_CMD, fd1, fd2, NULL) < 0)
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
