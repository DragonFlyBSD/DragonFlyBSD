#include <stdio.h>

#include <fspred.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TDIR	"/bin"
#define TEXE	"/bin/ls"
#define TDEV	"/dev/zero"
#define TPIPE	"/tmp/myfifo123"
#define TMOUNTP	"/dev"
#define TDEVM	"devfs"
#define TEXIST	"/nonexistent"

#define NTESTS	6

static int
test_is_dir(void)
{
	int pass = 0;

	pass = is_dir("%s", TDIR);
	pass = pass && !is_dir("%s", TEXIST);
	pass = pass && !is_dir("%s", TEXE);

	return pass;
}

static int
test_is_program(void)
{
	int pass;

	pass = is_program(TEXE);
	pass = pass && !is_program("%s", TEXIST);
	pass = pass && !is_program(TDIR);

	return pass;
}

static int
test_is_device(void)
{
	int pass;

	pass = is_device(TDEV);
	pass = pass && !is_device("%s", TEXIST);
	pass = pass && !is_device(TDIR);

	return pass;
}


static int
test_is_named_pipe(void)
{
	int pass;

	/* Need to make a named pipe */
	unlink(TPIPE);
	if (mkfifo(TPIPE, 0600) == -1) {
		perror("mkfifo");
		return EXIT_FAILURE;
	}

	pass = is_named_pipe(TPIPE);
	pass = pass && !is_named_pipe("%s", TEXIST);
	pass = pass && !is_named_pipe(TEXE);

	return pass;
}

static int
test_is_mountpoint_mounted(void)
{
	int pass;

	pass = is_mountpoint_mounted(TMOUNTP);
	pass = pass && !is_mountpoint_mounted(TEXIST);

	return pass;
}

static int
test_is_device_mounted(void)
{
	int pass;

	pass = is_device_mounted(TDEVM);
	pass = pass && !is_device_mounted(TEXIST);

	return pass;
}

typedef struct {
	const char *name;
	int (*fn)(void);
} fspred_test;

fspred_test all_tests[NTESTS] = {
	"is_dir", test_is_dir,
	"is_program", test_is_program,
	"is_device", test_is_device,
	"is_named_pipe", test_is_named_pipe,
	"is_mountpoint_mounted", test_is_mountpoint_mounted,
	"is_device_mounted", test_is_device_mounted
};

int
main(void)
{
	int pass = 1;
	int ret;

	for (int i = 0; i < NTESTS; i++) {
		ret = all_tests[i].fn();
		printf("%s ..... %s\n", all_tests[i].name,
		    (ret) ? "pass" : "fail");

		if (ret == 0)
			pass = 0;
	}

	/* XXX Any reasonable way of testing is_any_slice_mounted? */

	return (pass) ? EXIT_SUCCESS : EXIT_FAILURE;
}
