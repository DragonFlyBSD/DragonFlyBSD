/* Testcase for issue1343 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>



void* tester(void *arg)
{
	sleep(2);
}

int main(int argc, char *argv[])
{
	int i, ret, nthreads;
	pthread_t th;
	pthread_t *threads;

	if (argc <= 1)
	{
		printf("Need one argument\n");
		exit(1);
	}

	nthreads = atoi(argv[1]);
	threads = malloc(nthreads * sizeof(pthread_t));
	if (threads == NULL)
		return 2;

	printf("Trying with %d threads\n", nthreads);

	printf("Creating tester threads\n");
	for (i = 0; i < nthreads; i++)
		pthread_create(&threads[i], NULL, tester, NULL);

	sleep(5);

	printf("Starting join procedure...\n");
	for (i = 0; i < nthreads; i++)
		pthread_join(threads[i], NULL);

	printf("Done!\n");
	return 0;
}
