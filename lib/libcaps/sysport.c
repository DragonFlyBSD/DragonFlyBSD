/*
 * Copyright (c) 2003 Galen Sampson <galen_sampson@yahoo.com>
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/lib/libcaps/sysport.c,v 1.3 2003/12/07 04:21:52 dillon Exp $
 */
#include "defs.h"

#include "sendsys.h"
#include <sys/syscall.h>
#include <sys/sysproto.h>
#include <sys/sysunion.h>

/* XXX Temporary */
#include <unistd.h>

#ifdef DEBUG
#include <stdio.h>
#endif

static int sysport_putport(lwkt_port_t port, lwkt_msg_t msg);
static void sysport_loop(void *dummy);

struct thread sys_td;
lwkt_port_t sysport;

void
sysport_init(void)
{
    lwkt_init_thread(&sys_td, libcaps_alloc_stack(THREAD_STACK),
		    TDF_SYSTHREAD, mycpu);
    sysport = &sys_td.td_msgport;
    sysport->mp_putport = sysport_putport;
    sysport->mp_flags = MSGPORTF_WAITING;	/* XXX temporary */
    cpu_set_thread_handler(&sys_td, lwkt_exit, sysport_loop, NULL);
}

/************************************************************************
 *                             PORT FUNCTIONS                           *
 ************************************************************************/

/*
 * XXX We might need to separte this function in the way *lwkt_putport*
 * is separated in the case of multiple cpus. Secifically we will need
 * a lwkt_sysputport_remote().
 */
static
int
sysport_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    int error = 0;
    thread_t td = port->mp_td;

    /**
     * XXX sendsys() will only allow asynchronous messages from uid 0. This
     * is a kernel issue. See note below.
     */
    if(msg->ms_flags & MSGF_ASYNC)
    {
        /**
         * The message is not done.
         */
        msg->ms_flags &= ~MSGF_DONE;
        error = sendsys(NULL, msg, msg->ms_msgsize);
	if(error == EASYNC)
	{
	    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
            msg->ms_flags |= MSGF_QUEUED;
            /**
             * Shouldn't need this check, we are always waiting
             */
            if(port->mp_flags & MSGPORTF_WAITING)
            {
                lwkt_schedule(td);
            }
        }
#ifdef DEBUG
        printf("async return error %d\n", error);
#endif
    }

    /**
     * XXX this is a temporary hack until the kernel changes to implement
     * the desired asynchronous goals.
     *
     * The current asynchronous messaging systemcall interface that sendsys
     * uses has some potential security issues and is limited to use by the
     * superuser only.  Synchronous messages are allowed by anyone.  Sendsys
     * returns EPERM in the case where you are not the superuser but tried to
     * send an asynchonous message.
     *
     * If you are not the super user then the system call will be made again,
     * but without MSGF_ASYNC set.
     */
    if(error != EASYNC && error == EPERM)
    {
	msg->ms_flags &= ~MSGF_ASYNC;
#ifdef DEBUG
        printf("Warning, only super user can send asynchonous system messages\n");
#endif
    }

    /**
     * The message is synchronous.  Send it sychronously.
     */
    if((msg->ms_flags & MSGF_ASYNC) == 0)
    {
        error = sendsys(NULL, msg, msg->ms_msgsize);
	msg->ms_flags |= MSGF_DONE;
    }

    return(error);
}

void *
lwkt_syswaitport(lwkt_msg_t msg)
{
    lwkt_msg_t rmsg;

    /**
     * Block awaiting a return from the kernel.
     */
    for(rmsg = (lwkt_msg_t)sendsys(NULL, NULL, -1); rmsg != msg; )
    {
        usleep(1000000 / 10);
        rmsg = (lwkt_msg_t)sendsys(NULL, NULL, -1);
#ifdef DEBUG
        printf("    rmsg %p\n", rmsg);
#endif
    }

    return msg;
}

/************************************************************************
 *                            THREAD FUNCTIONS                          *
 ************************************************************************/
/* 
 * XXX Temporary function that provides a mechanism to return an asynchronous
 * message completed by the kernel to be returned to the port it originated
 * from.
 */
static 
void
sysport_loop(void *dummy)
{
    lwkt_msg_t msg;

    for(;;)
    {
        msg = lwkt_waitport(&curthread->td_msgport, NULL);

        msg = lwkt_syswaitport(msg);

        /**
         * The message was asynchronous
         */
        if(msg->ms_flags & MSGF_ASYNC)
        {
            lwkt_replymsg(msg, msg->ms_error);
        }
        else
        {
            msg->ms_flags |= MSGF_DONE;
        }
    }
}
