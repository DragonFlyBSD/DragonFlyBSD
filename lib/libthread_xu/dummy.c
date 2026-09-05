/*
 * The libthread_xu implementation now lives in libc (lib/libc/thread).
 * This shim only exists so that old binaries carrying a DT_NEEDED
 * entry for libthread_xu.so.2 keep loading; the pthread symbols they
 * reference resolve from libc.so.8 by name.
 */
static int dummy_thread_xu __attribute__((__used__));
