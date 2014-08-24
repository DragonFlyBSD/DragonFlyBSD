#include <sys/condvar.h>
#include <sys/spinlock2.h>
#include <sys/systm.h>
#include <sys/lock.h>

void
cv_init(struct cv *c, const char *desc)
{
	c->cv_desc = desc;
	c->cv_waiters = 0;
	spin_init(&c->cv_lock, "cvinit");
}

void
cv_destroy(struct cv *c)
{
	spin_uninit(&c->cv_lock);
}

int
_cv_timedwait(struct cv *c, struct lock *lk, int timo, int wakesig)
{
	int flags = wakesig ? PCATCH : 0;
	int error;

	/*
	 * Can interlock without critical section/spinlock as long
	 * as we don't block before calling *sleep().  PINTERLOCKED
	 * must be passed to the *sleep() to use the manual interlock
	 * (else a new one is created which opens a timing race).
	 */
	tsleep_interlock(c, flags);

	spin_lock(&c->cv_lock);
	c->cv_waiters++;
	spin_unlock(&c->cv_lock);

	if (lk)
		error = lksleep(c, lk, flags | PINTERLOCKED, c->cv_desc, timo);
	else
		error = tsleep(c, flags | PINTERLOCKED, c->cv_desc, timo);

	return (error);
}

void
_cv_signal(struct cv *c, int broadcast)
{
	spin_lock(&c->cv_lock);
	if (c->cv_waiters == 0) {
		spin_unlock(&c->cv_lock);
	} else if (broadcast) {
		c->cv_waiters = 0;
		spin_unlock(&c->cv_lock);	/* must unlock first */
		wakeup(c);
	} else {
		c->cv_waiters--;
		spin_unlock(&c->cv_lock);	/* must unlock first */
		wakeup_one(c);
	}
}

int
cv_has_waiters(const struct cv *c)
{
	return (c->cv_waiters);
}
