/*
 * sysmsg2.c
 *
 * $DragonFly: src/test/sysmsg/sysmsg2.c,v 1.3 2004/09/02 11:21:12 eirikn Exp $
 */

#include <time.h>

#include "syscall.h"

__attribute__((weak))

int
write_test(int fd, void * buf, size_t nbyte)
{
	struct write_args writemsg;
	int error;

	INITMSGSYSCALL(write, MSGF_ASYNC);
	writemsg.fd = fd;
	writemsg.buf = buf;
	writemsg.nbyte = nbyte;
	DOMSGSYSCALL(write);
	printf("write: error: %d\n", error);
	printf("write: ms_error: %d\n", writemsg.usrmsg.umsg.ms_error);
	printf("write: ms_result: %d\n", writemsg.usrmsg.umsg.u.ms_result);
	FINISHMSGSYSCALL(write, error);
}

int
nanosleep_test(const struct timespec * rqtp, struct timespec * rmtp)
{
	struct nanosleep_args nanosleepmsg;
	int error;

	INITMSGSYSCALL(nanosleep, MSGF_ASYNC);
	nanosleepmsg.rqtp = rqtp;
	nanosleepmsg.rmtp = rmtp;
	DOMSGSYSCALL(nanosleep);
	printf("nanosleep: error: %d\n", error);
	printf("nanosleep: ms_error: %d\n", nanosleepmsg.usrmsg.umsg.ms_error);
	printf("nanosleep: ms_result: %d\n", nanosleepmsg.usrmsg.umsg.u.ms_result);
	FINISHMSGSYSCALL(nanosleep, error);
}

int
main(void)
{
	struct timespec ts, ts2;
	int error;

	printf("test synchronous syscall msg write 'hello!', with async hint\n");
	printf("test ASynchronous syscall msg nanosleep for 1.5 seconds\n");
	printf("WARNING! super-user only, do not ^C the test program!\n");
	error = write_test(1, "hello!\n", 7);
	if (error == -1)
		err(1, "write");
	ts.tv_sec = 1;
	ts.tv_nsec = 500 * 1000000;
	error = nanosleep_test(&ts, &ts2);
	if (error == -1)
		err(1, "nanosleep");
}
