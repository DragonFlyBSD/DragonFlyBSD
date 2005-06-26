/*
 * SYS/USCHED.H
 *
 *	Userland scheduler API
 * 
 * $DragonFly: src/sys/sys/usched.h,v 1.3 2005/06/26 22:03:23 dillon Exp $
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
    void (*heuristic_forking)(struct proc *, struct proc *);
    void (*heuristic_exiting)(struct proc *, struct proc *);
};

union usched_data {
    /*
     * BSD4 scheduler. 
     */
    struct {
	short	priority;	/* lower is better */
	char	interactive;	/* (currently not used) */
	char	rqindex;
	u_int	childscpu;
    } bsd4;

    int		pad[4];		/* PAD for future expansion */
};

extern struct usched	usched_bsd4;

#endif

