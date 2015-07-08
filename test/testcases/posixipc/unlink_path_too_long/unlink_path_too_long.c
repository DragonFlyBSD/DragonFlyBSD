#include <common.h>

int
main(void) {
	int retval = 0;
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[MAXPATHLEN] = '\0';
	retval = sem_unlink_should_fail(page, ENAMETOOLONG);
	free(page);

	return retval;
}
