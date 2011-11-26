/*
 * Test a fork/munmap vm_object shadow chain race
 *
 * Written by "Samuel J. Greear" <sjg@evilcode.net>
 */
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/wait.h>

#define MAPPING_PAGES   4096

int
main(int argc, char *argv[])
{
    unsigned int i, size;
    int status;
    pid_t pid;
    void *mapping;
    char tmp[100];

    for (i = 1; i < MAPPING_PAGES; ++i) {
        size = PAGE_SIZE * i;
        mapping = mmap(0, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED,
		       -1, 0);
        if (mapping == MAP_FAILED)
            errx(EX_OSERR, "mmap() failed");

        memset(mapping, 'x', 10);
	printf(" %d", i);
	fflush(stdout);

        pid = fork();
        if (pid == 0) {
            memcpy(&tmp, mapping, 12);
            return (0);
        } else if (pid != -1) {
            if (munmap(mapping, PAGE_SIZE * i) != 0) {
                printf("munmap failed\n");
                return (0);
            }
            waitpid(pid, &status, 0);
            if (status == 10) {
                printf("child sig10 at %d pages\n", i);
                return (0);
            }
        } else {
            printf("fork failed\n");
        }
    }

    return (0);
}
