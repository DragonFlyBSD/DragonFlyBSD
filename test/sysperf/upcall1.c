/*
 * UPCALL1.C
 *
 *	Test upcall performance.  WARNING!  This test does not reflect 
 *	reality... the test is effectively making two system calls for
 *	each upcall when under normal conditions no system calls should be
 *	necessary when handling an upcall.
 *
 * $DragonFly: src/test/sysperf/upcall1.c,v 1.3 2004/01/12 16:48:37 drhodus Exp $
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
	printf("Warning: extra system calls in test means performance\n");
	printf("does not reflect reality.  Divide times by 3 for raw\n");
	printf("per-upcall overhead (approximately)\n");
	printf("register upcall %d\n", id);
	printf("try to dispatch the upcall\n");
	upc_control(UPC_CONTROL_DISPATCH, id, NULL);
	stop_timing(MAXCOUNT, "Full-up upcall test");
	printf("final: %d %d (should be 0 0)\n", upc.upc_critoff, upc.upc_pending);
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
		data, upc.upc_magic, upc.upc_critoff, upc.upc_pending, &data);
	if (count == MAXCOUNT - 2) {
	    printf("(sp should be same as before)\n");
	    printf("doing a total of %d upcalls\n", MAXCOUNT);
	    usleep(20000);
	    start_timing();
	}
    }
}

