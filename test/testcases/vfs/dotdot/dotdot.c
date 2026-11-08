/*
 * Test dot/dotdot create/open error codes.
 *
 * Each case is checked against the expected errno (0 means success).
 * Run inside a scratch directory that contains a "sub" directory and a
 * "reg" regular file.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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
check_open(const char *what, const char *path, int flags, int mode, int expect)
{
	int fd, err;

	errno = 0;
	fd = open(path, flags, mode);
	err = errno;
	if (fd >= 0) {
		close(fd);
		err = 0;
	}
	if (err != expect) {
		printf("FAIL: %-40s got %-22s expected %s\n",
		    what, errstr(err), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-40s %s\n", what, errstr(err));
	}
}

static void
check_mkdir(const char *what, const char *path, int expect)
{
	int err;

	errno = 0;
	if (mkdir(path, 0755) < 0)
		err = errno;
	else
		err = 0;
	if (err != expect) {
		printf("FAIL: %-40s got %-22s expected %s\n",
		    what, errstr(err), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-40s %s\n", what, errstr(err));
	}
}

static void
check_mkfifo(const char *what, const char *path, int expect)
{
	int err;

	errno = 0;
	if (mkfifo(path, 0644) < 0)
		err = errno;
	else
		err = 0;
	if (err != expect) {
		printf("FAIL: %-40s got %-22s expected %s\n",
		    what, errstr(err), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-40s %s\n", what, errstr(err));
	}
}

/*
 * Use a FIFO rather than a device node so that the check is meaningful for
 * unprivileged users as well; the expected EEXIST comes from the dot path,
 * not from the node type.
 */
static void
check_mknod(const char *what, const char *path, int expect)
{
	int err;

	errno = 0;
	if (mknod(path, S_IFIFO | 0600, 0) < 0)
		err = errno;
	else {
		err = 0;
		unlink(path);
	}
	if (err != expect) {
		printf("FAIL: %-40s got %-22s expected %s\n",
		    what, errstr(err), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-40s %s\n", what, errstr(err));
	}
}

static void
check_symlink(const char *what, const char *path, int expect)
{
	int err;

	errno = 0;
	if (symlink("target", path) < 0)
		err = errno;
	else {
		err = 0;
		unlink(path);
	}
	if (err != expect) {
		printf("FAIL: %-40s got %-22s expected %s\n",
		    what, errstr(err), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-40s %s\n", what, errstr(err));
	}
}

static void
check_link(const char *what, const char *path, int expect)
{
	int err;

	errno = 0;
	if (link("reg", path) < 0)
		err = errno;
	else {
		err = 0;
		unlink(path);
	}
	if (err != expect) {
		printf("FAIL: %-40s got %-22s expected %s\n",
		    what, errstr(err), errstr(expect));
		++failures;
	} else {
		printf("ok:   %-40s %s\n", what, errstr(err));
	}
}

int
main(void)
{
	printf("--- open() of dot/dotdot ---\n");
	check_open("open(.) O_WRONLY|O_CREAT", ".", O_WRONLY | O_CREAT, 0644, EISDIR);
	check_open("open(.) O_RDWR|O_CREAT", ".", O_RDWR | O_CREAT, 0644, EISDIR);
	check_open("open(.) O_RDONLY|O_CREAT", ".", O_RDONLY | O_CREAT, 0644, EISDIR);
	check_open("open(.) O_WRONLY", ".", O_WRONLY, 0, EISDIR);
	check_open("open(.) O_RDONLY", ".", O_RDONLY, 0, 0);
	check_open("open(.) O_WRONLY|O_TRUNC|O_CREAT", ".",
	    O_WRONLY | O_TRUNC | O_CREAT, 0644, EISDIR);
	check_open("open(.) O_WRONLY|O_CREAT|O_EXCL", ".",
	    O_WRONLY | O_CREAT | O_EXCL, 0644, EEXIST);
	check_open("open(.) O_RDONLY|O_CREAT|O_EXCL", ".",
	    O_RDONLY | O_CREAT | O_EXCL, 0644, EEXIST);
	check_open("open(.) O_RDONLY|O_CREAT|O_DIRECTORY", ".",
	    O_RDONLY | O_CREAT | O_DIRECTORY, 0644, EISDIR);

	check_open("open(..) O_WRONLY|O_CREAT", "..",
	    O_WRONLY | O_CREAT, 0644, EISDIR);
	check_open("open(..) O_RDONLY|O_CREAT", "..",
	    O_RDONLY | O_CREAT, 0644, EISDIR);
	check_open("open(..) O_WRONLY|O_CREAT|O_EXCL", "..",
	    O_WRONLY | O_CREAT | O_EXCL, 0644, EEXIST);
	check_open("open(..) O_RDONLY", "..", O_RDONLY, 0, 0);

	printf("--- open() of an ordinary directory (consistency) ---\n");
	check_open("open(sub) O_WRONLY|O_CREAT", "sub",
	    O_WRONLY | O_CREAT, 0644, EISDIR);
	check_open("open(sub) O_RDONLY|O_CREAT", "sub",
	    O_RDONLY | O_CREAT, 0644, EISDIR);
	check_open("open(sub) O_WRONLY|O_CREAT|O_EXCL", "sub",
	    O_WRONLY | O_CREAT | O_EXCL, 0644, EEXIST);
	check_open("open(sub) O_RDONLY", "sub", O_RDONLY, 0, 0);

	printf("--- mkdir() of dot/dotdot ---\n");
	check_mkdir("mkdir(.)", ".", EEXIST);
	check_mkdir("mkdir(./)", "./", EEXIST);
	check_mkdir("mkdir(..)", "..", EEXIST);
	check_mkdir("mkdir(sub/.)", "sub/.", EEXIST);
	check_mkdir("mkdir(sub/..)", "sub/..", EEXIST);
	check_mkdir("mkdir(newdir)", "newdir", 0);
	check_mkdir("mkdir(reg)", "reg", EEXIST);
	rmdir("newdir");

	printf("--- mkfifo()/mknod()/symlink()/link() of dot ---\n");
	check_mkfifo("mkfifo(.)", ".", EEXIST);
	check_mknod("mknod(.)", ".", EEXIST);
	check_symlink("symlink(x,.)", ".", EEXIST);
	check_link("link(reg,.)", ".", EEXIST);

	printf("--- ordinary create still works ---\n");
	check_open("open(new) O_CREAT|O_EXCL (1st)", "new",
	    O_CREAT | O_EXCL | O_WRONLY, 0644, 0);
	check_open("open(new) O_CREAT|O_EXCL (2nd)", "new",
	    O_CREAT | O_EXCL | O_WRONLY, 0644, EEXIST);
	unlink("new");
	check_open("open(new) O_CREAT|O_RDONLY", "new", O_CREAT | O_RDONLY, 0644, 0);
	unlink("new");
	check_mkdir("mkdir(plain)", "plain", 0);
	rmdir("plain");

	printf("--- root ---\n");
	check_open("open(/) O_WRONLY|O_CREAT", "/",
	    O_WRONLY | O_CREAT, 0644, EISDIR);
	check_open("open(/) O_RDONLY|O_CREAT", "/",
	    O_RDONLY | O_CREAT, 0644, EISDIR);
	check_open("open(/) O_WRONLY|O_CREAT|O_EXCL", "/",
	    O_WRONLY | O_CREAT | O_EXCL, 0644, EEXIST);
	check_open("open(/) O_RDONLY", "/", O_RDONLY, 0, 0);
	check_mkdir("mkdir(/)", "/", EEXIST);

	printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return (failures != 0);
}
