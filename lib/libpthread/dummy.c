/*
 * The pthread implementation now lives in libc (lib/libc/thread).
 * This shim only exists so that old binaries carrying a DT_NEEDED
 * entry for libpthread.so.0 keep loading, and so new links using
 * -lpthread still find a library.  The pthread symbols resolve from
 * libc.so.8 by name.
 */
static int dummy_pthread __attribute__((__used__));
