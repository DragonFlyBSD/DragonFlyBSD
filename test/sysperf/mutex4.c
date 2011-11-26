/*
 * mutex4.c
 *
 * $DragonFly: src/test/sysperf/mutex4.c,v 1.1 2008/05/09 15:49:42 dillon Exp $
 */

#include "blib.h"

#include <sys/types.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

struct globaldata {
	int gd_cpumask;
	int gd_spinlocks;
};

int *mtx;
struct globaldata gd;

typedef struct globaldata *globaldata_t;

void
rd_lock_contested(void)
{
}

static __inline void
rd_lock(void)
{
	atomic_set_int(mtx, 1);
	*mtx = 2;
	*mtx = 3;
}

static __inline void
rd_unlock(void)
{
}

int
main(int ac, char **av)
{
    long long count = 0;
    long long max;
    int j;
    int *counter;
    pid_t pid;

    printf("Test simple locked bus cycle mutex latency\n");
    printf("auto-forks two processes for the test with shared memory\n");
    printf("This test is only useful on a SMP box\n");

    start_timing();
    mtx = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
    counter = mtx + 64;
    gd.gd_cpumask = 0x00000001;
    gd.gd_spinlocks = 0;
    while (stop_timing(0, NULL) == 0) {
	for (j = 0; j < 100; ++j) {
	    rd_lock();
	    rd_unlock();
	}
	count += 100;
    }
    max = count;
    *mtx = 0;

    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j) {
	    rd_lock();
	    rd_unlock();	/* release */
	}
    }
    stop_timing(count, "complex_mtx(uncontested/1cpu)");

    if ((pid = fork()) == 0) {
	for (;;) {
	    for (j = 0; j < 100; ++j) {
		rd_lock();
		rd_unlock();	/* release */
		++counter[128];
	    }
	}
    } else {
	gd.gd_cpumask = 0x00000002;
	gd.gd_spinlocks = 0;
	start_timing();
	for (count = 0; count < max; count += 100) {
	    for (j = 0; j < 100; ++j) {
		rd_lock();
		rd_unlock();	/* release */
		++counter[64];
	    }
	}
	stop_timing(count, "complex_mtx");
	printf("proc1=%d proc2=%d\n", counter[64], counter[128]);
	kill(pid, 9);
    }
    return(0);
}

