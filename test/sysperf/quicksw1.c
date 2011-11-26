/*
 * quicksw1.c
 *
 * $DragonFly: src/test/sysperf/quicksw1.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

void qswitch(int **oldsw, int *newsw);
void qstart(int *newsw);
int *qinit(void *topstk, void *func, void *arg);

static void do_task1(void *arg);
static void do_task2(void *arg);

int *task1;
int *task2;
char stk1[16384];
char stk2[16384];
int count;

int
main(int ac, char **av)
{
#if USE_ALL
    printf("userthread switching test (nonfp) using pushal/popal\n");
#elif USE_CALLU1
    printf("userthread switching test (nonfp) using pushl (call-save only)\n");
#elif USE_CALLU2
    printf("userthread switching test (nonfp) using subl $N,%%esp;movl (call-save only)\n");
#else
#error "The switch method wasn't defined with -D"
#endif

    task1 = qinit(stk1 + sizeof(stk1), do_task1, NULL);
    task2 = qinit(stk2 + sizeof(stk2), do_task2, NULL);
    start_timing();
    qstart(task1);
}

static void
do_task1(void *arg)
{
    for (;;) {
	qswitch(&task1, task2);
	if (++count > 10000000) {
		stop_timing(count, "uthread_switch");
		exit(0);
	}
    }
}

static void
do_task2(void *arg)
{
    for (;;) {
	++count;
	qswitch(&task2, task1);
    }
}

