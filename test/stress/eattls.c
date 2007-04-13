/*
 * EATTLS.C
 *
 *	Rapidly switch between threads with different TLS pointers to
 *	test that the operating system properly switches the TLS segment.
 *
 * $DragonFly: src/test/stress/eattls.c,v 1.3 2007/04/13 12:12:27 corecode Exp $
 */

#include <sys/types.h>
#include <sys/tls.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BIGSIZE	(16 * 1024 * 1024)

struct test {
	int random1;
	int random2;
};

static int getdata(int offset);

int
main(int ac, char **av)
{
    struct tls_info info;
    struct test *test;
    char *buf;
    int gs;
    int random1;
    int random2;

    srandomdev();
    random1 = random();
    random2 = random();

    buf = malloc(BIGSIZE);
    buf += random() % (BIGSIZE - sizeof(struct test));
    test = (void *)buf;
    info.base = buf;
    info.size = sizeof(struct test);
    if ((gs = set_tls_area(2, &info, sizeof(info))) < 0) {
	perror("set_tls_area");
	exit(1);
    }
    test->random1 = random1;
    test->random2 = random2;
    printf("setting %%gs to 0x%02x segment at %p\n", gs, test);
    __asm __volatile("mov %0,%%gs" : : "g" (gs));
    for (;;) {
	if (getdata(0) != random1 || getdata(4) != random2)
	    printf("data verification failed!\n");
	if (random() % 1000 > 500)
	    usleep(random() % 1000);
    }

    return(0);
}

static int
getdata(int offset)
{
    int rv;
    __asm __volatile("movl %%gs:(%0),%%eax; movl %%eax,%1" : 
		     "+r" (offset) : "m" (rv) : "ax");
    return (rv);
}

