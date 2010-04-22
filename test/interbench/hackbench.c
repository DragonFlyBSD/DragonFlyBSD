/* Test groups of 20 processes spraying to 20 receivers */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/poll.h>
#include "interbench.h"

#define DATASIZE 100
#define LOOPS	100
#define NUM_FDS	20

static inline void barf(const char *msg)
{
	terminal_error(msg);
}

static void fdpair(int fds[2])
{
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
		barf("Creating fdpair");
}

/* Block until we're ready to go */
static void ready(int ready_out, int wakefd)
{
	char dummy;
	struct pollfd pollfd = { .fd = wakefd, .events = POLLIN };

	/* Tell them we're ready. */
	if (write(ready_out, &dummy, 1) != 1)
		barf("CLIENT: ready write");

	/* Wait for "GO" signal */
	if (poll(&pollfd, 1, -1) != 1)
		barf("poll");
}

/* Sender sprays LOOPS messages down each file descriptor */
static void sender(int out_fd[NUM_FDS],
		   int ready_out,
		   int wakefd)
{
	char data[DATASIZE];
	unsigned int i, j;

	ready(ready_out, wakefd);

	/* Now pump to every receiver. */
	for (i = 0; i < LOOPS; i++) {
		for (j = 0; j < NUM_FDS; j++) {
			int ret;
			unsigned long done = 0;

		again:
			ret = write(out_fd[j], data + done, sizeof(data)-done);
			if (ret < 0)
				barf("SENDER: write");
			done += ret;
			if (done < sizeof(data))
				goto again;
		}
	}
}

/* One receiver per fd */
static void receiver(unsigned int num_packets,
		     int in_fd,
		     int ready_out,
		     int wakefd)
{
	unsigned int i;

	/* Wait for start... */
	ready(ready_out, wakefd);

	/* Receive them all */
	for (i = 0; i < num_packets; i++) {
		char data[DATASIZE];
		int ret, done = 0;

	again:
		ret = Read(in_fd, data + done, DATASIZE - done);
		done += ret;
		if (done < DATASIZE)
			goto again;
	}
}

/* One group of senders and receivers */
static unsigned int group(int ready_out,
			  int wakefd)
{
	unsigned int i;
	int out_fds[NUM_FDS];

	for (i = 0; i < NUM_FDS; i++) {
		int fds[2];

		/* Create the pipe between client and server */
		fdpair(fds);

		/* Fork the receiver. */
		switch (fork()) {
		case -1: barf("fork()");
		case 0:
			close(fds[1]);
			receiver(NUM_FDS*LOOPS, fds[0], ready_out, wakefd);
			exit(0);
		}

		out_fds[i] = fds[1];
		close(fds[0]);
	}

	/* Now we have all the fds, fork the senders */
	for (i = 0; i < NUM_FDS; i++) {
		switch (fork()) {
		case -1: barf("fork()");
		case 0:
			sender(out_fds, ready_out, wakefd);
			exit(0);
		}
	}

	/* Close the fds we have left */
	for (i = 0; i < NUM_FDS; i++)
		close(out_fds[i]);

	/* Return number of children to reap */
	return NUM_FDS * 2;
}

void *hackbench_thread(void *t)
{
	unsigned int i, num_groups, total_children;
	int readyfds[2], wakefds[2];
	char dummy;

	num_groups = 50;
	t = 0;

	fdpair(readyfds);
	fdpair(wakefds);
	
	while (1) {
		total_children = 0;
		for (i = 0; i < num_groups; i++)
			total_children += group(readyfds[1], wakefds[0]);
	
		/* Wait for everyone to be ready */
		for (i = 0; i < total_children; i++)
			if (Read(readyfds[0], &dummy, 1) != 1)
				barf("Reading for readyfds");
	
		/* Kick them off */
		if (write(wakefds[1], &dummy, 1) != 1)
			barf("Writing to start them");
	
		/* Reap them all */
		for (i = 0; i < total_children; i++) {
			int status;
			wait(&status);
			if (!WIFEXITED(status))
				exit(1);
		}
		if (!trywait_sem(&hackthread.sem.stop))
			break;
	}

	post_sem(&hackthread.sem.complete);
	return NULL;
}
