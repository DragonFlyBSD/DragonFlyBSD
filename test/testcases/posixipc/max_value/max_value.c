#include <common.h>

int
main(void) {
	sem_t id;
	int val;

	if (sem_init(&id, 0, SEM_VALUE_MAX) < 0) {
		perror("sem_init");
		return 1;
	}
	if (sem_getvalue(&id, &val) < 0) {
		perror("sem_getvalue");
		sem_destroy(&id);
		return 1;
	}
	if (val != SEM_VALUE_MAX) {
		fprintf(stderr, "value %d != SEM_VALUE_MAX", val);
		sem_destroy(&id);
		return 1;
	}
	if (val < 0) {
		fprintf(stderr, "value < 0");
		sem_destroy(&id);
		return 1;
	}
	if (sem_destroy(&id) < 0) {
		perror("sem_destroy");
		return 1;
	}
	return 0;
}
