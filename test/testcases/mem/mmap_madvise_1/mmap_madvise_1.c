/*
 * t_mmap_madvise_1.c
 *
 * Check that an mprotect(PROT_NONE) region cannot be read even after an
 * madvise(MADV_WILLNEED) has been executed on the region.
 *
 * Returns 0 if mprotect() protected the segment, 1 if the segment was readable
 * despite PROT_NONE.
 *
 * $Id: t_mmap_madvise_1.c,v 1.1 2011/10/30 15:10:34 me Exp me $
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>

int
main(int argc, char *argv[])
{
	int fd;
	char *mem;
	char *tmpfile;
	char teststring[] = "hello, world!\0";
	int rc;
	char c0;
	int status;
	
	/* Setup: create a test file, write a test pattern, map it */
	tmpfile = tmpnam(NULL);

	fd = open(tmpfile, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (fd == -1)
		err(1, "open/creat failure");

	unlink(tmpfile);
	write(fd, teststring, strlen(teststring));
	fsync(fd);

	mem = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED)
		err(1, "mmap failure");
	
	/* At this point the segment should be readable and reflect the file */
	rc = strcmp(mem, teststring);
	if (rc != 0)
		err(1, "unexpected map region");
	
	rc = mprotect(mem, 4096, PROT_NONE);
	if (rc == -1)
		err(1, "mprotect error");
	
	/* At this point the segment should no longer be readable */
	
	/* POSIX hat: this call might want to fail w/ EINVAL; we are offering
	 * advice for a region that is invalid, posix_madvise() is marked as
	 * failing w/ EINVAL if "The value of advice is invalid." ; we need
	 * a more precise definition of invalid. */
	rc = madvise(mem, 4096, MADV_WILLNEED);
	if (rc == -1)
		err(1, "madvise failed");
	
	/* Segment should still not be readable */
	
	rc = fork();
	if (rc == 0) {
		c0 = mem[0];
		if (c0 == 'h')
			exit(0);
		exit(1);
	}
	wait(&status);
	rc = 0;
	
	/* The child was able to read the segment. Uhoh. */
	if (WIFEXITED(status)) {
		rc = -1;
	}
	/* Child died via a signal (SEGV) */
	if (WIFSIGNALED(status)) {
		rc = 0;
	}
	
	munmap(mem, 4096);
	close(fd);
	
	printf("%d \n", rc);
	return (rc);
}

