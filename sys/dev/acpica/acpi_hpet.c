/*-
 * Copyright (c) 2005 Poul-Henning Kamp
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
 * $FreeBSD: src/sys/dev/acpica/acpi_hpet.c,v 1.12.2.1.2.1 2008/11/25 02:59:29 kensmith Exp $
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systimer.h>
#include <sys/rman.h>

#if !defined(KLD_MODULE)
#include <machine/clock.h>
#include <machine/pmap.h>
#endif

#include "acpi.h"
#include "accommon.h"
#include "acpivar.h"
#include "acpi_hpet.h"

#if !defined(KLD_MODULE)
#include <platform/pc64/acpica/acpi_sdt_var.h>
#endif

/* Hooks for the ACPICA debugging infrastructure */
#define _COMPONENT	ACPI_TIMER
ACPI_MODULE_NAME("HPET")

#define ACPI_HPET_MASK		((1UL << 32) - 1)
#define ACPI_HPET_HIBITS	(~ACPI_HPET_MASK)

static bus_space_handle_t	acpi_hpet_bsh;
static bus_space_tag_t		acpi_hpet_bst;
static u_long			acpi_hpet_res_start;
static uint32_t			acpi_hpet_offset;

struct acpi_hpet_softc {
	device_t		dev;
	struct resource		*mem_res;
	ACPI_HANDLE		handle;
};

#define DEV_HPET(x)	(acpi_get_magic(x) == (uintptr_t)&acpi_hpet_devclass)

static sysclock_t	acpi_hpet_get_timecount(void);
static void		acpi_hpet_construct(struct cputimer *, sysclock_t);

static int		acpi_hpet_identify(driver_t *, device_t);
static int		acpi_hpet_probe(device_t);
static int		acpi_hpet_attach(device_t);
static int		acpi_hpet_resume(device_t);
static int		acpi_hpet_suspend(device_t);

static void		acpi_hpet_test(struct acpi_hpet_softc *sc);
static void		acpi_hpet_enable(struct acpi_hpet_softc *);
static void		acpi_hpet_disable(struct acpi_hpet_softc *);

static char *hpet_ids[] = { "PNP0103", NULL };

static struct cputimer acpi_hpet_timer = {
	.next		= SLIST_ENTRY_INITIALIZER,
	.name		= "HPET",
	.pri		= CPUTIMER_PRI_HPET,
	.type		= CPUTIMER_HPET,
	.count		= acpi_hpet_get_timecount,
	.fromhz		= cputimer_default_fromhz,
	.fromus		= cputimer_default_fromus,
	.construct	= acpi_hpet_construct,
	.destruct	= cputimer_default_destruct,
	.freq		= 0	/* determined later */
};

static device_method_t acpi_hpet_methods[] = {
	DEVMETHOD(device_identify,	acpi_hpet_identify),
	DEVMETHOD(device_probe,		acpi_hpet_probe),
	DEVMETHOD(device_attach,	acpi_hpet_attach),
	DEVMETHOD(device_suspend,	acpi_hpet_suspend),
	DEVMETHOD(device_resume,	acpi_hpet_resume),
	DEVMETHOD_END
};

static driver_t acpi_hpet_driver = {
	"acpi_hpet",
	acpi_hpet_methods,
	sizeof(struct acpi_hpet_softc),
	.gpri = KOBJ_GPRI_ACPI+2
};

static devclass_t acpi_hpet_devclass;
DRIVER_MODULE(acpi_hpet, acpi, acpi_hpet_driver, acpi_hpet_devclass, NULL, NULL);
MODULE_DEPEND(acpi_hpet, acpi, 1, 1, 1);

static __inline __always_inline uint32_t
_acpi_get_timer(void)
{
	uint32_t counter;

	counter = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh,
				   HPET_MAIN_COUNTER);
	return counter + acpi_hpet_offset;
}

#if !defined(KLD_MODULE)
static vm_offset_t ptr = 0;

static int acpi_hpet_for_calibration = 1;
TUNABLE_INT("hw.calibrate_timers_with_hpet", &acpi_hpet_for_calibration);

static __inline __always_inline uint32_t
_acpi_get_timer_early(void)
{
	return readl(ptr + HPET_MAIN_COUNTER) + acpi_hpet_offset;
}

static sysclock_t
acpi_hpet_early_get_timecount(void)
{
	sysclock_t last_counter;
	sysclock_t next_counter;
	uint32_t counter;

	last_counter = acpi_hpet_timer.base;
	for (;;) {
		cpu_ccfence();
		counter = _acpi_get_timer_early();
		next_counter = (last_counter & ACPI_HPET_HIBITS) | counter;
		if (counter < (last_counter & ACPI_HPET_MASK))
			next_counter += (1LU << 32);
		if (atomic_fcmpset_long(&acpi_hpet_timer.base, &last_counter,
					next_counter)) {
			break;
		}
	}
	return next_counter;
}

static void
acpi_hpet_early_construct(struct cputimer *timer, sysclock_t oldclock)
{
	uint32_t val;

	val = readl(ptr + HPET_CONFIG);
	writel(ptr + HPET_CONFIG, val | HPET_CNF_ENABLE);

	/* See also the comment for acpi_hpet_construct() below. */
	acpi_hpet_offset = 0;
	acpi_hpet_offset = (uint32_t)oldclock - _acpi_get_timer_early();
	timer->base = oldclock;
}

static void
acpi_hpet_early_destruct(struct cputimer *timer)
{
	uint32_t val;

	val = readl(ptr + HPET_CONFIG);
	writel(ptr + HPET_CONFIG, val & ~HPET_CNF_ENABLE);
}

static int
acpi_hpet_early_init(void)
{
	uintmax_t freq;
	uint64_t old_tsc, new_tsc;
	uint32_t val, val2;

	val = readl(ptr + HPET_CONFIG);
	writel(ptr + HPET_CONFIG, val | HPET_CNF_ENABLE);

	/* Read basic statistics about the timer. */
	val = readl(ptr + HPET_PERIOD);
	if (val == 0) {
		kprintf("acpi_hpet: invalid period\n");
		val = readl(ptr + HPET_CONFIG);
		writel(ptr + HPET_CONFIG, val & ~HPET_CNF_ENABLE);
		return ENXIO;
	}

	freq = (1000000000000000LL + val / 2) / val;
	if (bootverbose) {
		val = readl(ptr + HPET_CAPABILITIES);
		kprintf("acpi_hpet: "
		    "vend: 0x%x, rev: 0x%x, num: %d, opts:%s%s\n",
		    val >> 16, val & HPET_CAP_REV_ID,
		    (val & HPET_CAP_NUM_TIM) >> 8,
		    (val & HPET_CAP_LEG_RT) ? " legacy_route" : "",
		    (val & HPET_CAP_COUNT_SIZE) ? " 64-bit" : "");
	}

#if 0
	if (ktestenv("debug.acpi.hpet_test"))
		acpi_hpet_test(sc);
#endif

	/*
	 * Don't attach if the timer never increments.  Since the spec
	 * requires it to be at least 10 MHz, it has to change in 1 us.
	 */
	val = readl(ptr + HPET_MAIN_COUNTER);
	/* This delay correspond to 1us, even at 6 GHz TSC. */
	old_tsc = rdtsc();
	do {
		cpu_pause();
		new_tsc = rdtsc();
	} while (new_tsc - old_tsc < 6000);
	val2 = readl(ptr + HPET_MAIN_COUNTER);
	if (val == val2) {
		kprintf("acpi_hpet: HPET never increments, disabling\n");
		val = readl(ptr + HPET_CONFIG);
		writel(ptr + HPET_CONFIG, val & ~HPET_CNF_ENABLE);
		return ENXIO;
	}

	val = readl(ptr + HPET_CONFIG);
	writel(ptr + HPET_CONFIG, val & ~HPET_CNF_ENABLE);
	acpi_hpet_timer.freq = freq;
	kprintf("acpi_hpet: frequency %lu\n", acpi_hpet_timer.freq);

	acpi_hpet_timer.count = acpi_hpet_early_get_timecount;
	acpi_hpet_timer.construct = acpi_hpet_early_construct;
	acpi_hpet_timer.destruct = acpi_hpet_early_destruct;

	cputimer_register(&acpi_hpet_timer);
	cputimer_select(&acpi_hpet_timer, 0);
	return 0;
}

static void
acpi_hpet_cputimer_register(void)
{
	ACPI_TABLE_HPET *hpet;
	vm_paddr_t hpet_paddr;

	if (acpi_hpet_for_calibration == 0)
		return;

	if (acpi_disabled("hpet"))
		return;

	hpet_paddr = sdt_search(ACPI_SIG_HPET);
	if (hpet_paddr == 0) {
		if (bootverbose)
			kprintf("acpi_hpet: can't locate HPET\n");
		return;
	}

	hpet = sdt_sdth_map(hpet_paddr);
	if (hpet == NULL)
		return;

	if (hpet->Header.Length < 56) {
		kprintf("acpi_hpet: HPET table too short. Length: 0x%x\n",
		    hpet->Header.Length);
		return;
	}

	if (hpet->Sequence != 0) {
		kprintf("acpi_hpet: "
		    "HPET table Sequence not 0. Sequence: 0x%x\n", hpet->Id);
		goto done;
	}

	acpi_hpet_res_start = hpet->Address.Address;
	if (acpi_hpet_res_start == 0)
		goto done;

	ptr = (vm_offset_t)pmap_mapdev(acpi_hpet_res_start, HPET_MEM_WIDTH);
	if (acpi_hpet_early_init() == 0) {
		i8254_cputimer_disable = 1;
	} else {
		pmap_unmapdev(ptr, HPET_MEM_WIDTH);
		ptr = 0;
	}

done:
	sdt_sdth_unmap(&hpet->Header);
	return;
}

TIMECOUNTER_INIT(acpi_hpet_init, acpi_hpet_cputimer_register);
#endif /* !KLD_MODULE */

/*
 * Locate the ACPI timer using the FADT, set up and allocate the I/O resources
 * we will be using.
 */
static int
acpi_hpet_identify(driver_t *driver, device_t parent)
{
	ACPI_TABLE_HPET *hpet;
	ACPI_TABLE_HEADER *hdr;
	ACPI_STATUS status;
	device_t child;

	/*
	 * Just try once, do nothing if the 'acpi' bus is rescanned.
	 */
	if (device_get_state(parent) == DS_ATTACHED)
		return 0;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	/* Only one HPET device can be added. */
	if (devclass_get_device(acpi_hpet_devclass, 0))
		return ENXIO;

#if !defined(KLD_MODULE)
	if (ptr != 0) {
		/* Use data from early boot for attachment. */
		child = BUS_ADD_CHILD(parent, parent, 0, "acpi_hpet", 0);
		if (child == NULL) {
			device_printf(parent, "%s: can't add acpi_hpet0\n",
			    __func__);
			return ENXIO;
		}

		/* Record a magic value so we can detect this device later. */
		acpi_set_magic(child, (uintptr_t)&acpi_hpet_devclass);

		if (bus_set_resource(child, SYS_RES_MEMORY, 0,
		    acpi_hpet_res_start, HPET_MEM_WIDTH, -1)) {
			device_printf(child,
			    "could not set iomem resources: 0x%jx, %d\n",
			    (uintmax_t)acpi_hpet_res_start, HPET_MEM_WIDTH);
			return ENOMEM;
		}

		return 0;
	}
#endif

	/* Currently, ID and minimum clock tick info is unused. */

	status = AcpiGetTable(ACPI_SIG_HPET, 1, &hdr);
	if (ACPI_FAILURE(status))
		return ENXIO;

	/*
	 * The unit number could be derived from hdr->Sequence but we only
	 * support one HPET device.
	 */
	hpet = (ACPI_TABLE_HPET *)hdr;
	if (hpet->Sequence != 0) {
		kprintf("ACPI HPET table warning: Sequence is non-zero (%d)\n",
			hpet->Sequence);
	}

	child = BUS_ADD_CHILD(parent, parent, 0, "acpi_hpet", 0);
	if (child == NULL) {
		device_printf(parent, "%s: can't add acpi_hpet0\n", __func__);
		return ENXIO;
	}

	/* Record a magic value so we can detect this device later. */
	acpi_set_magic(child, (uintptr_t)&acpi_hpet_devclass);

	acpi_hpet_res_start = hpet->Address.Address;
	if (bus_set_resource(child, SYS_RES_MEMORY, 0,
			     hpet->Address.Address, HPET_MEM_WIDTH, -1)) {
		device_printf(child, "could not set iomem resources: "
			      "0x%jx, %d\n", (uintmax_t)hpet->Address.Address,
			      HPET_MEM_WIDTH);
		return ENOMEM;
	}
	return 0;
}

static int
acpi_hpet_probe(device_t dev)
{
	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	if (acpi_disabled("hpet"))
		return ENXIO;

	if (!DEV_HPET(dev) &&
	    (ACPI_ID_PROBE(device_get_parent(dev), dev, hpet_ids) == NULL ||
	     device_get_unit(dev) != 0))
		return ENXIO;

	device_set_desc(dev, "High Precision Event Timer");
	return 0;
}

static int
acpi_hpet_attach(device_t dev)
{
	struct acpi_hpet_softc *sc;
	int rid;
	uint32_t val, val2;
	uintmax_t freq;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->handle = acpi_get_handle(dev);

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					     RF_ACTIVE);
	if (sc->mem_res == NULL) {
		/*
		 * We only need to make sure that main counter
		 * is accessable.
		 */
		device_printf(dev, "can't map %dB register space, try %dB\n",
			      HPET_MEM_WIDTH, HPET_MEM_WIDTH_MIN);
		rid = 0;
		sc->mem_res = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid,
				acpi_hpet_res_start,
				acpi_hpet_res_start + HPET_MEM_WIDTH_MIN - 1,
				HPET_MEM_WIDTH_MIN, RF_ACTIVE);
		if (sc->mem_res == NULL)
			return ENOMEM;
	}

	/* Validate that we can access the whole region. */
	if (rman_get_size(sc->mem_res) < HPET_MEM_WIDTH_MIN) {
		device_printf(dev, "memory region width %ld too small\n",
			      rman_get_size(sc->mem_res));
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->mem_res);
		return ENXIO;
	}

	acpi_hpet_bsh = rman_get_bushandle(sc->mem_res);
	acpi_hpet_bst = rman_get_bustag(sc->mem_res);

#if !defined(KLD_MODULE)
	if (ptr != 0) {
		/* Use data from early boot for attachment. */
		if (ktestenv("debug.acpi.hpet_test"))
			acpi_hpet_test(sc);
		return 0;
	}
#endif

	/* Be sure timer is enabled. */
	acpi_hpet_enable(sc);

	/* Read basic statistics about the timer. */
	val = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_PERIOD);
	if (val == 0) {
		device_printf(dev, "invalid period\n");
		acpi_hpet_disable(sc);
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->mem_res);
		return ENXIO;
	}

	freq = (1000000000000000LL + val / 2) / val;
	if (bootverbose) {
		val = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh,
				       HPET_CAPABILITIES);
		device_printf(dev,
		    "vend: 0x%x, rev: 0x%x, num: %d, opts:%s%s\n",
		    val >> 16, val & HPET_CAP_REV_ID,
		    (val & HPET_CAP_NUM_TIM) >> 8,
		    (val & HPET_CAP_LEG_RT) ? " legacy_route" : "",
		    (val & HPET_CAP_COUNT_SIZE) ? " 64-bit" : "");
	}

	if (ktestenv("debug.acpi.hpet_test"))
		acpi_hpet_test(sc);

	/*
	 * Don't attach if the timer never increments.  Since the spec
	 * requires it to be at least 10 MHz, it has to change in 1 us.
	 */
	val = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh,
			       HPET_MAIN_COUNTER);
	DELAY(1);
	val2 = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh,
				HPET_MAIN_COUNTER);
	if (val == val2) {
		device_printf(dev, "HPET never increments, disabling\n");
		acpi_hpet_disable(sc);
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->mem_res);
		return ENXIO;
	}

	acpi_hpet_timer.freq = freq;
	device_printf(dev, "frequency %lu\n", acpi_hpet_timer.freq);

	cputimer_register(&acpi_hpet_timer);
	cputimer_select(&acpi_hpet_timer, 0);

	return 0;
}

/*
 * Construct the timer.  Adjust the base so the system clock does not
 * jump weirdly.  We want it to remain monotonic, so setup acpi_hpet_offset
 * such that the low 32 bits continues sequencing relative to the low bits of
 * oldclock.
 */
static void
acpi_hpet_construct(struct cputimer *timer, sysclock_t oldclock)
{
	acpi_hpet_offset = 0;
	acpi_hpet_offset = (uint32_t)oldclock - _acpi_get_timer();
	timer->base = oldclock;
}

static sysclock_t
acpi_hpet_get_timecount(void)
{
	sysclock_t last_counter;
	sysclock_t next_counter;
	uint32_t counter;

	last_counter = acpi_hpet_timer.base;
	for (;;) {
		cpu_ccfence();
		counter = _acpi_get_timer();
		next_counter = (last_counter & ACPI_HPET_HIBITS) | counter;
		if (counter < (last_counter & ACPI_HPET_MASK))
			next_counter += (1LU << 32);
		if (atomic_fcmpset_long(&acpi_hpet_timer.base, &last_counter,
					next_counter)) {
			break;
		}
	}
	return next_counter;
}

static void
acpi_hpet_enable(struct acpi_hpet_softc *sc)
{
	uint32_t val;

	val = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_CONFIG);
	bus_space_write_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_CONFIG,
			  val | HPET_CNF_ENABLE);
}

static void
acpi_hpet_disable(struct acpi_hpet_softc *sc)
{
	uint32_t val;

	val = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_CONFIG);
	bus_space_write_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_CONFIG,
			  val & ~HPET_CNF_ENABLE);
}

static int
acpi_hpet_suspend(device_t dev)
{
	/*
	 * According to IA-PC HPET specification rev 1.0a
	 *
	 * Page 10, 2.3.3:
	 * "1. The Event Timer registers (including the main counter)
	 *  are not expected to be preserved through an S3, S4, or S5
	 *  state."
	 *
	 * Page 11, 2.3.3:
	 * "3. The main counter is permitted, but not required, to run
	 *  during S1 or S2 states. ..."
	 *
	 * These mean we are not allowed to enter any of Sx states,
	 * if HPET is used as the sys_cputimer.
	 */
	if (sys_cputimer != &acpi_hpet_timer) {
		struct acpi_hpet_softc *sc;

		sc = device_get_softc(dev);
		acpi_hpet_disable(sc);

		return 0;
	} else {
		return EOPNOTSUPP;
	}
}

static int
acpi_hpet_resume(device_t dev)
{
	if (sys_cputimer != &acpi_hpet_timer) {
		struct acpi_hpet_softc *sc;

		sc = device_get_softc(dev);
		acpi_hpet_enable(sc);
	}
	return 0;
}

/* Print some basic latency/rate information to assist in debugging. */
static void
acpi_hpet_test(struct acpi_hpet_softc *sc)
{
	int i;
	uint32_t u1, u2;
	struct timeval b0, b1, b2;
	struct timespec ts;

	microuptime(&b0);
	microuptime(&b0);
	microuptime(&b1);
	u1 = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_MAIN_COUNTER);
	for (i = 1; i < 1000; i++) {
		u2 = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh,
				      HPET_MAIN_COUNTER);
	}
	microuptime(&b2);
	u2 = bus_space_read_4(acpi_hpet_bst, acpi_hpet_bsh, HPET_MAIN_COUNTER);

	timevalsub(&b2, &b1);
	timevalsub(&b1, &b0);
	timevalsub(&b2, &b1);

	TIMEVAL_TO_TIMESPEC(&b2, &ts);

	device_printf(sc->dev, "%ld.%09ld: %u ... %u = %u\n",
	    (long)b2.tv_sec, b2.tv_usec, u1, u2, u2 - u1);

	device_printf(sc->dev, "time per call: %ld ns\n", ts.tv_nsec / 1000);
}
