/*
 * UPCALL1.C
 *
 *	Test upcall performance.
 *
 * $DragonFly: src/test/sysperf/upcall1.c,v 1.1 2003/11/21 06:58:28 dillon Exp $
 */

#include <sys/types.h>
#include <sys/upcall.h>
#include <sys/time.h>
#include <stdio.h>
#include "blib.h"

#define MAXCOUNT	10000000

struct upcall	upc;			/* simple single-cpu upcall test */

extern void callused_wrapper(void *);	/* assembly */
static void myfunc(void *data);

int count = MAXCOUNT;
int id;

int
main(int ac, char **av)
{
	id = upc_register(&upc, callused_wrapper, myfunc, "blah");
	printf("register upcall %d\n", id);
	printf("try to dispatch the upcall\n");
	upc_control(UPC_CONTROL_DISPATCH, id, NULL);
	stop_timing(MAXCOUNT, "Full-up upcall test");
	printf("final: %d %d (should be 0 0)\n", upc.crit_count, upc.pending);
        return 0;
}

static void
myfunc(void *data)
{
    /*
     * Dispatch a reentrant UPC.  It should not stack because we are in a
     * critical section, but the context code will catch it when it calls
     * UPC_CONTROL_NEXT.
     */
    if (--count > 0) {
	upc_control(UPC_CONTROL_DISPATCH, id, NULL);
    }
    if (count > MAXCOUNT - 3) {
	printf("UPCALL! (%s) upc: %d crit=%d pend=%d (should be 32 1) @sp %p\n",
		data, upc.magic, upc.crit_count, upc.pending, &data);
	if (count == MAXCOUNT - 2) {
	    printf("(sp should be same as before)\n");
	    printf("doing a total of %d upcalls\n", MAXCOUNT);
	    usleep(20000);
	    start_timing();
	}
    }
}

