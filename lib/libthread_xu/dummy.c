#include <sys/cdefs.h>

/*
 * The libthread_xu implementation now lives in libc.  This shim only exists so
 * that old binaries carrying a DT_NEEDED entry for libthread_xu.so.2 keep
 * loading.
 */
static int dummy_thread_xu __used;
