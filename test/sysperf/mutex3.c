/*
 * mutex3.c
 *
 * $DragonFly: src/test/sysperf/mutex3.c,v 1.1 2006/05/18 02:22:46 dillon Exp $
 */

#include "blib.h"

#include <sys/types.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

int *mtx;
int refcnt;

static void
spin_lock_contested(void)
{
	int i;
	int j;

	j = 1;
	while (atomic_swap_int(mtx, 1) != 0) {
		for (i = 0; i < j; ++i)
			__asm __volatile("pause"::);
		j <<= 1;
	}
}

static __inline void
spin_lock(void)
{
	if (refcnt == 1) {
		if (*mtx == 0)
			*mtx = 1;
		else
			spin_lock_contested();
	} else if (atomic_swap_int(mtx, 1) != 0) {
		spin_lock_contested();
	}
}

static __inline void
spin_unlock(void)
{
	cpu_sfence();
	*mtx = 0;
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
    while (stop_timing(0, NULL) == 0) {
	for (j = 0; j < 100; ++j) {
	    spin_lock();
	    spin_unlock();
	}
	count += 100;
    }
    max = count;
    *mtx = 0;

    refcnt = 1;
    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j) {
	    spin_lock();
	    spin_unlock();	/* release */
	    ++counter[64];
	}
    }
    stop_timing(count, "complex_mtx(uncontested/1cpu)");
    refcnt = 2;

    if ((pid = fork()) == 0) {
	for (;;) {
	    for (j = 0; j < 100; ++j) {
		spin_lock();
		spin_unlock();	/* release */
		++counter[128];
	    }
	}
    } else {
	start_timing();
	for (count = 0; count < max; count += 100) {
	    for (j = 0; j < 100; ++j) {
		spin_lock();
		spin_unlock();	/* release */
		++counter[64];
	    }
	}
	stop_timing(count, "complex_mtx");
	printf("proc1=%d proc2=%d\n", counter[64], counter[128]);
	kill(pid, 9);
    }
    return(0);
}

