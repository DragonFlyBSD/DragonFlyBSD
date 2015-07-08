#include <common.h>

int
main(void) {
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		perror("sem_open(O_CREAT)");
		return 1;
	}

	if (sem_close(id) < 0) {
		perror("sem_close");
		return 1;
	}

	if (sem_unlink(TEST_PATH) < 0) {
		perror("sem_unlink");
		return 1;
	}
	return 0;
}
