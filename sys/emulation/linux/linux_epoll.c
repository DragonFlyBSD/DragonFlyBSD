/*-
 * Copyright (c) 2007 Roman Divacky
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
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kern_syscall.h>
#include <sys/event.h>
#include <sys/lock.h>
#include <sys/mplock2.h>
#include <sys/malloc.h>
#include <sys/ptrace.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/file.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <sys/exec.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/cpu.h>

#include "i386/linux.h"
#include "i386/linux_proto.h"
#include "linux_signal.h"
#include "linux_util.h"
#include "linux_epoll.h"


/* Create a new epoll file descriptor. */
int
sys_linux_epoll_create(struct linux_epoll_create_args *args)
{
        struct kqueue_args k_args;

        if (args->size <= 0)
                return (EINVAL);
        /* args->size is unused. Linux ignores it as well. */

        return (sys_kqueue(&k_args));
}

/* Structure converting function from epoll to kevent. */
static void
linux_epoll_to_kevent(int fd, struct linux_epoll_event *event, struct kevent *kevent)
{
        int filter = 0;
        int flags = kevent->flags;

        if (event->events & LINUX_EPOLLIN)
                filter |= EVFILT_READ;
        if (event->events & LINUX_EPOLLOUT)
                filter |= EVFILT_WRITE;
        if (event->events & LINUX_EPOLLPRI)
                filter |= EVFILT_READ;
        if (event->events & LINUX_EPOLLET)
                flags |= EV_CLEAR;
        if (event->events & LINUX_EPOLLONESHOT)
                flags |= EV_ONESHOT;

        EV_SET(kevent, fd, filter, flags, 0, 0, NULL);
}

/*
 * Structure converting function from kevent to epoll. In a case
 * this is called on error in registration we store the error in
 * event->data and pick it up later in linux_epoll_ctl().
 */
static void
linux_kevent_to_epoll(struct kevent *kevent, struct linux_epoll_event *event)
{
        if (kevent->flags & EV_ERROR) {
                event->data = kevent->data;
                return;
        }
        switch (kevent->filter) {
	case EVFILT_READ:
		if (kevent->data > 0)
			event->events = LINUX_EPOLLIN;
		event->data = kevent->ident;
		break;
	case EVFILT_WRITE:
		if (kevent->data > 0)
			event->events = LINUX_EPOLLOUT;
		event->data = kevent->ident;
		break;
        }
}

/*
 * Copyout callback used by kevent. This converts kevent
 * events to epoll events and copies them back to the
 * userspace. This is also called on error on registering
 * of the filter.
 */
static int
linux_kev_copyout(void *arg, struct kevent *kevp, int count, int *res)
{
        struct kevent_args *uap;
        struct linux_epoll_event *eep;
        int error, i;

        uap = (struct kevent_args*) arg;

        eep = kmalloc(sizeof(*eep) * count, M_TEMP, M_WAITOK | M_ZERO);

        for (i = 0; i < count; i++) {
                linux_kevent_to_epoll(&kevp[i], &eep[i]);
        }

        error = copyout(eep, uap->eventlist, count * sizeof(*eep));
        if (error == 0) {
                uap->eventlist = (struct kevent *)((char *)uap->eventlist + count * sizeof(*eep));
		*res += count;
	}

        kfree(eep, M_TEMP);
        return (error);
}

/*
 * Copyin callback used by kevent. This copies already
 * converted filters to the kevent internal memory.
 */
static int
linux_kev_copyin(void *arg, struct kevent *kevp, int maxevents, int *events)
{
        struct kevent_args *uap;

        uap = (struct kevent_args*) arg;

        memcpy(kevp, uap->changelist, maxevents * sizeof(*kevp));

        uap->changelist += maxevents;
	*events = maxevents;

        return (0);
}

/*
 * Load epoll filter, convert it to kevent filter
 * and load it into kevent subsystem.
 */
int
sys_linux_epoll_ctl(struct linux_epoll_ctl_args *args)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
        struct kevent_args k_args;
        struct kevent kev;
	struct kqueue *kq;
        struct linux_epoll_event le;
	struct file *fp = NULL;
        int error;

        error = copyin(args->event, &le, sizeof(le));
        if (error)
                return (error);
#ifdef DEBUG
        if (ldebug(epoll_ctl))
                kprintf(ARGS(epoll_ctl,"%i, %i, %i, %u"), args->epfd, args->op,
                        args->fd, le.events);
#endif
        k_args.fd = args->epfd;
        k_args.changelist = &kev;
        /* The epoll can register only 1 filter at once. */
        k_args.nchanges = 1;
        k_args.eventlist = NULL;
        k_args.nevents = 0;
        k_args.timeout = NULL;

        switch (args->op) {
        case LINUX_EPOLL_CTL_ADD:
                        kev.flags = EV_ADD | EV_ENABLE;
                break;
        case LINUX_EPOLL_CTL_MOD:
                        /* TODO: DELETE && ADD maybe? */
                        return (EINVAL);
                break;
        case LINUX_EPOLL_CTL_DEL:
                        kev.flags = EV_DELETE | EV_DISABLE;
                break;
        }
        linux_epoll_to_kevent(args->fd, &le, &kev);

	fp = holdfp(p->p_fd, args->epfd, -1);
	if (fp == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_KQUEUE) {
		fdrop(fp);
		return (EBADF);
	}

	kq = (struct kqueue *)fp->f_data;

	error = kern_kevent(kq, 0, &k_args.sysmsg_result, &k_args,
	    linux_kev_copyin, linux_kev_copyout, NULL);
        /* Check if there was an error during registration. */
        if (error == 0 && k_args.sysmsg_result != 0) {
                /* The copyout callback stored the error there. */
                error = le.data;
        }

	fdrop(fp);
        return (error);
}

/*
 * Wait for a filter to be triggered on the epoll file descriptor. */
int
sys_linux_epoll_wait(struct linux_epoll_wait_args *args)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
        struct timespec ts;
	struct kqueue *kq;
	struct file *fp = NULL;
        struct kevent_args k_args;
        int error;

        /* Convert from milliseconds to timespec. */
        ts.tv_sec = args->timeout / 1000;
        ts.tv_nsec = (args->timeout % 1000) * 1000 * 1000;

        k_args.fd = args->epfd;
        k_args.changelist = NULL;
        k_args.nchanges = 0;
        /*
         * We don't mind the bogus type-cast because
         * our copyout function knows about this and
         * handles it correctly.
         */
        k_args.eventlist = (struct kevent *)args->events;
        k_args.nevents = args->maxevents;
        k_args.timeout = &ts;

	fp = holdfp(p->p_fd, args->epfd, -1);
	if (fp == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_KQUEUE) {
		fdrop(fp);
		return (EBADF);
	}

	kq = (struct kqueue *)fp->f_data;

	error = kern_kevent(kq, args->maxevents, &args->sysmsg_result,
	    &k_args, linux_kev_copyin, linux_kev_copyout, &ts);

	fdrop(fp);
        /* translation? */
        return (error);
}
