/*-
 * Copyright (c) 1999,2000,2001 Jonathan Lemon <jlemon@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/sys/sys/event.h,v 1.5.2.6 2003/02/09 15:28:13 nectar Exp $
 *	$DragonFly: src/sys/sys/event.h,v 1.7 2007/01/15 01:26:56 dillon Exp $
 */

#ifndef _SYS_EVENT_H_
#define _SYS_EVENT_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _NET_NETISR_H_
#include <net/netisr.h>			/* struct notifymsglist */
#endif
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/queue.h>
#endif

#define EVFILT_READ		(-1)
#define EVFILT_WRITE		(-2)
#define EVFILT_AIO		(-3)	/* attached to aio requests */
#define EVFILT_VNODE		(-4)	/* attached to vnodes */
#define EVFILT_PROC		(-5)	/* attached to struct proc */
#define EVFILT_SIGNAL		(-6)	/* attached to struct proc */
#define EVFILT_TIMER		(-7)	/* timers */
#define EVFILT_EXCEPT		(-8)	/* exceptional conditions */
#define EVFILT_USER		(-9)	/* user events */

#define EVFILT_MARKER		0xF	/* placemarker for tailq */

#define EVFILT_SYSCOUNT		9

#define EV_SET(kevp_, a, b, c, d, e, f) do {	\
	struct kevent *kevp = (kevp_);		\
	(kevp)->ident = (a);			\
	(kevp)->filter = (b);			\
	(kevp)->flags = (c);			\
	(kevp)->fflags = (d);			\
	(kevp)->data = (e);			\
	(kevp)->udata = (f);			\
} while(0)

struct kevent {
	uintptr_t	ident;		/* identifier for this event */
	short		filter;		/* filter for event */
	u_short		flags;
	u_int		fflags;
	intptr_t	data;
	void		*udata;		/* opaque user data identifier */
};

/* actions */
#define EV_ADD		0x0001		/* add event to kq (implies enable) */
#define EV_DELETE	0x0002		/* delete event from kq */
#define EV_ENABLE	0x0004		/* enable event */
#define EV_DISABLE	0x0008		/* disable event (not reported) */

/* flags */
#define EV_ONESHOT	0x0010		/* only report one occurrence */
#define EV_CLEAR	0x0020		/* clear event state after reporting */

#define EV_SYSFLAGS	0xF000		/* reserved by system */
#define EV_FLAG1	0x2000		/* filter-specific flag */

/* returned values */
#define EV_EOF		0x8000		/* EOF detected */
#define EV_ERROR	0x4000		/* error, data contains errno */
#define EV_NODATA	0x1000		/* EOF and no more data */

/*
 * EVFILT_USER
 */
 /*
  * data/hint flags/masks for EVFILT_USER, shared with userspace
  *
  * On input, the top two bits of fflags specifies how the lower twenty four
  * bits should be applied to the stored value of fflags.
  *
  * On output, the top two bits will always be set to NOTE_FFNOP and the
  * remaining twenty four bits will contain the stored fflags value.
  */
#define NOTE_FFNOP      0x00000000	/* ignore input fflags */
#define NOTE_FFAND      0x40000000	/* AND fflags */
#define NOTE_FFOR       0x80000000	/* OR fflags */
#define NOTE_FFCOPY     0xc0000000	/* copy fflags */
#define NOTE_FFCTRLMASK 0xc0000000	/* masks for operations */
#define NOTE_FFLAGSMASK 0x00ffffff

#define NOTE_TRIGGER    0x01000000	/* trigger for output */

/*
 * data/hint flags for EVFILT_{READ|WRITE}, shared with userspace
 */
#define NOTE_LOWAT	0x0001			/* low water mark */

/*
 * data/hint flags for EVFILT_EXCEPT, shared with userspace and with
 * EVFILT_{READ|WRITE}
 */
#define NOTE_OOB	0x0002			/* OOB data on a socket */

/*
 * data/hint flags for EVFILT_VNODE, shared with userspace
 */
#define	NOTE_DELETE	0x0001			/* vnode was removed */
#define	NOTE_WRITE	0x0002			/* data contents changed */
#define	NOTE_EXTEND	0x0004			/* size increased */
#define	NOTE_ATTRIB	0x0008			/* attributes changed */
#define	NOTE_LINK	0x0010			/* link count changed */
#define	NOTE_RENAME	0x0020			/* vnode was renamed */
#define	NOTE_REVOKE	0x0040			/* vnode access was revoked */

/*
 * data/hint flags for EVFILT_PROC, shared with userspace
 */
#define	NOTE_EXIT	0x80000000		/* process exited */
#define	NOTE_FORK	0x40000000		/* process forked */
#define	NOTE_EXEC	0x20000000		/* process exec'd */
#define	NOTE_PCTRLMASK	0xf0000000		/* mask for hint bits */
#define	NOTE_PDATAMASK	0x000fffff		/* mask for pid */

/* additional flags for EVFILT_PROC */
#define	NOTE_TRACK	0x00000001		/* follow across forks */
#define	NOTE_TRACKERR	0x00000002		/* could not track child */
#define	NOTE_CHILD	0x00000004		/* am a child process */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct knote;
SLIST_HEAD(klist, knote);

/*
 * Used to maintain information about processes that wish to be
 * notified when I/O becomes possible.
 */
struct kqinfo {
	struct	klist ki_note;		/* kernel note list */
	struct	notifymsglist ki_mlist;	/* list of pending predicate messages */
};

#endif

#ifdef _KERNEL

/*
 * Global token for kqueue subsystem
 */
extern struct lwkt_token kq_token;

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_KQUEUE);
#endif

#define KNOTE(list, hint)	if ((list) != NULL) knote(list, hint)

/*
 * Flag indicating hint is a signal.  Used by EVFILT_SIGNAL, and also
 * shared by EVFILT_PROC  (all knotes attached to p->p_klist)
 *
 * NOTE_OLDAPI is used to signal that standard filters are being called
 * from the select/poll wrapper.
 */
#define NOTE_SIGNAL	0x08000000
#define NOTE_OLDAPI	0x04000000	/* select/poll note */

#define FILTEROP_ISFD	0x0001		/* if ident == filedescriptor */
#define FILTEROP_MPSAFE	0x0002

struct filterops {
	u_short	f_flags;

	/* f_attach returns 0 on success or valid error code on failure */
	int	(*f_attach)	(struct knote *kn);
	void	(*f_detach)	(struct knote *kn);

        /* f_event returns boolean truth */
	int	(*f_event)	(struct knote *kn, long hint);
};

struct knote {
	SLIST_ENTRY(knote)	kn_link;	/* for fd */
	TAILQ_ENTRY(knote)	kn_kqlink;	/* for kq_knlist */
	SLIST_ENTRY(knote)	kn_next;	/* for struct kqinfo */
	TAILQ_ENTRY(knote)	kn_tqe;		/* for kq_head */
	struct			kqueue *kn_kq;	/* which queue we are on */
	struct 			kevent kn_kevent;
	int			kn_status;
	int			kn_sfflags;	/* saved filter flags */
	intptr_t		kn_sdata;	/* saved data field */
	union {
		struct		file *p_fp;	/* file data pointer */
		struct		proc *p_proc;	/* proc pointer */
		int		hookid;
	} kn_ptr;
	struct			filterops *kn_fop;
	caddr_t			kn_hook;
};

#define KN_ACTIVE	0x0001			/* event has been triggered */
#define KN_QUEUED	0x0002			/* event is on queue */
#define KN_DISABLED	0x0004			/* event is disabled */
#define KN_DETACHED	0x0008			/* knote is detached */
#define KN_REPROCESS	0x0010			/* force reprocessing race */
#define KN_DELETING	0x0020			/* deletion in progress */
#define KN_PROCESSING	0x0040			/* event processing in prog */
#define KN_WAITING	0x0080			/* waiting on processing */

#define kn_id		kn_kevent.ident
#define kn_filter	kn_kevent.filter
#define kn_flags	kn_kevent.flags
#define kn_fflags	kn_kevent.fflags
#define kn_data		kn_kevent.data
#define kn_fp		kn_ptr.p_fp

struct proc;
struct thread;
struct filedesc;
struct kevent_args;

typedef int	(*k_copyout_fn)(void *arg, struct kevent *kevp, int count,
    int *res);
typedef int	(*k_copyin_fn)(void *arg, struct kevent *kevp, int max,
    int *events);
int kern_kevent(struct kqueue *kq, int nevents, int *res, void *uap,
    k_copyin_fn kevent_copyin, k_copyout_fn kevent_copyout,
    struct timespec *tsp);

extern void	knote(struct klist *list, long hint);
extern void	knote_insert(struct klist *klist, struct knote *kn);
extern void	knote_remove(struct klist *klist, struct knote *kn);
/*extern void	knote_empty(struct klist *list);*/
extern void	knote_assume_knotes(struct kqinfo *, struct kqinfo *,
		    struct filterops *, void *);
extern void	knote_fdclose(struct file *fp, struct filedesc *fdp, int fd);
extern void	kqueue_init(struct kqueue *kq, struct filedesc *fdp);
extern void	kqueue_terminate(struct kqueue *kq);
extern int 	kqueue_register(struct kqueue *kq, struct kevent *kev);

#endif 	/* _KERNEL */

#if !defined(_KERNEL) || defined(_KERNEL_VIRTUAL)

#include <sys/cdefs.h>
struct timespec;

__BEGIN_DECLS
int     kqueue (void);
int     kevent (int, const struct kevent *, int, struct kevent *,
		int, const struct timespec *);
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_EVENT_H_ */
