#include <netgraph7/ng_message.h>

#include <sys/malloc.h>
#include <sys/linker.h>
#include <sys/thread2.h>
#include <sys/vnode.h>

#include "dragonfly.h"

/* Temporary lock stuff. */
#include <sys/lock.h>
/* End Temporary lock stuff. */



/* Locking stuff. */


int
lockstatus_owned(struct lock *lkp, struct thread *td)
{
	int lock_type = 0;

	if (lkp->lk_exclusivecount != 0) {
		if (td == NULL || lkp->lk_lockholder == td)
			lock_type = LK_EXCLUSIVE;
		else
			lock_type = LK_EXCLOTHER;
	} else if (lkp->lk_sharecount != 0) {
		lock_type = LK_SHARED;
	}
	return (lock_type);
}

/*
 * Atomically drop a lockmgr lock and go to sleep. The lock is reacquired
 * before returning from this function. Passes on the value returned by
 * tsleep().
 */
int
lock_sleep(void *ident, int flags, const char *wmesg, int timo,
		struct lock *lk)
{
	int err, mode;

	mode = lockstatus_owned(lk, curthread);
	KKASSERT((mode == LK_EXCLUSIVE) || (mode == LK_SHARED));

	crit_enter();
	tsleep_interlock(ident, flags);
	lockmgr(lk, LK_RELEASE);
	err = tsleep(ident, flags, wmesg, timo);
	crit_exit();
	lockmgr(lk, mode);
	return err;
}
