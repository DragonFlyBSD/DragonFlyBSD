#include <common.h>

int
main(void) {
	int retval = 0;
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[0] = '/';
	page[MAXPATHLEN] = '\0';
	sem_open_should_fail(page, O_RDONLY, 0777, 1, ENAMETOOLONG);
	free(page);

	return retval;
}
