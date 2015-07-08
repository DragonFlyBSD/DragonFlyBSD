#include <common.h>

int
main(void) {
	sem_t *id;
	u_int elapsed;
	pid_t pid;

	id = construct_shared_unnamed_sem(0);
	if (id == SEM_FAILED) {
		fprintf(stderr, "construct sem\n");
		return 1;
	}

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

	if (timedwait(id, 2000, &elapsed, 0) < 0) {
		destruct_shared_unnamed_sem(id);
		return 1;
	}
	if (!ELAPSED(elapsed, 1000)) {
		fprintf(stderr, "sem_timedwait() with delayed post took %ums "
		    "instead of 1000ms", elapsed);
		destruct_shared_unnamed_sem(id);
		return 1;
	}

	destruct_shared_unnamed_sem(id);

	return 0;
}
