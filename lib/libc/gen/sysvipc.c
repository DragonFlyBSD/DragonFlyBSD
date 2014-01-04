/*
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <namespace.h>
#include <string.h>
#include <stdlib.h>
#include <un-namespace.h>

static void sysvipc_init(void) __attribute__ ((constructor));

char use_userland_impl = 0;

static void
sysvipc_init(void) {
	char *var = getenv("USR_SYSVIPC");
	if (var == NULL)
		return;
	if(strncmp(var, "1", 1) == 0)
		use_userland_impl = 1;
}
