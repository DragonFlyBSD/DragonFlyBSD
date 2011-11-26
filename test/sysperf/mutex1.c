/*
 * mutex1.c
 *
 * $DragonFly: src/test/sysperf/mutex1.c,v 1.2 2006/04/22 22:34:06 dillon Exp $
 */

#include "blib.h"

int *mtx;

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
	    try_spin_mtx();
	    rel_spin_mtx();
	}
	count += 100;
    }
    max = count;
    *mtx = 0;

    /*
     * Single cpu case
     */
    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j) {
	    while (try_spin_mtx() != 0)
		;
	    rel_spin_mtx();	/* release */
	    ++counter[64];
	}
    }
    stop_timing(count, "simple_mtx(uncontested/1cpu)");

    if ((pid = fork()) == 0) {
	for (;;) {
	    for (j = 0; j < 100; ++j) {
		while (try_spin_mtx() != 0)
		    ;
		rel_spin_mtx();	/* release */
		++counter[128];
	    }
	}
    } else {
	start_timing();
	for (count = 0; count < max; count += 100) {
	    for (j = 0; j < 100; ++j) {
		while (try_spin_mtx() != 0)
		    ;
		rel_spin_mtx();	/* release */
		++counter[64];
	    }
	}
	stop_timing(count, "simple_mtx");
	printf("proc1=%d proc2=%d\n", counter[64], counter[128]);
	kill(pid, 9);
    }
    return(0);
}

