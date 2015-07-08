#include <common.h>

int
main(void) {
	struct stat sb;
	sem_t *id;
	int error;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 0);
	if (id == SEM_FAILED) {
		perror("sem_open");
		return 1;
	}

	error = stat("/var/run/sem", &sb);
	if (error) {
		perror("stat");
		return 1;
	}
	if ((sb.st_mode & ALLPERMS) != (S_IRWXU|S_IRWXG|S_IRWXO|S_ISTXT)) {
		fprintf(stderr, "semaphore dir has incorrect mode: 0%o\n",
			 (sb.st_mode & ALLPERMS));
		return 1;
	}

	sem_close(id);
	return 0;
}
