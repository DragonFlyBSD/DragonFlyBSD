/*
 * Rudimentary test suite used while implementing pselect(2).
 */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


static int alarm_flag = 0;


static void
nop(int signo)
{
}


static void
set_alarm_flag(int signo)
{
	alarm_flag = 1;
}


/*
 * Try to detect regressions in select(2).
 */

static void
test_select()
{
	fd_set rset;
	fd_set wset;
	struct timeval timeout;
	int des[2];
	int r;
	char buf[1];

	printf("test_select\n");

	/*
	 * It is always possible to write to stdout (if not redirected).
	 */

	FD_ZERO(&wset);
	FD_SET(1, &wset);

	r = select(2, NULL, &wset, NULL, NULL);
	assert(r == 1);
	assert(FD_ISSET(1, &wset));

	/*
	 * Write to a pipe and check a select on the read end does not block.
	 */

	r = pipe(des);
	assert(r == 0);

	FD_ZERO(&rset);
	FD_SET(des[0], &rset);

	buf[0] = 'f';
	r = write(des[1], buf, 1);
	assert(r == 1);

	r = select(des[0]+1, &rset, NULL, NULL, NULL);
	assert(r == 1);
	assert(FD_ISSET(des[0], &rset));

	r = read(des[0], buf, 1);
	assert(r == 1);
	assert(buf[0] == 'f');

	/*
	 * Block until signal reception.
	 */

	signal(SIGALRM, nop);
	alarm(1);

	FD_ZERO(&rset);
	FD_SET(des[0], &rset);

	r = select(des[0]+1, &rset, NULL, NULL, NULL);
	assert(r == -1);
	assert(errno == EINTR);

	/*
	 * Block until timeout.
	 */

	FD_ZERO(&rset);
	FD_SET(des[0], &rset);

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	r = select(des[0]+1, &rset, NULL, NULL, &timeout);
	assert(r == 0);

	/*
	 * When the timeout is zero, the call should not block.
	 */

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO(&rset);
	FD_SET(des[0], &rset);

	r = select(des[0]+1, &rset, NULL, NULL, &timeout);
	assert(r == 0);

	close(des[0]);
	close(des[1]);
}

int
main(void)
{
	test_select();
	return (0);
}
