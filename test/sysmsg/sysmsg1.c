/*
 * sysmsg1.c
 *
 * $DragonFly: src/test/sysmsg/sysmsg1.c,v 1.1 2003/08/12 02:29:41 dillon Exp $
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/msgport.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/sysproto.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "sendsys.h"

int
main(int ac, char **av)
{
    struct write_args writemsg;
    struct nanosleep_args sleepmsg;
    struct timespec ts1;
    struct timespec ts2;
    int error;

    printf("synchronous sendsys() test: write 'hello!' and 1.5 second nanosleep\n");
    bzero(&writemsg, sizeof(writemsg));
    writemsg.usrmsg.umsg.ms_cmd = SYS_write;
    writemsg.usrmsg.umsg.ms_flags = 0;
    writemsg.fd = 1;
    writemsg.buf = "hello!\n";
    writemsg.nbyte = 7;
    error = sendsys(NULL, &writemsg.usrmsg, sizeof(writemsg));
    printf("error code %d\n", error);
    if (error != 0)
	exit(1);

    bzero(&sleepmsg, sizeof(sleepmsg));
    sleepmsg.usrmsg.umsg.ms_cmd = SYS_nanosleep;
    sleepmsg.usrmsg.umsg.ms_flags = 0;	/* NOTE: not async */
    sleepmsg.rqtp = &ts1;
    sleepmsg.rmtp = &ts2;
    ts1.tv_sec = 1;
    ts1.tv_nsec = 500 * 1000000;
    error = sendsys(NULL, &sleepmsg.usrmsg, sizeof(sleepmsg));
    printf("error code %d\n", error);
    if (error == EASYNC) {
	struct nanosleep_args *rmsg;
	printf("async return, waiting...");
	fflush(stdout);
	for (;;) {
	    rmsg = (void *)sendsys(NULL, NULL, -1);
	    printf("    rmsg %p\n", rmsg);
	    if (rmsg == &sleepmsg)
		break;
	    usleep(1000000 / 10);
	}
	printf("async return error %d\n", sleepmsg.usrmsg.umsg.ms_error);
    } else if (error) {
	printf("error %d\n", error);
    } 
    exit(0);
}

