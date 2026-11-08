/*
 * Test the *at() variants of dot/dotdot create/open.  The same nlookup and
 * vn_open paths are used, but with an explicit base directory or AT_FDCWD.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static const char *
errstr(int e)
{
	static char bufs[4][64];
	static int idx;
	char *buf = bufs[idx++ & 3];

	if (e == 0)
		return ("OK");
	snprintf(buf, sizeof(bufs[0]), "%s (%d)", strerror(e), e);
	return (buf);
}

static void
check(const char *what, int got, int expect)
{
	if (got != expect) {
		printf("FAIL: %-52s got %-22s expected %s\n",
		    what, errstr(got), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-52s %s\n", what, errstr(got));
	}
}

static int
rc_openat(int dirfd, const char *path, int flags, int mode)
{
	int fd, err;

	errno = 0;
	fd = openat(dirfd, path, flags, mode);
	err = errno;
	if (fd >= 0) {
		close(fd);
		err = 0;
	}
	return (err);
}

static int
rc_mkdirat(int dirfd, const char *path)
{
	errno = 0;
	if (mkdirat(dirfd, path, 0755) < 0)
		return (errno);
	return (0);
}

int
main(void)
{
	int dfd;

	printf("--- AT_FDCWD ---\n");
	check("mkdirat(AT_FDCWD, .)",
	    rc_mkdirat(AT_FDCWD, "."), EEXIST);
	check("mkdirat(AT_FDCWD, ./)",
	    rc_mkdirat(AT_FDCWD, "./"), EEXIST);
	check("mkdirat(AT_FDCWD, ..)",
	    rc_mkdirat(AT_FDCWD, ".."), EEXIST);
	check("openat(AT_FDCWD, .) O_WRONLY|O_CREAT",
	    rc_openat(AT_FDCWD, ".", O_WRONLY | O_CREAT, 0644), EISDIR);
	check("openat(AT_FDCWD, .) O_RDONLY|O_CREAT",
	    rc_openat(AT_FDCWD, ".", O_RDONLY | O_CREAT, 0644), EISDIR);
	check("openat(AT_FDCWD, .) O_CREAT|O_EXCL",
	    rc_openat(AT_FDCWD, ".", O_CREAT | O_EXCL | O_WRONLY, 0644),
	    EEXIST);

	printf("--- dirfd == sub ---\n");
	dfd = open("sub", O_RDONLY);
	if (dfd < 0) {
		printf("FAIL: cannot open sub: %s\n", strerror(errno));
		return (1);
	}
	check("mkdirat(sub, .)",
	    rc_mkdirat(dfd, "."), EEXIST);
	check("mkdirat(sub, ..)",
	    rc_mkdirat(dfd, ".."), EEXIST);
	check("openat(sub, .) O_WRONLY|O_CREAT",
	    rc_openat(dfd, ".", O_WRONLY | O_CREAT, 0644), EISDIR);
	check("openat(sub, .) O_CREAT|O_EXCL",
	    rc_openat(dfd, ".", O_CREAT | O_EXCL | O_WRONLY, 0644), EEXIST);
	check("openat(sub, ..) O_WRONLY|O_CREAT",
	    rc_openat(dfd, "..", O_WRONLY | O_CREAT, 0644), EISDIR);
	close(dfd);

	printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return (failures != 0);
}
