#include <common.h>

int
main(void) {
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		perror("sem_init");
		return 1;
	}
	if (checkvalue(&id, 1) < 0) {
		sem_destroy(&id);
		return 1;
	}
	if (sem_post(&id) < 0) {
		perror("sem_post");
		sem_destroy(&id);
		return 1;
	}
	if (checkvalue(&id, 2) < 0) {
		sem_destroy(&id);
		return 1;
	}
	if (sem_destroy(&id) < 0) {
		perror("sem_destroy");
		return 1;
	}
	return 0;
}
