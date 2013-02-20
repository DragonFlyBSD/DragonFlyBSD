/*-
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
 * Copyright (c) 2008 The DragonFly Project.
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

/*
 * Timer device driver for power management events.
 * The code for suspend/resume is derived from APM device driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/thread2.h>
#include <machine/clock.h>

#include <bus/isa/isavar.h>

static devclass_t pmtimer_devclass;

/* reject any PnP devices for now */
static struct isa_pnp_id pmtimer_ids[] = {
	{0}
};

static int
pmtimer_probe(device_t dev)
{

	if (ISA_PNP_PROBE(device_get_parent(dev), dev, pmtimer_ids) == ENXIO) {
		return (ENXIO);
	}

	/* only one instance always */
	return (device_get_unit(dev));
}

static struct timeval suspend_time;
static struct timeval diff_time;

static int
pmtimer_suspend(device_t dev)
{
	crit_enter();
	microtime(&diff_time);
	inittodr(0);
	microtime(&suspend_time);
	timevalsub(&diff_time, &suspend_time);
	crit_exit();
	return (0);
}

static int
pmtimer_resume(device_t dev)
{
	u_int second, minute, hour;
	struct timeval resume_time;

	/* modified for adjkerntz */
	crit_enter();
	timer_restore();		/* restore the all timers */
	inittodr(0);			/* adjust time to RTC */
	microtime(&resume_time);

	crit_exit();
	second = resume_time.tv_sec - suspend_time.tv_sec; 
	hour = second / 3600;
	second %= 3600;
	minute = second / 60;
	second %= 60;
	log(LOG_NOTICE, "wakeup from sleeping state (slept %02d:%02d:%02d)\n",
		hour, minute, second);
	return (0);
}

/*
 * Because pmtimer is a static device that always exists under any attached
 * isa device, and not scanned by the isa device, we need an identify
 * function to install the device.
 */
static device_method_t pmtimer_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	bus_generic_identify),
	DEVMETHOD(device_probe,		pmtimer_probe),
	DEVMETHOD(device_attach,	bus_generic_attach),
	DEVMETHOD(device_suspend,	pmtimer_suspend),
	DEVMETHOD(device_resume,	pmtimer_resume),
	DEVMETHOD_END
};

static driver_t pmtimer_driver = {
	"pmtimer",
	pmtimer_methods,
	1,		/* no softc */
};

DRIVER_MODULE(pmtimer, isa, pmtimer_driver, pmtimer_devclass, NULL, NULL);
