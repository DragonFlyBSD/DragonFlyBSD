/*
 * umtx1.c
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <pthread.h>
#include <machine/cpufunc.h>
#include <machine/atomic.h>
#include "blib.h"

static void *do_child(void *arg);
static void *do_parent(void *arg);

u_int mtx;
u_long total;

int
main(int ac, char **av)
{
	pthread_t td1;
	pthread_t td2;
	int n;
	int k;
	int status;

	printf("tests umtx hand-off loop\n");
	for (n = 1; n; --n) {
		if (fork() == 0) {
			start_timing();
			for (k = 0; k < 10; ++k) {
				pthread_create(&td1, NULL, do_child, NULL);
				pthread_create(&td2, NULL, do_parent, NULL);
				pthread_join(td2, NULL);
				pthread_join(td1, NULL);
			}
			stop_timing(total, "total");
			_exit(0);
		}
	}
	while (wait3(&status, 0, NULL) <= 0 || errno == EINTR)
		;

	return 0;
}

static
void *
do_child(void *arg __unused)
{
	for (;;) {
		while (mtx == 0)
			umtx_sleep(&mtx, 0, 0);
		if (atomic_swap_int(&mtx, 0) == 2) {
			umtx_wakeup(&mtx, 0);
			pthread_yield();
			break;
		}
		umtx_wakeup(&mtx, 0);
	}
	return NULL;
}

static
void *
do_parent(void *arg __unused)
{
	int j;
	int loops;

	for (j = 0; j < 1000000; ++j) {
		atomic_swap_int(&mtx, 1);
		umtx_wakeup(&mtx, 0);
		pthread_yield();
		while (mtx == 1)
			umtx_sleep(&mtx, 1, 0);
	}
	start_timing();
	for (j = 0; j < 1000000; ++j) {
		atomic_swap_int(&mtx, 1);
		umtx_wakeup(&mtx, 0);
		while (mtx == 1)
			umtx_sleep(&mtx, 1, 0);
	}
	stop_timing(j, "mtx1");
	atomic_add_long(&total, j * 2);

	atomic_swap_int(&mtx, 2);
	umtx_wakeup(&mtx, 0);
	while (mtx == 2)
		umtx_sleep(&mtx, 2, 0);

	return NULL;
}
