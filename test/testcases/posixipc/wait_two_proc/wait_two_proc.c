#include <common.h>

int
main(void) {
	sem_t *id;
	int retval = 0;
	int stat;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 0);
	if (id == SEM_FAILED) {
		perror("sem_open");
		return 1;
	}

	if (schedule_post(id, 500) < 0) {
		sem_close(id);
		sem_unlink(TEST_PATH);
		return 1;
	}

	if (child_worker(wait_twoproc_child, NULL, &stat) < 0) {
		check_alarm(1);
		sem_close(id);
		sem_unlink(TEST_PATH);
		return 1;
	}

	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		/* Pass! */
		retval = 0;
		break;
	case 1:
		perror("child sem_open()");
		retval = 1;
		break;
	case 2:
		perror("child sem_wait()");
		retval = 1;
		break;
	case 3:
		perror("child sem_close()");
		retval = 1;
		break;
	default:
		fprintf(stderr, "bad child state %#x", stat);
		retval = 1;
		break;
	}

	check_alarm(1);
	sem_close(id);
	sem_unlink(TEST_PATH);

	return retval;
}
