/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
/*
 * Install a signal handler for SIGTERM which shuts down the virtual kernel
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <ddb/ddb.h>

#include <sys/thread2.h>

#include <machine/trap.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <machine/cpu.h>

#include <err.h>
#include <signal.h>
#include <unistd.h>

static void shutdownsig(int signo);
static void shutdown_intr(void *arg __unused, void *frame __unused);

static
void
initshutdown(void *arg __unused)
{
	struct sigaction sa;

	bzero(&sa, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags |= SA_NODEFER;
	sa.sa_handler = shutdownsig;
	sigaction(SIGTERM, &sa, NULL);

	register_int_virtual(2, shutdown_intr, NULL, "shutdown", NULL,
	    INTR_MPSAFE);
}

static
void
shutdownsig(int signo)
{
	signalintr(2);
}

SYSINIT(initshutdown, SI_BOOT2_PROC0, SI_ORDER_ANY, 
	initshutdown, NULL); 

/*
 * DragonFly-safe interrupt thread.  We are the only handler on interrupt
 * #2 so we can just steal the thread's context forever.
 */
static
void
shutdown_intr(void *arg __unused, void *frame __unused)
{
	kprintf("Caught SIGTERM from host system. Shutting down...\n");
	if (initproc != NULL) {
		ksignal(initproc, SIGUSR2);
	} else {
		reboot(RB_POWEROFF);
	}	
}

