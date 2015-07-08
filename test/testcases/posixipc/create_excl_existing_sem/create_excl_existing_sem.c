#include <common.h>

int
main(void) {
	int retval = 0;
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		perror("sem_open(O_CREAT)");
		return 1;
	}
	sem_close(id);

	retval = sem_open_should_fail(TEST_PATH, O_CREAT | O_EXCL, 0777, 1, EEXIST);

	sem_unlink(TEST_PATH);

	return retval;
}
