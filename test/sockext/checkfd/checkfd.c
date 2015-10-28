#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int i;

	if (argc < 2)
		exit(1);

	for (i = 1; i < argc; ++i) {
		char *endptr;
		int fd;

		fd = strtol(argv[i], &endptr, 10);
		if (*endptr != '\0')
			exit(1);

		if (close(fd) < 0) {
			int error = errno;

			if (error != EBADF) {
				fprintf(stderr, "close error %d\n", error);
				exit(2);
			}
		} else {
			fprintf(stderr, "%d is still valid\n", fd);
			exit(2);
		}
	}
	exit(0);
}
