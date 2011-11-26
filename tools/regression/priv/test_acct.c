#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include "test.h"

static char *tmp_file;

void
setup() {
	tmp_file = "./tmpfile";
	int fh = open(tmp_file, O_CREAT | O_TRUNC | O_WRONLY, 0666);
	close(fh);
}

void
teardown() {
	unlink(tmp_file);
}

int
test_acct() {
	int error;

	error = acct(tmp_file);
	if (error == -1)
  		return errno;

	error = acct(NULL);
	if (error == -1)
		return errno;

	return 0;
}

int main()
{
	test_as_root		(test_acct, 0, "acct");
	test_as_jailed_root	(test_acct, EPERM, "acct");
	test_as_unpriv		(test_acct, EPERM, "acct");
	test_as_jailed_unpriv	(test_acct, EPERM, "acct");

	return 0;
}
