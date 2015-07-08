#include <common.h>

int
main(void) {
	sem_t *id;
	u_int elapsed;
	pid_t pid;

	id = construct_shared_unnamed_sem(0);

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		perror("fork");
		destruct_shared_unnamed_sem(id);
		return 1;
	case 0:
		/* Child. */
		sleep(1);
		sem_post(id);
		exit(0);
	}

	if (testwait(id, &elapsed) < 0) {
		destruct_shared_unnamed_sem(id);
		return 1;
	}
	if (!ELAPSED(elapsed, 1000)) {
		fprintf(stderr, "sem_wait() with delayed post took %ums "
		    "instead of 1000ms", elapsed);
		destruct_shared_unnamed_sem(id);
		return 1;
	}

	destruct_shared_unnamed_sem(id);

	return 0;
}
