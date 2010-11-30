#include <sys/condvar.h>
#include <sys/spinlock2.h>
#include <sys/systm.h>
#include <sys/lock.h>

void
cv_init(struct cv *c, const char *desc)
{
	c->cv_desc = desc;
	c->cv_waiters = 0;
	spin_init(&c->cv_lock);
}

void
cv_destroy(struct cv *c)
{
	spin_uninit(&c->cv_lock);
}

int
_cv_timedwait(struct cv *c, struct lock *l, int timo, int wakesig)
{
	int flags = wakesig ? PCATCH : 0;
	int error;

	spin_lock(&c->cv_lock);
	tsleep_interlock(c, flags);
	c->cv_waiters++;
	spin_unlock(&c->cv_lock);
	if (l != NULL)
		lockmgr(l, LK_RELEASE);
	error = tsleep(c, flags, c->cv_desc, timo);
	if (l != NULL)
		lockmgr(l, LK_EXCLUSIVE);

	return (error);
}

void
_cv_signal(struct cv *c, int broadcast)
{
	spin_lock(&c->cv_lock);
	if (c->cv_waiters == 0)
		goto out;

	if (broadcast) {
		c->cv_waiters = 0;
		wakeup(c);
	} else {
		c->cv_waiters--;
		wakeup_one(c);
	}

out:
	spin_unlock(&c->cv_lock);
}

int
cv_has_waiters(const struct cv *c)
{
	return (c->cv_waiters);
}
