#include <stdio.h>

#include <fspred.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TDIR	"/bin"
#define TEXE	"/bin/ls"
#define TREG	"/boot/kernel/kernel"
#define TDEV	"/dev/zero"
#define TPIPE	"/tmp/myfifo123"
#define TMOUNTP	"/dev/"
#define TDEVM	"devfs"

int
main(void)
{
	if (!is_dir(TDIR)) {
		printf("is_dir(%s) failed\n", TDIR);
		return EXIT_FAILURE;
	}

	if (is_dir(TEXE)) {
		printf("is_dir(%s) failed\n", TEXE);
		return EXIT_FAILURE;
	}

	if (!is_program(TEXE)) {
		printf("is_program(%s) failed\n", TEXE);
		return EXIT_FAILURE;
	}

	if (is_program(TDIR)) {
		printf("is_program(%s) failed\n", TDIR);
		return EXIT_FAILURE;
	}

	if (!is_device(TDEV)) {
		printf("is_device(%s) failed\n", TDEV);
		return EXIT_FAILURE;
		}

	if (is_device(TDIR)) {
		printf("is_device(%s) failed\n", TDIR);
		return EXIT_FAILURE;
	}

	unlink(TPIPE);
	if (mkfifo(TPIPE, 0600) == -1) {
		perror("mkfifo");
		return EXIT_FAILURE;
	}

	if (!is_named_pipe(TPIPE)) {
		printf("is_named_pipe(%s) failed\n", TPIPE);
		return EXIT_FAILURE;
	}

	if (is_named_pipe(TEXE)) {
		printf("is_named_pipe(%s) failed\n", TEXE);
		return EXIT_FAILURE;
	}

	if (is_mountpoint_mounted(TMOUNTP)) {
		printf("is_mountpoint_mounted(%s) failed\n", TMOUNTP);
		return EXIT_FAILURE;
	}

	if (!is_device_mounted(TDEVM)) {
		printf("is_device_mounted(%s) failed\n", TDEVM);
		return EXIT_FAILURE;
	}

	/* XXX Any reasonable way of testing is_any_slice_mounted? */

	return EXIT_SUCCESS;
}
