#include <err.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *
setselfname(void *arg __unused)
{
	pthread_set_name_np(pthread_self(), __func__);
	pause();
	return NULL;
}

static void *
waitname(void *arg __unused)
{
	pause();
	return NULL;
}

static void *
resetname(void *arg __unused)
{
	pthread_set_name_np(pthread_self(), __func__);
	sleep(10);
	pthread_set_name_np(pthread_self(), NULL);
	pause();
	return NULL;
}

int
main(void)
{
	pthread_t tid1, tid2, tid3;
	char longname[256];
	int error;

	error = pthread_create(&tid1, NULL, setselfname, NULL);
	if (error)
		errc(1, error, "pthread_create(setselfname) failed");

	error = pthread_create(&tid2, NULL, waitname, NULL);
	if (error)
		errc(1, error, "pthread_create(waitname) failed");
	pthread_set_name_np(tid2, "waitname");

	error = pthread_create(&tid3, NULL, resetname, NULL);
	if (error)
		errc(1, error, "pthread_create(resetname) failed");

	memset(longname, 'x', sizeof(longname));
	longname[sizeof(longname) - 1] = '\0';
	pthread_set_name_np(pthread_self(), longname);

	pause();
	exit(0);
}
