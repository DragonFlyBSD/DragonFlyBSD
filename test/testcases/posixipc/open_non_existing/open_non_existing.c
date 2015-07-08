#include <common.h>

int
main(void) {
	int retval = 0;

	sem_open_should_fail("/notreallythere", 0, 0777, 1, ENOENT);

	return retval;
}
