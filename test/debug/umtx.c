/*
 * UMTX file[:offset] command
 *
 * $DragonFly: src/test/debug/umtx.c,v 1.1 2005/01/14 04:15:12 dillon Exp $
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>

int cmp_and_exg(volatile int *lockp, int old, int new);

struct umtx {
    volatile int lock;
};

#define MTX_LOCKED	0x80000000

static int userland_get_mutex(struct umtx *mtx, int timo);
static int userland_get_mutex_contested(struct umtx *mtx, int timo);
static void userland_rel_mutex(struct umtx *mtx);
static void userland_rel_mutex_contested(struct umtx *mtx);
static void docleanup(int signo);

static struct umtx *cleanup_mtx_contested;
static struct umtx *cleanup_mtx_held;

int verbose_opt;

int
main(int ac, char **av)
{
    char *path;
    char *str;
    off_t off = 0;
    pid_t pid;
    int ch;
    int fd;
    int pgsize;
    int pgmask;
    int timo = 0;
    struct stat st;
    struct umtx *mtx;

    signal(SIGINT, docleanup);

    while ((ch = getopt(ac, av, "t:v")) != -1) {
	switch(ch) {
	case 't':
	    timo = strtol(optarg, NULL, 0);
	    break;
	case 'v':
	    verbose_opt = 1;
	    break;
	default:
	    fprintf(stderr, "unknown option: -%c\n", optopt);
	    exit(1);
	}
    }
    ac -= optind;
    av += optind;

    if (ac < 2) {
	fprintf(stderr, "umtx file[:offset] command\n");
	exit(1);
    }
    path = av[0];
    if ((str = strchr(path, ':')) != NULL) {
	*str++ = 0;
	off = strtoull(str, NULL, 0);
    }
    if ((fd = open(path, O_RDWR|O_CREAT, 0666)) < 0) {
	perror("open");
	exit(1);
    }
    if (fstat(fd, &st) < 0) {
	perror("fstat");
	exit(1);
    }
    if (off + 4 > st.st_size) {
	int v = 0;
	lseek(fd, off, 0);
	write(fd, &v, sizeof(v));
    }
    pgsize = getpagesize();
    pgmask = pgsize - 1;
    str = mmap(NULL, pgsize, PROT_READ|PROT_WRITE, MAP_SHARED, 
		fd, off & ~(off_t)pgmask);
    mtx = (struct umtx *)(str + ((int)off & pgmask));
    if (userland_get_mutex(mtx, timo) < 0) {
	fprintf(stderr, "Mutex at %s:%ld timed out\n", path, off);
	exit(1);
    }
    if (verbose_opt)
	fprintf(stderr, "Obtained mutex at %s:%ld\n", path, off);
    if ((pid = fork()) == 0) {
	execvp(av[1], av + 1);
	_exit(0);
    } else if (pid > 0) {
	while (waitpid(pid, NULL, 0) != pid)
	    ;
    } else {
	fprintf(stderr, "Unable to exec %s\n", av[1]);
    }
    userland_rel_mutex(mtx);
    close(fd);
    return(0);
}

static int
userland_get_mutex(struct umtx *mtx, int timo)
{
    int v;

    for (;;) {
	v = mtx->lock;
	if ((v & MTX_LOCKED) == 0) {
	    /*
	     * not locked, attempt to lock.
	     */
	    if (cmp_and_exg(&mtx->lock, v, v | MTX_LOCKED) == 0) {
		cleanup_mtx_held = mtx;
		return(0);
	    }
	} else {
	    /*
	     * Locked, bump the contested count and obtain the contested
	     * mutex.
	     */
	    if (cmp_and_exg(&mtx->lock, v, v + 1) == 0) {
		cleanup_mtx_contested = mtx;
		return(userland_get_mutex_contested(mtx, timo));
	    }
	}
    }
}

static int
userland_get_mutex_contested(struct umtx *mtx, int timo)
{
    int v;

    for (;;) {
	v = mtx->lock;
	assert(v & ~MTX_LOCKED);	/* our contesting count still there */
	if ((v & MTX_LOCKED) == 0) {
	    /*
	     * not locked, attempt to remove our contested count and
	     * lock at the same time.
	     */
	    if (cmp_and_exg(&mtx->lock, v, (v - 1) | MTX_LOCKED) == 0) {
		cleanup_mtx_contested = NULL;
		cleanup_mtx_held = mtx;
		return(0);
	    }
	} else {
	    /*
	     * Still locked, sleep and try again.
	     */
	    if (verbose_opt)
		fprintf(stderr, "waiting on mutex timeout=%d\n", timo);
	    if (timo == 0) {
		umtx_sleep(&mtx->lock, v, 0);
	    } else {
		if (umtx_sleep(&mtx->lock, v, 1000000) < 0) {
		    if (errno == EAGAIN && --timo == 0) {
			cleanup_mtx_contested = NULL;
			userland_rel_mutex_contested(mtx);
			return(-1);
		    }
		}
	    }
	}
    }
}

static void
userland_rel_mutex(struct umtx *mtx)
{
    int v;

    for (;;) {
	v = mtx->lock;
	assert(v & MTX_LOCKED);	/* we still have it locked */
	if (v == MTX_LOCKED) {
	    /*
	     * We hold an uncontested lock, try to set to an unlocked
	     * state.
	     */
	    if (cmp_and_exg(&mtx->lock, MTX_LOCKED, 0) == 0) {
		if (verbose_opt)
		    fprintf(stderr, "releasing uncontested mutex\n");
		return;
	    }
	} else {
	    /*
	     * We hold a contested lock, unlock and wakeup exactly
	     * one sleeper.  It is possible for this to race a new
	     * thread obtaining a lock, in which case any contested
	     * sleeper we wake up will simply go back to sleep.
	     */
	    if (cmp_and_exg(&mtx->lock, v, v & ~MTX_LOCKED) == 0) {
		umtx_wakeup(&mtx->lock, 1);
		if (verbose_opt)
		    fprintf(stderr, "releasing contested mutex\n");
		return;
	    }
	}
    }
}

static void
userland_rel_mutex_contested(struct umtx *mtx)
{
    int v;

    for (;;) {
	if (cmp_and_exg(&mtx->lock, v, v - 1) == 0)
	    return;
	v = mtx->lock;
	assert(v & ~MTX_LOCKED);
    }
}

static void
docleanup(int signo)
{
    printf("cleanup\n");
    if (cleanup_mtx_contested)
	userland_rel_mutex_contested(cleanup_mtx_contested);
    if (cleanup_mtx_held)
	userland_rel_mutex(cleanup_mtx_held);
    exit(1);
}

__asm(
	"		.text\n"
	"cmp_and_exg:\n"
	"		movl 4(%esp),%ebx\n"
	"		movl 8(%esp),%eax\n"
	"		movl 12(%esp),%edx\n"
	"		lock cmpxchgl %edx,(%ebx)\n"
	"		jz 1f\n"
	"		movl $-1,%eax\n"
	"		ret\n"
	"1:\n"
	"		subl %eax,%eax\n"
	"		ret\n"
);

