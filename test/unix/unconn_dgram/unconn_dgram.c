#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNCONN_DGRAM_PATH	"/tmp/unconn_dgram.sock"

static void
test_send_unconn_dgram(void)
{
	struct sockaddr_un un;
	int s, n;

	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "send socket failed");

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_LOCAL;
	strlcpy(un.sun_path, UNCONN_DGRAM_PATH, sizeof(un.sun_path));

	n = sendto(s, UNCONN_DGRAM_PATH, sizeof(UNCONN_DGRAM_PATH), 0,
	    (const struct sockaddr *)&un, sizeof(un));
	if (n < 0)
		err(1, "sendto failed");
	else if (n != sizeof(UNCONN_DGRAM_PATH))
		err(1, "sendto size mismatch");
}

int
main(void)
{
	struct sockaddr_un un;
	char buf[64];
	pid_t pid;
	int s, n, status;

	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket failed");

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_LOCAL;
	strlcpy(un.sun_path, UNCONN_DGRAM_PATH, sizeof(un.sun_path));
	unlink(un.sun_path);

	if (bind(s, (const struct sockaddr *)&un, sizeof(un)) < 0)
		err(1, "bind failed");

	pid = fork();
	if (pid < 0) {
		err(1, "fork failed");
	} else if (pid == 0) {
		close(s);
		test_send_unconn_dgram();
		exit(0);
	}
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status))
		err(1, "child did not exit");
	if (WEXITSTATUS(status) != 0)
		err(WEXITSTATUS(status), "child failed");

	n = read(s, buf, sizeof(buf));
	if (n < 0) {
		err(1, "read failed");
	} else if (n != sizeof(UNCONN_DGRAM_PATH)) {
		warnx("dgram size mismatch");
		abort();
	}
	fprintf(stderr, "%s\n", buf);

	exit(0);
}
