#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

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

int
main(void)
{
	struct itimerval it;
	int s[2], n, error;
	char buf;

	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, s) < 0)
		err(1, "socketpair failed");

	memset(&it, 0, sizeof(it));
	it.it_value.tv_sec = READ_BLOCK_TIME;
	if (signal(SIGALRM, sig_alarm) == SIG_ERR)
		err(1, "signal failed");
	if (setitimer(ITIMER_REAL, &it, NULL) < 0)
		err(1, "setitimer failed");

	n = read(s[0], &buf, 1);
	if (n < 0) {
		error = errno;
		if (error != EAGAIN) {
			warnx("invalid errno %d", error);
			abort();
		}
	} else {
		warnx("read0 works");
		abort();
	}

	n = read(s[1], &buf, 1);
	if (n < 0) {
		error = errno;
		if (error != EAGAIN) {
			warnx("invalid errno %d", error);
			abort();
		}
	} else {
		warnx("read1 works");
		abort();
	}

	fprintf(stderr, "passed\n");
	exit(0);
}
