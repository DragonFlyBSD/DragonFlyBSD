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
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>

static void randomfill(int fd);
static int mmap01(void *);
static int mmap02(void *);
static int build_files(void);

int opt_verbose;
int opt_testno;
int opt_nofiles;
int opt_mbfile;
int opt_count;
int fliparg;

int *fd;
struct stat *st;
char **pp;

static const struct test {
	char testdesc[128];
	int (*testfn)(void *);
} testlist[] = {
	{ "mmap01 - Massive mmap / msync (flushing all pages)", mmap01 },
	{ "mmap02 - Massive mmap / msync (flushing only specified pages)", mmap02 },
	{ "", NULL }
};

static int
mmap01(void *arg)
{
	int i;
	int *bug = arg;
	long jump;
	size_t size, len;

	if ((build_files()) != 0)
		err(1, "Failed to create the files");

	if (opt_verbose)
		printf("\n");

        for (i = 0; i <  opt_nofiles; i++) {

		if (opt_verbose) {
			fflush(stdout);
			fprintf(stdout, "\rDoing mmap() + msync() [%d/%d] ", i+1, opt_nofiles);
		}
                size = st[i].st_size;
                pp[i] = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd[i], 0);
                if (pp[i] == MAP_FAILED)
                        err(1, "mmap");

                for (jump = 0; jump < size; jump += 65535) {
                        pp[i][jump] = jump + i;
                }

		if (fliparg)
			len = MS_SYNC;
		else
			len = 0;

                if ((msync(pp[i], len, MS_SYNC)) == -1) {
                        printf("fd %d %p\n", fd[i], pp[i]);
                        err(1, "msync");

                }
        }
	printf("\n");

	return 0;
}

static int
mmap02(void *arg)
{
	fliparg = 1;
	mmap01(&fliparg);

	return 0;
}

static void
usage(void)
{
	int i;
	const struct test *tp;

	printf("tuxload: [-v] [-c count] [-m megs_per_file] [-n no_of_files] [-t test_to_run] \n"
	    "Available tests: \n");

	for (tp = testlist; tp->testfn != NULL; tp++)
		printf("\t%s\n", tp->testdesc);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int forever;
	char c;

	opt_verbose = opt_testno = 0;
	opt_nofiles = 500;
	opt_mbfile = 8;
	opt_count = 1;
	fliparg = 0;
	forever = 0;

        while ((c = getopt(argc, argv, "n:t:m:c:v")) != -1) {
		switch(c) {
		case 'v':
			opt_verbose++;
			break;
		case 'n':
			opt_nofiles = (int)strtol(optarg, NULL, 0);
			break;
		case 't':
			opt_testno = (int)strtol(optarg, NULL, 0);
			opt_testno--;
			break;
		case 'm':
			opt_mbfile = (int)strtol(optarg, NULL, 0);
			break;
		case 'c':
			opt_count = (int)strtol(optarg, NULL, 0);
			if (opt_count == 0)
				forever++;
			break;
		default:
			usage();
			;
		}
	}
        argc -= optind;
        argv += optind;

	if (argc != 0)
		usage();

	st = malloc(opt_nofiles * sizeof(*st));
	fd = malloc(opt_nofiles * sizeof(*fd));
	pp = malloc(opt_nofiles * sizeof(*pp));

	while (opt_count-- || forever)
		testlist[opt_testno].testfn(0);

        return 0;
}

static int
build_files(void)
{
	char name[128];
	int i;
	int error;

        for (i = 0, error = 0; i <  opt_nofiles; i++) {
                snprintf(name, 128, "tmpfiles/file%d", i);
                if ((fd[i] = open(name, O_RDWR)) < 1) {
			if ((fd[i] = open(name, O_RDWR | O_CREAT, 0644)) < 1) {
				error = errno;
				break;
			}
			randomfill(fd[i]);
		}
                if ((fstat(fd[i], &st[i])) == -1) {
			error = errno;
			break;
		}
		if (opt_verbose) {
			fprintf(stdout, "\rFile creation, random data filled [%d/%d] ", i+1, opt_nofiles);
			fflush(stdout);
		}
	}

	return error;
}

static void
randomfill(int fd)
{
	char buf[32768];
	long tot;
	int i;

	srandomdev();
	tot = opt_mbfile * 1024L;
	for (i = 0; i < 32768; ++i)
		buf[i] = random();
	for (i = 0; i < tot; i += 32)	/* 8MB by default */
		write(fd, buf, 32768);
	fsync(fd);
	lseek(fd, 0L, 0);
}
