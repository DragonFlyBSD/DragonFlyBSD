/*
 * SYS/USCHED.H
 *
 *	Userland scheduler API
 * 
 * $DragonFly: src/sys/sys/usched.h,v 1.6 2005/10/11 09:59:56 corecode Exp $
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
    void (*acquire_curproc)(struct lwp *);
    void (*release_curproc)(struct lwp *);
    void (*select_curproc)(struct globaldata *);
    void (*setrunqueue)(struct lwp *);
    void (*remrunqueue)(struct lwp *);
    void (*schedulerclock)(struct lwp *, sysclock_t, sysclock_t);
    void (*recalculate)(struct lwp *);
    void (*resetpriority)(struct lwp *);
    void (*heuristic_forking)(struct lwp *, struct lwp *);
    void (*heuristic_exiting)(struct lwp *, struct lwp *);
};

union usched_data {
    /*
     * BSD4 scheduler. 
     */
    struct {
	short	priority;	/* lower is better */
	char	interactive;	/* (currently not used) */
	char	rqindex;
	int	origcpu;
	int	estcpu;		/* dynamic priority modification */
    } bsd4;

    int		pad[4];		/* PAD for future expansion */
};

extern struct usched	usched_bsd4;

#endif

