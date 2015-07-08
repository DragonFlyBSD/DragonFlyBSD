#include <common.h>

int
main(void) {
	sem_t id;
	u_int elapsed;

	if (sem_init(&id, 0, 1) < 0) {
		perror("sem_init");
		return 1;
	}

	/* This should succeed right away and set the value to 0. */
	if (timedwait(&id, 5000, &elapsed, 0) < 0) {
		sem_destroy(&id);
		return 1;
	}
	if (!ELAPSED(elapsed, 0)) {
		fprintf(stderr, "sem_timedwait() of unlocked sem took %ums",
		    elapsed);
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
