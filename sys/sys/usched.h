/*
 * SYS/USCHED.H
 *
 *	Userland scheduler API
 * 
 * $DragonFly: src/sys/sys/usched.h,v 1.1 2005/06/25 20:03:30 dillon Exp $
 */

#ifndef _SYS_USCHED_H_
#define _SYS_USCHED_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct proc;
struct globaldata;

struct usched {
    TAILQ_ENTRY(usched) entry;
    const char *name;
    const char *desc;
    void (*acquire_curproc)(struct proc *);
    void (*release_curproc)(struct proc *);
    void (*select_curproc)(struct globaldata *);
    void (*setrunqueue)(struct proc *);
    void (*remrunqueue)(struct proc *);
    void (*resetpriority)(struct proc *);
};

extern struct usched	usched_bsd4;

#endif

