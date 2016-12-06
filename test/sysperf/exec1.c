/*
 * exec1.c
 *
 * $DragonFly: src/test/sysperf/exec1.c,v 1.2 2004/04/14 17:59:45 dillon Exp $
 */

#include "blib.h"
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <machine/atomic.h>

char *Av0;

static
void
execltest(void)
{
    pid_t pid;
    char *elm;

    if ((elm = strrchr(Av0, '/')) == NULL)
	elm = Av0;
    else
	++elm;

    if ((pid = vfork()) == 0) {
	execl(Av0, elm, "dummy", NULL);
	_exit(1);
    } else if (pid < 0) {
	perror("vfork");
	exit(1);
    } else {
	int status;

	while (waitpid(pid, &status, 0) != pid)
	    ;
	if (WEXITSTATUS(status)) {
	    fprintf(stderr, "execl in child failed\n");
	    exit(1);
	}
    }
}

int
main(int ac, char **av)
{
    int i;
    int count;
    int status;
    int ncpus;
    int n;
    long *countr;

    countr = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);

    Av0 = av[0];
    if (ac == 2 && strcmp(av[1], "dummy") == 0)
	exit(0);
    ncpus = 1;
    if (ac > 1)
	ncpus = strtol(av[1], NULL, 0);

    count = 0;
    start_timing();
    while (stop_timing(0, NULL) == 0) {
	for (i = 0; i < 100; ++i)
	    execltest();
	count += 100;
    }
    count *= 5;		/* 5 second run */
    start_timing();
    for (n = 0; n < ncpus; ++n) {
	if (fork() == 0) {
	    count = 0;
	    while (get_timing() < 5000000) {
		execltest();
		++count;
		stop_timing(0, NULL);
	    }
	    atomic_add_long(countr, count);
	    _exit(0);
	}
    }
    while (wait3(&status, 0, NULL) >= 0 || errno == EINTR)
	;
#ifdef ISSTATIC
    stop_timing(*countr, "execl static program:");
#else
    stop_timing(*countr, "execl dynamic program:");
#endif
    return(0);
}

