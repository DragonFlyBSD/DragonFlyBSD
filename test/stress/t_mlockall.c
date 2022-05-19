#include <err.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TESTSIZE	(32*1024*1024L)

char *TestBuf;

void
child(void)
{
	size_t i;

        sleep(1);
	for (i = 0; i < TESTSIZE; i += 4096) {
		++TestBuf[i];
		//usleep(1000000 / 10);
	}
	usleep(1000000 / 4);	/* ensure children are concurrent */
}

int
main(int argc, char **argv)
{
        int status;

	TestBuf = malloc(TESTSIZE);

        if (fork() == 0) {
		if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1)
			err(1, "mlockall(MCL_CURRENT | MCL_FUTURE)");
		fork();
                child();
		exit(0);
	}
        wait(&status);

        return (0);
}
