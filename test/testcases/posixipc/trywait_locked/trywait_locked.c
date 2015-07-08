#include <common.h>

int
main(void) {
	sem_t id;

	if (sem_init(&id, 0, 0) < 0) {
		perror("sem_init");
		return 1;
	}

	/* This should fail with EAGAIN and leave the value at 0. */
	if (sem_trywait(&id) >= 0) {
		sem_destroy(&id);
		return 1;
	}
	if (errno != EAGAIN) {
		perror("wrong error from sem_trywait()");
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
