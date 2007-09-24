/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/platform/pc64/amd64/systimer.c,v 1.2 2007/09/24 03:24:45 yanyh Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/systimer.h>
#include <sys/sysctl.h>
#include <sys/signal.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <sys/time.h>
#include <machine/globaldata.h>
#include <machine/md_var.h>

int adjkerntz;
int wall_cmos_clock = 0;

/*
 * SYSTIMER IMPLEMENTATION
 */
/*
 * Initialize the systimer subsystem, called from MI code in early boot.
 */
void
cpu_initclocks(void *arg __unused)
{
}

/*
 * Configure the interrupt for our core systimer.  Use the kqueue timer
 * support functions.
 */
void
cputimer_intr_config(struct cputimer *timer)
{
}

/*
 * Reload the interrupt for our core systimer.  Because the caller's
 * reload calculation can be negatively indexed, we need a minimal
 * check to ensure that a reasonable reload value is selected. 
 */
void
cputimer_intr_reload(sysclock_t reload)
{
}

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(time_t base)
{
}

/*
 * Write system time back to the RTC
 */
void
resettodr(void)
{
}

void
DELAY(int usec)
{
}

void
DRIVERSLEEP(int usec)
{
}

