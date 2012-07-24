/*
 * poll_1.c
 *
 * This code used to panic the kernel because of a filter bug.
 */
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>

int main()
{
	struct pollfd fds[1];

	int p = open("/dev/tty", O_RDWR);
	
	printf("tty: %d\n", p);

	fds[0].fd = p;
	fds[0].events = POLLIN|POLLPRI;
	fds[0].revents = 0;

	poll(fds, 1, -1);

	printf("polled\n");
}
