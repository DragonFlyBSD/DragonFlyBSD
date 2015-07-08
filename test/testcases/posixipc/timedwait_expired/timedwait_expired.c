#include <common.h>

int
main(void) {
	sem_t id;
	u_int elapsed;

	if (sem_init(&id, 0, 0) < 0) {
		perror("sem_init");
		return 1;
	}

	/* This should fail with a timeout and leave the value at 0. */
	if (timedwait(&id, 2500, &elapsed, ETIMEDOUT) < 0) {
		sem_destroy(&id);
		return 1;
	}
	if (!ELAPSED(elapsed, 2500)) {
		fprintf(stderr, "sem_timedwait() of locked sem took %ums "
		    "instead of 2500ms", elapsed);
		sem_destroy(&id);
		return 1;
	}
	if (checkvalue(&id, 0) < 0) {
		sem_destroy(&id);
		return 1;
	}

	if (sem_destroy(&id) < 0) {
		perror("sem_destroy");
		return 1;
	}
	return 0;
}
