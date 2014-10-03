/*
 * SYS/IOSCHED.H
 *
 * I/O Scheduler
 * 
 * $DragonFly: src/sys/sys/iosched.h,v 1.1 2008/06/28 17:59:47 dillon Exp $
 */

#ifndef _SYS_IOSCHED_H_
#define _SYS_IOSCHED_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_SYSTIMER_H_
#include <sys/systimer.h>
#endif

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

struct iosched_data {
    size_t	iorbytes;
    size_t	iowbytes;
    int		lastticks;	/* decay last recorded */
};

#ifdef _KERNEL

struct thread;
void    bwillwrite(int bytes);
void    bwillread(int bytes);
void    bwillinode(int count);
void	biosched_done(struct thread *td);

#endif

#endif
