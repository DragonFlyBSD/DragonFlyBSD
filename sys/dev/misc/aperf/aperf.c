/*
 * Copyright (c) 2015 Imre VadÃ¡sz <imre@vdsz.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* sensor driver for effective CPU frequency, using APERF/MPERF MSRs */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/sensors.h>
#include <sys/systm.h>
#include <sys/thread2.h>

#include <machine/clock.h>
#include <machine/cpufunc.h>

#include "cpu_if.h"

#define MSR_MPERF	0xE7
#define MSR_APERF	0xE8

struct aperf_softc {
	struct ksensordev	*sc_sensdev;
	struct ksensor		sc_sens;
	struct sensor_task	*sc_senstask;

	uint64_t		sc_aperf_prev;
	uint64_t		sc_mperf_prev;
	uint64_t		sc_tsc_prev;
	uint32_t		sc_flags;	/* APERF_FLAG_ */
};

#define APERF_FLAG_PREV_VALID	0x1

/*
 * Device methods.
 */
static void	aperf_identify(driver_t *, device_t);
static int	aperf_probe(device_t);
static int	aperf_attach(device_t);
static int	aperf_detach(device_t);

static void	aperf_sensor_task(void *);

static device_method_t aperf_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	aperf_identify),
	DEVMETHOD(device_probe,		aperf_probe),
	DEVMETHOD(device_attach,	aperf_attach),
	DEVMETHOD(device_detach,	aperf_detach),

	DEVMETHOD_END
};

static driver_t aperf_driver = {
	"aperf",
	aperf_methods,
	sizeof(struct aperf_softc),
};

static devclass_t aperf_devclass;
DRIVER_MODULE(aperf, cpu, aperf_driver, aperf_devclass, NULL, NULL);
MODULE_VERSION(aperf, 1);

static void
aperf_identify(driver_t *driver, device_t parent)
{
	uint32_t regs[4];
	device_t child;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "aperf", -1) != NULL)
		return;

	/* CPUID Fn0000_0006_ECX Effective Processor Frequency Interface */
	do_cpuid(0x00000006, regs);
	if ((regs[2] & 1) == 0)
		return;

	child = device_add_child(parent, "aperf", -1);
	if (child == NULL)
		device_printf(parent, "add aperf child failed\n");
}

static int
aperf_probe(device_t dev)
{
	if (resource_disabled("aperf", 0))
		return ENXIO;

	device_set_desc(dev, "CPU Frequency Sensor");

	return 0;
}

static int
aperf_attach(device_t dev)
{
	struct aperf_softc *sc = device_get_softc(dev);
	device_t parent;
	int cpu;

	parent = device_get_parent(dev);
	cpu = device_get_unit(parent);

	sc->sc_sensdev = CPU_GET_SENSDEV(parent);
	if (sc->sc_sensdev == NULL)
		return ENXIO;

	/*
	 * Add hw.sensors.cpuN.raw0 MIB.
	 */
	ksnprintf(sc->sc_sens.desc, sizeof(sc->sc_sens.desc),
	    "cpu%d freq", cpu);
	sc->sc_sens.type = SENSOR_FREQ;
	sensor_set_unknown(&sc->sc_sens);
	sensor_attach(sc->sc_sensdev, &sc->sc_sens);

	sc->sc_senstask = sensor_task_register2(sc, aperf_sensor_task, 1, cpu);

	return 0;
}

static int
aperf_detach(device_t dev)
{
	struct aperf_softc *sc = device_get_softc(dev);

	if (sc->sc_senstask != NULL)
		sensor_task_unregister2(sc->sc_senstask);

	if (sc->sc_sensdev != NULL)
		sensor_detach(sc->sc_sensdev, &sc->sc_sens);

	return 0;
}

static void
aperf_sensor_task(void *xsc)
{
	struct aperf_softc *sc = xsc;
	uint64_t aperf, mperf, tsc, freq;
	uint64_t aperf_diff, mperf_diff, tsc_diff;

	crit_enter();
	tsc = rdtsc_ordered();
	aperf = rdmsr(MSR_APERF);
	mperf = rdmsr(MSR_MPERF);
	crit_exit();

	if ((sc->sc_flags & APERF_FLAG_PREV_VALID) == 0) {
		sc->sc_aperf_prev = aperf;
		sc->sc_mperf_prev = mperf;
		sc->sc_tsc_prev = tsc;
		sc->sc_flags |= APERF_FLAG_PREV_VALID;
		return;
	}

	aperf_diff = aperf - sc->sc_aperf_prev;
	mperf_diff = mperf - sc->sc_mperf_prev;
	if (tsc_invariant)
		tsc_diff = tsc - sc->sc_tsc_prev;
	else
		tsc_diff = tsc_frequency;

	sc->sc_aperf_prev = aperf;
	sc->sc_mperf_prev = mperf;
	sc->sc_tsc_prev = tsc;

	/* Avoid division by zero */
	if (mperf_diff == 0)
		mperf_diff = 1;

	/* Using tsc_diff/1000 to avoid overflowing */
	freq = ((aperf_diff * (tsc_diff / 1000)) / mperf_diff) * 1000;
	sensor_set(&sc->sc_sens, freq, SENSOR_S_UNSPEC);
}
