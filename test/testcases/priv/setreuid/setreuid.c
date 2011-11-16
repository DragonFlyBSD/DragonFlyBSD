/*
 * usage:
 * $ gcc -W -Wall at.c
 * $ su
 * # chown root a.out; chmod u+s a.out
 * # exit
 * $ ./a.out
 */
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define CHECK(expr) \
if ((expr) != 0) \
  err(1, #expr)

int
main()
{
	uid_t real_uid, effective_uid;
	int fd;

	real_uid = getuid();
	effective_uid = geteuid();
	CHECK(setreuid(effective_uid, real_uid));

	CHECK(setreuid(real_uid, effective_uid));
	fd = open("/etc/hosts", O_RDONLY);
	CHECK(setreuid(effective_uid, real_uid));
	close(fd), fd = -1;	/* move this above the previous line */

	CHECK(setreuid(real_uid, effective_uid));
	printf("uid %d, euid %d\n", getuid(), geteuid());
	CHECK(setreuid(effective_uid, real_uid));
	return 0;
}

