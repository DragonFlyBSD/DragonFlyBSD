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
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <kvm.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

#include <common.h>

/*
 * Use a timer to post a specific semaphore after a timeout.  A timer
 * is scheduled via schedule_post().  check_alarm() must be called
 * afterwards to clean up and check for errors.
 */
sem_t *alarm_id = SEM_FAILED;
int alarm_errno;
int alarm_handler_installed;

int
checkvalue(sem_t *id, int expected)
{
	int val;

	if (sem_getvalue(id, &val) < 0) {
		perror("sem_getvalue");
		return (-1);
	}
	if (val != expected) {
		fprintf(stderr, "sem value should be %d instead of %d",
		    expected, val);
		return (-1);
	}
	return (0);
}

sem_t *
construct_shared_unnamed_sem(unsigned int count)
{
	sem_t *id = mmap(NULL, sizeof(sem_t), PROT_READ|PROT_WRITE,
			 MAP_SHARED|MAP_ANON, -1, 0);
	if (id == MAP_FAILED) {
		perror("mmap");
		return SEM_FAILED;
	}

	if (sem_init(id, 1, count) < 0) {
		perror("sem_init");
		munmap(id, sizeof(sem_t));
		return SEM_FAILED;
	}

	return id;
}

void
destruct_shared_unnamed_sem(sem_t *id)
{
	if (sem_destroy(id) < 0)
		perror("sem_destroy");

	if (munmap(id, sizeof(sem_t)) < 0)
		perror("munmap");
}

int
testwait(sem_t *id, u_int *delta)
{
	struct timespec start, end;

	if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
		perror("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	if (sem_wait(id) < 0) {
		perror("sem_wait");
		return (-1);
	}
	if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
		perror("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	timespecsub(&end, &start);
	*delta = end.tv_nsec / 1000000;
	*delta += end.tv_sec * 1000;
	return (0);
}

static void
alarm_handler(int signo)
{

	if (sem_post(alarm_id) < 0)
		alarm_errno = errno;
}

int
schedule_post(sem_t *id, u_int msec)
{
	struct itimerval it;

	if (!alarm_handler_installed) {
		if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
			perror("signal(SIGALRM)");
			return (-1);
		}
		alarm_handler_installed = 1;
	}
	if (alarm_id != SEM_FAILED) {
		fprintf(stderr, "sem_post() already scheduled");
		return (-1);
	}
	alarm_id = id;
	bzero(&it, sizeof(it));
	it.it_value.tv_sec = msec / 1000;
	it.it_value.tv_usec = (msec % 1000) * 1000;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		perror("setitimer");
		return (-1);
	}
	return (0);
}

int
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
		perror("setitimer");
		return (-1);
	}
	if (alarm_errno != 0 && !just_clear) {
		errno = alarm_errno;
		perror("sem_post() (via timeout)");
		alarm_errno = 0;
		return (-1);
	}
	alarm_id = SEM_FAILED;

	return (0);
}

/*
 * Helper routine for tests that use a child process.  This routine
 * creates a pipe and forks a child process.  The child process runs
 * the 'func' routine which returns a status integer.  The status
 * integer gets written over the pipe to the parent and returned in
 * '*stat'.  If there is an error in pipe(), fork(), or wait() this
 * returns -1 and fails the test.
 */
int
child_worker(int (*func)(void *arg), void *arg, int *stat)
{
	pid_t pid;
	int pfd[2], cstat;

	if (pipe(pfd) < 0) {
		perror("pipe");
		return (-1);
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		perror("fork");
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
		perror("read(pipe)");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	}
	if (waitpid(pid, NULL, 0) < 0) {
		perror("wait");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	}
	close(pfd[0]);
	close(pfd[1]);
	return (0);
}

/*
 * Fork off a child process.  The child will open the semaphore via
 * the same name.  The child will then block on the semaphore waiting
 * for the parent to post it.
 */
int
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

int
timedwait(sem_t *id, u_int msec, u_int *delta, int error)
{
	struct timespec start, end;

	if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
		perror("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	end.tv_sec = msec / 1000;
	end.tv_nsec = msec % 1000 * 1000000;
	timespecadd(&end, &start);
	if (sem_timedwait(id, &end) < 0) {
		if (errno != error) {
			perror("sem_timedwait");
			return (-1);
		}
	} else if (error != 0) {
		return (-1);
	}
	if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
		perror("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	timespecsub(&end, &start);
	*delta = end.tv_nsec / 1000000;
	*delta += end.tv_sec * 1000;
	return (0);
}

/*
 * Attempt a sem_open() that should fail with an expected error of
 * 'error'.
 */
int
sem_open_should_fail(const char *path, int flags, mode_t mode,
		     unsigned int value, int error)
{
	int retval = 0;
	sem_t *id;

	id = sem_open(path, flags, mode, value);
	if (id != SEM_FAILED) {
		sem_close(id);
		retval = 1;
	}
	if (errno != error) {
		fprintf(stderr, "sem_open: %s\n", strerror(errno));
		retval = 1;
	}
	return retval;
}

/*
 * Attempt a sem_init() that should fail with an expected error of
 * 'error'.
 */
int
sem_init_should_fail(unsigned int value, int error)
{
	sem_t id;

	if (sem_init(&id, 0, value) >= 0) {
		sem_destroy(&id);
		return 1;
	}
	if (errno != error) {
		perror("sem_init");
		return 1;
	}

	return 0;
}

/*
 * Attempt a sem_unlink() that should fail with an expected error of
 * 'error'.
 */
int
sem_unlink_should_fail(const char *path, int error)
{

	if (sem_unlink(path) >= 0) {
		return 1;
	}
	if (errno != error) {
		perror("sem_unlink");
		return 1;
	}
	return 0;
}

/*
 * Attempt a sem_destroy() that should fail with an expected error of
 * 'error'.
 */
int
sem_destroy_should_fail(sem_t *id, int error)
{
	if (sem_destroy(id) >= 0) {
		return 1;
	}
	if (errno != error) {
		perror("sem_destroy");
		return 1;
	}
	return 0;
}

/*
 * Attempt a sem_close() that should fail with an expected error of
 * 'error'.
 */
int
sem_close_should_fail(sem_t *id, int error)
{

	if (sem_close(id) >= 0) {
		return 1;
	}
	if (errno != error) {
		perror("sem_close");
		return 1;
	}
	return 0;
}
