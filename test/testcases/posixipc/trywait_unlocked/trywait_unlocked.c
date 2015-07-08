#include <common.h>

int
main(void) {
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		perror("sem_init");
		return 1;
	}

	/* This should succeed and decrement the value to 0. */
	if (sem_trywait(&id) < 0) {
		perror("sem_trywait()");
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
