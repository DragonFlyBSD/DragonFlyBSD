/*
 * read1.c
 *
 * Tests reading 1 byte at a time from a file.
 *
 * $DragonFly: src/test/sysperf/read1.c,v 1.1 2004/08/13 02:28:42 dillon Exp $
 */

#include "blib.h"
#include <errno.h>
#include <sys/resource.h>
#include <sys/fcntl.h>

char Buf[8192];

int
main(int ac, char **av)
{
    int bytes;
    int fd;
    int i;
    int j;
    char c;
    char *ptr;
    const char *filename;

    if (ac == 1) {
	fprintf(stderr, "%s filesize[k,m]\n", av[0]);
	exit(1);
    }
    bytes = strtol(av[1], &ptr, 0);
    if (*ptr == 'k' || *ptr == 'K') {
	bytes *= 1024;
    } else if (*ptr == 'm' || *ptr == 'M') {
	bytes *= 1024 * 1024;
    } else if (*ptr) {
	fprintf(stderr, "Illegal numerical suffix: %s\n", ptr);
	exit(1);
    }
    if (bytes <= 0) {
	fprintf(stderr, "I can't handle %d sized buffers\n", bytes);
	exit(1);
    }

    filename = "read1.dat";
    fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
    if (fd < 0) {
	if (errno == EROFS) {
	    filename = "/tmp/read1.dat";
	    fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
	}
	if (fd < 0) {
	    perror("open()");
	    exit(1);
	}
    }
    for (i = 0; i < bytes; i += sizeof(Buf)) {
	int n = (bytes - i > sizeof(Buf)) ? sizeof(Buf) : bytes - i;
	if (write(fd, Buf, n) != n) {
	    close(fd);
	    perror("write()");
	    remove(filename);
	    exit(1);
	}
    }
    fsync(fd);
    fsync(fd);
    sleep(1);
    fsync(fd);
    lseek(fd, 0L, 0);
    sleep(1);

    start_timing();
    i = 0;
    while (stop_timing(0, NULL) == 0) {
	for (j = 0; j < 256 * 1024; ++j) {
	    if (read(fd, &c, 1) != 1)
		lseek(fd, 0L, 0);
	}
	i += j;
    }
    lseek(fd, 0L, 0);
    start_timing();
    for (j = 0; j < i; ++j) {
	if (read(fd, &c, 1) != 1)
	    lseek(fd, 0L, 0);
    }
    stop_timing(j, "read 1char from file:");
    remove(filename);
    return(0);
}

