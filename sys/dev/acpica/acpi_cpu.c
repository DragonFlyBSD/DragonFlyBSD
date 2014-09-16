/*-
 * Copyright (c) 2003-2005 Nate Lawson (SDG)
 * Copyright (c) 2001 Michael Smith
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
 * $FreeBSD: src/sys/dev/acpica/acpi_cpu.c,v 1.72 2008/04/12 12:06:00 rpaulo Exp $
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/globaldata.h>
#include <machine/smp.h>

#include "acpi.h"
#include "acpivar.h"
#include "acpi_cpu.h"

#define ACPI_NOTIFY_CX_STATES	0x81	/* _CST changed. */

static int	acpi_cpu_probe(device_t dev);
static int	acpi_cpu_attach(device_t dev);
static struct resource_list *
		acpi_cpu_get_rlist(device_t, device_t);
static struct resource *
		acpi_cpu_alloc_resource(device_t, device_t,
			int, int *, u_long, u_long, u_long, u_int, int);
static int	acpi_cpu_release_resource(device_t, device_t,
			int, int, struct resource *);

static int	acpi_cpu_get_id(uint32_t, uint32_t *, uint32_t *);
static void	acpi_cpu_notify(ACPI_HANDLE, UINT32, void *);

static device_method_t acpi_cpu_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		acpi_cpu_probe),
    DEVMETHOD(device_attach,		acpi_cpu_attach),
    DEVMETHOD(device_detach,		bus_generic_detach),
    DEVMETHOD(device_shutdown,		bus_generic_shutdown),
    DEVMETHOD(device_suspend,		bus_generic_suspend),
    DEVMETHOD(device_resume,		bus_generic_resume),

    /* Bus interface */
    DEVMETHOD(bus_add_child,		bus_generic_add_child),
    DEVMETHOD(bus_print_child,		bus_generic_print_child),
    DEVMETHOD(bus_read_ivar,		bus_generic_read_ivar),
    DEVMETHOD(bus_write_ivar,		bus_generic_write_ivar),
    DEVMETHOD(bus_get_resource_list,	acpi_cpu_get_rlist),
    DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
    DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
    DEVMETHOD(bus_alloc_resource,	acpi_cpu_alloc_resource),
    DEVMETHOD(bus_release_resource,	acpi_cpu_release_resource),
    DEVMETHOD(bus_driver_added,		bus_generic_driver_added),
    DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

    DEVMETHOD_END
};

static driver_t acpi_cpu_driver = {
    "cpu",
    acpi_cpu_methods,
    sizeof(struct acpi_cpu_softc)
};

static devclass_t acpi_cpu_devclass;
DRIVER_MODULE(cpu, acpi, acpi_cpu_driver, acpi_cpu_devclass, NULL, NULL);
MODULE_DEPEND(cpu, acpi, 1, 1, 1);

static int
acpi_cpu_probe(device_t dev)
{
    int acpi_id, cpu_id;
    ACPI_BUFFER buf;
    ACPI_HANDLE handle;
    ACPI_STATUS	status;
    ACPI_OBJECT *obj;

    if (acpi_disabled("cpu") || acpi_get_type(dev) != ACPI_TYPE_PROCESSOR)
	return ENXIO;

    handle = acpi_get_handle(dev);

    /*
     * Get our Processor object.
     */
    buf.Pointer = NULL;
    buf.Length = ACPI_ALLOCATE_BUFFER;
    status = AcpiEvaluateObject(handle, NULL, NULL, &buf);
    if (ACPI_FAILURE(status)) {
	device_printf(dev, "probe failed to get Processor obj - %s\n",
		      AcpiFormatException(status));
	return ENXIO;
    }

    obj = (ACPI_OBJECT *)buf.Pointer;
    if (obj->Type != ACPI_TYPE_PROCESSOR) {
	device_printf(dev, "Processor object has bad type %d\n", obj->Type);
	AcpiOsFree(obj);
	return ENXIO;
    }

    acpi_id = obj->Processor.ProcId;
    AcpiOsFree(obj);

    /*
     * Find the processor associated with our unit.  We could use the
     * ProcId as a key, however, some boxes do not have the same values
     * in their Processor object as the ProcId values in the MADT.
     */
    if (acpi_cpu_get_id(device_get_unit(dev), &acpi_id, &cpu_id) != 0)
	return ENXIO;

    acpi_set_magic(dev, cpu_id);
    device_set_desc(dev, "ACPI CPU");

    return 0;
}

static int
acpi_cpu_attach(device_t dev)
{
    struct acpi_cpu_softc *sc = device_get_softc(dev);
    ACPI_HANDLE handle;
    device_t child;
    int cpu_id, cpu_features;
    struct acpi_softc *acpi_sc;

    handle = acpi_get_handle(dev);
    cpu_id = acpi_get_magic(dev);

    acpi_sc = acpi_device_get_parent_softc(dev);
    if (cpu_id == 0) {
	sysctl_ctx_init(&sc->glob_sysctl_ctx);
	sc->glob_sysctl_tree = SYSCTL_ADD_NODE(&sc->glob_sysctl_ctx,
			       SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree),
			       OID_AUTO, "cpu", CTLFLAG_RD, 0,
			       "node for CPU global settings");
    	if (sc->glob_sysctl_tree == NULL)
	    return ENOMEM;
    }

    sysctl_ctx_init(&sc->pcpu_sysctl_ctx);
    sc->pcpu_sysctl_tree = SYSCTL_ADD_NODE(&sc->pcpu_sysctl_ctx,
			   SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree),
			   OID_AUTO, device_get_nameunit(dev), CTLFLAG_RD, 0,
			   "node for per-CPU settings");
    if (sc->pcpu_sysctl_tree == NULL) {
	sysctl_ctx_free(&sc->glob_sysctl_ctx);
	return ENOMEM;
    }

    /*
     * Before calling any CPU methods, collect child driver feature hints
     * and notify ACPI of them.  We support unified SMP power control
     * so advertise this ourselves.  Note this is not the same as independent
     * SMP control where each CPU can have different settings.
     */
    cpu_features = ACPI_PDC_MP_C1PXTX | ACPI_PDC_MP_C2C3;
    cpu_features |= acpi_cpu_md_features();

    /*
     * CPU capabilities are specified as a buffer of 32-bit integers:
     * revision, count, and one or more capabilities.
     */
    if (cpu_features) {
	uint32_t cap_set[3];
	ACPI_STATUS status;

	cap_set[0] = 0;
	cap_set[1] = cpu_features;
	status = acpi_eval_osc(dev, handle,
	    "4077A616-290C-47BE-9EBD-D87058713953", 1, cap_set, 2);

	if (ACPI_FAILURE(status)) {
	    ACPI_OBJECT_LIST arglist;
	    ACPI_OBJECT arg[4];

	    if (bootverbose)
		device_printf(dev, "_OSC failed, using _PDC\n");

	    arglist.Pointer = arg;
	    arglist.Count = 1;
	    arg[0].Type = ACPI_TYPE_BUFFER;
	    arg[0].Buffer.Length = sizeof(cap_set);
	    arg[0].Buffer.Pointer = (uint8_t *)cap_set;
	    cap_set[0] = 1; /* revision */
	    cap_set[1] = 1; /* # of capabilities integers */
	    cap_set[2] = cpu_features;
	    AcpiEvaluateObject(handle, "_PDC", &arglist, NULL);
	}
    }

    child = BUS_ADD_CHILD(dev, dev, 0, "cpu_cst", -1);
    if (child == NULL)
	return ENXIO;
    acpi_set_handle(child, handle);
    acpi_set_magic(child, cpu_id);
    sc->cpu_cst = child;

    child = BUS_ADD_CHILD(dev, dev, 0, "cpu_pst", -1);
    if (child == NULL)
	return ENXIO;
    acpi_set_handle(child, handle);
    acpi_set_magic(child, cpu_id);

    bus_generic_probe(dev);
    bus_generic_attach(dev);

    AcpiInstallNotifyHandler(handle, ACPI_DEVICE_NOTIFY, acpi_cpu_notify, sc);

    return 0;
}

/*
 * All resources are assigned directly to us by acpi,
 * so 'child' is bypassed here.
 */
static struct resource_list *
acpi_cpu_get_rlist(device_t dev, device_t child __unused)
{
    return BUS_GET_RESOURCE_LIST(device_get_parent(dev), dev);
}

static struct resource *
acpi_cpu_alloc_resource(device_t dev, device_t child __unused,
			int type, int *rid, u_long start, u_long end,
			u_long count, u_int flags, int cpuid)
{
    return BUS_ALLOC_RESOURCE(device_get_parent(dev), dev, type, rid,
			      start, end, count, flags, cpuid);
}

static int
acpi_cpu_release_resource(device_t dev, device_t child __unused,
			  int type, int rid, struct resource *r)
{
    return BUS_RELEASE_RESOURCE(device_get_parent(dev), dev, type, rid, r);
}

/*
 * Find the nth present CPU and return its pc_cpuid as well as set the
 * pc_acpi_id from the most reliable source.
 */
static int
acpi_cpu_get_id(uint32_t idx, uint32_t *acpi_id, uint32_t *cpu_id)
{
    struct mdglobaldata *md;
    uint32_t i;

    KASSERT(acpi_id != NULL, ("Null acpi_id"));
    KASSERT(cpu_id != NULL, ("Null cpu_id"));
    for (i = 0; i < ncpus; i++) {
	if (CPUMASK_TESTBIT(smp_active_mask, i) == 0)
	    continue;
	md = (struct mdglobaldata *)globaldata_find(i);
	KASSERT(md != NULL, ("no pcpu data for %d", i));
	if (idx-- == 0) {
	    /*
	     * If pc_acpi_id was not initialized (e.g., a non-APIC UP box)
	     * override it with the value from the ASL.  Otherwise, if the
	     * two don't match, prefer the MADT-derived value.  Finally,
	     * return the pc_cpuid to reference this processor.
	     */
	    if (md->gd_acpi_id == 0xffffffff)
		md->gd_acpi_id = *acpi_id;
	    else if (md->gd_acpi_id != *acpi_id)
		*acpi_id = md->gd_acpi_id;
	    *cpu_id = md->mi.gd_cpuid;
	    return 0;
	}
    }
    return ESRCH;
}

static void
acpi_cpu_notify(ACPI_HANDLE handler __unused, UINT32 notify, void *xsc)
{
    struct acpi_cpu_softc *sc = xsc;

    switch (notify) {
    case ACPI_NOTIFY_CX_STATES:
	if (sc->cpu_cst_notify != NULL)
	    sc->cpu_cst_notify(sc->cpu_cst);
	break;
    }
}
