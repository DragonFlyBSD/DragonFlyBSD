/*-
 * Copyright (c) 2000, 2001 Michael Smith
 * Copyright (c) 2000 BSDi
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
 * $FreeBSD: src/sys/dev/acpica/acpi_timer.c,v 1.35 2004/07/22 05:42:14 njl Exp $
 */
#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/rman.h>

#include <machine/lock.h>
#include <bus/pci/pcivar.h>

#include "acpi.h"
#include "accommon.h"
#include "acpivar.h"

/*
 * A timecounter based on the free-running ACPI timer.
 *
 * Based on the i386-only mp_clock.c by <phk@FreeBSD.ORG>.
 */

/* Hooks for the ACPICA debugging infrastructure */
#define _COMPONENT	ACPI_TIMER
ACPI_MODULE_NAME("TIMER")

static device_t			acpi_timer_dev;
static UINT32			acpi_timer_resolution;
static sysclock_t		acpi_last_counter;

static sysclock_t acpi_timer_get_timecount(void);
static sysclock_t acpi_timer_get_timecount24(void);
static sysclock_t acpi_timer_get_timecount_safe(void);
static void acpi_timer_construct(struct cputimer *timer, sysclock_t oldclock);

static struct cputimer acpi_cputimer = {
	SLIST_ENTRY_INITIALIZER,
	"ACPI",
	CPUTIMER_PRI_ACPI,
	CPUTIMER_ACPI,
	acpi_timer_get_timecount_safe,
	cputimer_default_fromhz,
	cputimer_default_fromus,
	acpi_timer_construct,
	cputimer_default_destruct,
	ACPI_PM_TIMER_FREQUENCY,
	0, 0, 0
};

static int	acpi_timer_identify(driver_t *driver, device_t parent);
static int	acpi_timer_probe(device_t dev);
static int	acpi_timer_attach(device_t dev);
static int	acpi_timer_sysctl_freq(SYSCTL_HANDLER_ARGS);

static int	acpi_timer_test(void);

static device_method_t acpi_timer_methods[] = {
    DEVMETHOD(device_identify,	acpi_timer_identify),
    DEVMETHOD(device_probe,	acpi_timer_probe),
    DEVMETHOD(device_attach,	acpi_timer_attach),

    DEVMETHOD_END
};

static driver_t acpi_timer_driver = {
    "acpi_timer",
    acpi_timer_methods,
    0,
};

static devclass_t acpi_timer_devclass;
DRIVER_MODULE(acpi_timer, acpi, acpi_timer_driver, acpi_timer_devclass, NULL, NULL);
MODULE_DEPEND(acpi_timer, acpi, 1, 1, 1);

/*
 * Locate the ACPI timer using the FADT, set up and allocate the I/O resources
 * we will be using.
 */
static int
acpi_timer_identify(driver_t *driver, device_t parent)
{
    device_t dev;

    /*
     * Just try once, do nothing if the 'acpi' bus is rescanned.
     */
    if (device_get_state(parent) == DS_ATTACHED)
	return (0);

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (acpi_disabled("timer") || (acpi_quirks & ACPI_Q_TIMER) ||
	acpi_timer_dev)
	return (ENXIO);

    if ((dev = BUS_ADD_CHILD(parent, parent, 0, "acpi_timer", 0)) == NULL) {
	device_printf(parent, "could not add acpi_timer0\n");
	return (ENXIO);
    }
    acpi_timer_dev = dev;

    return (0);
}

static int
acpi_timer_probe(device_t dev)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (dev != acpi_timer_dev)
	return (ENXIO);

    if (ACPI_FAILURE(AcpiGetTimerResolution(&acpi_timer_resolution)))
	return (ENXIO);

    return (0);
}

static int
acpi_timer_attach(device_t dev)
{
    char desc[40];
    int i, j;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /*
     * If all tests of the counter succeed, use the ACPI-fast method.  If
     * at least one failed, default to using the safe routine, which reads
     * the timer multiple times to get a consistent value before returning.
     */
    j = 0;
    for (i = 0; i < 10; i++)
	j += acpi_timer_test();
    if (j == 10) {
	if (acpi_timer_resolution == 32) {
	    acpi_cputimer.name = "ACPI-fast";
	    acpi_cputimer.count = acpi_timer_get_timecount;
	} else {
	    acpi_cputimer.name = "ACPI-fast24";
	    acpi_cputimer.count = acpi_timer_get_timecount24;
	}
    } else {
	if (acpi_timer_resolution == 32)
	    acpi_cputimer.name = "ACPI-safe";
	else
	    acpi_cputimer.name = "ACPI-safe24";
	acpi_cputimer.count = acpi_timer_get_timecount_safe;
    }

    ksprintf(desc, "%u-bit timer at 3.579545MHz", acpi_timer_resolution);
    device_set_desc_copy(dev, desc);

    cputimer_register(&acpi_cputimer);
    cputimer_select(&acpi_cputimer, 0);

    return (0);
}

/*
 * Construct the timer.  Adjust the base so the system clock does not
 * jump weirdly.
 */
static void
acpi_timer_construct(struct cputimer *timer, sysclock_t oldclock)
{
    timer->base = 0;
    timer->base = oldclock - acpi_timer_get_timecount_safe();
}

/*
 * Fetch current time value from reliable hardware.
 *
 * The cputimer interface requires a 32 bit return value.  If the ACPI timer
 * is only 24 bits then we have to keep track of the upper 8 bits on our
 * own.
 *
 * XXX we could probably get away with using a per-cpu field for this and
 * just use interrupt disablement instead of clock_lock.
 */
static sysclock_t
acpi_timer_get_timecount24(void)
{
    sysclock_t counter;

    clock_lock();
    AcpiGetTimer(&counter);
    if (counter < acpi_last_counter)
	acpi_cputimer.base += 0x01000000;
    acpi_last_counter = counter;
    counter += acpi_cputimer.base;
    clock_unlock();
    return (counter);
}

static sysclock_t
acpi_timer_get_timecount(void)
{
    sysclock_t counter;

    AcpiGetTimer(&counter);
    return (counter + acpi_cputimer.base);
}

/*
 * Fetch current time value from hardware that may not correctly
 * latch the counter.  We need to read until we have three monotonic
 * samples and then use the middle one, otherwise we are not protected
 * against the fact that the bits can be wrong in two directions.  If
 * we only cared about monosity, two reads would be enough.
 */
static sysclock_t
acpi_timer_get_timecount_safe(void)
{
    u_int u1, u2, u3;

    if (acpi_timer_resolution != 32)
	clock_lock();

    AcpiGetTimer(&u2);
    AcpiGetTimer(&u3);
    do {
	u1 = u2;
	u2 = u3;
	AcpiGetTimer(&u3);
    } while (u1 > u2 || u2 > u3);

    if (acpi_timer_resolution != 32) {
	if (u2 < acpi_last_counter)
	    acpi_cputimer.base += 0x01000000;
	acpi_last_counter = u2;
	clock_unlock();
    }
    return (u2 + acpi_cputimer.base);
}

/*
 * Timecounter freqency adjustment interface.
 */ 
static int
acpi_timer_sysctl_freq(SYSCTL_HANDLER_ARGS)
{
    int error;
    u_int freq;
 
    if (acpi_cputimer.freq == 0)
	return (EOPNOTSUPP);
    freq = acpi_cputimer.freq;
    error = sysctl_handle_int(oidp, &freq, 0, req);
    if (error == 0 && req->newptr != NULL)
	cputimer_set_frequency(&acpi_cputimer, freq);

    return (error);
}
 
SYSCTL_PROC(_machdep, OID_AUTO, acpi_timer_freq, CTLTYPE_INT | CTLFLAG_RW,
    0, sizeof(u_int), acpi_timer_sysctl_freq, "I", "ACPI timer frequency");

/*
 * Some ACPI timers are known or believed to suffer from implementation
 * problems which can lead to erroneous values being read.  This function
 * tests for consistent results from the timer and returns 1 if it believes
 * the timer is consistent, otherwise it returns 0.
 *
 * It appears the cause is that the counter is not latched to the PCI bus
 * clock when read:
 *
 * ] 20. ACPI Timer Errata
 * ]
 * ]   Problem: The power management timer may return improper result when
 * ]   read. Although the timer value settles properly after incrementing,
 * ]   while incrementing there is a 3nS window every 69.8nS where the
 * ]   timer value is indeterminate (a 4.2% chance that the data will be
 * ]   incorrect when read). As a result, the ACPI free running count up
 * ]   timer specification is violated due to erroneous reads.  Implication:
 * ]   System hangs due to the "inaccuracy" of the timer when used by
 * ]   software for time critical events and delays.
 * ]
 * ] Workaround: Read the register twice and compare.
 * ] Status: This will not be fixed in the PIIX4 or PIIX4E, it is fixed
 * ] in the PIIX4M.
 */

static int
acpi_timer_test(void)
{
    uint32_t	last, this;
    int		min, max, max2, n, delta;
    register_t	s;

    min = INT32_MAX;
    max = max2 = 0;

    /* Test the timer with interrupts disabled to get accurate results. */
#if defined(__x86_64__)
    s = read_rflags();
#else
#error "no read_*flags"
#endif
    cpu_disable_intr();
    AcpiGetTimer(&last);
    for (n = 0; n < 2000; n++) {
	AcpiGetTimer(&this);
	delta = acpi_TimerDelta(this, last);
	if (delta > max) {
	    max2 = max;
	    max = delta;
	} else if (delta > max2) {
	    max2 = delta;
	}
	if (delta < min)
	    min = delta;
	last = this;
    }
#if defined(__x86_64__)
    write_rflags(s);
#else
#error "no read_*flags"
#endif

    delta = max2 - min;
    if ((max - min > 8 || delta > 3) && vmm_guest == VMM_GUEST_NONE)
	n = 0;
    else if (min < 0 || max == 0 || max2 == 0)
	n = 0;
    else
	n = 1;
    if (bootverbose) {
	kprintf("ACPI timer looks %s min = %d, max = %d, width = %d\n",
		n ? "GOOD" : "BAD ",
		min, max, max - min);
    }

    return (n);
}
