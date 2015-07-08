#include <common.h>

int
main(void) {
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		return 1;
	}

	if (sem_destroy(&id) < 0) {
		perror("sem_destroy");
		return 1;
	}
	return 0;
}
