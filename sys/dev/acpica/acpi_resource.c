/*-
 * Copyright (c) 2000 Michael Smith
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
 * $FreeBSD: head/sys/dev/acpica/acpi_resource.c 263975 2014-03-31 19:37:39Z imp $
 */

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/machintr.h>

#include <sys/rman.h>

#include "acpi.h"
#include <dev/acpica/acpivar.h>

/* Hooks for the ACPICA debugging infrastructure */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("RESOURCE")

struct lookup_irq_request {
    ACPI_RESOURCE *acpi_res;
    struct resource *res;
    int		counter;
    int		rid;
    int		found;
};

static ACPI_STATUS
acpi_lookup_irq_handler(ACPI_RESOURCE *res, void *context)
{
    struct lookup_irq_request *req;
    size_t len;
    u_int irqnum;
    u_int irq __debugvar;

    switch (res->Type) {
    case ACPI_RESOURCE_TYPE_IRQ:
	irqnum = res->Data.Irq.InterruptCount;
	irq = res->Data.Irq.Interrupts[0];
	len = ACPI_RS_SIZE(ACPI_RESOURCE_IRQ);
	break;
    case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
	irqnum = res->Data.ExtendedIrq.InterruptCount;
	irq = res->Data.ExtendedIrq.Interrupts[0];
	len = ACPI_RS_SIZE(ACPI_RESOURCE_EXTENDED_IRQ);
	break;
    default:
	return (AE_OK);
    }
    if (irqnum != 1)
	return (AE_OK);
    req = (struct lookup_irq_request *)context;
    if (req->counter != req->rid) {
	req->counter++;
	return (AE_OK);
    }
    req->found = 1;
    KASSERT(irq == rman_get_start(req->res),
	("IRQ resources do not match"));
    bcopy(res, req->acpi_res, len);
    return (AE_CTRL_TERMINATE);
}

ACPI_STATUS
acpi_lookup_irq_resource(device_t dev, int rid, struct resource *res,
    ACPI_RESOURCE *acpi_res)
{
    struct lookup_irq_request req;
    ACPI_STATUS status;

    req.acpi_res = acpi_res;
    req.res = res;
    req.counter = 0;
    req.rid = rid;
    req.found = 0;
    status = AcpiWalkResources(acpi_get_handle(dev), "_CRS",
	acpi_lookup_irq_handler, &req);
    if (ACPI_SUCCESS(status) && req.found == 0)
	status = AE_NOT_FOUND;
    return (status);
}

void
acpi_config_intr(device_t dev, ACPI_RESOURCE *res)
{
    u_int irq;
    int pol, trig;
    enum intr_trigger trigger;
    enum intr_polarity polarity;

    switch (res->Type) {
    case ACPI_RESOURCE_TYPE_IRQ:
	KASSERT(res->Data.Irq.InterruptCount == 1,
	    ("%s: multiple interrupts", __func__));
	irq = res->Data.Irq.Interrupts[0];
	trig = res->Data.Irq.Triggering;
	pol = res->Data.Irq.Polarity;
	break;
    case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
	KASSERT(res->Data.ExtendedIrq.InterruptCount == 1,
	    ("%s: multiple interrupts", __func__));
	irq = res->Data.ExtendedIrq.Interrupts[0];
	trig = res->Data.ExtendedIrq.Triggering;
	pol = res->Data.ExtendedIrq.Polarity;
	break;
    default:
	panic("%s: bad resource type %u", __func__, res->Type);
    }

#if defined(__x86_64__) || defined(__i386__)
    /*
     * XXX: Certain BIOSes have buggy AML that specify an IRQ that is
     * edge-sensitive and active-lo.  However, edge-sensitive IRQs
     * should be active-hi.  Force IRQs with an ISA IRQ value to be
     * active-hi instead.
     */
    if (irq < 16 && trig == ACPI_EDGE_SENSITIVE && pol == ACPI_ACTIVE_LOW)
	pol = ACPI_ACTIVE_HIGH;
#endif

    if (trig == ACPI_EDGE_SENSITIVE)
    	trigger = INTR_TRIGGER_EDGE;
    else
    	trigger = INTR_TRIGGER_LEVEL;

    if (pol == ACPI_ACTIVE_HIGH)
	polarity = INTR_POLARITY_HIGH;
    else
    	polarity = INTR_POLARITY_LOW;

    if (machintr_legacy_intr_find(irq, trigger, polarity) < 0)
	kprintf("%s: Skip irq %d config\n", __func__, irq);
    else
	BUS_CONFIG_INTR(dev, dev, irq, trigger, polarity);
}

struct acpi_resource_context {
    struct acpi_parse_resource_set *set;
    device_t	dev;
    void	*context;
};

#ifdef ACPI_DEBUG_OUTPUT
static const char *
acpi_address_range_name(UINT8 ResourceType)
{
    static char buf[16];

    switch (ResourceType) {
    case ACPI_MEMORY_RANGE:
	    return ("Memory");
    case ACPI_IO_RANGE:
	    return ("IO");
    case ACPI_BUS_NUMBER_RANGE:
	    return ("Bus Number");
    default:
	    ksnprintf(buf, sizeof(buf), "type %u", ResourceType);
	    return (buf);
    }
}
#endif
	    
static ACPI_STATUS
acpi_parse_resource(ACPI_RESOURCE *res, void *context)
{
    struct acpi_parse_resource_set *set;
    struct acpi_resource_context *arc;
    UINT64 min, max, length, gran;
#ifdef ACPI_DEBUG
    const char *name;
#endif
    device_t dev;

    arc = context;
    dev = arc->dev;
    set = arc->set;

    switch (res->Type) {
    case ACPI_RESOURCE_TYPE_END_TAG:
	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "EndTag\n"));
	break;
    case ACPI_RESOURCE_TYPE_FIXED_IO:
	if (res->Data.FixedIo.AddressLength <= 0)
	    break;
	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "FixedIo 0x%x/%d\n",
	    res->Data.FixedIo.Address, res->Data.FixedIo.AddressLength));
	set->set_ioport(dev, arc->context, res->Data.FixedIo.Address,
	    res->Data.FixedIo.AddressLength);
	break;
    case ACPI_RESOURCE_TYPE_IO:
	if (res->Data.Io.AddressLength <= 0)
	    break;
	if (res->Data.Io.Minimum == res->Data.Io.Maximum) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "Io 0x%x/%d\n",
		res->Data.Io.Minimum, res->Data.Io.AddressLength));
	    set->set_ioport(dev, arc->context, res->Data.Io.Minimum,
		res->Data.Io.AddressLength);
	} else {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "Io 0x%x-0x%x/%d\n",
		res->Data.Io.Minimum, res->Data.Io.Maximum,
		res->Data.Io.AddressLength));
	    set->set_iorange(dev, arc->context, res->Data.Io.Minimum,
		res->Data.Io.Maximum, res->Data.Io.AddressLength,
		res->Data.Io.Alignment);
	}
	break;
    case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
	if (res->Data.FixedMemory32.AddressLength <= 0)
	    break;
	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "FixedMemory32 0x%x/%d\n",
	    res->Data.FixedMemory32.Address,
	    res->Data.FixedMemory32.AddressLength));
	set->set_memory(dev, arc->context, res->Data.FixedMemory32.Address, 
	    res->Data.FixedMemory32.AddressLength);
	break;
    case ACPI_RESOURCE_TYPE_MEMORY32:
	if (res->Data.Memory32.AddressLength <= 0)
	    break;
	if (res->Data.Memory32.Minimum == res->Data.Memory32.Maximum) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "Memory32 0x%x/%d\n",
		res->Data.Memory32.Minimum, res->Data.Memory32.AddressLength));
	    set->set_memory(dev, arc->context, res->Data.Memory32.Minimum,
		res->Data.Memory32.AddressLength);
	} else {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "Memory32 0x%x-0x%x/%d\n",
		res->Data.Memory32.Minimum, res->Data.Memory32.Maximum,
		res->Data.Memory32.AddressLength));
	    set->set_memoryrange(dev, arc->context, res->Data.Memory32.Minimum,
		res->Data.Memory32.Maximum, res->Data.Memory32.AddressLength,
		res->Data.Memory32.Alignment);
	}
	break;
    case ACPI_RESOURCE_TYPE_MEMORY24:
	if (res->Data.Memory24.AddressLength <= 0)
	    break;
	if (res->Data.Memory24.Minimum == res->Data.Memory24.Maximum) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "Memory24 0x%x/%d\n",
		res->Data.Memory24.Minimum, res->Data.Memory24.AddressLength));
	    set->set_memory(dev, arc->context, res->Data.Memory24.Minimum,
		res->Data.Memory24.AddressLength);
	} else {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "Memory24 0x%x-0x%x/%d\n",
		res->Data.Memory24.Minimum, res->Data.Memory24.Maximum,
		res->Data.Memory24.AddressLength));
	    set->set_memoryrange(dev, arc->context, res->Data.Memory24.Minimum,
		res->Data.Memory24.Maximum, res->Data.Memory24.AddressLength,
		res->Data.Memory24.Alignment);
	}
	break;
    case ACPI_RESOURCE_TYPE_IRQ:
	/*
	 * from 1.0b 6.4.2 
	 * "This structure is repeated for each separate interrupt
	 * required"
	 */
	set->set_irq(dev, arc->context, res->Data.Irq.Interrupts,
	    res->Data.Irq.InterruptCount, res->Data.Irq.Triggering,
	    res->Data.Irq.Polarity);
	break;
    case ACPI_RESOURCE_TYPE_DMA:
	/*
	 * from 1.0b 6.4.3 
	 * "This structure is repeated for each separate DMA channel
	 * required"
	 */
	set->set_drq(dev, arc->context, res->Data.Dma.Channels,
	    res->Data.Dma.ChannelCount);
	break;
    case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "start dependent functions\n"));
	set->set_start_dependent(dev, arc->context,
	    res->Data.StartDpf.CompatibilityPriority);
	break;
    case ACPI_RESOURCE_TYPE_END_DEPENDENT:
	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "end dependent functions\n"));
	set->set_end_dependent(dev, arc->context);
	break;
    case ACPI_RESOURCE_TYPE_ADDRESS16:
    case ACPI_RESOURCE_TYPE_ADDRESS32:
    case ACPI_RESOURCE_TYPE_ADDRESS64:
    case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	    gran = res->Data.Address16.Address.Granularity;
	    min = res->Data.Address16.Address.Minimum;
	    max = res->Data.Address16.Address.Maximum;
	    length = res->Data.Address16.Address.AddressLength;
#ifdef ACPI_DEBUG
	    name = "Address16";
#endif
	    break;
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	    gran = res->Data.Address32.Address.Granularity;
	    min = res->Data.Address32.Address.Minimum;
	    max = res->Data.Address32.Address.Maximum;
	    length = res->Data.Address32.Address.AddressLength;
#ifdef ACPI_DEBUG
	    name = "Address32";
#endif
	    break;
	case ACPI_RESOURCE_TYPE_ADDRESS64:
	    gran = res->Data.Address64.Address.Granularity;
	    min = res->Data.Address64.Address.Minimum;
	    max = res->Data.Address64.Address.Maximum;
	    length = res->Data.Address64.Address.AddressLength;
#ifdef ACPI_DEBUG
	    name = "Address64";
#endif
	    break;
	default:
	    KASSERT(res->Type == ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64,
		("should never happen"));
	    gran = res->Data.ExtAddress64.Address.Granularity;
	    min = res->Data.ExtAddress64.Address.Minimum;
	    max = res->Data.ExtAddress64.Address.Maximum;
	    length = res->Data.ExtAddress64.Address.AddressLength;
#ifdef ACPI_DEBUG
	    name = "ExtAddress64";
#endif
	    break;
	}
	if (length <= 0)
	    break;
	if (res->Data.Address.ProducerConsumer != ACPI_CONSUMER) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES,
		"ignored %s %s producer\n", name,
		acpi_address_range_name(res->Data.Address.ResourceType)));
	    break;
	}
	if (res->Data.Address.ResourceType != ACPI_MEMORY_RANGE &&
	    res->Data.Address.ResourceType != ACPI_IO_RANGE) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES,
		"ignored %s for non-memory, non-I/O\n", name));
	    break;
	}

#ifdef __i386__
	if (min > ULONG_MAX || (res->Data.Address.MaxAddressFixed && max >
	    ULONG_MAX)) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "ignored %s above 4G\n",
		name));
	    break;
	}
	if (max > ULONG_MAX)
		max = ULONG_MAX;
#endif
	if (res->Data.Address.MinAddressFixed == ACPI_ADDRESS_FIXED &&
	    res->Data.Address.MaxAddressFixed == ACPI_ADDRESS_FIXED) {
	    if (res->Data.Address.ResourceType == ACPI_MEMORY_RANGE) {
		ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "%s/Memory 0x%jx/%ju\n",
		    name, (uintmax_t)min, (uintmax_t)length));
		set->set_memory(dev, arc->context, min, length);
	    } else {
		ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "%s/IO 0x%jx/%ju\n", name,
		    (uintmax_t)min, (uintmax_t)length));
		set->set_ioport(dev, arc->context, min, length);
	    }
	} else {
	    if (res->Data.Address32.ResourceType == ACPI_MEMORY_RANGE) {
		ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES,
		    "%s/Memory 0x%jx-0x%jx/%ju\n", name, (uintmax_t)min,
		    (uintmax_t)max, (uintmax_t)length));
		set->set_memoryrange(dev, arc->context, min, max, length, gran);
	    } else {
		ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "%s/IO 0x%jx-0x%jx/%ju\n",
		    name, (uintmax_t)min, (uintmax_t)max, (uintmax_t)length));
		set->set_iorange(dev, arc->context, min, max, length, gran);
	    }
	}		    
	break;
    case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
	if (res->Data.ExtendedIrq.ProducerConsumer != ACPI_CONSUMER) {
	    ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES, "ignored ExtIRQ producer\n"));
	    break;
	}
	set->set_ext_irq(dev, arc->context, res->Data.ExtendedIrq.Interrupts,
	    res->Data.ExtendedIrq.InterruptCount,
	    res->Data.ExtendedIrq.Triggering, res->Data.ExtendedIrq.Polarity);
	break;
    case ACPI_RESOURCE_TYPE_VENDOR:
	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES,
	    "unimplemented VendorSpecific resource\n"));
	break;
    default:
	break;
    }
    return (AE_OK);
}

/*
 * Fetch a device's resources and associate them with the device.
 *
 * Note that it might be nice to also locate ACPI-specific resource items, such
 * as GPE bits.
 *
 * We really need to split the resource-fetching code out from the
 * resource-parsing code, since we may want to use the parsing
 * code for _PRS someday.
 */
ACPI_STATUS
acpi_parse_resources(device_t dev, ACPI_HANDLE handle,
		     struct acpi_parse_resource_set *set, void *arg)
{
    struct acpi_resource_context arc;
    ACPI_STATUS		status;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    set->set_init(dev, arg, &arc.context);
    arc.set = set;
    arc.dev = dev;
    status = AcpiWalkResources(handle, "_CRS", acpi_parse_resource, &arc);
    if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
	kprintf("can't fetch resources for %s - %s\n",
	    acpi_name(handle), AcpiFormatException(status));
	return_ACPI_STATUS (status);
    }
    set->set_done(dev, arc.context);
    return_ACPI_STATUS (AE_OK);
}

/*
 * Resource-set vectors used to attach _CRS-derived resources 
 * to an ACPI device.
 */
static void	acpi_res_set_init(device_t dev, void *arg, void **context);
static void	acpi_res_set_done(device_t dev, void *context);
static void	acpi_res_set_ioport(device_t dev, void *context,
				    uint64_t base, uint64_t length);
static void	acpi_res_set_iorange(device_t dev, void *context,
				     uint64_t low, uint64_t high, 
				     uint64_t length, uint64_t align);
static void	acpi_res_set_memory(device_t dev, void *context,
				    uint64_t base, uint64_t length);
static void	acpi_res_set_memoryrange(device_t dev, void *context,
					 uint64_t low, uint64_t high, 
					 uint64_t length, uint64_t align);
static void	acpi_res_set_irq(device_t dev, void *context, uint8_t *irq,
				 int count, int trig, int pol);
static void	acpi_res_set_ext_irq(device_t dev, void *context,
				 uint32_t *irq, int count, int trig, int pol);
static void	acpi_res_set_drq(device_t dev, void *context, uint8_t *drq,
				 int count);
static void	acpi_res_set_start_dependent(device_t dev, void *context,
					     int preference);
static void	acpi_res_set_end_dependent(device_t dev, void *context);

struct acpi_parse_resource_set acpi_res_parse_set = {
    acpi_res_set_init,
    acpi_res_set_done,
    acpi_res_set_ioport,
    acpi_res_set_iorange,
    acpi_res_set_memory,
    acpi_res_set_memoryrange,
    acpi_res_set_irq,
    acpi_res_set_ext_irq,
    acpi_res_set_drq,
    acpi_res_set_start_dependent,
    acpi_res_set_end_dependent
};

struct acpi_res_context {
    int		ar_nio;
    int		ar_nmem;
    int		ar_nirq;
    int		ar_ndrq;
    void 	*ar_parent;
};

static void
acpi_res_set_init(device_t dev, void *arg, void **context)
{
    struct acpi_res_context	*cp;

    if ((cp = AcpiOsAllocate(sizeof(*cp))) != NULL) {
	bzero(cp, sizeof(*cp));
	cp->ar_parent = arg;
	*context = cp;
    }
}

static void
acpi_res_set_done(device_t dev, void *context)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;
    AcpiOsFree(cp);
}

static void
acpi_res_set_ioport(device_t dev, void *context, uint64_t base,
		    uint64_t length)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;
    bus_set_resource(dev, SYS_RES_IOPORT, cp->ar_nio++, base, length, -1);
}

static void
acpi_res_set_iorange(device_t dev, void *context, uint64_t low,
		     uint64_t high, uint64_t length, uint64_t align)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;
    device_printf(dev, "I/O range not supported\n");
}

static void
acpi_res_set_memory(device_t dev, void *context, uint64_t base,
		    uint64_t length)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;

    bus_set_resource(dev, SYS_RES_MEMORY, cp->ar_nmem++, base, length, -1);
}

static void
acpi_res_set_memoryrange(device_t dev, void *context, uint64_t low,
			 uint64_t high, uint64_t length, uint64_t align)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;
    device_printf(dev, "memory range not supported\n");
}

static void
acpi_res_set_irq(device_t dev, void *context, uint8_t *irq, int count,
    int trig, int pol)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL || irq == NULL)
	return;

    /* This implements no resource relocation. */
    if (count != 1)
	return;

    bus_set_resource(dev, SYS_RES_IRQ, cp->ar_nirq++, *irq, 1,
        machintr_legacy_intr_cpuid(*irq));
}

static void
acpi_res_set_ext_irq(device_t dev, void *context, uint32_t *irq, int count,
    int trig, int pol)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL || irq == NULL)
	return;

    /* This implements no resource relocation. */
    if (count != 1)
	return;

    /* There is no such IRQ at all */
    if (machintr_legacy_intr_find(*irq,
	INTR_TRIGGER_CONFORM, INTR_POLARITY_CONFORM) < 0)
	return;

    bus_set_resource(dev, SYS_RES_IRQ, cp->ar_nirq++, *irq, 1,
        machintr_legacy_intr_cpuid(*irq));
}

static void
acpi_res_set_drq(device_t dev, void *context, uint8_t *drq, int count)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL || drq == NULL)
	return;
    
    /* This implements no resource relocation. */
    if (count != 1)
	return;

    bus_set_resource(dev, SYS_RES_DRQ, cp->ar_ndrq++, *drq, 1, -1);
}

static void
acpi_res_set_start_dependent(device_t dev, void *context, int preference)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;
    device_printf(dev, "dependent functions not supported\n");
}

static void
acpi_res_set_end_dependent(device_t dev, void *context)
{
    struct acpi_res_context	*cp = (struct acpi_res_context *)context;

    if (cp == NULL)
	return;
    device_printf(dev, "dependent functions not supported\n");
}

/*
 * Resource-owning placeholders for IO and memory pseudo-devices.
 *
 * This code allocates system resources that will be used by ACPI
 * child devices.  The acpi parent manages these resources through a
 * private rman.
 */

static int	acpi_sysres_rid = 100;

static int	acpi_sysres_probe(device_t dev);
static int	acpi_sysres_attach(device_t dev);

static device_method_t acpi_sysres_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	acpi_sysres_probe),
    DEVMETHOD(device_attach,	acpi_sysres_attach),

    DEVMETHOD_END
};

static driver_t acpi_sysres_driver = {
    "acpi_sysresource",
    acpi_sysres_methods,
    0,
};

static devclass_t acpi_sysres_devclass;
DRIVER_MODULE(acpi_sysresource, acpi, acpi_sysres_driver, acpi_sysres_devclass,
    NULL, NULL);
MODULE_DEPEND(acpi_sysresource, acpi, 1, 1, 1);

static int
acpi_sysres_probe(device_t dev)
{
    static char *sysres_ids[] = { "PNP0C01", "PNP0C02", NULL };

    if (acpi_disabled("sysresource") ||
	ACPI_ID_PROBE(device_get_parent(dev), dev, sysres_ids) == NULL)
	return (ENXIO);

    device_set_desc(dev, "System Resource");
    device_quiet(dev);
    return (BUS_PROBE_DEFAULT);
}

static int
acpi_sysres_attach(device_t dev)
{
    device_t bus;
    struct resource_list_entry *bus_rle, *dev_rle;
    struct resource_list *bus_rl, *dev_rl;
    int done, type;
    u_long start, end, count;

    /*
     * Loop through all current resources to see if the new one overlaps
     * any existing ones.  If so, grow the old one up and/or down
     * accordingly.  Discard any that are wholly contained in the old.  If
     * the resource is unique, add it to the parent.  It will later go into
     * the rman pool.
     */
    bus = device_get_parent(dev);
    dev_rl = BUS_GET_RESOURCE_LIST(bus, dev);
    bus_rl = BUS_GET_RESOURCE_LIST(device_get_parent(bus), bus);
    if (bus_rl)
	kprintf("busrl is not null!\n");
    SLIST_FOREACH(dev_rle, dev_rl, link) {
	if (dev_rle->type != SYS_RES_IOPORT && dev_rle->type != SYS_RES_MEMORY)
	    continue;

	start = dev_rle->start;
	end = dev_rle->end;
	count = dev_rle->count;
	type = dev_rle->type;
	done = FALSE;
	if (bus_rl) {
	    SLIST_FOREACH(bus_rle, bus_rl, link) {
		if (bus_rle->type != type)
		    continue;

		/* New resource wholly contained in old, discard. */
		if (start >= bus_rle->start && end <= bus_rle->end)
		    break;

		/* New tail overlaps old head, grow existing resource downward. */
		if (start < bus_rle->start && end >= bus_rle->start) {
		    bus_rle->count += bus_rle->start - start;
		    bus_rle->start = start;
		    done = TRUE;
		}

		/* New head overlaps old tail, grow existing resource upward. */
		if (start <= bus_rle->end && end > bus_rle->end) {
		    bus_rle->count += end - bus_rle->end;
		    bus_rle->end = end;
		    done = TRUE;
		}

		/* If we adjusted the old resource, we're finished. */
		if (done)
		    break;
	    }
	} else {
	    bus_rle = NULL;
	}
	/* If we didn't merge with anything, add this resource. */
	if (bus_rle == NULL)
	    bus_set_resource(bus, type, acpi_sysres_rid++, start, count, -1);
    }

    /* After merging/moving resources to the parent, free the list. */
    resource_list_free(dev_rl);

    return (0);
}
