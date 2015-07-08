#include <common.h>

int
main(void) {
	int retval = 0;
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		perror("sem_open");
		return 1;
	}
	sem_close(id);

	if (sem_unlink(TEST_PATH) < 0) {
		perror("sem_unlink");
		return 1;
	}

	retval = sem_open_should_fail(TEST_PATH, O_RDONLY, 0777, 1, ENOENT);

	return retval;
}
