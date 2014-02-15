/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

#define INTEL_MSR_PERF_BIAS		0x1b0
#define INTEL_MSR_PERF_BIAS_HINTMASK	0xf

struct perfbias_softc {
	device_t		sc_dev;
	int			sc_cpuid;
	int			sc_hint;

	struct sysctl_ctx_list	sc_sysctl_ctx;
	struct sysctl_oid	*sc_sysctl_tree;
};

struct netmsg_perfbias {
	struct netmsg_base	base;
	struct perfbias_softc	*sc;
	int			hint;
};

static void	perfbias_identify(driver_t *, device_t);
static int	perfbias_probe(device_t);
static int	perfbias_attach(device_t);
static int	perfbias_detach(device_t);

static void	perfbias_set_handler(netmsg_t);
static int	perfbias_set(struct perfbias_softc *, int);

static int	perfbias_sysctl(SYSCTL_HANDLER_ARGS);

static device_method_t perfbias_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	perfbias_identify),
	DEVMETHOD(device_probe,		perfbias_probe),
	DEVMETHOD(device_attach,	perfbias_attach),
	DEVMETHOD(device_detach,	perfbias_detach),

	DEVMETHOD_END
};

static driver_t perfbias_driver = {
	"perfbias",
	perfbias_methods,
	sizeof(struct perfbias_softc),
};

static devclass_t perfbias_devclass;
DRIVER_MODULE(perfbias, cpu, perfbias_driver, perfbias_devclass, NULL, NULL);

static void
perfbias_identify(driver_t *driver, device_t parent)
{
	device_t child;
	u_int regs[4];

	if (device_find_child(parent, "perfbias", -1) != NULL)
		return;

	if (cpu_high < 6 || cpu_vendor_id != CPU_VENDOR_INTEL)
		return;

	do_cpuid(6, regs);
	if ((regs[2] & CPUID_THERMAL2_SETBH) == 0)
		return;

	child = device_add_child(parent, "perfbias", device_get_unit(parent));
	if (child == NULL)
		device_printf(parent, "add perfbias failed\n");
}

static int
perfbias_probe(device_t dev)
{
	device_set_desc(dev, "CPU perf-energy bias");
	return 0;
}

static int
perfbias_attach(device_t dev)
{
	struct perfbias_softc *sc = device_get_softc(dev);

	sc->sc_dev = dev;
	sc->sc_cpuid = device_get_unit(dev);

	sysctl_ctx_init(&sc->sc_sysctl_ctx);
	sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_machdep), OID_AUTO,
	    device_get_nameunit(sc->sc_dev), CTLFLAG_RD, 0, "");
	if (sc->sc_sysctl_tree == NULL) {
		device_printf(sc->sc_dev, "can't add sysctl node\n");
		return ENOMEM;
	}

	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
	    OID_AUTO, "hint", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    perfbias_sysctl, "I", "0 - highest perf; 15 - max energy saving");

	return 0;
}

static int
perfbias_detach(device_t dev)
{
	struct perfbias_softc *sc = device_get_softc(dev);

	if (sc->sc_sysctl_tree != NULL)
		sysctl_ctx_free(&sc->sc_sysctl_ctx);

	/* Restore to highest performance */
	perfbias_set(sc, 0);
	return 0;
}

static int
perfbias_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct perfbias_softc *sc = (void *)arg1;
	int error, hint;

	hint = sc->sc_hint;
	error = sysctl_handle_int(oidp, &hint, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (hint < 0 || hint > INTEL_MSR_PERF_BIAS_HINTMASK)
		return EINVAL;

	return perfbias_set(sc, hint);
}

static void
perfbias_set_handler(netmsg_t msg)
{
	struct netmsg_perfbias *pmsg = (struct netmsg_perfbias *)msg;
	struct perfbias_softc *sc = pmsg->sc;
	uint64_t hint = pmsg->hint;

	wrmsr(INTEL_MSR_PERF_BIAS, hint);
	hint = rdmsr(INTEL_MSR_PERF_BIAS);

	sc->sc_hint = hint & INTEL_MSR_PERF_BIAS_HINTMASK;

	lwkt_replymsg(&pmsg->base.lmsg, 0);
}

static int
perfbias_set(struct perfbias_softc *sc, int hint)
{
	struct netmsg_perfbias msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	    perfbias_set_handler);
	msg.hint = hint;
	msg.sc = sc;

	return lwkt_domsg(netisr_cpuport(sc->sc_cpuid), &msg.base.lmsg, 0);
}
