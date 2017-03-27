/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/thread.h>

#include <sys/cpuhelper.h>

static struct thread	*cpuhelper[MAXCPU];
static int		(*cpuhelper_saved_putport)(lwkt_port_t, lwkt_msg_t);

static __inline lwkt_port_t
cpuhelper_port(int cpuid)
{

	return (&cpuhelper[cpuid]->td_msgport);
}

/*
 * To prevent deadlocking, we must execute these self-referential messages
 * synchronously, effectively turning the message into a direct procedure
 * call.
 */
static int
cpuhelper_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
	struct cpuhelper_msg *msg = (struct cpuhelper_msg *)lmsg;


	if ((lmsg->ms_flags & MSGF_SYNC) && port == &curthread->td_msgport) {
		msg->ch_cb(msg);
		return (EASYNC);
	} else {
		return (cpuhelper_saved_putport(port, lmsg));
	}
}

static void
cpuhelper_mainloop(void *arg __unused)
{
	struct cpuhelper_msg *msg;


	while ((msg = lwkt_waitport(&curthread->td_msgport, 0))) {
		KASSERT(msg->ch_cb != NULL, ("cpuhelper%d: badmsg", mycpuid));
		msg->ch_cb(msg);
	}
	/* NEVER REACHED */
}

void
cpuhelper_initmsg(struct cpuhelper_msg *msg, lwkt_port_t rport,
    cpuhelper_cb_t cb, void *cbarg, int flags)
{

	lwkt_initmsg(&msg->ch_lmsg, rport, flags);
	msg->ch_cb = cb;
	msg->ch_cbarg = cbarg;
	msg->ch_cbarg1 = 0;
}

void
cpuhelper_replymsg(struct cpuhelper_msg *msg, int error)
{

	lwkt_replymsg(&msg->ch_lmsg, error);
}

int
cpuhelper_domsg(struct cpuhelper_msg *msg, int cpuid)
{

	KASSERT(cpuid >= 0 && cpuid < ncpus, ("invalid cpuid"));
	return (lwkt_domsg(cpuhelper_port(cpuid), &msg->ch_lmsg, 0));
}

void
cpuhelper_assert(int cpuid, bool in)
{

#ifdef INVARIANTS
	KASSERT(cpuid >= 0 && cpuid < ncpus, ("invalid cpuid"));
	if (in) {
		KASSERT(&curthread->td_msgport == cpuhelper_port(cpuid),
		    ("not in cpuhelper%d", cpuid));
	} else {
		KASSERT(&curthread->td_msgport != cpuhelper_port(cpuid),
		    ("in cpuhelper%d", cpuid));
	}
#endif
}

static void
cpuhelper_init(void)
{
	int i;

	/*
	 * Create per-cpu helper threads.
	 */
	for (i = 0; i < ncpus; ++i) {
		lwkt_port_t port;

		lwkt_create(cpuhelper_mainloop, NULL, &cpuhelper[i], NULL,
		    TDF_NOSTART|TDF_FORCE_SPINPORT|TDF_FIXEDCPU, i,
		    "cpuhelper %d", i);

		/*
		 * Override the putport function.  Our custom function checks
		 * for self-references.
		 */
		port = cpuhelper_port(i);
		if (cpuhelper_saved_putport == NULL)
			cpuhelper_saved_putport = port->mp_putport;
		KKASSERT(cpuhelper_saved_putport == port->mp_putport);
		port->mp_putport = cpuhelper_putport;

		lwkt_schedule(cpuhelper[i]);
	}
}
SYSINIT(cpuhelper, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, cpuhelper_init, NULL);
