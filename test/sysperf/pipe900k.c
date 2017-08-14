/*
 * Chained pipe test with 900,000 processes and 900,000 pipes
 *
 * Requires system w/ 128GB+ of ram, kern.maxproc=4000000 set in
 * /boot/loader.conf.  80 second stabilization time after last
 * process is forked.
 *
 * Also test tear-down by ^C'ing the test.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define COUNT	900000

int
main(int ac, char **av)
{
	int status;
	int i;
	int j;
	int idno;
	int fdsbeg[2];
	int fdsend[2];
	char *path;
	char *id;
	char c;

	if (ac == 1) {
		pipe(fdsbeg);
		for (i = 0; i < COUNT; i += 100) {
#if 0
			asprintf(&path, "cp %s /tmp/x%06d", av[0], i);
			system(path);
			free(path);
			asprintf(&path, "/tmp/x%06d", i);
#else
			asprintf(&path, "%s", av[0]);
#endif
			asprintf(&id, "%d", i);
			pipe(fdsend);
			if (vfork() == 0) {
				close(fdsbeg[1]);
				dup2(fdsbeg[0], 0);
				dup2(fdsend[1], 1);
				close(fdsbeg[0]);
				close(fdsend[1]);
				execl(path, path, id, NULL);
				_exit(0);
			}
			close(fdsbeg[0]);
			close(fdsend[1]);
			fdsbeg[0] = fdsend[0];
			if (i % 1000 == 0) {
				printf("running %d\r", i);
				fflush(stdout);
			}
			free(path);
			free(id);
		}
	} else {
		idno = strtol(av[1], NULL, 0);
		setpriority(PRIO_PROCESS, 0, 5);
		for (j = 0; j < 100; ++j) {
			if (j != 99)
				pipe(fdsend);
			else
				fdsend[1] = 1;
			if (j == 99 || fork() == 0) {
				if (fdsend[1] != 1) {
					dup2(fdsend[1], 1);
					close(fdsend[1]);
				}
				if (j != 99)
					close(fdsend[0]);
				setpriority(PRIO_PROCESS, 0, 15);

				while (1) {
					if (read(0, &c, 1) < 0)
						break;
#if 0
					fprintf(stderr, "x%d\n", idno + j);
					fflush(stderr);
#endif
					write(1, &c, 1);
				}
				_exit(0);
			}
			dup2(fdsend[0], 0);
			close(fdsend[1]);
			close(fdsend[0]);
		}
		while (wait3(NULL, 0, NULL) >= 0 || errno == EINTR)
			;
		_exit(0);
	}
	printf("running %d\n", i);
	for (;;) {
		write(fdsbeg[1], &c, 1);
		if (read(fdsend[0], &c, 1) < 0)
			break;
		write(2, "x", 1);
	}
	return 0;
}
