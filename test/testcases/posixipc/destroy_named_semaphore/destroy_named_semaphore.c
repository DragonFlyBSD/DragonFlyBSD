#include <common.h>

int
main(void) {
	int retval = 0;
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		return 1;
	}

	retval = sem_destroy_should_fail(id, EINVAL);

	sem_close(id);
	sem_unlink(TEST_PATH);

	return retval;
}
