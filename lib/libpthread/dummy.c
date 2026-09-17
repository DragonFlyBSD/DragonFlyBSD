#include <sys/cdefs.h>

/*
 * The pthread implementation now lives in libc.  This shim only exists so
 * that old binaries carrying a DT_NEEDED entry for libpthread.so.0 keep
 * loading.
 */
static int dummy_pthread __used;
