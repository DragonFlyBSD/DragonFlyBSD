#include <common.h>

int
main(void) {
	int retval = 0;

	retval = sem_open_should_fail(TEST_PATH, O_RDONLY | O_DIRECT, 0777, 1, EINVAL);

	return retval;
}
