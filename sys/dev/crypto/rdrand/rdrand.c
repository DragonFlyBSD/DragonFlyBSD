/*
 * Copyright (c) 2012 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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
#include <sys/kobj.h>
#include <sys/libkern.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/specialreg.h>

/*
 * WARNING!
 *
 * The RDRAND instruction is a very slow instruction, burning approximately
 * 0.79uS per 64-bit word on a modern ryzen cpu.  Intel cpu's run this
 * instruction far more quickly.  The quality of the results are unknown
 * either way.
 *
 * However, possibly an even bigger problem is the cost of calling
 * add_buffer_randomness(), which takes an enormous amount of time
 * when handed a large buffer.
 *
 * Our code harvests at a 10hz rate on every single core, and also chains
 * some entropy from core to core so honestly it doesn't take much to really
 * mix things up.  Use a decent size (16 or 32 bytes should be good).
 *
 * On a TR3990 going from 512 to 16 gave userland almost 10% additional
 * performance... a very stark difference.  A simple test loop:
 *
 * BEFORE: (RDRAND_SIZE 512)
 *	7.258u 0.000s 0:07.69 94.2%     2+70k 2+0io 0pf+0w
 *	7.222u 0.000s 0:07.67 94.1%     2+70k 0+0io 0pf+0w
 *	7.239u 0.000s 0:07.69 94.0%     2+70k 0+0io 0pf+0w
 *
 * AFTER: (RDRAND_SIZE 16)		(9.3% faster)
 *	7.019u 0.000s 0:07.02 99.8%     2+66k 0+0io 0pf+0w
 *	7.019u 0.000s 0:07.02 99.8%     2+66k 0+0io 0pf+0w
 *	7.028u 0.000s 0:07.02 100.0%    2+66k 0+0io 0pf+0w
 */
#define	RDRAND_ALIGN(p)	(void *)(roundup2((uintptr_t)(p), 16))
#define RDRAND_SIZE	16

static int rdrand_debug;
SYSCTL_INT(_debug, OID_AUTO, rdrand, CTLFLAG_RW, &rdrand_debug, 0,
	   "Enable rdrand debugging");

struct rdrand_softc {
	struct callout	*sc_rng_co;
	int32_t		sc_rng_ticks;
};


static void rdrand_rng_harvest(void *);
int rdrand_rng(uint8_t *out, long limit);


static void
rdrand_identify(driver_t *drv, device_t parent)
{

	/* NB: order 10 is so we get attached after h/w devices */
	if (device_find_child(parent, "rdrand", -1) == NULL &&
	    BUS_ADD_CHILD(parent, parent, 10, "rdrand", -1) == 0)
		panic("rdrand: could not attach");
}


static int
rdrand_probe(device_t dev)
{

	if ((cpu_feature2 & CPUID2_RDRAND) == 0) {
		device_printf(dev, "No RdRand support.\n");
		return (EINVAL);
	}

	device_set_desc(dev, "RdRand RNG");
	return 0;
}


static int
rdrand_attach(device_t dev)
{
	struct rdrand_softc *sc;
	int i;

	sc = device_get_softc(dev);

	if (hz > 10)
		sc->sc_rng_ticks = hz / 10;
	else
		sc->sc_rng_ticks = 1;

	sc->sc_rng_co = kmalloc(ncpus * sizeof(*sc->sc_rng_co),
				M_TEMP, M_WAITOK | M_ZERO);

	/*
	 * Set an initial offset so we don't pound all cores simultaneously
	 * for no good reason.
	 */
	for (i = 0; i < ncpus; ++i) {
		callout_init_mp(&sc->sc_rng_co[i]);
		callout_reset_bycpu(&sc->sc_rng_co[i],
				    i, rdrand_rng_harvest, sc, i);
	}

	return 0;
}


static int
rdrand_detach(device_t dev)
{
	struct rdrand_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < ncpus; ++i) {
		callout_terminate(&sc->sc_rng_co[i]);
	}

	return (0);
}


static void
rdrand_rng_harvest(void *arg)
{
	struct rdrand_softc *sc = arg;
	uint8_t randomness[RDRAND_SIZE + 32];
	uint8_t *arandomness; /* randomness aligned */
	int cnt;

	arandomness = RDRAND_ALIGN(randomness);

	cnt = rdrand_rng(arandomness, RDRAND_SIZE);
	if (cnt > 0 && cnt < sizeof(randomness)) {
		add_buffer_randomness_src(arandomness, cnt,
					  RAND_SRC_RDRAND |
					  RAND_SRCF_PCPU);

		if (rdrand_debug > 0) {
			--rdrand_debug;
			kprintf("rdrand(%d,cpu=%d): %02x %02x %02x %02x...\n",
				cnt, mycpu->gd_cpuid,
				arandomness[0],
				arandomness[1],
				arandomness[2],
				arandomness[3]);
		}
	}

	callout_reset(&sc->sc_rng_co[mycpu->gd_cpuid], sc->sc_rng_ticks,
		      rdrand_rng_harvest, sc);
}


static device_method_t rdrand_methods[] = {
	DEVMETHOD(device_identify, rdrand_identify),
	DEVMETHOD(device_probe, rdrand_probe),
	DEVMETHOD(device_attach, rdrand_attach),
	DEVMETHOD(device_detach, rdrand_detach),

	DEVMETHOD_END
};


static driver_t rdrand_driver = {
	"rdrand",
	rdrand_methods,
	sizeof(struct rdrand_softc),
};

static devclass_t rdrand_devclass;

DRIVER_MODULE(rdrand, nexus, rdrand_driver, rdrand_devclass, NULL, NULL);
MODULE_VERSION(rdrand, 1);
MODULE_DEPEND(rdrand, crypto, 1, 1, 1);
