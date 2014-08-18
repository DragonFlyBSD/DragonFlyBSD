/*
 * cc evfilt_user.c -pthread -I/usr/src/sys/ -o /tmp/evfilt_user
 */

#include <sys/types.h>
#include <sys/event.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static void *domaster(void *arg);
static void *doslave(void *arg);

int
main(int ac, char **avj)
{
    int fd;
    int r;
    struct kevent kev;
    pthread_t td1;
    pthread_t td2;

    fd = kqueue();
    EV_SET(&kev, 1, EVFILT_USER, EV_ADD|EV_CLEAR, 0, 0, 0);
    r = kevent(fd, &kev, 1, NULL, 0, NULL);
    printf("r = %d\n", r);
    pthread_create(&td1, NULL, domaster, (void *)(intptr_t)fd);
    sleep(1);
    pthread_create(&td2, NULL, doslave, (void *)(intptr_t)fd);
    pthread_join(td1, NULL);
    pthread_join(td2, NULL);
}

static void *
domaster(void *arg)
{
	struct kevent kev;
	int fd = (int)(intptr_t)arg;
	int r;

	printf("master running\n");
	for (;;) {
		EV_SET(&kev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, 0);
		r = kevent(fd, &kev, 1, NULL, 0, NULL);
		printf("issued trigger %d\n", r);
		sleep(1);
	}

	return NULL;
}

static void *
doslave(void *arg)
{
	struct kevent kev;
	int fd = (int)(intptr_t)arg;
	int r;

	printf("slave running\n");
	for (;;) {
		r = kevent(fd, NULL, 0, &kev, 1, NULL);
		printf("received %d\n", r);
		/*
		EV_SET(&kev, 1, EVFILT_USER, EV_CLEAR, 0, 0, 0);
		r = kevent(fd, &kev, 1, NULL, 0, NULL);
		*/
	}
	return NULL;
}
