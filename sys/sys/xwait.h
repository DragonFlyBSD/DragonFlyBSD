/*
 * SYS/XWAIT.H
 *
 * $DragonFly: src/sys/sys/xwait.h,v 1.1 2003/06/21 07:54:57 dillon Exp $
 */

#ifndef _SYS_XWAIT_H_
#define _SYS_XWAIT_H_

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

