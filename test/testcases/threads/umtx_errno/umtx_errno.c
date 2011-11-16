#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

pthread_barrier_t bar;


void* tester(void *arg)
{
	struct dirent en, *enp;
	DIR *d = opendir("/");
	int r;

	if (d == NULL) {
		perror("opendir() failed");
		return NULL;
	}

	pthread_barrier_wait(&bar);

	do {
		r = readdir_r(d, &en, &enp);
		if (errno != 0)
		{
			perror("thread tester error");
			exit(2);
			break;
		}
	} while (enp != NULL);

	closedir(d);
}

void* checker(void *arg)
{
	pthread_barrier_wait(&bar);
	printf("Passed barrier!\n");
}


int main(int argc, char *argv[])
{
	int i, ret, nthreads;
	pthread_t *threads;
	pthread_t th;

	if (argc <= 1)
	{
		printf("Need one argument\n");
		exit(1);
	}

	nthreads = atoi(argv[1]);
	threads = malloc(sizeof(pthread_t) * (nthreads + 1));
	if (threads == NULL) {
		fprintf(stderr, "malloc failed!\n");
		exit(3);
	}

	printf("Trying with %d threads\n", nthreads);

	if ( (ret = pthread_barrier_init(&bar, NULL, nthreads + 1)) != 0)
	{
		printf("error occured during pthread_barrier_init, ret = %d\n", ret);
		perror("Or: ");
		exit(1);
	}

	printf("Creating checker thread\n");
	pthread_create(&th, NULL, checker, NULL);

	printf("Creating tester threads\n");


	for (i = 0; i < nthreads; i++) {
		ret = pthread_create(&threads[i], NULL, tester, NULL);
		if (ret != 0)
			threads[i] = 0;
	}

	//sleep(5);

	printf("Starting join\n");
	for (i = 0; i < nthreads; i++) {
		if (threads[i] != 0)
			pthread_join(threads[i], NULL);
	}

	printf("All done!\n");
	return 0;
}
