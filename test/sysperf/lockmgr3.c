/*
 * varsym..c
 *
 * varsym [threads]
 *
 * tests shared lock using varsym_get()
 */

#include "blib.h"
#include <sys/file.h>
#include <assert.h>

int
main(int ac, char **av)
{
	long long count = 0;
	long long max;
	char c;
	int n;
	int i;
	int j;
	int fd;
	int status;
	char *path;
	char buf[256];
	struct stat st;

	printf("timing standard fstat() syscall\n");

	fd = open("/tmp/lockmgr3.test", O_RDWR|O_CREAT, 0666);
	assert(fd >= 0);
	start_timing();
	while (stop_timing(0, NULL) == 0) {
		fstat(fd, &st);
		fstat(fd, &st);
		fstat(fd, &st);
		fstat(fd, &st);
		++count;
	}
	max = count * 4;
	close(fd);

	if (ac > 1)
		n = strtol(av[1], NULL, 0);
	else
		n = 1;

	start_timing();
	for (i = 0; i < n; ++i) {
		if (fork() == 0) {
			asprintf(&path, "/tmp/lockmgr.test");
			fd = open(path, O_RDWR|O_CREAT, 0666);
			assert(fd >= 0);
			for (count = 0; count < max; ++count) {
				fstat(fd, &st);
				fstat(fd, &st);
				fstat(fd, &st);
				fstat(fd, &st);
			}
			_exit(0);
		}
	}
	while (wait3(&status, 0, NULL) >= 0 || errno == EINTR)
		;
	stop_timing(max * n * 4, "lockmgr3");

	return(0);
}
