#include <common.h>

int
main(void) {
	int retval = 0;

	(void)sem_unlink(TEST_PATH);
	retval = sem_open_should_fail(TEST_PATH, O_CREAT, 0777,
	    SEM_VALUE_MAX+1U, EINVAL);

	return retval;
}
