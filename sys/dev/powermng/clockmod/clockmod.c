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
#include <sys/cpu_topology.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/serialize.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

struct clockmod_dom;

struct netmsg_clockmod {
	struct netmsg_base	base;
	uint64_t		ctl_value;
};

struct clockmod_softc {
	TAILQ_ENTRY(clockmod_softc) sc_link;
	struct clockmod_dom	*sc_dom;
	int			sc_cpuid;
};

struct clockmod_dom {
	TAILQ_ENTRY(clockmod_dom) dom_link;
	TAILQ_HEAD(, clockmod_softc) dom_list;
	struct sysctl_ctx_list	dom_sysctl_ctx;
	struct sysctl_oid	*dom_sysctl_tree;
	cpumask_t		dom_cpumask;
	char			dom_name[16];
	int			dom_select;
	uint32_t		dom_flags;
};

#define CLOCKMOD_DOM_FLAG_ACTIVE	0x1

struct clockmod_dom_ctrl {
	char			ctl_name[8];
	uint64_t		ctl_value;
};

static int	clockmod_dom_attach(struct clockmod_softc *);
static void	clockmod_dom_detach(struct clockmod_softc *);
static struct clockmod_dom *clockmod_dom_find(cpumask_t);
static struct clockmod_dom *clockmod_dom_create(cpumask_t);
static void	clockmod_dom_destroy(struct clockmod_dom *);

static int	clockmod_dom_sysctl_select(SYSCTL_HANDLER_ARGS);
static int	clockmod_dom_sysctl_members(SYSCTL_HANDLER_ARGS);
static int	clockmod_dom_sysctl_available(SYSCTL_HANDLER_ARGS);

static void	clockmod_identify(driver_t *, device_t);
static int	clockmod_probe(device_t);
static int	clockmod_attach(device_t);
static int	clockmod_detach(device_t);

static void	clockmod_select_handler(netmsg_t);
static int	clockmod_select(const struct clockmod_softc *,
		    const struct clockmod_dom_ctrl *);

static boolean_t clockmod_errata_duty(int);

static struct lwkt_serialize clockmod_dom_slize = LWKT_SERIALIZE_INITIALIZER;
static int	clockmod_dom_id;
static TAILQ_HEAD(, clockmod_dom) clockmod_dom_list =
    TAILQ_HEAD_INITIALIZER(clockmod_dom_list);
static int	clockmod_dom_nctrl;
static struct clockmod_dom_ctrl *clockmod_dom_controls;

static device_method_t clockmod_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	clockmod_identify),
	DEVMETHOD(device_probe,		clockmod_probe),
	DEVMETHOD(device_attach,	clockmod_attach),
	DEVMETHOD(device_detach,	clockmod_detach),

	DEVMETHOD_END
};

static driver_t clockmod_driver = {
	"clockmod",
	clockmod_methods,
	sizeof(struct clockmod_softc),
};

static devclass_t clockmod_devclass;
DRIVER_MODULE(clockmod, cpu, clockmod_driver, clockmod_devclass, NULL, NULL);

static void
clockmod_identify(driver_t *driver, device_t parent)
{
	device_t child;

	if (device_find_child(parent, "clockmod", -1) != NULL)
		return;

	if (cpu_vendor_id != CPU_VENDOR_INTEL)
		return;

	if ((cpu_feature & (CPUID_ACPI | CPUID_TM)) != (CPUID_ACPI | CPUID_TM))
		return;

	child = device_add_child(parent, "clockmod", device_get_unit(parent));
	if (child == NULL)
		device_printf(parent, "add clockmod failed\n");
}

static int
clockmod_probe(device_t dev)
{
	device_set_desc(dev, "CPU clock modulation");
	return 0;
}

static int
clockmod_attach(device_t dev)
{
	struct clockmod_softc *sc = device_get_softc(dev);
	int error;

	sc->sc_cpuid = device_get_unit(dev);

	error = clockmod_dom_attach(sc);
	if (error) {
		device_printf(dev, "domain attach failed\n");
		return error;
	}

	return 0;
}

static int
clockmod_detach(device_t dev)
{
	clockmod_dom_detach(device_get_softc(dev));
	return 0;
}

static int
clockmod_dom_attach(struct clockmod_softc *sc)
{
	struct clockmod_softc *sc1;
	struct clockmod_dom *dom;
	cpumask_t mask, found_mask;
	int error = 0;

	CPUMASK_ASSZERO(found_mask);

	mask = get_cpumask_from_level(sc->sc_cpuid, CORE_LEVEL);
	if (CPUMASK_TESTZERO(mask))
		CPUMASK_ASSBIT(mask, sc->sc_cpuid);

	lwkt_serialize_enter(&clockmod_dom_slize);

	dom = clockmod_dom_find(mask);
	if (dom == NULL) {
		dom = clockmod_dom_create(mask);
		if (dom == NULL) {
			error = ENOMEM;
			goto back;
		}
	}

	sc->sc_dom = dom;
	TAILQ_INSERT_TAIL(&dom->dom_list, sc, sc_link);

	TAILQ_FOREACH(sc1, &dom->dom_list, sc_link)
		CPUMASK_ORBIT(found_mask, sc1->sc_cpuid);

	if (CPUMASK_CMPMASKEQ(found_mask, dom->dom_cpumask)) {
		/* All cpus in this domain is found */
		dom->dom_flags |= CLOCKMOD_DOM_FLAG_ACTIVE;
	}
back:
	lwkt_serialize_exit(&clockmod_dom_slize);
	return error;
}

static void
clockmod_dom_detach(struct clockmod_softc *sc)
{
	struct clockmod_dom *dom;

	lwkt_serialize_enter(&clockmod_dom_slize);

	dom = sc->sc_dom;
	sc->sc_dom = NULL;

	if (dom->dom_flags & CLOCKMOD_DOM_FLAG_ACTIVE) {
		struct clockmod_softc *sc1;

		/* Raise to 100% */
		TAILQ_FOREACH(sc1, &dom->dom_list, sc_link)
			clockmod_select(sc1, &clockmod_dom_controls[0]);
	}

	/* One cpu is leaving; domain is no longer active */
	dom->dom_flags &= ~CLOCKMOD_DOM_FLAG_ACTIVE;

	TAILQ_REMOVE(&dom->dom_list, sc, sc_link);
	if (TAILQ_EMPTY(&dom->dom_list))
		clockmod_dom_destroy(dom);

	lwkt_serialize_exit(&clockmod_dom_slize);
}

static struct clockmod_dom *
clockmod_dom_find(cpumask_t mask)
{
	struct clockmod_dom *dom;

	TAILQ_FOREACH(dom, &clockmod_dom_list, dom_link) {
		if (CPUMASK_CMPMASKEQ(dom->dom_cpumask, mask))
			return dom;
	}
	return NULL;
}

static struct clockmod_dom *
clockmod_dom_create(cpumask_t mask)
{
	struct clockmod_dom *dom;
	int id;

	id = clockmod_dom_id++;
	dom = kmalloc(sizeof(*dom), M_DEVBUF, M_WAITOK | M_ZERO);

	TAILQ_INIT(&dom->dom_list);
	dom->dom_cpumask = mask;
	ksnprintf(dom->dom_name, sizeof(dom->dom_name), "clockmod_dom%d", id);

	sysctl_ctx_init(&dom->dom_sysctl_ctx);
	dom->dom_sysctl_tree = SYSCTL_ADD_NODE(&dom->dom_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_machdep), OID_AUTO, dom->dom_name,
	    CTLFLAG_RD, 0, "");
	if (dom->dom_sysctl_tree == NULL) {
		kprintf("%s: can't add sysctl node\n", dom->dom_name);
		kfree(dom, M_DEVBUF);
		return NULL;
	}

	SYSCTL_ADD_PROC(&dom->dom_sysctl_ctx,
	    SYSCTL_CHILDREN(dom->dom_sysctl_tree),
	    OID_AUTO, "members", CTLTYPE_STRING | CTLFLAG_RD,
	    dom, 0, clockmod_dom_sysctl_members, "A", "member cpus");

	SYSCTL_ADD_PROC(&dom->dom_sysctl_ctx,
	    SYSCTL_CHILDREN(dom->dom_sysctl_tree),
	    OID_AUTO, "available", CTLTYPE_STRING | CTLFLAG_RD,
	    dom, 0, clockmod_dom_sysctl_available, "A",
	    "available duty percent");

	SYSCTL_ADD_PROC(&dom->dom_sysctl_ctx,
	    SYSCTL_CHILDREN(dom->dom_sysctl_tree),
	    OID_AUTO, "select", CTLTYPE_STRING | CTLFLAG_RW,
	    dom, 0, clockmod_dom_sysctl_select, "A", "select duty");

	TAILQ_INSERT_TAIL(&clockmod_dom_list, dom, dom_link);

	if (clockmod_dom_controls == NULL) {
		int nctrl, step, i, shift, cnt;

#ifdef __x86_64__
		if (cpu_thermal_feature & CPUID_THERMAL_ECMD)
			shift = 0;
		else
#endif
			shift = 1;

		nctrl = 8 << (1 - shift);
		step = 10000 / nctrl;

		clockmod_dom_controls =
		    kmalloc(sizeof(struct clockmod_dom_ctrl) * nctrl, M_DEVBUF,
		    M_WAITOK | M_ZERO);

		if (bootverbose)
			kprintf("clock modulation:\n");

		cnt = 0;
		for (i = 0; i < nctrl; ++i) {
			struct clockmod_dom_ctrl *ctrl =
			    &clockmod_dom_controls[cnt];
			int duty;

			duty = 10000 - (i * step);
			if (clockmod_errata_duty(duty))
				continue;
			++cnt;

			ksnprintf(ctrl->ctl_name, sizeof(ctrl->ctl_name),
			    "%d.%02d%%", duty / 100, duty % 100);
			ctrl->ctl_value = (((nctrl - i) << shift) & 0xf);
			if (i != 0)
				ctrl->ctl_value |= 1 << 4;

			if (bootverbose) {
				kprintf("  0x%04jx %s\n", 
				    (uintmax_t)ctrl->ctl_value,
				    ctrl->ctl_name);
			}
		}
		clockmod_dom_nctrl = cnt;
	}
	return dom;
}

static void
clockmod_dom_destroy(struct clockmod_dom *dom)
{
	KASSERT(TAILQ_EMPTY(&dom->dom_list),
	    ("%s: still has member cpus", dom->dom_name));
	TAILQ_REMOVE(&clockmod_dom_list, dom, dom_link);

	sysctl_ctx_free(&dom->dom_sysctl_ctx);
	kfree(dom, M_DEVBUF);

	if (TAILQ_EMPTY(&clockmod_dom_list)) {
		clockmod_dom_nctrl = 0;
		kfree(clockmod_dom_controls, M_DEVBUF);
		clockmod_dom_controls = NULL;
	}
}

static int
clockmod_dom_sysctl_members(SYSCTL_HANDLER_ARGS)
{
	struct clockmod_dom *dom = arg1;
	struct clockmod_softc *sc;
	int loop, error;

	lwkt_serialize_enter(&clockmod_dom_slize);

	loop = error = 0;
	TAILQ_FOREACH(sc, &dom->dom_list, sc_link) {
		char buf[16];

		if (error == 0 && loop)
			error = SYSCTL_OUT(req, " ", 1);
		if (error == 0) {
			ksnprintf(buf, sizeof(buf), "cpu%d", sc->sc_cpuid);
			error = SYSCTL_OUT(req, buf, strlen(buf));
		}
		++loop;
	}

	lwkt_serialize_exit(&clockmod_dom_slize);
	return error;
}

static int
clockmod_dom_sysctl_available(SYSCTL_HANDLER_ARGS)
{
	struct clockmod_dom *dom = arg1;
	int loop, error, i;

	lwkt_serialize_enter(&clockmod_dom_slize);

	if ((dom->dom_flags & CLOCKMOD_DOM_FLAG_ACTIVE) == 0) {
		error = SYSCTL_OUT(req, " ", 1);
		goto done;
	}

	loop = error = 0;
	for (i = 0; i < clockmod_dom_nctrl; ++i) {
		if (error == 0 && loop)
			error = SYSCTL_OUT(req, " ", 1);
		if (error == 0) {
			error = SYSCTL_OUT(req,
			    clockmod_dom_controls[i].ctl_name,
			    strlen(clockmod_dom_controls[i].ctl_name));
		}
		++loop;
	}
done:
	lwkt_serialize_exit(&clockmod_dom_slize);
	return error;
}

static int
clockmod_dom_sysctl_select(SYSCTL_HANDLER_ARGS)
{
	struct clockmod_dom *dom = arg1;
	struct clockmod_softc *sc;
	const struct clockmod_dom_ctrl *ctrl = NULL;
	char duty[16];
	int error, i;

	lwkt_serialize_enter(&clockmod_dom_slize);
	KKASSERT(dom->dom_select >= 0 && dom->dom_select < clockmod_dom_nctrl);
	ksnprintf(duty, sizeof(duty), "%s",
	    clockmod_dom_controls[dom->dom_select].ctl_name);
	lwkt_serialize_exit(&clockmod_dom_slize);

	error = sysctl_handle_string(oidp, duty, sizeof(duty), req);
	if (error != 0 || req->newptr == NULL)
		return error;

	lwkt_serialize_enter(&clockmod_dom_slize);

	if ((dom->dom_flags & CLOCKMOD_DOM_FLAG_ACTIVE) == 0) {
		error = EOPNOTSUPP;
		goto back;
	}

	for (i = 0; i < clockmod_dom_nctrl; ++i) {
		ctrl = &clockmod_dom_controls[i];
		if (strcmp(duty, ctrl->ctl_name) == 0)
			break;
	}
	if (i == clockmod_dom_nctrl) {
		error = EINVAL;
		goto back;
	}
	dom->dom_select = i;

	TAILQ_FOREACH(sc, &dom->dom_list, sc_link)
		clockmod_select(sc, ctrl);
back:
	lwkt_serialize_exit(&clockmod_dom_slize);
	return error;
}

static void
clockmod_select_handler(netmsg_t msg)
{
	struct netmsg_clockmod *cmsg = (struct netmsg_clockmod *)msg;

#if 0
	if (bootverbose) {
		kprintf("cpu%d: clockmod 0x%04jx\n", mycpuid,
		    (uintmax_t)cmsg->ctl_value);
	}
#endif

	wrmsr(MSR_THERM_CONTROL, cmsg->ctl_value);
	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static int 
clockmod_select(const struct clockmod_softc *sc,
    const struct clockmod_dom_ctrl *ctrl)
{
	struct netmsg_clockmod msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	    clockmod_select_handler);
	msg.ctl_value = ctrl->ctl_value;
	return lwkt_domsg(netisr_cpuport(sc->sc_cpuid), &msg.base.lmsg, 0);
}

static boolean_t
clockmod_errata_duty(int duty)
{
	uint32_t model, stepping;

	/*
	 * This is obtained from the original p4tcc code.
	 *
	 * The original errata checking code in p4tcc is obviously wrong.
	 * However, I am no longer being able to find the errata mentioned
	 * in the code.  The guess is that the errata only affects family
	 * 0x0f CPUs, since:
	 * - The errata applies to only to model 0x00, 0x01 and 0x02 in
	 *   the original p4tcc code.
	 * - Software controlled clock modulation has been supported since
	 *   0f_00 and the model of the oldest family 0x06 CPUs supporting
	 *   this feature is 0x09.
	 */
	if (CPUID_TO_FAMILY(cpu_id) != 0xf)
		return FALSE;

	model = CPUID_TO_MODEL(cpu_id);
	stepping = cpu_id & 0xf;

	if (model == 0x6) {
		switch (stepping) {
		case 0x2:
		case 0x4:
		case 0x5:
			/* Hang w/ 12.50% and 25.00% */
			if (duty == 1250 || duty == 2500)
				return TRUE;
			break;
		}
	} else if (model == 0x2) {
		switch (stepping) {
		case 0x2:
		case 0x4:
		case 0x5:
		case 0x7:
		case 0x9:
			/* Hang w/ 12.50% */
			if (duty == 1250)
				return TRUE;
			break;
		}
	} else if (model == 0x1) {
		switch (stepping) {
		case 0x2:
		case 0x3:
			/* Hang w/ 12.50% and 25.00% */
			if (duty == 1250 || duty == 2500)
				return TRUE;
			break;
		}
	} else if (model == 0x0) {
		switch (stepping) {
		case 0x7:
		case 0xa:
			/* Hang w/ 12.50% and 25.00% */
			if (duty == 1250 || duty == 2500)
				return TRUE;
			break;
		}
	}
	return FALSE;
}
