/*-
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
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
 * $FreeBSD: src/sys/sys/eventhandler.h,v 1.5 2000/01/16 06:11:33 bde Exp $
 */

#include <sys/queue.h>

#ifndef _SYS_EVENTHANDLER_H_
#define _SYS_EVENTHANDLER_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)

#error "This file should not be included by userland programs."

#else

struct eventhandler_entry 
{
    TAILQ_ENTRY(eventhandler_entry)	ee_link;
    int					ee_priority;
    void				*ee_arg;
};

struct eventhandler_list 
{
    TAILQ_ENTRY(eventhandler_list)	el_link;
    char				*el_name;
    int					el_flags;
#define EHE_INITTED	(1<<0)
    TAILQ_HEAD(,eventhandler_entry)	el_entries;
};

typedef struct eventhandler_entry	*eventhandler_tag;

/* 
 * Fast handler lists require the eventhandler list be present
 * at link time.  They don't allow addition of entries to
 * unknown eventhandler lists, ie. each list must have an 
 * "owner".
 *
 * Fast handler lists must be defined once by the owner 
 * of the eventhandler list, and the declaration must be in 
 * scope at any point the list is manipulated.
 */
#define EVENTHANDLER_FAST_DECLARE(name, type)			\
extern struct eventhandler_list Xeventhandler_list_ ## name ;	\
struct eventhandler_entry_ ## name 				\
{								\
    struct eventhandler_entry	ee;				\
    type		eh_func;				\
};								\
struct __hack

#define EVENTHANDLER_FAST_DEFINE(name, type)				\
struct eventhandler_list Xeventhandler_list_ ## name = { #name };	\
struct __hack

#define EVENTHANDLER_FAST_INVOKE(name, args...)				\
do {									\
    struct eventhandler_list *_el = &Xeventhandler_list_ ## name ;	\
    struct eventhandler_entry *_ep = TAILQ_FIRST(&(_el->el_entries));	\
									\
    while (_ep != NULL) {						\
	((struct eventhandler_entry_ ## name *)_ep)->eh_func(_ep->ee_arg , ## args); \
	_ep = TAILQ_NEXT(_ep, ee_link);					\
    }									\
} while (0)

#define EVENTHANDLER_FAST_REGISTER(name, func, arg, priority) \
    eventhandler_register(Xeventhandler_list_ ## name, #name, func, arg, priority)

#define EVENTHANDLER_FAST_DEREGISTER(name, tag) \
    eventhandler_deregister(Xeventhandler_list ## name, tag)


/*
 * Slow handlers are entirely dynamic; lists are created
 * when entries are added to them, and thus have no concept of "owner".
 *
 * Slow handlers need to be declared, but do not need to be defined. The
 * declaration must be in scope wherever the handler is to be invoked.
 */
#define EVENTHANDLER_DECLARE(name, type)	\
struct eventhandler_entry_ ## name 		\
{						\
    struct eventhandler_entry	ee;		\
    type		eh_func;		\
};						\
struct __hack

#define EVENTHANDLER_INVOKE(name, args...)				\
do {									\
    struct eventhandler_list *_el;					\
    struct eventhandler_entry *_ep;					\
									\
    if ((_el = eventhandler_find_list(#name)) != NULL) {		\
	for (_ep = TAILQ_FIRST(&(_el->el_entries));			\
	     _ep != NULL;						\
	     _ep = TAILQ_NEXT(_ep, ee_link)) {				\
	    ((struct eventhandler_entry_ ## name *)_ep)->eh_func(_ep->ee_arg , ## args); \
	}								\
    }									\
} while (0)

#define EVENTHANDLER_REGISTER(name, func, arg, priority) \
    eventhandler_register(NULL, #name, func, arg, priority)

#define EVENTHANDLER_DEREGISTER(name, tag) 		\
do {							\
    struct eventhandler_list *_el;			\
							\
    if ((_el = eventhandler_find_list(#name)) != NULL)	\
	eventhandler_deregister(_el, tag);		\
} while(0)


#ifdef _KERNEL
extern eventhandler_tag	eventhandler_register(struct eventhandler_list *list, 
					      const char *name,
					      void *func, 
					      void *arg, 
					      int priority);
extern void		eventhandler_deregister(struct eventhandler_list *list,
						eventhandler_tag tag);
extern struct eventhandler_list	*eventhandler_find_list(const char *name);
#endif /* _KERNEL */

/*
 * Standard system event queues.
 */

/* Generic priority levels */
#define	EVENTHANDLER_PRI_FIRST	0
#define	EVENTHANDLER_PRI_ANY	10000
#define	EVENTHANDLER_PRI_LAST	20000

/* BPF attach/detach events */
struct ifnet;
typedef void (*bpf_track_fn)(void *, struct ifnet *, int /* dlt */,
    int /* 1 =>'s attach */);
EVENTHANDLER_DECLARE(bpf_track, bpf_track_fn);

struct image_params;
struct proc;

/* Shutdown events */
typedef void (*shutdown_fn) (void *, int);

#define	SHUTDOWN_PRI_FIRST	EVENTHANDLER_PRI_FIRST
#define	SHUTDOWN_PRI_SECOND	(EVENTHANDLER_PRI_FIRST + 1)
#define	SHUTDOWN_PRI_THIRD	(EVENTHANDLER_PRI_FIRST + 2)
#define	SHUTDOWN_PRI_DEFAULT	EVENTHANDLER_PRI_ANY
#define	SHUTDOWN_PRI_DRIVER	(EVENTHANDLER_PRI_ANY + 5000)
#define	SHUTDOWN_PRI_LAST	EVENTHANDLER_PRI_LAST
typedef void (*execlist_fn)(void *, struct image_params *);
typedef void (*exit_list_fn)(void *, struct proc *);

EVENTHANDLER_DECLARE(shutdown_pre_sync, shutdown_fn);	/* before fs sync */
EVENTHANDLER_DECLARE(shutdown_post_sync, shutdown_fn);	/* after fs sync */
EVENTHANDLER_DECLARE(shutdown_final, shutdown_fn);
EVENTHANDLER_DECLARE(process_exec, execlist_fn);
EVENTHANDLER_DECLARE(process_exit, exit_list_fn);

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_EVENTHANDLER_H_ */
