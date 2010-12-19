/*******************************************
 *
 * Interbench - Interactivity benchmark
 *
 * Author:  Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *******************************************/

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64	/* Large file support */
#define INTERBENCH_VERSION	"0.30"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include <math.h>
#include <fenv.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include "interbench.h"

#define MAX_UNAME_LENGTH	100
#define MAX_LOG_LENGTH		((MAX_UNAME_LENGTH) + 4)
#define MIN_BLK_SIZE		1024
#define DEFAULT_RESERVE		64
#define MB			(1024 * 1024)	/* 2^20 bytes */
#define KB			1024
#define MAX_MEM_IN_MB		(1024 * 64)	/* 64 GB */

struct user_data {
	unsigned long loops_per_ms;
	unsigned long ram, swap;
	int duration;
	int do_rt;
	int bench_nice;
	int load_nice;
	unsigned long custom_run;
	unsigned long custom_interval;
	unsigned long cpu_load;
	char logfilename[MAX_LOG_LENGTH];
	int log;
	char unamer[MAX_UNAME_LENGTH];
	char datestamp[13];
	FILE *logfile;
} ud = {
	.duration = 30,
	.cpu_load = 4,
	.log = 1,
};

/* Pipes main to/from load and bench processes */
static int m2l[2], l2m[2], m2b[2], b2m[2];

/* Which member of becnhmarks is used when not benchmarking */
#define NOT_BENCHING	(THREADS)
#define CUSTOM		(THREADS - 1)

/*
 * To add another load or a benchmark you need to increment the value of
 * THREADS, add a function prototype for your function and add an entry to
 * the threadlist. To specify whether the function is a benchmark or a load
 * set the benchmark and/or load flag as appropriate. The basic requirements
 * of a new load can be seen by using emulate_none as a template.
 */

void emulate_none(struct thread *th);
void emulate_audio(struct thread *th);
void emulate_video(struct thread *th);
void emulate_x(struct thread *th);
void emulate_game(struct thread *th);
void emulate_burn(struct thread *th);
void emulate_write(struct thread *th);
void emulate_read(struct thread *th);
void emulate_ring(struct thread *th);
void emulate_compile(struct thread *th);
void emulate_memload(struct thread *th);
void emulate_hackbench(struct thread *th);
void emulate_custom(struct thread *th);

struct thread threadlist[THREADS] = {
	{.label = "None", .name = emulate_none, .load = 1, .rtload = 1},
	{.label = "Audio", .name = emulate_audio, .bench = 1, .rtbench = 1},
	{.label = "Video", .name = emulate_video, .bench = 1, .rtbench = 1, .load = 1, .rtload = 1},
	{.label = "X", .name = emulate_x, .bench = 1, .load = 1, .rtload = 1},
	{.label = "Gaming", .name = emulate_game, .nodeadlines = 1, .bench = 1},
	{.label = "Burn", .name = emulate_burn, .load = 1, .rtload = 1},
	{.label = "Write", .name = emulate_write, .load = 1, .rtload = 1},
	{.label = "Read", .name = emulate_read, .load = 1, .rtload = 1},
	{.label = "Ring", .name = emulate_ring, .load = 0, .rtload = 0},	/* No useful data from this */
	{.label = "Compile", .name = emulate_compile, .load = 1, .rtload = 1},
	{.label = "Memload", .name = emulate_memload, .load = 1, .rtload = 1},
	{.label = "Hack", .name = emulate_hackbench, .load = 0, .rtload = 0},	/* This is causing signal headaches */
	{.label = "Custom", .name = emulate_custom},	/* Leave custom as last entry */
};

void init_sem(sem_t *sem);
void init_all_sems(struct sems *s);
void initialise_thread(int i);
void start_thread(struct thread *th);
void stop_thread(struct thread *th);

void terminal_error(const char *name)
{
	fprintf(stderr, "\n");
	perror(name);
	exit (1);
}

void terminal_fileopen_error(FILE *fp, char *name)
{
	if (fclose(fp) == -1)
		terminal_error("fclose");
	terminal_error(name);
}

unsigned long long get_nsecs(struct timespec *myts)
{
	if (clock_gettime(CLOCK_REALTIME, myts))
		terminal_error("clock_gettime");
	return (myts->tv_sec * 1000000000 + myts->tv_nsec );
}

unsigned long get_usecs(struct timespec *myts)
{
	if (clock_gettime(CLOCK_REALTIME, myts))
		terminal_error("clock_gettime");
	return (myts->tv_sec * 1000000 + myts->tv_nsec / 1000 );
}

void set_fifo(int prio)
{
	struct sched_param sp;

	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = prio;
	if (sched_setscheduler(0, SCHED_FIFO, &sp) == -1) {
		if (errno != EPERM)
			terminal_error("sched_setscheduler");
	}
}

void set_mlock(void)
{
	int mlockflags;

	mlockflags = MCL_CURRENT | MCL_FUTURE;
#if 0
	mlockall(mlockflags);	/* Is not critical if this fails */
#endif
}

void set_munlock(void)
{
#if 0
	if (munlockall() == -1)
		terminal_error("munlockall");
#endif
}

void set_thread_fifo(pthread_t pthread, int prio)
{
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = prio;
	if (pthread_setschedparam(pthread, SCHED_FIFO, &sp) == -1)
		terminal_error("pthread_setschedparam");
}

void set_normal(void)
{
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 0;
	if (sched_setscheduler(0, SCHED_OTHER, &sp) == -1) {
		fprintf(stderr, "Weird, could not unset RT scheduling!\n");
	}
}

void set_nice(int prio)
{
	if (setpriority(PRIO_PROCESS, 0, prio) == -1)
		terminal_error("setpriority");
}

int test_fifo(void)
{
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 99;
	if (sched_setscheduler(0, SCHED_FIFO, &sp) == -1) {
		if (errno != EPERM)
			terminal_error("sched_setscheduler");
		goto out_fail;
	}
	if (sched_getscheduler(0) != SCHED_FIFO)
		goto out_fail;
	set_normal();
	return 1;
out_fail:
	set_normal();
	return 0;
}

void set_thread_normal(pthread_t pthread)
{
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 0;
	if (pthread_setschedparam(pthread, SCHED_OTHER, &sp) == -1)
		terminal_error("pthread_setschedparam");
}

void sync_flush(void)
{
	if ((fflush(NULL)) == EOF)
		terminal_error("fflush");
	sync();
	sync();
	sync();
}

unsigned long compute_allocable_mem(void)
{
	unsigned long total = ud.ram + ud.swap;
	unsigned long usage = ud.ram * 110 / 100 ;

	/* Leave at least DEFAULT_RESERVE free space and check for maths overflow. */
	if (total - DEFAULT_RESERVE < usage)
		usage = total - DEFAULT_RESERVE;
	usage /= 1024;	/* to megabytes */
	if (usage > 2930)
		usage = 2930;
	return usage;
}

void burn_loops(unsigned long loops)
{
	unsigned long i;

	/*
	 * We need some magic here to prevent the compiler from optimising
	 * this loop away. Otherwise trying to emulate a fixed cpu load
	 * with this loop will not work.
	 */
	for (i = 0 ; i < loops ; i++)
	     asm volatile("" : : : "memory");
}

/* Use this many usecs of cpu time */
void burn_usecs(unsigned long usecs)
{
	unsigned long ms_loops;

	ms_loops = ud.loops_per_ms / 1000 * usecs;
	burn_loops(ms_loops);
}

void microsleep(unsigned long long usecs)
{
	struct timespec req, rem;

	rem.tv_sec = rem.tv_nsec = 0;

	req.tv_sec = usecs / 1000000;
	req.tv_nsec = (usecs - (req.tv_sec * 1000000)) * 1000;
continue_sleep:
	if ((nanosleep(&req, &rem)) == -1) {
		if (errno == EINTR) {
			if (rem.tv_sec || rem.tv_nsec) {
				req.tv_sec = rem.tv_sec;
				req.tv_nsec = rem.tv_nsec;
				goto continue_sleep;
			}
			goto out;
		}
		terminal_error("nanosleep");
	}
out:
	return;
}

/*
 * Yes, sem_post and sem_wait shouldn't return -1 but they do so we must
 * handle it.
 */
inline void post_sem(sem_t *s)
{
retry:
	if ((sem_post(s)) == -1) {
		if (errno == EINTR)
			goto retry;
		terminal_error("sem_post");
	}
}

inline void wait_sem(sem_t *s)
{
retry:
	if ((sem_wait(s)) == -1) {
		if (errno == EINTR)
			goto retry;
		terminal_error("sem_wait");
	}
}

inline int trywait_sem(sem_t *s)
{
	int ret;

retry:
	if ((ret = sem_trywait(s)) == -1) {
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN)
			terminal_error("sem_trywait");
	}
	return ret;
}

inline ssize_t Read(int fd, void *buf, size_t count)
{
	ssize_t retval;

retry:
	retval = read(fd, buf, count);
	if (retval == -1) {
		if (errno == EINTR)
			goto retry;
		terminal_error("read");
	}
	return retval;
}

inline ssize_t Write(int fd, const void *buf, size_t count)
{
	ssize_t retval;

retry:
	retval = write(fd, &buf, count);
	if (retval == -1) {
		if (errno == EINTR)
			goto retry;
		terminal_error("write");
	}
	return retval;
}

unsigned long periodic_schedule(struct thread *th, unsigned long run_usecs,
	unsigned long interval_usecs, unsigned long long deadline)
{
	unsigned long long latency, missed_latency;
	unsigned long long current_time;
	struct tk_thread *tk;
	struct data_table *tb;
	struct timespec myts;

	latency = 0;
	tb = th->dt;
	tk = &th->tkthread;

	current_time = get_usecs(&myts);
	if (current_time > deadline + tk->slept_interval)
		latency = current_time - deadline- tk->slept_interval;

	/* calculate the latency for missed frames */
	missed_latency = 0;

	current_time = get_usecs(&myts);
	if (interval_usecs && current_time > deadline + interval_usecs) {
		/* We missed the deadline even before we consumed cpu */
		unsigned long intervals;

		deadline += interval_usecs;
		intervals = (current_time - deadline) /
			interval_usecs + 1;

		tb->missed_deadlines += intervals;
		missed_latency = intervals * interval_usecs;
		deadline += intervals * interval_usecs;
		tb->missed_burns += intervals;
		goto bypass_burn;
	}

	burn_usecs(run_usecs);
	current_time = get_usecs(&myts);
	tb->achieved_burns++;

	/*
	 * If we meet the deadline we move the deadline forward, otherwise
	 * we consider it a missed deadline and dropped frame etc.
	 */
	deadline += interval_usecs;
	if (deadline >= current_time) {
		tb->deadlines_met++;
	} else {
		if (interval_usecs) {
			unsigned long intervals = (current_time - deadline) /
				interval_usecs + 1;
	
			tb->missed_deadlines += intervals;
			missed_latency = intervals * interval_usecs;
			deadline += intervals * interval_usecs;
			if (intervals > 1)
				tb->missed_burns += intervals;
		} else {
			deadline = current_time;
			goto out_nosleep;
		}
	}
bypass_burn:
	tk->sleep_interval = deadline - current_time;

	post_sem(&tk->sem.start);
	wait_sem(&tk->sem.complete);
out_nosleep:
	/* 
	 * Must add missed_latency to total here as this function may not be
	 * called again and the missed latency can be lost
	 */
	latency += missed_latency;
	if (latency > tb->max_latency)
		tb->max_latency = latency;
	tb->total_latency += latency;
	tb->sum_latency_squared += latency * latency;
	tb->nr_samples++;

	return deadline;
}

void initialise_thread_data(struct data_table *tb)
{
	tb->max_latency =
		tb->total_latency =
		tb->sum_latency_squared =
		tb->deadlines_met =
		tb->missed_deadlines =
		tb->missed_burns =
		tb->nr_samples = 0;
}

void create_pthread(pthread_t  * thread, pthread_attr_t * attr,
	void * (*start_routine)(void *), void *arg)
{
	if (pthread_create(thread, attr, start_routine, arg))
		terminal_error("pthread_create");
}

void join_pthread(pthread_t th, void **thread_return)
{
	if (pthread_join(th, thread_return))
		terminal_error("pthread_join");
}

void emulate_none(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	wait_sem(s);
}

#define AUDIO_INTERVAL	(50000)
#define AUDIO_RUN	(AUDIO_INTERVAL / 20)
/* We emulate audio by using 5% cpu and waking every 50ms */
void emulate_audio(struct thread *th)
{
	unsigned long long deadline;
	sem_t *s = &th->sem.stop;
	struct timespec myts;

	th->decasecond_deadlines = 1000000 / AUDIO_INTERVAL * 10;
	deadline = get_usecs(&myts);

	while (1) {
		deadline = periodic_schedule(th, AUDIO_RUN, AUDIO_INTERVAL,
			deadline);
		if (!trywait_sem(s))
			return;
	}
}

/* We emulate video by using 40% cpu and waking for 60fps */
#define VIDEO_INTERVAL	(1000000 / 60)
#define VIDEO_RUN	(VIDEO_INTERVAL * 40 / 100)
void emulate_video(struct thread *th)
{
	unsigned long long deadline;
	sem_t *s = &th->sem.stop;
	struct timespec myts;

	th->decasecond_deadlines = 1000000 / VIDEO_INTERVAL * 10;
	deadline = get_usecs(&myts);

	while (1) {
		deadline = periodic_schedule(th, VIDEO_RUN, VIDEO_INTERVAL,
			deadline);
		if (!trywait_sem(s))
			return;
	}
}

/*
 * We emulate X by running for a variable percentage of cpu from 0-100% 
 * in 1ms chunks.
 */
void emulate_x(struct thread *th)
{
	unsigned long long deadline;
	sem_t *s = &th->sem.stop;
	struct timespec myts;

	th->decasecond_deadlines = 100;
	deadline = get_usecs(&myts);

	while (1) {
		int i, j;
		for (i = 0 ; i <= 100 ; i++) {
			j = 100 - i;
			deadline = periodic_schedule(th, i * 1000, j * 1000,
				deadline);
			deadline += i * 1000;
			if (!trywait_sem(s))
				return;
		}
	}
}

/* 
 * We emulate gaming by using 100% cpu and seeing how many frames (jobs
 * completed) we can do in that time. Deadlines are meaningless with 
 * unlocked frame rates. We do not use periodic schedule because for
 * this load because this never wants to sleep.
 */
#define GAME_INTERVAL	(100000)
#define GAME_RUN	(GAME_INTERVAL)
void emulate_game(struct thread *th)
{
	unsigned long long deadline, current_time, latency;
	sem_t *s = &th->sem.stop;
	struct timespec myts;
	struct data_table *tb;

	tb = th->dt;
	th->decasecond_deadlines = 1000000 / GAME_INTERVAL * 10;

	while (1) {
		deadline = get_usecs(&myts) + GAME_INTERVAL;
		burn_usecs(GAME_RUN);
		current_time = get_usecs(&myts);
		/* use usecs instead of simple count for game burn statistics */
		tb->achieved_burns += GAME_RUN;
		if (current_time > deadline) {
			latency = current_time - deadline;
			tb->missed_burns += latency;
		} else
			latency = 0;
		if (latency > tb->max_latency)
			tb->max_latency = latency;
		tb->total_latency += latency;
		tb->sum_latency_squared += latency * latency;
		tb->nr_samples++;
		if (!trywait_sem(s))
			return;
	}
}

void *burn_thread(void *t)
{
	struct thread *th;
	sem_t *s;
	long i = (long)t;

	th = &threadlist[i];
	s = &th->sem.stopchild;

	while (1) {
		burn_loops(ud.loops_per_ms);
		if (!trywait_sem(s)) {
			post_sem(s);
			break;
		}
	}
	return NULL;
}

/* Have ud.cpu_load threads burn cpu continuously */
void emulate_burn(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	unsigned long i;
	long t;
	pthread_t burnthreads[ud.cpu_load];

	t = th->threadno;
	for (i = 0 ; i < ud.cpu_load ; i++)
		create_pthread(&burnthreads[i], NULL, burn_thread,
			(void*)(long) t);
	wait_sem(s);
	post_sem(&th->sem.stopchild);
	for (i = 0 ; i < ud.cpu_load ; i++)
		join_pthread(burnthreads[i], NULL);
}

/* Write a file the size of ram continuously */
void emulate_write(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	FILE *fp;
	char *name = "interbench.write";
	void *buf = NULL;
	struct stat statbuf;
	unsigned long mem;

	if (!(fp = fopen(name, "w")))
		terminal_error("fopen");
	if (stat(name, &statbuf) == -1)
		terminal_fileopen_error(fp, "stat");
	if (statbuf.st_blksize < MIN_BLK_SIZE)
		statbuf.st_blksize = MIN_BLK_SIZE;
	mem = ud.ram / (statbuf.st_blksize / 1024);	/* kilobytes to blocks */
	if (!(buf = calloc(1, statbuf.st_blksize)))
		terminal_fileopen_error(fp, "calloc");
	if (fclose(fp) == -1)
		terminal_error("fclose");

	while (1) {
		unsigned int i;

		if (!(fp = fopen(name, "w")))
			terminal_error("fopen");
		if (stat(name, &statbuf) == -1)
			terminal_fileopen_error(fp, "stat");
		for (i = 0 ; i < mem; i++) {
			if (fwrite(buf, statbuf.st_blksize, 1, fp) != 1)
				terminal_fileopen_error(fp, "fwrite");
			if (!trywait_sem(s))
				goto out;
		}
		if (fclose(fp) == -1)
			terminal_error("fclose");
	}

out:
	if (fclose(fp) == -1)
		terminal_error("fclose");
	if (remove(name) == -1)
		terminal_error("remove");
	sync_flush();
}

/* Read a file the size of ram continuously */
void emulate_read(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	char *name = "interbench.read";
	void *buf = NULL;
	struct stat statbuf;
	unsigned long bsize;
	int tmp;

	if ((tmp = open(name, O_RDONLY)) == -1)
		terminal_error("open");
	if (stat(name, &statbuf) == -1) 
		terminal_error("stat");
	bsize = statbuf.st_blksize;
	if (!(buf = malloc(bsize)))
		terminal_error("malloc");

	while (1) {
		int rd;

		/* 
		 * We have to read the whole file before quitting the load
		 * to prevent the data being cached for the next read. This
		 * is also the reason the file is the size of physical ram.
		 */
		while ((rd = Read(tmp , buf, bsize)) > 0);
		if(!trywait_sem(s))
			return;
		if (lseek(tmp, (off_t)0, SEEK_SET) == -1)
			terminal_error("lseek");
	}
}

#define RINGTHREADS	4

struct thread ringthreads[RINGTHREADS];

void *ring_thread(void *t)
{
	struct thread *th;
	struct sems *s;
	int i, post_to;

	i = (long)t;
	th = &ringthreads[i];
	s = &th->sem;
	post_to = i + 1;
	if (post_to == RINGTHREADS)
		post_to = 0;
	if (i == 0)
		post_sem(&s->ready);

	while (1) {
		wait_sem(&s->start);
		post_sem(&ringthreads[post_to].sem.start);
		if (!trywait_sem(&s->stop))
			goto out;
	}
out:	
	post_sem(&ringthreads[post_to].sem.start);
	post_sem(&s->complete);
	return NULL;
}

/* Create a ring of 4 processes that wake each other up in a circle */
void emulate_ring(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	int i;

	for (i = 0 ; i < RINGTHREADS ; i++) {
		init_all_sems(&ringthreads[i].sem);
		create_pthread(&ringthreads[i].pthread, NULL, 
			ring_thread, (void*)(long) i);
	}

	wait_sem(&ringthreads[0].sem.ready);
	post_sem(&ringthreads[0].sem.start);
	wait_sem(s);
	for (i = 0 ; i < RINGTHREADS ; i++)
		post_sem(&ringthreads[i].sem.stop);
	for (i = 0 ; i < RINGTHREADS ; i++) {
		wait_sem(&ringthreads[i].sem.complete);
		join_pthread(ringthreads[i].pthread, NULL);
	}
}

/* We emulate a compile by running burn, write and read threads simultaneously */
void emulate_compile(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	unsigned long i, threads[3];

	bzero(threads, 3 * sizeof(threads[0]));

	for (i = 0 ; i < THREADS ; i++) {
		if (strcmp(threadlist[i].label, "Burn") == 0)
			threads[0] = i;
		if (strcmp(threadlist[i].label, "Write") == 0)
			threads[1] = i;
		if (strcmp(threadlist[i].label, "Read") == 0)
			threads[2] = i;
	}
	for (i = 0 ; i < 3 ; i++) {
		if (!threads[i]) {
			fprintf(stderr, "Can't find all threads for compile load\n");
			exit(1);
		}
	}
	for (i = 0 ; i < 3 ; i++) {
		initialise_thread(threads[i]);
		start_thread(&threadlist[threads[i]]);
	}
	wait_sem(s);
	for (i = 0 ; i < 3 ; i++)
		stop_thread(&threadlist[threads[i]]);
}

int *grab_and_touch (char *block[], int i)
{
	block[i] = (char *) malloc(MB);
	if (!block[i])
		return NULL;
	return (memset(block[i], 1, MB));
}

/* We emulate a memory load by allocating and torturing 110% of available ram */
void emulate_memload(struct thread *th)
{
	sem_t *s = &th->sem.stop;
	unsigned long touchable_mem, i;
	char *mem_block[MAX_MEM_IN_MB];
	void *success;

	touchable_mem = compute_allocable_mem();
	/* loop until we're killed, frobbing memory in various perverted ways */
	while (1) {
		for (i = 0;  i < touchable_mem; i++) {
			success = grab_and_touch(mem_block, i);
			if (!success) {
				touchable_mem = i-1;
				break;
			}
		}
		if (!trywait_sem(s))
			goto out_freemem;
		for (i = 0;  i < touchable_mem; i++) {
			memcpy(mem_block[i], mem_block[(i + touchable_mem / 2) %
				touchable_mem], MB);
			if (!trywait_sem(s))
				goto out_freemem;
		}
		for (i = 0; i < touchable_mem; i++) {
			free(mem_block[i]);
		}	
		if (!trywait_sem(s))
			goto out;
	}
out_freemem:
	for (i = 0; i < touchable_mem; i++)
		free(mem_block[i]);
out:
	return;
}

struct thread hackthread;

void emulate_hackbench(struct thread *th)
{
	sem_t *s = &th->sem.stop;

	init_all_sems(&hackthread.sem);
	create_pthread(&hackthread.pthread, NULL, hackbench_thread, (void *) 0);

	wait_sem(s);

	post_sem(&hackthread.sem.stop);
	wait_sem(&hackthread.sem.complete);

	join_pthread(hackthread.pthread, NULL);
}

#define CUSTOM_INTERVAL	(ud.custom_interval)
#define CUSTOM_RUN	(ud.custom_run)
void emulate_custom(struct thread *th)
{
	unsigned long long deadline;
	sem_t *s = &th->sem.stop;
	struct timespec myts;

	th->decasecond_deadlines = 1000000 / CUSTOM_INTERVAL * 10;
	deadline = get_usecs(&myts);

	while (1) {
		deadline = periodic_schedule(th, CUSTOM_RUN, CUSTOM_INTERVAL,
			deadline);
		if (!trywait_sem(s))
			return;
	}
}

void *timekeeping_thread(void *t)
{
	struct thread *th;
	struct tk_thread *tk;
	struct sems *s;
	struct timespec myts;
	long i = (long)t;

	th = &threadlist[i];
	tk = &th->tkthread;
	s = &th->tkthread.sem;
	/*
	 * If this timekeeping thread is that of a benchmarked thread we run
	 * even higher priority than the benched thread is if running real
	 * time. Otherwise, the load timekeeping thread, which does not need
	 * accurate accounting remains SCHED_NORMAL;
	 */
	if (th->dt != &th->benchmarks[NOT_BENCHING])
		set_fifo(96);
	/* These values must be changed at the appropriate places or race */
	tk->sleep_interval = tk->slept_interval = 0;
	post_sem(&s->ready);

	while (1) {
		unsigned long start_time, now;

		if (!trywait_sem(&s->stop))
			goto out;
		wait_sem(&s->start);
		tk->slept_interval = 0;
		start_time = get_usecs(&myts);
		if (!trywait_sem(&s->stop))
			goto out;
		if (tk->sleep_interval) {
			unsigned long diff = 0;
			microsleep(tk->sleep_interval);
			now = get_usecs(&myts);
			/* now should always be > start_time but... */
			if (now > start_time) {
				diff = now - start_time;
				if (diff > tk->sleep_interval)
					tk->slept_interval = diff -
						tk->sleep_interval;
			}
		}
		tk->sleep_interval = 0;
		post_sem(&s->complete);
	}
out:
	return NULL;
}

/*
 * All the sleep functions such as nanosleep can only guarantee that they
 * sleep for _at least_ the time requested. We work around this by having
 * a high priority real time thread that accounts for the extra time slept
 * in nanosleep. This allows wakeup latency of the tested thread to be
 * accurate and reflect true scheduling delays.
 */
void *emulation_thread(void *t)
{
	struct thread *th;
	struct tk_thread *tk;
	struct sems *s, *tks;
	long i = (long)t;

	th = &threadlist[i];
	tk = &th->tkthread;
	s = &th->sem;
	tks = &tk->sem;
	init_all_sems(tks);

	/* Start the timekeeping thread */
	create_pthread(&th->tk_pthread, NULL, timekeeping_thread,
		(void*)(long) i);
	/* Wait for timekeeping thread to be ready */
	wait_sem(&tks->ready);

	/* Tell main we're ready to start*/
	post_sem(&s->ready);

	/* Wait for signal from main to start thread */
	wait_sem(&s->start);

	/* Start the actual function being benched/or running as load */
	th->name(th);

	/* Stop the timekeeping thread */
	post_sem(&tks->stop);
	post_sem(&tks->start);
	join_pthread(th->tk_pthread, NULL);

	/* Tell main we've finished */
	post_sem(&s->complete);
	return NULL;
}

/*
 * In an unoptimised loop we try to benchmark how many meaningless loops
 * per second we can perform on this hardware to fairly accurately
 * reproduce certain percentage cpu usage
 */
void calibrate_loop(void)
{
	unsigned long long start_time, loops_per_msec, run_time = 0;
	unsigned long loops;
	struct timespec myts;

	loops_per_msec = 100000;
redo:
	/* Calibrate to within 1% accuracy */
	while (run_time > 1010000 || run_time < 990000) {
		loops = loops_per_msec;
		start_time = get_nsecs(&myts);
		burn_loops(loops);
		run_time = get_nsecs(&myts) - start_time;
		loops_per_msec = (1000000 * loops_per_msec / run_time ? :
			loops_per_msec);
	}

	/* Rechecking after a pause increases reproducibility */
	sleep(1);
	loops = loops_per_msec;
	start_time = get_nsecs(&myts);
	burn_loops(loops);
	run_time = get_nsecs(&myts) - start_time;

	/* Tolerate 5% difference on checking */
	if (run_time > 1050000 || run_time < 950000)
		goto redo;

	ud.loops_per_ms = loops_per_msec;
}

void log_output(const char *format, ...) __attribute__ ((format(printf, 1, 2)));

/* Output to console +/- logfile */
void log_output(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	if (vprintf(format, ap) == -1)
		terminal_error("vprintf");
	va_end(ap);
	if (ud.log) {
		va_start(ap, format);
		if (vfprintf(ud.logfile, format, ap) == -1)
			terminal_error("vpfrintf");
		va_end(ap);
	}
	fflush(NULL);
}

/* Calculate statistics and output them */
void show_latencies(struct thread *th)
{
	struct data_table *tbj;
	struct tk_thread *tk;
	double average_latency, deadlines_met, samples_met, sd, max_latency;
	long double variance = 0;

	tbj = th->dt;
	tk = &th->tkthread;

	if (tbj->nr_samples > 1) {
		average_latency = tbj->total_latency / tbj->nr_samples;
		variance = (tbj->sum_latency_squared - (average_latency *
			average_latency) / tbj->nr_samples) / (tbj->nr_samples - 1);
		sd = sqrt((double)variance);
	} else {
		average_latency = tbj->total_latency;
		sd = 0.0;
	}

	/*
	 * Landing on the boundary of a deadline can make loaded runs appear
	 * to do more work than unloaded due to tiny duration differences.
	 */
	if (tbj->achieved_burns > 0)
		samples_met = (double)tbj->achieved_burns /
		    (double)(tbj->achieved_burns + tbj->missed_burns) * 100;
	else
		samples_met = 0.0;
	max_latency = tbj->max_latency;
	/* When benchmarking rt we represent the data in us */
	if (!ud.do_rt) {
		average_latency /= 1000;
		sd /= 1000;
		max_latency /= 1000;
	}
	if (tbj->deadlines_met == 0)
		deadlines_met = 0;
	else
		deadlines_met = (double)tbj->deadlines_met /
		    (double)(tbj->missed_deadlines + tbj->deadlines_met) * 100;

	/* Messy nonsense to format the output nicely */
	if (average_latency >= 100)
		log_output("%7.0f +/- ", average_latency);
	else
		log_output("%7.3g +/- ", average_latency);
	if (sd >= 100)
		log_output("%-9.0f", sd);
	else
		log_output("%-9.3g", sd);
	if (max_latency >= 100)
		log_output("%7.0f\t", max_latency);
	else
		log_output("%7.3g\t", max_latency);
	log_output("\t%4.3g", samples_met);
	if (!th->nodeadlines)
		log_output("\t%11.3g", deadlines_met);
	log_output("\n");
	sync_flush();
}

void create_read_file(void)
{
	unsigned int i;
	FILE *fp;
	char *name = "interbench.read";
	void *buf = NULL;
	struct stat statbuf;
	unsigned long mem, bsize;
	int tmp;

	if ((tmp = open(name, O_RDONLY)) == -1) {
		if (errno != ENOENT)
			terminal_error("open");
		goto write;
	}
	if (stat(name, &statbuf) == -1)
		terminal_error("stat");
	if (statbuf.st_blksize < MIN_BLK_SIZE)
		statbuf.st_blksize = MIN_BLK_SIZE;
	bsize = statbuf.st_blksize;
	if (statbuf.st_size / 1024 / bsize == ud.ram / bsize)
		return;
	if (remove(name) == -1)
		terminal_error("remove");
write:
	fprintf(stderr,"Creating file for read load...\n");
	if (!(fp = fopen(name, "w")))
		terminal_error("fopen");
	if (stat(name, &statbuf) == -1)
		terminal_fileopen_error(fp, "stat");
	if (statbuf.st_blksize < MIN_BLK_SIZE)
		statbuf.st_blksize = MIN_BLK_SIZE;
	bsize = statbuf.st_blksize;
	if (!(buf = calloc(1, bsize)))
		terminal_fileopen_error(fp, "calloc");
	mem = ud.ram / (bsize / 1024);	/* kilobytes to blocks */

	for (i = 0 ; i < mem; i++) {
		if (fwrite(buf, bsize, 1, fp) != 1)
			terminal_fileopen_error(fp, "fwrite");
	}
	if (fclose(fp) == -1)
		terminal_error("fclose");
	sync_flush();
}

void get_ram(void)
{
        struct vmstats vms;
        size_t vms_size = sizeof(vms);

        if (sysctlbyname("vm.vmstats", &vms, &vms_size, NULL, 0))
                terminal_error("sysctlbyname: vm.vmstats");

	ud.ram = vms.v_page_count * vms.v_page_size;
	ud.ram /= 1024; /* linux size is in kB */
	ud.swap = ud.ram; /* XXX: swap doesn't have to be the same as RAM */

	if( !ud.ram || !ud.swap ) {
		unsigned long i;
		fprintf(stderr, "\nCould not get memory or swap size. ");
		fprintf(stderr, "Will not perform mem_load\n");
		for (i = 0 ; i < THREADS ; i++) {
			if (strcmp(threadlist[i].label, "Memload") == 0) {
				threadlist[i].load = 0;
				threadlist[i].rtload = 0;
			}
		}
	}
}

void get_logfilename(void)
{
	struct tm *mytm;
	struct utsname buf;
	time_t t;
	int year, month, day, hours, minutes;

	time(&t);
	if (uname(&buf) == -1)
		terminal_error("uname");
	if (!(mytm = localtime(&t)))
		terminal_error("localtime");
	year = mytm->tm_year + 1900;
	month = mytm->tm_mon + 1;
	day = mytm->tm_mday;
	hours = mytm->tm_hour;
	minutes = mytm->tm_min;
	strncpy(ud.unamer, buf.release, MAX_UNAME_LENGTH);

	sprintf(ud.datestamp, "%2d%02d%02d%02d%02d",
		year, month, day, hours, minutes);
	snprintf(ud.logfilename, MAX_LOG_LENGTH, "%s.log", ud.unamer);
}

void start_thread(struct thread *th)
{
	post_sem(&th->sem.start);
}

void stop_thread(struct thread *th)
{
	post_sem(&th->sem.stop);
	wait_sem(&th->sem.complete);

	/* Kill the thread */
	join_pthread(th->pthread, NULL);
}

void init_sem(sem_t *sem)
{
	if (sem_init(sem, 0, 0))
		terminal_error("sem_init");
}

void init_all_sems(struct sems *s)
{
	/* Initialise the semaphores */
	init_sem(&s->ready);
	init_sem(&s->start);
	init_sem(&s->stop);
	init_sem(&s->complete);
	init_sem(&s->stopchild);
}

void initialise_thread(int i)
{
	struct thread *th = &threadlist[i];

	init_all_sems(&th->sem);
	/* Create the threads. Yes, the (long) cast is fugly but it's safe*/
	create_pthread(&th->pthread, NULL, emulation_thread, (void*)(long)i);

	wait_sem(&th->sem.ready);
	/*
	 * We set this pointer generically to NOT_BENCHING and set it to the
	 * benchmarked array entry only on benched threads.
	 */
	th->dt = &th->benchmarks[NOT_BENCHING];
	initialise_thread_data(th->dt);
	
}

/* A pseudo-semaphore for processes using a pipe */
void wait_on(int pype)
{
	int retval, buf = 0;

	retval = Read(pype, &buf, sizeof(buf));
	if (retval == 0) {
		fprintf(stderr, "\nread returned 0\n");
		exit (1);
	}
}

void wakeup_with(int pype)
{
	int retval, buf = 1;

	retval = Write(pype, &buf, sizeof(buf));
	if (retval == 0) {
		fprintf(stderr, "\nwrite returned 0\n");
		exit (1);
	}
}

void run_loadchild(int j)
{
	struct thread *thj;
	thj = &threadlist[j];

	set_nice(ud.load_nice);
	initialise_thread(j);

	/* Tell main we're ready */
	wakeup_with(l2m[1]);

	/* Main tells us we're ready */
	wait_on(m2l[0]);
	start_thread(thj);

	/* Tell main we received the start and are running */
	wakeup_with(l2m[1]);

	/* Main tells us to stop */
	wait_on(m2l[0]);
	stop_thread(thj);

	/* Tell main we've finished */
	wakeup_with(l2m[1]);
	exit (0);
}

void run_benchchild(int i, int j)
{
	struct thread *thi;

	thi = &threadlist[i];

	set_nice(ud.bench_nice);
	if (ud.do_rt)
		set_mlock();
	initialise_thread(i);
	/* Point the data table to the appropriate load being tested */
	thi->dt = &thi->benchmarks[j];
	initialise_thread_data(thi->dt);
	if (ud.do_rt)
		set_thread_fifo(thi->pthread, 95);
	
	/* Tell main we're ready */
	wakeup_with(b2m[1]);

	/* Main tells us we're ready */
	wait_on(m2b[0]);
	start_thread(thi);

	/* Tell main we have started */
	wakeup_with(b2m[1]);

	/* Main tells us to stop */
	wait_on(m2b[0]);
	stop_thread(thi);

	if (ud.do_rt) {
		set_thread_normal(thi->pthread);
		set_munlock();
	}
	show_latencies(thi);

	/* Tell main we've finished */
	wakeup_with(b2m[1]);
	exit(0);
}

void bench(int i, int j)
{
	pid_t bench_pid, load_pid;

	if ((load_pid = fork()) == -1)
		terminal_error("fork");
	if (!load_pid)
		run_loadchild(j);

	/* Wait for load process to be ready */

	wait_on(l2m[0]);
	if ((bench_pid = fork()) == -1)
		terminal_error("fork");
	if (!bench_pid)
		run_benchchild(i, j);

	/* Wait for bench process to be ready */
	wait_on(b2m[0]);

	/* 
	 * We want to be higher priority than everything to signal them to
	 * stop and we lock our memory if we can as well
	 */
	set_fifo(99);
	set_mlock();

	/* Wakeup the load process */
	wakeup_with(m2l[1]);
	/* Load tells it has received the first message and is running */
	wait_on(l2m[0]);

	/* After a small delay, wake up the benched process */
	sleep(1);
	wakeup_with(m2b[1]);

	/* Bench tells it has received the first message and is running */
	wait_on(b2m[0]);
	microsleep(ud.duration * 1000000);

	/* Tell the benched process to stop its threads and output results */
	wakeup_with(m2b[1]);

	/* Tell the load process to stop its threads */
	wakeup_with(m2l[1]);

	/* Return to SCHED_NORMAL */
	set_normal();
	set_munlock();

	/* Wait for load and bench processes to terminate */
	wait_on(l2m[0]);
	wait_on(b2m[0]);
}

void init_pipe(int *pype)
{
	if (pipe(pype) == -1)
		terminal_error("pipe");
}

void init_pipes(void)
{
	init_pipe(m2l);
	init_pipe(l2m);
	init_pipe(m2b);
	init_pipe(b2m);
}

void usage(void)
{
	/* Affinity commented out till working on all architectures */
	fprintf(stderr, "interbench v " INTERBENCH_VERSION " by Con Kolivas\n");
	fprintf(stderr, "interbench [-l <int>] [-L <int>] [-t <int] [-B <int>] [-N <int>]\n");
	fprintf(stderr, "\t[-b] [-c] [-r] [-C <int> -I <int>] [-m <comment>]\n");
	fprintf(stderr, "\t[-w <load type>] [-x <load type>] [-W <bench>] [-X <bench>]\n");
	fprintf(stderr, "\t[-h]\n\n");
	fprintf(stderr, " -l\tUse <int> loops per sec (default: use saved benchmark)\n");
	fprintf(stderr, " -L\tUse cpu load of <int> with burn load (default: 4)\n");
	fprintf(stderr, " -t\tSeconds to run each benchmark (default: 30)\n");
	fprintf(stderr, " -B\tNice the benchmarked thread to <int> (default: 0)\n");
	fprintf(stderr, " -N\tNice the load thread to <int> (default: 0)\n");
	//fprintf(stderr, " -u\tImitate uniprocessor\n");
	fprintf(stderr, " -b\tBenchmark loops_per_ms even if it is already known\n");
	fprintf(stderr, " -c\tOutput to console only (default: use console and logfile)\n");
	fprintf(stderr, " -r\tPerform real time scheduling benchmarks (default: non-rt)\n");
	fprintf(stderr, " -C\tUse <int> percentage cpu as a custom load (default: no custom load)\n");
	fprintf(stderr, " -I\tUse <int> microsecond intervals for custom load (needs -C as well)\n");
	fprintf(stderr, " -m\tAdd <comment> to the log file as a separate line\n");
	fprintf(stderr, " -w\tAdd <load type> to the list of loads to be tested against\n");
	fprintf(stderr, " -x\tExclude <load type> from the list of loads to be tested against\n");
	fprintf(stderr, " -W\tAdd <bench> to the list of benchmarks to be tested\n");
	fprintf(stderr, " -X\tExclude <bench> from the list of benchmarks to be tested\n");
	fprintf(stderr, " -h\tShow this help\n");
	fprintf(stderr, "\nIf run without parameters interbench will run a standard benchmark\n\n");
}

#ifdef DEBUG
void deadchild(int crap)
{
	pid_t retval;
	int status;

	crap = 0;

	if ((retval = waitpid(-1, &status, WNOHANG)) == -1) {
		if (errno == ECHILD)
			return;
		terminal_error("waitpid");
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return;
	fprintf(stderr, "\nChild terminated abnormally ");
	if (WIFSIGNALED(status))
		fprintf(stderr, "with signal %d", WTERMSIG(status));
	fprintf(stderr, "\n");
	exit (1);
}
#endif

int load_index(const char* loadname)
{
	int i;

	for (i = 0 ; i < THREADS ; i++)
		if (strcasecmp(loadname, threadlist[i].label) == 0)
			return i;
	return -1;
}

inline int bit_is_on(const unsigned int mask, int index)
{
	return (mask & (1 << index)) != 0;
}

inline void set_bit_on(unsigned int *mask, int index)
{
	*mask |= (1 << index);
}

int main(int argc, char **argv)
{
	unsigned long custom_cpu = 0;
	int q, i, j, affinity, benchmark = 0;
	unsigned int selected_loads = 0;
	unsigned int excluded_loads = 0;
	unsigned int selected_benches = 0;
	unsigned int excluded_benches = 0;
	FILE *fp;
	/* 
	 * This file stores the loops_per_ms to be reused in a filename that
	 * can't be confused
	 */
	char *fname = "interbench.loops_per_ms";
	char *comment = NULL;
#ifdef DEBUG
	feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
	if (signal(SIGCHLD, deadchild) == SIG_ERR)
		terminal_error("signal");
#endif

	while ((q = getopt(argc, argv, "hl:L:B:N:ut:bcnrC:I:m:w:x:W:X:")) != -1) {
		switch (q) {
			case 'h':
				usage();
				return (0);
			case 'l':
				ud.loops_per_ms = atoi(optarg);
				break;
			case 't':
				ud.duration = atoi(optarg);
				break;
			case 'L':
				ud.cpu_load = atoi(optarg);
				break;
			case 'B':
				ud.bench_nice = atoi(optarg);
				break;
			case 'N':
				ud.load_nice = atoi(optarg);
				break;
			case 'u':
				affinity = 1;
				break;
			case 'b':
				benchmark = 1;
				break;
			case 'c':
				ud.log = 0;
				break;
			case 'r':
				ud.do_rt = 1;
				break;
			case 'C':
				custom_cpu = (unsigned long)atol(optarg);
				break;
			case 'I':
				ud.custom_interval = atol(optarg);
				break;
			case 'm':
				comment = optarg;
				break;
			case 'w':
				i = load_index(optarg);
				if (i == -1) {
					fprintf(stderr, "Unknown load \"%s\"\n", optarg);
					return (-2);
				}
				set_bit_on(&selected_loads, i);
				break;
			case 'x':
				i = load_index(optarg);
				if (i == -1) {
					fprintf(stderr, "Unknown load \"%s\"\n", optarg);
					return (-2);
				}
				set_bit_on(&excluded_loads, i);
				break;
			case 'W':
				i = load_index(optarg);
				if (i == -1) {
					fprintf(stderr, "Unknown bench \"%s\"\n", optarg);
					return (-2);
				}
				set_bit_on(&selected_benches, i);
				break;
			case 'X':
				i = load_index(optarg);
				if (i == -1) {
					fprintf(stderr, "Unknown bench \"%s\"\n", optarg);
					return (-2);
				}
				set_bit_on(&excluded_benches, i);
				break;
			default:
				usage();
				return (1);
		}
	}
	argc -= optind;
	argv += optind;
	/* default is all loads */
	if (selected_loads == 0)
		selected_loads = (unsigned int)-1;
	selected_loads &= ~excluded_loads;
	/* default is all benches */
	if (selected_benches == 0)
		selected_benches = (unsigned int)-1;
	selected_benches &= ~excluded_benches;

	if (!test_fifo()) {
		fprintf(stderr, "Unable to get SCHED_FIFO (real time scheduling).\n");
		fprintf(stderr, "You either need to run this as root user or have support for real time RLIMITS.\n");
		if (ud.do_rt) {
			fprintf(stderr, "Real time tests were requested, aborting.\n");
			exit (1);
		}
		fprintf(stderr, "Results will be unreliable.\n");
	}
	if (!ud.cpu_load) {
		fprintf(stderr, "Invalid cpu load\n");
		exit (1);
	}

	if ((custom_cpu && !ud.custom_interval) ||
		(ud.custom_interval && !custom_cpu) ||
		custom_cpu > 100) {
			fprintf(stderr, "Invalid custom values, aborting.\n");
			exit (1);
	}

	if (custom_cpu && ud.custom_interval) {
		ud.custom_run = ud.custom_interval * custom_cpu / 100;
		threadlist[CUSTOM].bench = 1;
		threadlist[CUSTOM].load = 1;
		threadlist[CUSTOM].rtbench = 1;
		threadlist[CUSTOM].rtload = 1;
	}

	/*FIXME Affinity commented out till working on all architectures */
#if 0
	if (affinity) {
#ifdef CPU_SET	/* Current glibc expects cpu_set_t */
		cpu_set_t cpumask;

		CPU_ZERO(&cpumask);
		CPU_SET(0, &cpumask);
#else		/* Old glibc expects unsigned long */
		unsigned long cpumask = 1;
#endif
		if (sched_setaffinity(0, sizeof(cpumask), &cpumask) == -1) {
			if (errno != EPERM)
				terminal_error("sched_setaffinity");
			fprintf(stderr, "could not set cpu affinity\n");
		}
	}
#endif

	/* Make benchmark a multiple of 10 seconds for proper range of X loads */
	if (ud.duration % 10)
		ud.duration += 10 - ud.duration % 10;

	if (benchmark)
		ud.loops_per_ms = 0;
	/* 
	 * Try to get loops_per_ms from command line first, file second, and
	 * benchmark if not available.
	 */
	if (!ud.loops_per_ms) {
		if (benchmark)
			goto bench;
		if ((fp = fopen(fname, "r"))) {
			fscanf(fp, "%lu", &ud.loops_per_ms);
			if (fclose(fp) == -1)
				terminal_error("fclose");
			if (ud.loops_per_ms) {
				fprintf(stderr,
					"%lu loops_per_ms read from file interbench.loops_per_ms\n",
					ud.loops_per_ms);
				goto loops_known;
			}
		} else
			if (errno != ENOENT)
				terminal_error("fopen");
bench:
		fprintf(stderr, "loops_per_ms unknown; benchmarking...\n");

		/*
		 * To get as accurate a loop as possible we time it running
		 * SCHED_FIFO if we can
		 */
		set_fifo(99);
		calibrate_loop();
		set_normal();
	} else
		fprintf(stderr, "loops_per_ms specified from command line\n");

	if (!(fp = fopen(fname, "w"))) {
		if (errno != EACCES)	/* No write access is not terminal */
			terminal_error("fopen");
		fprintf(stderr, "Unable to write to file interbench.loops_per_ms\n");
		goto loops_known;
	}
	fprintf(fp, "%lu", ud.loops_per_ms);
	fprintf(stderr, "%lu loops_per_ms saved to file interbench.loops_per_ms\n",
		ud.loops_per_ms);
	if (fclose(fp) == -1)
		terminal_error("fclose");

loops_known:
	get_ram();
	get_logfilename();
	create_read_file();
	init_pipes();

	if (ud.log && !(ud.logfile = fopen(ud.logfilename, "a"))) {
		if (errno != EACCES)
			terminal_error("fopen");
		fprintf(stderr, "Unable to write to logfile\n");
		ud.log = 0;
	}
	log_output("\n");
	log_output("Using %lu loops per ms, running every load for %d seconds\n",
		ud.loops_per_ms, ud.duration);
	log_output("Benchmarking kernel %s at datestamp %s\n",
		ud.unamer, ud.datestamp);
	if (comment)
		log_output("Comment: %s\n", comment);
	log_output("\n");

	for (i = 0 ; i < THREADS ; i++)
		threadlist[i].threadno = i;

	for (i = 0 ; i < THREADS ; i++) {
		struct thread *thi = &threadlist[i];
		int *benchme;

		if (ud.do_rt)
			benchme = &threadlist[i].rtbench;
		else
			benchme = &threadlist[i].bench;

		if (!*benchme || !bit_is_on(selected_benches, i))
			continue;

		log_output("--- Benchmarking simulated cpu of %s ", threadlist[i].label);
		if (ud.do_rt)
			log_output("real time ");
		else if (ud.bench_nice)
			log_output("nice %d ", ud.bench_nice);
		log_output("in the presence of simulated ");
		if (ud.load_nice)
			log_output("nice %d ", ud.load_nice);
		log_output("---\n");

		log_output("Load");
		if (ud.do_rt)
			log_output("\tLatency +/- SD (us)");
		else
			log_output("\tLatency +/- SD (ms)");
		log_output("  Max Latency ");
		log_output("  %% Desired CPU");
		if (!thi->nodeadlines)
			log_output("  %% Deadlines Met");
		log_output("\n");

		for (j = 0 ; j < THREADS ; j++) {
			struct thread *thj = &threadlist[j];

			if (j == i || !bit_is_on(selected_loads, j) ||
				(!threadlist[j].load && !ud.do_rt) ||
				(!threadlist[j].rtload && ud.do_rt))
					continue;
			log_output("%s\t", thj->label);
			sync_flush();
			bench(i, j);
		}
		log_output("\n");
	}
	log_output("\n");
	if (ud.log)
		fclose(ud.logfile);

	return 0;
}
