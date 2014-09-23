/*
 * SYS/XWAIT.H
 *
 * $DragonFly: src/sys/sys/xwait.h,v 1.2 2006/05/20 02:42:13 dillon Exp $
 */

#ifndef _SYS_XWAIT_H_
#define _SYS_XWAIT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct proc;

/*
 * XWAIT structure for xsleep()/xwakeup()
 */

struct xwait {
    int     gen;
    TAILQ_HEAD(,proc) waitq;
};

static __inline void
xupdate_gen(struct xwait *w)
{
    ++w->gen;
}

#endif
