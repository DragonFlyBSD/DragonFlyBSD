#include <common.h>

int
main(void) {
	sem_t *id;

	/*
	 * Create named semaphore with value of 1 and then unlink it
	 * while still retaining the initial reference.
	 */
	id = sem_open(TEST_PATH, O_CREAT | O_EXCL, 0777, 1);
	if (id == SEM_FAILED) {
		perror("sem_open(O_CREAT | O_EXCL)");
		return 1;
	}
	if (sem_unlink(TEST_PATH) < 0) {
		perror("sem_unlink");
		sem_close(id);
		return 1;
	}
	if (checkvalue(id, 1) < 0) {
		sem_close(id);
		return 1;
	}

	/* Post the semaphore to set its value to 2. */
	if (sem_post(id) < 0) {
		perror("sem_post");
		sem_close(id);
		return 1;
	}
	if (checkvalue(id, 2) < 0) {
		sem_close(id);
		return 1;
	}

	/* Wait on the semaphore which should set its value to 1. */
	if (sem_wait(id) < 0) {
		perror("sem_wait");
		sem_close(id);
		return 1;
	}
	if (checkvalue(id, 1) < 0) {
		sem_close(id);
		return 1;
	}

	if (sem_close(id) < 0) {
		perror("sem_close");
		return 1;
	}
	return 0;
}
