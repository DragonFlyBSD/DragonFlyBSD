/*
 * TUXLOAD.C
 *
 * (c)Copyright 2012 Antonio Huete Jimenez <tuxillo@quantumachine.net>,
 *    this code is hereby placed in the public domain.
 *
 * As a safety the directory 'tmpfiles/' must exist.  This program will
 * create 500 x 8MB files in tmpfiles/*, memory map the files MAP_SHARED,
 * R+W, make random modifications, and msync() in a loop.
 *
 * The purpose is to stress the VM system.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <err.h>

#define NFILES 500

static void randomfill(int fd);

int
main(int argc, char *argv[])
{

        int i;
        size_t size;
        long jump;
        int fd[NFILES];
        struct stat st[NFILES];
        char name[128];
        char *p[NFILES];

        for (i = 0; i <  NFILES; i++) {
                snprintf(name, 128, "tmpfiles/file%d", i);
                if ((fd[i] = open(name, O_RDWR)) < 1) {
			if ((fd[i] = open(name, O_RDWR | O_CREAT, 0644)) < 1)
				err(1, "open");
			randomfill(fd[i]);
		}

                if ((fstat(fd[i], &st[i])) == -1)
                        err(1, "fstat");

                size = st[i].st_size;
                p[i] = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd[i], 0);
                if (p[i] == MAP_FAILED)
                        err(1, "mmap");

                for (jump = 0; jump < size; jump += 65535) {
                        p[i][jump] = jump + i;
                }

		/*
		 * This is a funny bug. MS_SYNC and 0 are reversed (i.e.
		 * the msync() call wasn't written correctly), but the
		 * broken msync() leads to a heavier VM load as a veritible
		 * ton of dirty file-backed pages wind up accumulating in
		 * the memory maps.
		 *
		 * So we leave it as is for now.
		 */
                if ((msync(*p, MS_SYNC, 0)) == -1) {
                        printf("%s: %d %p\n", name, i, *p);
                        err(1, "msync");

                }
        }
        return 0;
}

static void
randomfill(int fd)
{
	char buf[32768];
	int i;

	srandomdev();
	for (i = 0; i < 32768; ++i)
		buf[i] = random();
	for (i = 0; i < 8192; i += 32)	/* 8MB */
		write(fd, buf, 32768);
	fsync(fd);
	lseek(fd, 0L, 0);
}
