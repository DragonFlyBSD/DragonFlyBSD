/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/spinlock2.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/filedesc.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/wdog.h>
#include <machine/limits.h>

static LIST_HEAD(, watchdog) wdoglist = LIST_HEAD_INITIALIZER(&wdoglist);
static struct spinlock	wdogmtx;
static struct callout	wdog_callout;

static int wdog_auto_enable = 1;
static int wdog_auto_period = WDOG_DEFAULT_PERIOD;

static void wdog_reset_all(void *unused);

void
wdog_register(struct watchdog *wd)
{
	spin_lock(&wdogmtx);
	wd->period = WDOG_DEFAULT_PERIOD;
	LIST_INSERT_HEAD(&wdoglist, wd, link);
	spin_unlock(&wdogmtx);

	wdog_reset_all(NULL);

	kprintf("wdog: Watchdog %s registered, max period = %ds , period = %ds\n",
	    wd->name, wd->period_max, wd->period);
}

void
wdog_unregister(struct watchdog *wd)
{
	spin_lock(&wdogmtx);
	LIST_REMOVE(wd, link);
	spin_unlock(&wdogmtx);

	kprintf("wdog: Watchdog %s unregistered\n", wd->name);
}

static int
wdog_reset(struct watchdog *wd)
{
	return (wd->period = wd->wdog_fn(wd->arg, wd->period));
}

static void
wdog_reset_all(void *unused)
{
	struct watchdog *wd;
	int period, min_period = INT_MAX;

	spin_lock(&wdogmtx);
	if (LIST_EMPTY(&wdoglist))
		goto done;
	LIST_FOREACH(wd, &wdoglist, link) {
		period = wdog_reset(wd);
		if (period < min_period)
			min_period = period;
	}
	if (wdog_auto_enable)
		callout_reset(&wdog_callout, min_period * hz / 2, wdog_reset_all, NULL);

	wdog_auto_period = min_period;

done:
	spin_unlock(&wdogmtx);
}

static void
wdog_set_period(int period)
{
	struct watchdog *wd;

	spin_lock(&wdogmtx);
	LIST_FOREACH(wd, &wdoglist, link) {
		/* XXX: check for period_max */
		wd->period = period;
	}
	spin_unlock(&wdogmtx);
}


static int
wdog_sysctl_auto(SYSCTL_HANDLER_ARGS)
{
	int		error;

	error = sysctl_handle_int(oidp, &wdog_auto_enable, 1, req);
	if (error || req->newptr == NULL)
		return error;

	/* has changed, do something */
	callout_stop(&wdog_callout);
	if (wdog_auto_enable) {
		wdog_reset_all(NULL);
	}

	kprintf("wdog: In-kernel automatic watchdog reset %s\n",
	    (wdog_auto_enable)?"enabled":"disabled");

	return 0;
}

static int
wdog_sysctl_period(SYSCTL_HANDLER_ARGS)
{
	int		error;

	error = sysctl_handle_int(oidp, &wdog_auto_period, WDOG_DEFAULT_PERIOD, req);
	if (error || req->newptr == NULL)
		return error;

	/* has changed, do something */
	callout_stop(&wdog_callout);
	wdog_set_period(wdog_auto_period);
	wdog_reset_all(NULL);

	if (wdog_auto_period != 0)
		kprintf("wdog: Watchdog period set to %ds\n", wdog_auto_period);
	else
		kprintf("wdog: Disabled watchdog(s)\n");

	return 0;
}

void
wdog_disable(void)
{
	callout_stop(&wdog_callout);
	wdog_set_period(0);
	wdog_reset_all(NULL);
}

static SYSCTL_NODE(_kern, OID_AUTO, watchdog, CTLFLAG_RW, 0, "watchdog");
SYSCTL_PROC(_kern_watchdog, OID_AUTO, auto, CTLTYPE_INT | CTLFLAG_RW,
			NULL, 0, wdog_sysctl_auto, "I", "auto in-kernel watchdog reset "
			"(0 = disabled, 1 = enabled)");
SYSCTL_PROC(_kern_watchdog, OID_AUTO, period, CTLTYPE_INT | CTLFLAG_RW,
			NULL, 0, wdog_sysctl_period, "I", "watchdog period "
			"(value in seconds)");


static int
wdog_ioctl(struct dev_ioctl_args *ap)
{
	if (wdog_auto_enable)
		return EINVAL;

	if (ap->a_cmd == WDIOCRESET) {
		wdog_reset_all(NULL);
	} else {
		return EINVAL;
	}

	return 0;
}

static struct dev_ops wdog_ops = {
	{ "wdog", 0, 0 },
	.d_ioctl = 	wdog_ioctl,
};

static void
wdog_init(void)
{
	spin_init(&wdogmtx, "wdog");
	make_dev(&wdog_ops, 0,
	    UID_ROOT, GID_WHEEL, 0600, "wdog");
	callout_init_mp(&wdog_callout);

	kprintf("wdog: In-kernel automatic watchdog reset %s\n",
	    (wdog_auto_enable)?"enabled":"disabled");
}

static void
wdog_uninit(void)
{
	callout_stop(&wdog_callout);
	callout_deactivate(&wdog_callout);
	dev_ops_remove_all(&wdog_ops);
	spin_uninit(&wdogmtx);
}

SYSINIT(wdog_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, wdog_init, NULL);
SYSUNINIT(wdog_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, wdog_uninit, NULL);
