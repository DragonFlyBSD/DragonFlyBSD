/*
 * exec1.c
 *
 * $DragonFly: src/test/sysperf/exec1.c,v 1.1 2004/04/10 21:36:52 dillon Exp $
 */

#include "blib.h"
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/time.h>

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
	setpriority(PRIO_PROCESS, getpid(), -20);
	execl(Av0, elm, "dummy", NULL);
	_exit(1);
    } else if (pid < 0) {
	perror("vfork");
	exit(1);
    } else {
	int status;

	while(waitpid(pid, &status, 0) != pid)
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

    Av0 = av[0];
    if (ac == 2)
	exit(0);

    count = 0;
    start_timing();
    while (stop_timing(0, NULL) == 0) {
	for (i = 0; i < 100; ++i)
	    execltest();
	count += 100;
    }
    start_timing();
    for (i = 0; i < count; ++i)
	execltest();
#ifdef ISSTATIC
    stop_timing(count, "execl static program:", count);
#else
    stop_timing(count, "execl dynamic program:", count);
#endif
    return(0);
}

