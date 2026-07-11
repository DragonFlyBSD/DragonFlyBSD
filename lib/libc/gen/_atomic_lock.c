/*
 * Historical libc_r-era locking primitive, kept for ABI compatibility.
 * Unlike the old no-op stub this is a real test-and-set: it atomically
 * stores 1 and returns the previous value.
 */
#include <sys/cdefs.h>
#include "spinlock.h"

long
_atomic_lock(volatile long *lck)
{
	return (__atomic_exchange_n(lck, 1L, __ATOMIC_ACQUIRE));
}
