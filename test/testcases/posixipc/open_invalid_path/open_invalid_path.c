#include <common.h>

int
main(void) {
	int retval = 0;

	retval = sem_open_should_fail("blah", 0, 0777, 1, ENOENT);

	return retval;
}
