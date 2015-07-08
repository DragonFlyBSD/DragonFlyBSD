#include <common.h>

int
main(void) {
	int retval = 0;
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		perror("sem_init");
		return 1;
	}

	retval = sem_close_should_fail(&id, EINVAL);

	sem_destroy(&id);

	return retval;
}
