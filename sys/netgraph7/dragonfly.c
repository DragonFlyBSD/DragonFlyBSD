#include <netgraph7/ng_message.h>

#include <sys/malloc.h>
#include <sys/linker.h>
#include <sys/thread2.h>
#include <sys/vnode.h>

#include "dragonfly.h"

/* Temporary lock stuff. */
#include <sys/lock.h>
/* End Temporary lock stuff. */

int
linker_api_available(void)
{
	/* linker_* API won't work without a process context */
	if (curproc == NULL)
		return 0;
	/*
	 * nlookup_init() relies on namei_oc to be initialized,
	 * but it's not when the netgraph module is loaded during boot.
	 */
	if (namei_oc == NULL)
		return 0;
	return 1;
}

int
ng_load_module(const char *name)
{
	char *path, filename[NG_TYPESIZ + 3];
	linker_file_t lf;
	int error;

	if (!linker_api_available())
		return (ENXIO);

	/* Not found, try to load it as a loadable module */
	ksnprintf(filename, sizeof(filename), "ng_%s.ko", name);
	if ((path = linker_search_path(filename)) == NULL)
		return (ENXIO);
	error = linker_load_file(path, &lf);
	FREE(path, M_LINKER);
	if (error == 0)
		lf->userrefs++;		/* pretend kldload'ed */
	return (error);
}

int
ng_unload_module(const char *name)
{
	char filename[NG_TYPESIZ + 3];
	linker_file_t lf;
	int error;

	if (!linker_api_available())
		return (ENXIO);

	/* Not found, try to load it as a loadable module */
	ksnprintf(filename, sizeof(filename), "ng_%s.ko", name);
	if ((lf = linker_find_file_by_name(filename)) == NULL)
		return (ENXIO);
	error = linker_file_unload(lf);

	if (error == 0)
		lf->userrefs--;		/* pretend kldunload'ed */
	return (error);
}



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
