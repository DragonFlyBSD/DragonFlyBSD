#include <common.h>

int
main(void) {
	int retval = 0;

	retval = sem_init_should_fail(SEM_VALUE_MAX+1U, EINVAL);

	return retval;
}
