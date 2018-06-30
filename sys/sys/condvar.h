#ifndef	_SYS_CONDVAR_H_
#define	_SYS_CONDVAR_H_

#include <sys/spinlock.h>
#include <sys/mutex.h>

struct lock;

struct cv {
	struct spinlock cv_lock;
	int		cv_waiters;
	const char	*cv_desc;
};

void	cv_init(struct cv *, const char *desc);
void	cv_destroy(struct cv *);

int	_cv_timedwait(struct cv *, struct lock *, int timo, int wakesig);
int	_cv_mtx_timedwait(struct cv *, struct mtx *, int timo, int wakesig);
void	_cv_signal(struct cv *, int broadcast);

int	cv_has_waiters(const struct cv *);

#define	cv_wait(cv, lock)			\
		_cv_timedwait((cv), (lock), 0, 0)
#define	cv_wait_sig(cv, lock)			\
		_cv_timedwait((cv), (lock), 0, 1)
#define	cv_timedwait(cv, lock, timeo)		\
		_cv_timedwait((cv), (lock), (timeo), 0)
#define	cv_timedwait_sig(cv, lock, timeo)	\
		_cv_timedwait((cv), (lock), (timeo), 1)

#define	cv_mtx_wait(cv, mtx)			\
		_cv_mtx_timedwait((cv), (mtx), 0, 0)
#define	cv_mtx_wait_sig(cv, mtx)		\
		_cv_mtx_timedwait((cv), (mtx), 0, 1)
#define	cv_mtx_timedwait(cv, mtx, timeo)	\
		_cv_mtx_timedwait((cv), (mtx), (timeo), 0)
#define	cv_mtx_timedwait_sig(cv, mtx, timeo)	\
		_cv_mtx_timedwait((cv), (mtx), (timeo), 1)

#define	cv_signal(cv)				\
		_cv_signal((cv), 0)
#define	cv_broadcast(cv)			\
		_cv_signal((cv), 1)
#define	cv_broadcastpri(cv, pri)		\
		cv_broadcast((cv))

#endif	/* _SYS_CONDVAR_H_ */
