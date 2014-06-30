/*-
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "test.h"


/* Cut and pasted from kernel header, bah! */

/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timespecadd(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec += (uvp)->tv_sec;				\
		(vvp)->tv_nsec += (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec >= 1000000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_nsec -= 1000000000;			\
		}							\
	} while (0)
#define timespecsub(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)


#define	TEST_PATH	"/posixsem_regression_test"

#define	ELAPSED(elapsed, limit)		(abs((elapsed) - (limit)) < 100)

/* Macros for passing child status to parent over a pipe. */
#define	CSTAT(class, error)		((class) << 16 | (error))
#define	CSTAT_CLASS(stat)		((stat) >> 16)
#define	CSTAT_ERROR(stat)		((stat) & 0xffff)

static sem_t *
construct_shared_unnamed_sem(unsigned int count)
{
	sem_t *id = mmap(NULL, sizeof(sem_t), PROT_READ|PROT_WRITE,
			 MAP_SHARED|MAP_ANON, -1, 0);
	if (id == MAP_FAILED) {
		fail_errno("mmap");
		return SEM_FAILED;
	}

	if (sem_init(id, 1, count) < 0) {
		fail_errno("sem_init");
		munmap(id, sizeof(sem_t));
		return SEM_FAILED;
	}

	return id;
}

static void
destruct_shared_unnamed_sem(sem_t *id)
{
	if (sem_destroy(id) < 0)
		fail_errno("sem_destroy");

	if (munmap(id, sizeof(sem_t)) < 0)
		fail_errno("munmap");
}

/*
 * Helper routine for tests that use a child process.  This routine
 * creates a pipe and forks a child process.  The child process runs
 * the 'func' routine which returns a status integer.  The status
 * integer gets written over the pipe to the parent and returned in
 * '*stat'.  If there is an error in pipe(), fork(), or wait() this
 * returns -1 and fails the test.
 */
static int
child_worker(int (*func)(void *arg), void *arg, int *stat)
{
	pid_t pid;
	int pfd[2], cstat;

	if (pipe(pfd) < 0) {
		fail_errno("pipe");
		return (-1);
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		fail_errno("fork");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	case 0:
		/* Child. */
		cstat = func(arg);
		write(pfd[1], &cstat, sizeof(cstat));
		exit(0);
	}

	if (read(pfd[0], stat, sizeof(*stat)) < 0) {
		fail_errno("read(pipe)");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	}
	if (waitpid(pid, NULL, 0) < 0) {
		fail_errno("wait");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	}
	close(pfd[0]);
	close(pfd[1]);
	return (0);
}

/*
 * Attempt a sem_open() that should fail with an expected error of
 * 'error'.
 */
static void
sem_open_should_fail(const char *path, int flags, mode_t mode,
		     unsigned int value, int error)
{
	sem_t *id;

	id = sem_open(path, flags, mode, value);
	if (id != SEM_FAILED) {
		fail_err("sem_open() didn't fail");
		sem_close(id);
		return;
	}
	if (errno != error) {
		fail_errno("sem_open");
		return;
	}
	pass();
}

/*
 * Attempt a sem_unlink() that should fail with an expected error of
 * 'error'.
 */
static void
sem_unlink_should_fail(const char *path, int error)
{

	if (sem_unlink(path) >= 0) {
		fail_err("sem_unlink() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("sem_unlink");
		return;
	}
	pass();
}

/*
 * Attempt a sem_close() that should fail with an expected error of
 * 'error'.
 */
static void
sem_close_should_fail(sem_t *id, int error)
{

	if (sem_close(id) >= 0) {
		fail_err("sem_close() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("sem_close");
		return;
	}
	pass();
}

/*
 * Attempt a sem_init() that should fail with an expected error of
 * 'error'.
 */
static void
sem_init_should_fail(unsigned int value, int error)
{
	sem_t id;

	if (sem_init(&id, 0, value) >= 0) {
		fail_err("sem_init() didn't fail");
		sem_destroy(&id);
		return;
	}
	if (errno != error) {
		fail_errno("sem_init");
		return;
	}
	pass();
}

/*
 * Attempt a sem_destroy() that should fail with an expected error of
 * 'error'.
 */
static void
sem_destroy_should_fail(sem_t *id, int error)
{

	if (sem_destroy(id) >= 0) {
		fail_err("sem_destroy() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}

static void
open_after_unlink(void)
{
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		fail_errno("sem_open(1)");
		return;
	}
	sem_close(id);

	if (sem_unlink(TEST_PATH) < 0) {
		fail_errno("sem_unlink");
		return;
	}

	sem_open_should_fail(TEST_PATH, O_RDONLY, 0777, 1, ENOENT);
}
TEST(open_after_unlink, "open after unlink");

static void
open_invalid_path(void)
{

	sem_open_should_fail("blah", 0, 0777, 1, ENOENT);
}
TEST(open_invalid_path, "open invalid path");

static void
open_extra_flags(void)
{

	sem_open_should_fail(TEST_PATH, O_RDONLY | O_DIRECT, 0777, 1, EINVAL);
}
TEST(open_extra_flags, "open with extra flags");

static void
open_bad_value(void)
{

	(void)sem_unlink(TEST_PATH);

	sem_open_should_fail(TEST_PATH, O_CREAT, 0777, SEM_VALUE_MAX+1U, EINVAL);
}
TEST(open_bad_value, "open with invalid initial value");

static void
open_path_too_long(void)
{
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[0] = '/';
	page[MAXPATHLEN] = '\0';
	sem_open_should_fail(page, O_RDONLY, 0777, 1, ENAMETOOLONG);
	free(page);
}
TEST(open_path_too_long, "open pathname too long");

static void
open_nonexisting_semaphore(void)
{
	sem_open_should_fail("/notreallythere", 0, 0777, 1, ENOENT);
}

TEST(open_nonexisting_semaphore, "open nonexistent semaphore");

static void
exclusive_create_existing_semaphore(void)
{
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		fail_errno("sem_open(O_CREAT)");
		return;
	}
	sem_close(id);

	sem_open_should_fail(TEST_PATH, O_CREAT | O_EXCL, 0777, 1, EEXIST);

	sem_unlink(TEST_PATH);
}
TEST(exclusive_create_existing_semaphore, "O_EXCL of existing semaphore");

static void
init_bad_value(void)
{

	sem_init_should_fail(SEM_VALUE_MAX+1U, EINVAL);
}
TEST(init_bad_value, "init with invalid initial value");

static void
unlink_path_too_long(void)
{
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[MAXPATHLEN] = '\0';
	sem_unlink_should_fail(page, ENAMETOOLONG);
	free(page);
}
TEST(unlink_path_too_long, "unlink pathname too long");

static void
destroy_named_semaphore(void)
{
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		fail_errno("sem_open(O_CREAT)");
		return;
	}

	sem_destroy_should_fail(id, EINVAL);

	sem_close(id);
	sem_unlink(TEST_PATH);
}
TEST(destroy_named_semaphore, "destroy named semaphore");

static void
close_unnamed_semaphore(void)
{
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		fail_errno("sem_init");
		return;
	}

	sem_close_should_fail(&id, EINVAL);

	sem_destroy(&id);
}
TEST(close_unnamed_semaphore, "close unnamed semaphore");

static void
create_unnamed_semaphore(void)
{
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		fail_errno("sem_init");
		return;
	}

	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(create_unnamed_semaphore, "create unnamed semaphore");

static void
open_named_semaphore(void)
{
	sem_t *id;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 1);
	if (id == SEM_FAILED) {
		fail_errno("sem_open(O_CREAT)");
		return;
	}

	if (sem_close(id) < 0) {
		fail_errno("sem_close");
		return;
	}

	if (sem_unlink(TEST_PATH) < 0) {
		fail_errno("sem_unlink");
		return;
	}
	pass();
}
TEST(open_named_semaphore, "create named semaphore");

static int
checkvalue(sem_t *id, int expected)
{
	int val;

	if (sem_getvalue(id, &val) < 0) {
		fail_errno("sem_getvalue");
		return (-1);
	}
	if (val != expected) {
		fail_err("sem value should be %d instead of %d", expected, val);
		return (-1);
	}
	return (0);
}

static void
post_test(void)
{
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		fail_errno("sem_init");
		return;
	}
	if (checkvalue(&id, 1) < 0) {
		sem_destroy(&id);
		return;
	}
	if (sem_post(&id) < 0) {
		fail_errno("sem_post");
		sem_destroy(&id);
		return;
	}
	if (checkvalue(&id, 2) < 0) {
		sem_destroy(&id);
		return;
	}
	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(post_test, "simple post");

static void
use_after_unlink_test(void)
{
	sem_t *id;

	/*
	 * Create named semaphore with value of 1 and then unlink it
	 * while still retaining the initial reference.
	 */
	id = sem_open(TEST_PATH, O_CREAT | O_EXCL, 0777, 1);
	if (id == SEM_FAILED) {
		fail_errno("sem_open(O_CREAT | O_EXCL)");
		return;
	}
	if (sem_unlink(TEST_PATH) < 0) {
		fail_errno("sem_unlink");
		sem_close(id);
		return;
	}
	if (checkvalue(id, 1) < 0) {
		sem_close(id);
		return;
	}

	/* Post the semaphore to set its value to 2. */
	if (sem_post(id) < 0) {
		fail_errno("sem_post");
		sem_close(id);
		return;
	}
	if (checkvalue(id, 2) < 0) {
		sem_close(id);
		return;
	}

	/* Wait on the semaphore which should set its value to 1. */
	if (sem_wait(id) < 0) {
		fail_errno("sem_wait");
		sem_close(id);
		return;
	}
	if (checkvalue(id, 1) < 0) {
		sem_close(id);
		return;
	}

	if (sem_close(id) < 0) {
		fail_errno("sem_close");
		return;
	}
	pass();
}
TEST(use_after_unlink_test, "use named semaphore after unlink");

static void
unlocked_trywait(void)
{
	sem_t id;

	if (sem_init(&id, 0, 1) < 0) {
		fail_errno("sem_init");
		return;
	}

	/* This should succeed and decrement the value to 0. */
	if (sem_trywait(&id) < 0) {
		fail_errno("sem_trywait()");
		sem_destroy(&id);
		return;
	}
	if (checkvalue(&id, 0) < 0) {
		sem_destroy(&id);
		return;
	}

	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(unlocked_trywait, "unlocked trywait");

static void
locked_trywait(void)
{
	sem_t id;

	if (sem_init(&id, 0, 0) < 0) {
		fail_errno("sem_init");
		return;
	}

	/* This should fail with EAGAIN and leave the value at 0. */
	if (sem_trywait(&id) >= 0) {
		fail_err("sem_trywait() didn't fail");
		sem_destroy(&id);
		return;
	}
	if (errno != EAGAIN) {
		fail_errno("wrong error from sem_trywait()");
		sem_destroy(&id);
		return;
	}
	if (checkvalue(&id, 0) < 0) {
		sem_destroy(&id);
		return;
	}

	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(locked_trywait, "locked trywait");

/*
 * Use a timer to post a specific semaphore after a timeout.  A timer
 * is scheduled via schedule_post().  check_alarm() must be called
 * afterwards to clean up and check for errors.
 */
static sem_t *alarm_id = SEM_FAILED;
static int alarm_errno;
static int alarm_handler_installed;

static void
alarm_handler(int signo)
{

	if (sem_post(alarm_id) < 0)
		alarm_errno = errno;
}

static int
check_alarm(int just_clear)
{
	struct itimerval it;

	bzero(&it, sizeof(it));
	if (just_clear) {
		setitimer(ITIMER_REAL, &it, NULL);
		alarm_errno = 0;
		alarm_id = SEM_FAILED;
		return (0);
	}
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		fail_errno("setitimer");
		return (-1);
	}
	if (alarm_errno != 0 && !just_clear) {
		errno = alarm_errno;
		fail_errno("sem_post() (via timeout)");
		alarm_errno = 0;
		return (-1);
	}
	alarm_id = SEM_FAILED;

	return (0);
}

static int
schedule_post(sem_t *id, u_int msec)
{
	struct itimerval it;

	if (!alarm_handler_installed) {
		if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
			fail_errno("signal(SIGALRM)");
			return (-1);
		}
		alarm_handler_installed = 1;
	}
	if (alarm_id != SEM_FAILED) {
		fail_err("sem_post() already scheduled");
		return (-1);
	}
	alarm_id = id;
	bzero(&it, sizeof(it));
	it.it_value.tv_sec = msec / 1000;
	it.it_value.tv_usec = (msec % 1000) * 1000;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		fail_errno("setitimer");
		return (-1);
	}
	return (0);
}

static int
timedwait(sem_t *id, u_int msec, u_int *delta, int error)
{
	struct timespec start, end;

	if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	end.tv_sec = msec / 1000;
	end.tv_nsec = msec % 1000 * 1000000;
	timespecadd(&end, &start);
	if (sem_timedwait(id, &end) < 0) {
		if (errno != error) {
			fail_errno("sem_timedwait");
			return (-1);
		}
	} else if (error != 0) {
		fail_err("sem_timedwait() didn't fail");
		return (-1);
	}
	if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	timespecsub(&end, &start);
	*delta = end.tv_nsec / 1000000;
	*delta += end.tv_sec * 1000;
	return (0);
}

static void
unlocked_timedwait(void)
{
	sem_t id;
	u_int elapsed;

	if (sem_init(&id, 0, 1) < 0) {
		fail_errno("sem_init");
		return;
	}

	/* This should succeed right away and set the value to 0. */
	if (timedwait(&id, 5000, &elapsed, 0) < 0) {
		sem_destroy(&id);
		return;
	}
	if (!ELAPSED(elapsed, 0)) {
		fail_err("sem_timedwait() of unlocked sem took %ums", elapsed);
		sem_destroy(&id);
		return;
	}
	if (checkvalue(&id, 0) < 0) {
		sem_destroy(&id);
		return;
	}

	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(unlocked_timedwait, "unlocked timedwait");

static void
expired_timedwait(void)
{
	sem_t id;
	u_int elapsed;

	if (sem_init(&id, 0, 0) < 0) {
		fail_errno("sem_init");
		return;
	}

	/* This should fail with a timeout and leave the value at 0. */
	if (timedwait(&id, 2500, &elapsed, ETIMEDOUT) < 0) {
		sem_destroy(&id);
		return;
	}
	if (!ELAPSED(elapsed, 2500)) {
		fail_err("sem_timedwait() of locked sem took %ums instead of 2500ms",
			 elapsed);
		sem_destroy(&id);
		return;
	}
	if (checkvalue(&id, 0) < 0) {
		sem_destroy(&id);
		return;
	}

	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(expired_timedwait, "locked timedwait timeout");

static void
locked_timedwait(void)
{
	sem_t *id;
	u_int elapsed;
	pid_t pid;

	id = construct_shared_unnamed_sem(0);
	if (id == SEM_FAILED) {
		fail_err("construct sem");
		return;
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		fail_errno("fork");
		destruct_shared_unnamed_sem(id);
		return;
	case 0:
		/* Child. */
		sleep(1);
		sem_post(id);
		exit(0);
	}

	if (timedwait(id, 2000, &elapsed, 0) < 0) {
		destruct_shared_unnamed_sem(id);
		return;
	}
	if (!ELAPSED(elapsed, 1000)) {
		fail_err("sem_timedwait() with delayed post took %ums instead of 1000ms",
			 elapsed);
		destruct_shared_unnamed_sem(id);
		return;
	}

	destruct_shared_unnamed_sem(id);

	pass();
}
TEST(locked_timedwait, "locked timedwait");

static int
testwait(sem_t *id, u_int *delta)
{
	struct timespec start, end;

	if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	if (sem_wait(id) < 0) {
		fail_errno("sem_wait");
		return (-1);
	}
	if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	timespecsub(&end, &start);
	*delta = end.tv_nsec / 1000000;
	*delta += end.tv_sec * 1000;
	return (0);
}

static void
unlocked_wait(void)
{
	sem_t id;
	u_int elapsed;

	if (sem_init(&id, 0, 1) < 0) {
		fail_errno("sem_init");
		return;
	}

	/* This should succeed right away and set the value to 0. */
	if (testwait(&id, &elapsed) < 0) {
		sem_destroy(&id);
		return;
	}
	if (!ELAPSED(elapsed, 0)) {
		fail_err("sem_wait() of unlocked sem took %ums", elapsed);
		sem_destroy(&id);
		return;
	}
	if (checkvalue(&id, 0) < 0) {
		sem_destroy(&id);
		return;
	}

	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(unlocked_wait, "unlocked wait");

static void
locked_wait(void)
{
	sem_t *id;
	u_int elapsed;
	pid_t pid;

	id = construct_shared_unnamed_sem(0);

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		fail_errno("fork");
		destruct_shared_unnamed_sem(id);
		return;
	case 0:
		/* Child. */
		sleep(1);
		sem_post(id);
		exit(0);
	}

	if (testwait(id, &elapsed) < 0) {
		destruct_shared_unnamed_sem(id);
		return;
	}
	if (!ELAPSED(elapsed, 1000)) {
		fail_err("sem_wait() with delayed post took %ums instead of 1000ms",
			 elapsed);
		destruct_shared_unnamed_sem(id);
		return;
	}

	destruct_shared_unnamed_sem(id);

	pass();
}
TEST(locked_wait, "locked wait");

/*
 * Fork off a child process.  The child will open the semaphore via
 * the same name.  The child will then block on the semaphore waiting
 * for the parent to post it.
 */
static int
wait_twoproc_child(void *arg)
{
	sem_t *id;

	id = sem_open(TEST_PATH, 0, 0, 0);
	if (id == SEM_FAILED)
		return (CSTAT(1, errno));
	if (sem_wait(id) < 0)
		return (CSTAT(2, errno));
	if (sem_close(id) < 0)
		return (CSTAT(3, errno));
	return (CSTAT(0, 0));
}

static void
wait_twoproc_test(void)
{
	sem_t *id;
	int stat;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 0);
	if (id == SEM_FAILED) {
		fail_errno("sem_open");
		return;
	}

	if (schedule_post(id, 500) < 0) {
		sem_close(id);
		sem_unlink(TEST_PATH);
		return;
	}		

	if (child_worker(wait_twoproc_child, NULL, &stat) < 0) {
		check_alarm(1);
		sem_close(id);
		sem_unlink(TEST_PATH);
		return;
	}

	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		pass();
		break;
	case 1:
		fail_errno("child sem_open()");
		break;
	case 2:
		fail_errno("child sem_wait()");
		break;
	case 3:
		fail_errno("child sem_close()");
		break;
	default:
		fail_err("bad child state %#x", stat);
		break;
	}

	check_alarm(1);
	sem_close(id);
	sem_unlink(TEST_PATH);
}
TEST(wait_twoproc_test, "two proc wait");

static void
maxvalue_test(void)
{
	sem_t id;
	int val;

	if (sem_init(&id, 0, SEM_VALUE_MAX) < 0) {
		fail_errno("sem_init");
		return;
	}
	if (sem_getvalue(&id, &val) < 0) {
		fail_errno("sem_getvalue");
		sem_destroy(&id);
		return;
	}
	if (val != SEM_VALUE_MAX) {
		fail_err("value %d != SEM_VALUE_MAX");
		sem_destroy(&id);
		return;
	}
	if (val < 0) {
		fail_err("value < 0");
		sem_destroy(&id);
		return;
	}
	if (sem_destroy(&id) < 0) {
		fail_errno("sem_destroy");
		return;
	}
	pass();
}
TEST(maxvalue_test, "get value of SEM_VALUE_MAX semaphore");

static void
file_test(void)
{
	struct stat sb;
	sem_t *id;
	int error;

	id = sem_open(TEST_PATH, O_CREAT, 0777, 0);
	if (id == SEM_FAILED) {
		fail_errno("sem_open");
		return;
	}

	error = stat("/var/run/sem", &sb);
	if (error) {
		fail_errno("stat");
		return;
	}
	if ((sb.st_mode & ALLPERMS) != (S_IRWXU|S_IRWXG|S_IRWXO|S_ISTXT)) {
		fail_err("semaphore dir has incorrect mode: 0%o\n",
			 (sb.st_mode & ALLPERMS));
		return;
	}

	sem_close(id);
	pass();
}

TEST(file_test, "check semaphore directory has correct mode bits");

int
main(int argc, char *argv[])
{

	signal(SIGSYS, SIG_IGN);
	run_tests();
	return (test_exit_value);
}
