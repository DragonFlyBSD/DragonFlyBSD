#include <namespace.h>
#include <string.h>
#include <stdlib.h>
#include <un-namespace.h>

static void sysvipc_init(void) __attribute__ ((constructor));

char sysvipc_userland = 0;

static void
sysvipc_init(void)
{
	if (getenv("USR_SYSVIPC") != NULL)
		sysvipc_userland = 1;
}
