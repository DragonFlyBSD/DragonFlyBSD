/*
 * loop4.c
 *
 * Used to test syscall mpsafeness, with editing
 *
 * $DragonFly: src/test/sysperf/loop4.c,v 1.1 2008/05/09 15:49:42 dillon Exp $
 */

#include "blib.h"
#include <fcntl.h>

#define INNER 100

void doloop(int count);
void func_nop1(void);
void func_nop2(void);

int
main(int ac, char **av)
{
    int i;
    int fd;
    int count;
    int nfork;
    pid_t pid;

    if (ac > 1)
	nfork = strtoul(av[1], NULL, 0);

    printf("SMP contention, dup() + close(), Run just one\n");

    start_timing();
    count = 0;
    while (stop_timing(0, NULL) == 0) {
	doloop(1000);
	count += 1000;
    }
    printf("loop %d times\n", count);

    start_timing();
    for (i = 1; i < nfork; ++i) {
	    if (fork() == 0) {
		doloop(count);
		_exit(1);
	    }
    }
    doloop(count);
    while (wait(NULL) > 0)
	;
    stop_timing(count * INNER, "loop2/2xfork");
    return(0);
}

void
doloop(int count)
{
    int i;
    int j;
    int fd = open("/usr/share/dict/words", O_RDONLY);
    register void (*func)(void) = func_nop1;

    if (fd < 0)
	perror("open");
    for (i = count; i > 0; --i) {
	for (j = INNER; j > 0; --j) {
		getuid();
	}
    }
    close(fd);
}

void
func_nop1(void)
{
}

void
func_nop2(void)
{
}

