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
#include <sys/malloc.h>
#include <sys/globaldata.h>
#include <sys/power.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/thread2.h>
#include <sys/serialize.h>
#include <sys/msgport2.h>
#include <sys/microtime_pcpu.h>

#include <bus/pci/pcivar.h>
#include <machine/atomic.h>
#include <machine/globaldata.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <sys/rman.h>

#include <net/netisr2.h>
#include <net/netmsg2.h>
#include <net/if_var.h>

#include "acpi.h"
#include "acpivar.h"
#include "acpi_cpu.h"
#include "acpi_cpu_cstate.h"

/*
 * Support for ACPI Processor devices, including C[1-3+] sleep states.
 */

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_PROCESSOR
ACPI_MODULE_NAME("PROCESSOR")

struct netmsg_acpi_cst {
	struct netmsg_base base;
	struct acpi_cst_softc *sc;
	int		val;
};

#define MAX_CX_STATES	 8

struct acpi_cst_softc {
    device_t		cst_dev;
    struct acpi_cpux_softc *cst_parent;
    ACPI_HANDLE		cst_handle;
    int			cst_cpuid;
    uint32_t		cst_flags;	/* ACPI_CST_FLAG_ */
    uint32_t		cst_p_blk;	/* ACPI P_BLK location */
    uint32_t		cst_p_blk_len;	/* P_BLK length (must be 6). */
    struct acpi_cst_cx	cst_cx_states[MAX_CX_STATES];
    int			cst_cx_count;	/* Number of valid Cx states. */
    int			cst_prev_sleep;	/* Last idle sleep duration. */
    /* Runtime state. */
    int			cst_non_c3;	/* Index of lowest non-C3 state. */
    u_long		cst_cx_stats[MAX_CX_STATES];/* Cx usage history. */
    /* Values for sysctl. */
    int			cst_cx_lowest;	/* Current Cx lowest */
    int			cst_cx_lowest_req; /* Requested Cx lowest */
    char 		cst_cx_supported[64];
};

#define ACPI_CST_FLAG_PROBING	0x1

#define PCI_VENDOR_INTEL	0x8086
#define PCI_DEVICE_82371AB_3	0x7113	/* PIIX4 chipset for quirks. */
#define PCI_REVISION_A_STEP	0
#define PCI_REVISION_B_STEP	1
#define PCI_REVISION_4E		2
#define PCI_REVISION_4M		3
#define PIIX4_DEVACTB_REG	0x58
#define PIIX4_BRLD_EN_IRQ0	(1<<0)
#define PIIX4_BRLD_EN_IRQ	(1<<1)
#define PIIX4_BRLD_EN_IRQ8	(1<<5)
#define PIIX4_STOP_BREAK_MASK	(PIIX4_BRLD_EN_IRQ0 | \
				 PIIX4_BRLD_EN_IRQ | \
				 PIIX4_BRLD_EN_IRQ8)
#define PIIX4_PCNTRL_BST_EN	(1<<10)

/* Platform hardware resource information. */
static uint32_t		 acpi_cst_smi_cmd; /* Value to write to SMI_CMD. */
static uint8_t		 acpi_cst_ctrl;	/* Indicate we are _CST aware. */
int		 	 acpi_cst_quirks; /* Indicate any hardware bugs. */
static boolean_t	 acpi_cst_use_fadt;

/* Runtime state. */
static boolean_t	 acpi_cst_disable_idle;
					/* Disable entry to idle function */
static int		 acpi_cst_cx_count; /* Number of valid Cx states */

/* Values for sysctl. */
static int		 acpi_cst_cx_lowest; /* Current Cx lowest */
static int		 acpi_cst_cx_lowest_req; /* Requested Cx lowest */

/* Number of C3 state requesters */
static int		 acpi_cst_c3_reqs;

static device_t		*acpi_cst_devices;
static int		 acpi_cst_ndevices;
static struct acpi_cst_softc **acpi_cst_softc;
static struct lwkt_serialize acpi_cst_slize = LWKT_SERIALIZE_INITIALIZER;

static int	acpi_cst_probe(device_t);
static int	acpi_cst_attach(device_t);
static int	acpi_cst_suspend(device_t);
static int	acpi_cst_resume(device_t);
static int	acpi_cst_shutdown(device_t);

static void	acpi_cst_notify(device_t);
static void	acpi_cst_postattach(void *);
static void	acpi_cst_idle(void);

static void	acpi_cst_cx_probe(struct acpi_cst_softc *);
static void	acpi_cst_cx_probe_fadt(struct acpi_cst_softc *);
static int	acpi_cst_cx_probe_cst(struct acpi_cst_softc *, int);
static int	acpi_cst_cx_reprobe_cst(struct acpi_cst_softc *);

static void	acpi_cst_startup(struct acpi_cst_softc *);
static void	acpi_cst_support_list(struct acpi_cst_softc *);
static int	acpi_cst_set_lowest(struct acpi_cst_softc *, int);
static int	acpi_cst_set_lowest_oncpu(struct acpi_cst_softc *, int);
static void	acpi_cst_non_c3(struct acpi_cst_softc *);
static void	acpi_cst_global_cx_count(void);
static int	acpi_cst_set_quirks(void);
static void	acpi_cst_c3_bm_rld(struct acpi_cst_softc *);
static void	acpi_cst_free_resource(struct acpi_cst_softc *, int);
static void	acpi_cst_c1_halt(void);

static int	acpi_cst_usage_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cst_lowest_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cst_lowest_use_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cst_global_lowest_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cst_global_lowest_use_sysctl(SYSCTL_HANDLER_ARGS);

static int	acpi_cst_cx_setup(struct acpi_cst_cx *cx);
static void	acpi_cst_c1_halt_enter(const struct acpi_cst_cx *);
static void	acpi_cst_cx_io_enter(const struct acpi_cst_cx *);

static device_method_t acpi_cst_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	acpi_cst_probe),
    DEVMETHOD(device_attach,	acpi_cst_attach),
    DEVMETHOD(device_detach,	bus_generic_detach),
    DEVMETHOD(device_shutdown,	acpi_cst_shutdown),
    DEVMETHOD(device_suspend,	acpi_cst_suspend),
    DEVMETHOD(device_resume,	acpi_cst_resume),

    /* Bus interface */
    DEVMETHOD(bus_add_child,	bus_generic_add_child),
    DEVMETHOD(bus_read_ivar,	bus_generic_read_ivar),
    DEVMETHOD(bus_get_resource_list, bus_generic_get_resource_list),
    DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
    DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
    DEVMETHOD(bus_alloc_resource, bus_generic_rl_alloc_resource),
    DEVMETHOD(bus_release_resource, bus_generic_rl_release_resource),
    DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
    DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr, bus_generic_teardown_intr),
    DEVMETHOD_END
};

static driver_t acpi_cst_driver = {
    "cpu_cst",
    acpi_cst_methods,
    sizeof(struct acpi_cst_softc),
};

static devclass_t acpi_cst_devclass;
DRIVER_MODULE(cpu_cst, cpu, acpi_cst_driver, acpi_cst_devclass, NULL, NULL);
MODULE_DEPEND(cpu_cst, acpi, 1, 1, 1);

static int
acpi_cst_probe(device_t dev)
{
    int cpu_id;

    if (acpi_disabled("cpu_cst") || acpi_get_type(dev) != ACPI_TYPE_PROCESSOR)
	return (ENXIO);

    cpu_id = acpi_get_magic(dev);

    if (acpi_cst_softc == NULL)
	acpi_cst_softc = kmalloc(sizeof(struct acpi_cst_softc *) *
	    SMP_MAXCPU, M_TEMP /* XXX */, M_INTWAIT | M_ZERO);

    /*
     * Check if we already probed this processor.  We scan the bus twice
     * so it's possible we've already seen this one.
     */
    if (acpi_cst_softc[cpu_id] != NULL) {
	device_printf(dev, "CPU%d cstate already exist\n", cpu_id);
	return (ENXIO);
    }

    /* Mark this processor as in-use and save our derived id for attach. */
    acpi_cst_softc[cpu_id] = (void *)1;
    device_set_desc(dev, "ACPI CPU C-State");

    return (0);
}

static int
acpi_cst_attach(device_t dev)
{
    ACPI_BUFFER		   buf;
    ACPI_OBJECT		   *obj;
    struct acpi_cst_softc *sc;
    ACPI_STATUS		   status;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    sc = device_get_softc(dev);
    sc->cst_dev = dev;
    sc->cst_parent = device_get_softc(device_get_parent(dev));
    sc->cst_handle = acpi_get_handle(dev);
    sc->cst_cpuid = acpi_get_magic(dev);
    acpi_cst_softc[sc->cst_cpuid] = sc;
    acpi_cst_smi_cmd = AcpiGbl_FADT.SmiCommand;
    acpi_cst_ctrl = AcpiGbl_FADT.CstControl;

    buf.Pointer = NULL;
    buf.Length = ACPI_ALLOCATE_BUFFER;
    status = AcpiEvaluateObject(sc->cst_handle, NULL, NULL, &buf);
    if (ACPI_FAILURE(status)) {
	device_printf(dev, "attach failed to get Processor obj - %s\n",
		      AcpiFormatException(status));
	return (ENXIO);
    }
    obj = (ACPI_OBJECT *)buf.Pointer;
    sc->cst_p_blk = obj->Processor.PblkAddress;
    sc->cst_p_blk_len = obj->Processor.PblkLength;
    AcpiOsFree(obj);
    ACPI_DEBUG_PRINT((ACPI_DB_INFO, "cpu_cst%d: P_BLK at %#x/%d\n",
		     device_get_unit(dev), sc->cst_p_blk, sc->cst_p_blk_len));

    /*
     * If this is the first cpu we attach, create and initialize the generic
     * resources that will be used by all acpi cpu devices.
     */
    if (device_get_unit(dev) == 0) {
	/* Assume we won't be using FADT for Cx states by default */
	acpi_cst_use_fadt = FALSE;

	/* Queue post cpu-probing task handler */
	AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_cst_postattach, NULL);
    }

    /* Probe for Cx state support. */
    acpi_cst_cx_probe(sc);

    /* Finally,  call identify and probe/attach for child devices. */
    bus_generic_probe(dev);
    bus_generic_attach(dev);

    return (0);
}

/*
 * Disable any entry to the idle function during suspend and re-enable it
 * during resume.
 */
static int
acpi_cst_suspend(device_t dev)
{
    int error;

    error = bus_generic_suspend(dev);
    if (error)
	return (error);
    acpi_cst_disable_idle = TRUE;
    return (0);
}

static int
acpi_cst_resume(device_t dev)
{
    acpi_cst_disable_idle = FALSE;
    return (bus_generic_resume(dev));
}

static int
acpi_cst_shutdown(device_t dev)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /* Allow children to shutdown first. */
    bus_generic_shutdown(dev);

    /*
     * Disable any entry to the idle function.  There is a small race where
     * an idle thread have passed this check but not gone to sleep.  This
     * is ok since device_shutdown() does not free the softc, otherwise
     * we'd have to be sure all threads were evicted before returning.
     */
    acpi_cst_disable_idle = TRUE;

    return_VALUE (0);
}

static void
acpi_cst_cx_probe(struct acpi_cst_softc *sc)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /* Use initial sleep value of 1 sec. to start with lowest idle state. */
    sc->cst_prev_sleep = 1000000;
    sc->cst_cx_lowest = 0;
    sc->cst_cx_lowest_req = 0;

    /*
     * Check for the ACPI 2.0 _CST sleep states object.  If we can't find
     * any, we'll revert to FADT/P_BLK Cx control method which will be
     * handled by acpi_cst_postattach.  We need to defer to after having
     * probed all the cpus in the system before probing for Cx states from
     * FADT as we may already have found cpus with valid _CST packages.
     */
    if (!acpi_cst_use_fadt && acpi_cst_cx_probe_cst(sc, 0) != 0) {
	/*
	 * We were unable to find a _CST package for this cpu or there
	 * was an error parsing it. Switch back to generic mode.
	 */
	acpi_cst_use_fadt = TRUE;
	if (bootverbose)
	    device_printf(sc->cst_dev, "switching to FADT Cx mode\n");
    }

    /*
     * TODO: _CSD Package should be checked here.
     */
}

static void
acpi_cst_cx_probe_fadt(struct acpi_cst_softc *sc)
{
    struct acpi_cst_cx *cx_ptr;
    int error;

    /*
     * Free all previously allocated resources.
     *
     * NITE:
     * It is needed, since we could enter here because of other
     * cpu's _CST probing failure.
     */
    acpi_cst_free_resource(sc, 0);

    sc->cst_cx_count = 0;
    cx_ptr = sc->cst_cx_states;

    /* Use initial sleep value of 1 sec. to start with lowest idle state. */
    sc->cst_prev_sleep = 1000000;

    /* C1 has been required since just after ACPI 1.0 */
    cx_ptr->gas.SpaceId = ACPI_ADR_SPACE_FIXED_HARDWARE;
    cx_ptr->type = ACPI_STATE_C1;
    cx_ptr->trans_lat = 0;
    cx_ptr->enter = acpi_cst_c1_halt_enter;
    error = acpi_cst_cx_setup(cx_ptr);
    if (error)
	panic("C1 FADT HALT setup failed: %d", error);
    cx_ptr++;
    sc->cst_cx_count++;

    /* C2(+) is not supported on MP system */
    if (ncpus > 1 && (AcpiGbl_FADT.Flags & ACPI_FADT_C2_MP_SUPPORTED) == 0)
	return;

    /*
     * The spec says P_BLK must be 6 bytes long.  However, some systems
     * use it to indicate a fractional set of features present so we
     * take 5 as C2.  Some may also have a value of 7 to indicate
     * another C3 but most use _CST for this (as required) and having
     * "only" C1-C3 is not a hardship.
     */
    if (sc->cst_p_blk_len < 5)
	return; 

    /* Validate and allocate resources for C2 (P_LVL2). */
    if (AcpiGbl_FADT.C2Latency <= 100) {
	cx_ptr->gas.SpaceId = ACPI_ADR_SPACE_SYSTEM_IO;
	cx_ptr->gas.BitWidth = 8;
	cx_ptr->gas.Address = sc->cst_p_blk + 4;

	cx_ptr->rid = sc->cst_parent->cpux_next_rid;
	acpi_bus_alloc_gas(sc->cst_dev, &cx_ptr->res_type, &cx_ptr->rid,
	    &cx_ptr->gas, &cx_ptr->res, RF_SHAREABLE);
	if (cx_ptr->res != NULL) {
	    sc->cst_parent->cpux_next_rid++;
	    cx_ptr->type = ACPI_STATE_C2;
	    cx_ptr->trans_lat = AcpiGbl_FADT.C2Latency;
	    cx_ptr->enter = acpi_cst_cx_io_enter;
	    cx_ptr->btag = rman_get_bustag(cx_ptr->res);
	    cx_ptr->bhand = rman_get_bushandle(cx_ptr->res);
	    error = acpi_cst_cx_setup(cx_ptr);
	    if (error)
		panic("C2 FADT I/O setup failed: %d", error);
	    cx_ptr++;
	    sc->cst_cx_count++;
	    sc->cst_non_c3 = 1;
	}
    }
    if (sc->cst_p_blk_len < 6)
	return;

    /* Validate and allocate resources for C3 (P_LVL3). */
    if (AcpiGbl_FADT.C3Latency <= 1000 &&
        !(acpi_cst_quirks & ACPI_CST_QUIRK_NO_C3)) {
	cx_ptr->gas.SpaceId = ACPI_ADR_SPACE_SYSTEM_IO;
	cx_ptr->gas.BitWidth = 8;
	cx_ptr->gas.Address = sc->cst_p_blk + 5;

	cx_ptr->rid = sc->cst_parent->cpux_next_rid;
	acpi_bus_alloc_gas(sc->cst_dev, &cx_ptr->res_type, &cx_ptr->rid,
	    &cx_ptr->gas, &cx_ptr->res, RF_SHAREABLE);
	if (cx_ptr->res != NULL) {
	    sc->cst_parent->cpux_next_rid++;
	    cx_ptr->type = ACPI_STATE_C3;
	    cx_ptr->trans_lat = AcpiGbl_FADT.C3Latency;
	    cx_ptr->enter = acpi_cst_cx_io_enter;
	    cx_ptr->btag = rman_get_bustag(cx_ptr->res);
	    cx_ptr->bhand = rman_get_bushandle(cx_ptr->res);
	    error = acpi_cst_cx_setup(cx_ptr);
	    if (error)
		panic("C3 FADT I/O setup failed: %d", error);
	    cx_ptr++;
	    sc->cst_cx_count++;
	}
    }
}

/*
 * Parse a _CST package and set up its Cx states.  Since the _CST object
 * can change dynamically, our notify handler may call this function
 * to clean up and probe the new _CST package.
 */
static int
acpi_cst_cx_probe_cst(struct acpi_cst_softc *sc, int reprobe)
{
    struct	 acpi_cst_cx *cx_ptr;
    ACPI_STATUS	 status;
    ACPI_BUFFER	 buf;
    ACPI_OBJECT	*top;
    ACPI_OBJECT	*pkg;
    uint32_t	 count;
    int		 i;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

#ifdef INVARIANTS
    if (reprobe)
	KKASSERT(&curthread->td_msgport == netisr_cpuport(sc->cst_cpuid));
#endif

    buf.Pointer = NULL;
    buf.Length = ACPI_ALLOCATE_BUFFER;
    status = AcpiEvaluateObject(sc->cst_handle, "_CST", NULL, &buf);
    if (ACPI_FAILURE(status))
	return (ENXIO);

    /* _CST is a package with a count and at least one Cx package. */
    top = (ACPI_OBJECT *)buf.Pointer;
    if (!ACPI_PKG_VALID(top, 2) || acpi_PkgInt32(top, 0, &count) != 0) {
	device_printf(sc->cst_dev, "invalid _CST package\n");
	AcpiOsFree(buf.Pointer);
	return (ENXIO);
    }
    if (count != top->Package.Count - 1) {
	device_printf(sc->cst_dev, "invalid _CST state count (%d != %d)\n",
	       count, top->Package.Count - 1);
	count = top->Package.Count - 1;
    }
    if (count > MAX_CX_STATES) {
	device_printf(sc->cst_dev, "_CST has too many states (%d)\n", count);
	count = MAX_CX_STATES;
    }

    sc->cst_flags |= ACPI_CST_FLAG_PROBING;
    cpu_sfence();

    /*
     * Free all previously allocated resources
     *
     * NOTE: It is needed for _CST reprobing.
     */
    acpi_cst_free_resource(sc, 0);

    /* Set up all valid states. */
    sc->cst_cx_count = 0;
    cx_ptr = sc->cst_cx_states;
    for (i = 0; i < count; i++) {
	int error;

	pkg = &top->Package.Elements[i + 1];
	if (!ACPI_PKG_VALID(pkg, 4) ||
	    acpi_PkgInt32(pkg, 1, &cx_ptr->type) != 0 ||
	    acpi_PkgInt32(pkg, 2, &cx_ptr->trans_lat) != 0 ||
	    acpi_PkgInt32(pkg, 3, &cx_ptr->power) != 0) {

	    device_printf(sc->cst_dev, "skipping invalid Cx state package\n");
	    continue;
	}

	/* Validate the state to see if we should use it. */
	switch (cx_ptr->type) {
	case ACPI_STATE_C1:
	    sc->cst_non_c3 = i;
	    cx_ptr->enter = acpi_cst_c1_halt_enter;
	    error = acpi_cst_cx_setup(cx_ptr);
	    if (error)
		panic("C1 CST HALT setup failed: %d", error);
	    cx_ptr++;
	    sc->cst_cx_count++;
	    continue;
	case ACPI_STATE_C2:
	    sc->cst_non_c3 = i;
	    break;
	case ACPI_STATE_C3:
	default:
	    if ((acpi_cst_quirks & ACPI_CST_QUIRK_NO_C3) != 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				 "cpu_cst%d: C3[%d] not available.\n",
				 device_get_unit(sc->cst_dev), i));
		continue;
	    }
	    break;
	}

	/*
	 * Allocate the control register for C2 or C3(+).
	 */
	KASSERT(cx_ptr->res == NULL, ("still has res"));
	acpi_PkgRawGas(pkg, 0, &cx_ptr->gas);

	cx_ptr->rid = sc->cst_parent->cpux_next_rid;
	acpi_bus_alloc_gas(sc->cst_dev, &cx_ptr->res_type, &cx_ptr->rid,
	    &cx_ptr->gas, &cx_ptr->res, RF_SHAREABLE);
	if (cx_ptr->res != NULL) {
	    sc->cst_parent->cpux_next_rid++;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			     "cpu_cst%d: Got C%d - %d latency\n",
			     device_get_unit(sc->cst_dev), cx_ptr->type,
			     cx_ptr->trans_lat));
	    cx_ptr->enter = acpi_cst_cx_io_enter;
	    cx_ptr->btag = rman_get_bustag(cx_ptr->res);
	    cx_ptr->bhand = rman_get_bushandle(cx_ptr->res);
	    error = acpi_cst_cx_setup(cx_ptr);
	    if (error)
		panic("C%d CST I/O setup failed: %d", cx_ptr->type, error);
	    cx_ptr++;
	    sc->cst_cx_count++;
	} else {
	    error = acpi_cst_cx_setup(cx_ptr);
	    if (!error) {
		KASSERT(cx_ptr->enter != NULL,
		    ("C%d enter is not set", cx_ptr->type));
		cx_ptr++;
		sc->cst_cx_count++;
	    }
	}
    }
    AcpiOsFree(buf.Pointer);

    if (reprobe) {
	/* If there are C3(+) states, always enable bus master wakeup */
	if ((acpi_cst_quirks & ACPI_CST_QUIRK_NO_BM) == 0) {
	    for (i = 0; i < sc->cst_cx_count; ++i) {
		struct acpi_cst_cx *cx = &sc->cst_cx_states[i];

		if (cx->type >= ACPI_STATE_C3) {
		    AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_RLD, 1);
		    break;
		}
	    }
	}

	/* Fix up the lowest Cx being used */
	acpi_cst_set_lowest_oncpu(sc, sc->cst_cx_lowest_req);
    }

    /*
     * Cache the lowest non-C3 state.
     * NOTE: must after cst_cx_lowest is set.
     */
    acpi_cst_non_c3(sc);

    cpu_sfence();
    sc->cst_flags &= ~ACPI_CST_FLAG_PROBING;

    return (0);
}

static void
acpi_cst_cx_reprobe_cst_handler(netmsg_t msg)
{
    struct netmsg_acpi_cst *rmsg = (struct netmsg_acpi_cst *)msg;
    int error;

    error = acpi_cst_cx_probe_cst(rmsg->sc, 1);
    lwkt_replymsg(&rmsg->base.lmsg, error);
}

static int
acpi_cst_cx_reprobe_cst(struct acpi_cst_softc *sc)
{
    struct netmsg_acpi_cst msg;

    netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	acpi_cst_cx_reprobe_cst_handler);
    msg.sc = sc;

    return lwkt_domsg(netisr_cpuport(sc->cst_cpuid), &msg.base.lmsg, 0);
}

/*
 * Call this *after* all CPUs Cx states have been attached.
 */
static void
acpi_cst_postattach(void *arg)
{
    struct acpi_cst_softc *sc;
    int i;

    /* Get set of Cx state devices */
    devclass_get_devices(acpi_cst_devclass, &acpi_cst_devices,
	&acpi_cst_ndevices);

    /*
     * Setup any quirks that might necessary now that we have probed
     * all the CPUs' Cx states.
     */
    acpi_cst_set_quirks();

    if (acpi_cst_use_fadt) {
	/*
	 * We are using Cx mode from FADT, probe for available Cx states
	 * for all processors.
	 */
	for (i = 0; i < acpi_cst_ndevices; i++) {
	    sc = device_get_softc(acpi_cst_devices[i]);
	    acpi_cst_cx_probe_fadt(sc);
	}
    } else {
	/*
	 * We are using _CST mode, remove C3 state if necessary.
	 *
	 * As we now know for sure that we will be using _CST mode
	 * install our notify handler.
	 */
	for (i = 0; i < acpi_cst_ndevices; i++) {
	    sc = device_get_softc(acpi_cst_devices[i]);
	    if (acpi_cst_quirks & ACPI_CST_QUIRK_NO_C3) {
		/* Free part of unused resources */
		acpi_cst_free_resource(sc, sc->cst_non_c3 + 1);
		sc->cst_cx_count = sc->cst_non_c3 + 1;
	    }
	    sc->cst_parent->cpux_cst_notify = acpi_cst_notify;
	}
    }
    acpi_cst_global_cx_count();

    /* Perform Cx final initialization. */
    for (i = 0; i < acpi_cst_ndevices; i++) {
	sc = device_get_softc(acpi_cst_devices[i]);
	acpi_cst_startup(sc);

	if (sc->cst_parent->glob_sysctl_tree != NULL) {
	    struct acpi_cpux_softc *cpux = sc->cst_parent;

	    /* Add a sysctl handler to handle global Cx lowest setting */
	    SYSCTL_ADD_PROC(&cpux->glob_sysctl_ctx,
	    		    SYSCTL_CHILDREN(cpux->glob_sysctl_tree),
			    OID_AUTO, "cx_lowest",
			    CTLTYPE_STRING | CTLFLAG_RW, NULL, 0,
			    acpi_cst_global_lowest_sysctl, "A",
			    "Requested global lowest Cx sleep state");
	    SYSCTL_ADD_PROC(&cpux->glob_sysctl_ctx,
	    		    SYSCTL_CHILDREN(cpux->glob_sysctl_tree),
			    OID_AUTO, "cx_lowest_use",
			    CTLTYPE_STRING | CTLFLAG_RD, NULL, 0,
			    acpi_cst_global_lowest_use_sysctl, "A",
			    "Global lowest Cx sleep state to use");
	}
    }

    /* Take over idling from cpu_idle_default(). */
    acpi_cst_cx_lowest = 0;
    acpi_cst_cx_lowest_req = 0;
    acpi_cst_disable_idle = FALSE;

    cpu_sfence();
    cpu_idle_hook = acpi_cst_idle;
}

static void
acpi_cst_support_list(struct acpi_cst_softc *sc)
{
    struct sbuf sb;
    int i;

    /*
     * Set up the list of Cx states
     */
    sbuf_new(&sb, sc->cst_cx_supported, sizeof(sc->cst_cx_supported),
	SBUF_FIXEDLEN);
    for (i = 0; i < sc->cst_cx_count; i++)
	sbuf_printf(&sb, "C%d/%d ", i + 1, sc->cst_cx_states[i].trans_lat);
    sbuf_trim(&sb);
    sbuf_finish(&sb);
}	

static void
acpi_cst_c3_bm_rld_handler(netmsg_t msg)
{
    struct netmsg_acpi_cst *rmsg = (struct netmsg_acpi_cst *)msg;

    AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_RLD, 1);
    lwkt_replymsg(&rmsg->base.lmsg, 0);
}

static void
acpi_cst_c3_bm_rld(struct acpi_cst_softc *sc)
{
    struct netmsg_acpi_cst msg;

    netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	acpi_cst_c3_bm_rld_handler);
    msg.sc = sc;

    lwkt_domsg(netisr_cpuport(sc->cst_cpuid), &msg.base.lmsg, 0);
}

static void
acpi_cst_startup(struct acpi_cst_softc *sc)
{
    struct acpi_cpux_softc *cpux = sc->cst_parent;
    int i, bm_rld_done = 0;

    for (i = 0; i < sc->cst_cx_count; ++i) {
	struct acpi_cst_cx *cx = &sc->cst_cx_states[i];
	int error;

	/* If there are C3(+) states, always enable bus master wakeup */
	if (cx->type >= ACPI_STATE_C3 && !bm_rld_done &&
	    (acpi_cst_quirks & ACPI_CST_QUIRK_NO_BM) == 0) {
	    acpi_cst_c3_bm_rld(sc);
	    bm_rld_done = 1;
	}

	/* Redo the Cx setup, since quirks have been changed */
	error = acpi_cst_cx_setup(cx);
	if (error)
	    panic("C%d startup setup failed: %d", i + 1, error);
    }

    acpi_cst_support_list(sc);
    
    SYSCTL_ADD_STRING(&cpux->pcpu_sysctl_ctx,
		      SYSCTL_CHILDREN(cpux->pcpu_sysctl_tree),
		      OID_AUTO, "cx_supported", CTLFLAG_RD,
		      sc->cst_cx_supported, 0,
		      "Cx/microsecond values for supported Cx states");
    SYSCTL_ADD_PROC(&cpux->pcpu_sysctl_ctx,
		    SYSCTL_CHILDREN(cpux->pcpu_sysctl_tree),
		    OID_AUTO, "cx_lowest", CTLTYPE_STRING | CTLFLAG_RW,
		    (void *)sc, 0, acpi_cst_lowest_sysctl, "A",
		    "requested lowest Cx sleep state");
    SYSCTL_ADD_PROC(&cpux->pcpu_sysctl_ctx,
		    SYSCTL_CHILDREN(cpux->pcpu_sysctl_tree),
		    OID_AUTO, "cx_lowest_use", CTLTYPE_STRING | CTLFLAG_RD,
		    (void *)sc, 0, acpi_cst_lowest_use_sysctl, "A",
		    "lowest Cx sleep state to use");
    SYSCTL_ADD_PROC(&cpux->pcpu_sysctl_ctx,
		    SYSCTL_CHILDREN(cpux->pcpu_sysctl_tree),
		    OID_AUTO, "cx_usage", CTLTYPE_STRING | CTLFLAG_RD,
		    (void *)sc, 0, acpi_cst_usage_sysctl, "A",
		    "percent usage for each Cx state");

#ifdef notyet
    /* Signal platform that we can handle _CST notification. */
    if (!acpi_cst_use_fadt && acpi_cst_ctrl != 0) {
	ACPI_LOCK(acpi);
	AcpiOsWritePort(acpi_cst_smi_cmd, acpi_cst_ctrl, 8);
	ACPI_UNLOCK(acpi);
    }
#endif
}

/*
 * Idle the CPU in the lowest state possible.  This function is called with
 * interrupts disabled.  Note that once it re-enables interrupts, a task
 * switch can occur so do not access shared data (i.e. the softc) after
 * interrupts are re-enabled.
 */
static void
acpi_cst_idle(void)
{
    struct	acpi_cst_softc *sc;
    struct	acpi_cst_cx *cx_next;
    union microtime_pcpu start, end;
    int		cx_next_idx, i, tdiff, bm_arb_disabled = 0;

    /* If disabled, return immediately. */
    if (acpi_cst_disable_idle) {
	ACPI_ENABLE_IRQS();
	return;
    }

    /*
     * Look up our CPU id to get our softc.  If it's NULL, we'll use C1
     * since there is no Cx state for this processor.
     */
    sc = acpi_cst_softc[mdcpu->mi.gd_cpuid];
    if (sc == NULL) {
	acpi_cst_c1_halt();
	return;
    }

    /* Still probing; use C1 */
    if (sc->cst_flags & ACPI_CST_FLAG_PROBING) {
	acpi_cst_c1_halt();
	return;
    }

    /* Find the lowest state that has small enough latency. */
    cx_next_idx = 0;
    for (i = sc->cst_cx_lowest; i >= 0; i--) {
	if (sc->cst_cx_states[i].trans_lat * 3 <= sc->cst_prev_sleep) {
	    cx_next_idx = i;
	    break;
	}
    }

    /*
     * Check for bus master activity if needed for the selected state.
     * If there was activity, clear the bit and use the lowest non-C3 state.
     */
    cx_next = &sc->cst_cx_states[cx_next_idx];
    if (cx_next->flags & ACPI_CST_CX_FLAG_BM_STS) {
	int bm_active;

	AcpiReadBitRegister(ACPI_BITREG_BUS_MASTER_STATUS, &bm_active);
	if (bm_active != 0) {
	    AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_STATUS, 1);
	    cx_next_idx = sc->cst_non_c3;
	}
    }

    /* Select the next state and update statistics. */
    cx_next = &sc->cst_cx_states[cx_next_idx];
    sc->cst_cx_stats[cx_next_idx]++;
    KASSERT(cx_next->type != ACPI_STATE_C0, ("C0 sleep"));

    /*
     * Execute HLT (or equivalent) and wait for an interrupt.  We can't
     * calculate the time spent in C1 since the place we wake up is an
     * ISR.  Assume we slept half of quantum and return.
     */
    if (cx_next->type == ACPI_STATE_C1) {
	sc->cst_prev_sleep = (sc->cst_prev_sleep * 3 + 500000 / hz) / 4;
	cx_next->enter(cx_next);
	return;
    }

    /* Execute the proper preamble before enter the selected state. */
    if (cx_next->preamble == ACPI_CST_CX_PREAMBLE_BM_ARB) {
	AcpiWriteBitRegister(ACPI_BITREG_ARB_DISABLE, 1);
	bm_arb_disabled = 1;
    } else if (cx_next->preamble == ACPI_CST_CX_PREAMBLE_WBINVD) {
	ACPI_FLUSH_CPU_CACHE();
    }

    /*
     * Enter the selected state and check time spent asleep.
     */
    microtime_pcpu_get(&start);
    cpu_mfence();

    cx_next->enter(cx_next);

    cpu_mfence();
    microtime_pcpu_get(&end);

    /* Enable bus master arbitration, if it was disabled. */
    if (bm_arb_disabled)
	AcpiWriteBitRegister(ACPI_BITREG_ARB_DISABLE, 0);

    ACPI_ENABLE_IRQS();

    /* Find the actual time asleep in microseconds. */
    tdiff = microtime_pcpu_diff(&start, &end);
    sc->cst_prev_sleep = (sc->cst_prev_sleep * 3 + tdiff) / 4;
}

/*
 * Re-evaluate the _CST object when we are notified that it changed.
 */
static void
acpi_cst_notify(device_t dev)
{
    struct acpi_cst_softc *sc = device_get_softc(dev);

    KASSERT(curthread->td_type != TD_TYPE_NETISR,
        ("notify in netisr%d", mycpuid));

    lwkt_serialize_enter(&acpi_cst_slize);

    /* Update the list of Cx states. */
    acpi_cst_cx_reprobe_cst(sc);
    acpi_cst_support_list(sc);

    /* Update the new lowest useable Cx state for all CPUs. */
    acpi_cst_global_cx_count();

    /*
     * Fix up the lowest Cx being used
     */
    if (acpi_cst_cx_lowest_req < acpi_cst_cx_count)
	acpi_cst_cx_lowest = acpi_cst_cx_lowest_req;
    if (acpi_cst_cx_lowest > acpi_cst_cx_count - 1)
	acpi_cst_cx_lowest = acpi_cst_cx_count - 1;

    lwkt_serialize_exit(&acpi_cst_slize);
}

static int
acpi_cst_set_quirks(void)
{
    device_t acpi_dev;
    uint32_t val;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /*
     * Bus mastering arbitration control is needed to keep caches coherent
     * while sleeping in C3.  If it's not present but a working flush cache
     * instruction is present, flush the caches before entering C3 instead.
     * Otherwise, just disable C3 completely.
     */
    if (AcpiGbl_FADT.Pm2ControlBlock == 0 ||
	AcpiGbl_FADT.Pm2ControlLength == 0) {
	if ((AcpiGbl_FADT.Flags & ACPI_FADT_WBINVD) &&
	    (AcpiGbl_FADT.Flags & ACPI_FADT_WBINVD_FLUSH) == 0) {
	    acpi_cst_quirks |= ACPI_CST_QUIRK_NO_BM;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"cpu_cst: no BM control, using flush cache method\n"));
	} else {
	    acpi_cst_quirks |= ACPI_CST_QUIRK_NO_C3;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"cpu_cst: no BM control, C3 not available\n"));
	}
    }

    /* Look for various quirks of the PIIX4 part. */
    acpi_dev = pci_find_device(PCI_VENDOR_INTEL, PCI_DEVICE_82371AB_3);
    if (acpi_dev != NULL) {
	switch (pci_get_revid(acpi_dev)) {
	/*
	 * Disable C3 support for all PIIX4 chipsets.  Some of these parts
	 * do not report the BMIDE status to the BM status register and
	 * others have a livelock bug if Type-F DMA is enabled.  Linux
	 * works around the BMIDE bug by reading the BM status directly
	 * but we take the simpler approach of disabling C3 for these
	 * parts.
	 *
	 * See erratum #18 ("C3 Power State/BMIDE and Type-F DMA
	 * Livelock") from the January 2002 PIIX4 specification update.
	 * Applies to all PIIX4 models.
	 *
	 * Also, make sure that all interrupts cause a "Stop Break"
	 * event to exit from C2 state.
	 * Also, BRLD_EN_BM (ACPI_BITREG_BUS_MASTER_RLD in ACPI-speak)
	 * should be set to zero, otherwise it causes C2 to short-sleep.
	 * PIIX4 doesn't properly support C3 and bus master activity
	 * need not break out of C2.
	 */
	case PCI_REVISION_A_STEP:
	case PCI_REVISION_B_STEP:
	case PCI_REVISION_4E:
	case PCI_REVISION_4M:
	    acpi_cst_quirks |= ACPI_CST_QUIRK_NO_C3;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"cpu_cst: working around PIIX4 bug, disabling C3\n"));

	    val = pci_read_config(acpi_dev, PIIX4_DEVACTB_REG, 4);
	    if ((val & PIIX4_STOP_BREAK_MASK) != PIIX4_STOP_BREAK_MASK) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		    "cpu_cst: PIIX4: enabling IRQs to generate Stop Break\n"));
	    	val |= PIIX4_STOP_BREAK_MASK;
		pci_write_config(acpi_dev, PIIX4_DEVACTB_REG, val, 4);
	    }
	    AcpiReadBitRegister(ACPI_BITREG_BUS_MASTER_RLD, &val);
	    if (val) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		    "cpu_cst: PIIX4: reset BRLD_EN_BM\n"));
		AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_RLD, 0);
	    }
	    break;
	default:
	    break;
	}
    }

    return (0);
}

static int
acpi_cst_usage_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_cst_softc *sc;
    struct sbuf	 sb;
    char	 buf[128];
    int		 i;
    uintmax_t	 fract, sum, whole;

    sc = (struct acpi_cst_softc *) arg1;
    sum = 0;
    for (i = 0; i < sc->cst_cx_count; i++)
	sum += sc->cst_cx_stats[i];
    sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
    for (i = 0; i < sc->cst_cx_count; i++) {
	if (sum > 0) {
	    whole = (uintmax_t)sc->cst_cx_stats[i] * 100;
	    fract = (whole % sum) * 100;
	    sbuf_printf(&sb, "%u.%02u%% ", (u_int)(whole / sum),
		(u_int)(fract / sum));
	} else
	    sbuf_printf(&sb, "0.00%% ");
    }
    sbuf_printf(&sb, "last %dus", sc->cst_prev_sleep);
    sbuf_trim(&sb);
    sbuf_finish(&sb);
    sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
    sbuf_delete(&sb);

    return (0);
}

static int
acpi_cst_set_lowest_oncpu(struct acpi_cst_softc *sc, int val)
{
    int old_lowest, error = 0, old_lowest_req;
    uint32_t old_type, type;

    KKASSERT(mycpuid == sc->cst_cpuid);

    old_lowest_req = sc->cst_cx_lowest_req;
    sc->cst_cx_lowest_req = val;

    if (val > sc->cst_cx_count - 1)
	val = sc->cst_cx_count - 1;
    old_lowest = atomic_swap_int(&sc->cst_cx_lowest, val);

    old_type = sc->cst_cx_states[old_lowest].type;
    type = sc->cst_cx_states[val].type;
    if (old_type >= ACPI_STATE_C3 && type < ACPI_STATE_C3) {
	KKASSERT(acpi_cst_c3_reqs > 0);
	if (atomic_fetchadd_int(&acpi_cst_c3_reqs, -1) == 1) {
	    /*
	     * All of the CPUs exit C3(+) state, use a better
	     * one shot timer.
	     */
	    error = cputimer_intr_select_caps(CPUTIMER_INTR_CAP_NONE);
	    KKASSERT(!error || error == ERESTART);
	    if (error == ERESTART) {
		if (bootverbose)
		    kprintf("disable C3(+), restart intr cputimer\n");
		cputimer_intr_restart();
	    }
    	}
    } else if (type >= ACPI_STATE_C3 && old_type < ACPI_STATE_C3) {
	if (atomic_fetchadd_int(&acpi_cst_c3_reqs, 1) == 0) {
	    /*
	     * When the first CPU enters C3(+) state, switch
	     * to an one shot timer, which could handle
	     * C3(+) state, i.e. the timer will not hang.
	     */
	    error = cputimer_intr_select_caps(CPUTIMER_INTR_CAP_PS);
	    if (error == ERESTART) {
		if (bootverbose)
		    kprintf("enable C3(+), restart intr cputimer\n");
		cputimer_intr_restart();
	    } else if (error) {
		kprintf("no suitable intr cputimer found\n");

		/* Restore */
		sc->cst_cx_lowest_req = old_lowest_req;
		sc->cst_cx_lowest = old_lowest;
		atomic_fetchadd_int(&acpi_cst_c3_reqs, -1);
	    }
	}
    }

    if (error)
	return error;

    /* Cache the new lowest non-C3 state. */
    acpi_cst_non_c3(sc);

    /* Reset the statistics counters. */
    bzero(sc->cst_cx_stats, sizeof(sc->cst_cx_stats));
    return (0);
}

static void
acpi_cst_set_lowest_handler(netmsg_t msg)
{
    struct netmsg_acpi_cst *rmsg = (struct netmsg_acpi_cst *)msg;
    int error;

    error = acpi_cst_set_lowest_oncpu(rmsg->sc, rmsg->val);
    lwkt_replymsg(&rmsg->base.lmsg, error);
}

static int
acpi_cst_set_lowest(struct acpi_cst_softc *sc, int val)
{
    struct netmsg_acpi_cst msg;

    netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	acpi_cst_set_lowest_handler);
    msg.sc = sc;
    msg.val = val;

    return lwkt_domsg(netisr_cpuport(sc->cst_cpuid), &msg.base.lmsg, 0);
}

static int
acpi_cst_lowest_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct	 acpi_cst_softc *sc;
    char	 state[8];
    int		 val, error;

    sc = (struct acpi_cst_softc *)arg1;
    ksnprintf(state, sizeof(state), "C%d", sc->cst_cx_lowest_req + 1);
    error = sysctl_handle_string(oidp, state, sizeof(state), req);
    if (error != 0 || req->newptr == NULL)
	return (error);
    if (strlen(state) < 2 || toupper(state[0]) != 'C')
	return (EINVAL);
    val = (int) strtol(state + 1, NULL, 10) - 1;
    if (val < 0)
	return (EINVAL);

    lwkt_serialize_enter(&acpi_cst_slize);
    error = acpi_cst_set_lowest(sc, val);
    lwkt_serialize_exit(&acpi_cst_slize);

    return error;
}

static int
acpi_cst_lowest_use_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct	 acpi_cst_softc *sc;
    char	 state[8];

    sc = (struct acpi_cst_softc *)arg1;
    ksnprintf(state, sizeof(state), "C%d", sc->cst_cx_lowest + 1);
    return sysctl_handle_string(oidp, state, sizeof(state), req);
}

static int
acpi_cst_global_lowest_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct	acpi_cst_softc *sc;
    char	state[8];
    int		val, error, i;

    ksnprintf(state, sizeof(state), "C%d", acpi_cst_cx_lowest_req + 1);
    error = sysctl_handle_string(oidp, state, sizeof(state), req);
    if (error != 0 || req->newptr == NULL)
	return (error);
    if (strlen(state) < 2 || toupper(state[0]) != 'C')
	return (EINVAL);
    val = (int) strtol(state + 1, NULL, 10) - 1;
    if (val < 0)
	return (EINVAL);

    lwkt_serialize_enter(&acpi_cst_slize);

    acpi_cst_cx_lowest_req = val;
    acpi_cst_cx_lowest = val;
    if (acpi_cst_cx_lowest > acpi_cst_cx_count - 1)
	acpi_cst_cx_lowest = acpi_cst_cx_count - 1;

    /* Update the new lowest useable Cx state for all CPUs. */
    for (i = 0; i < acpi_cst_ndevices; i++) {
	sc = device_get_softc(acpi_cst_devices[i]);
	error = acpi_cst_set_lowest(sc, val);
	if (error) {
	    KKASSERT(i == 0);
	    break;
	}
    }

    lwkt_serialize_exit(&acpi_cst_slize);

    return error;
}

static int
acpi_cst_global_lowest_use_sysctl(SYSCTL_HANDLER_ARGS)
{
    char	state[8];

    ksnprintf(state, sizeof(state), "C%d", acpi_cst_cx_lowest + 1);
    return sysctl_handle_string(oidp, state, sizeof(state), req);
}

/*
 * Put the CPU in C1 in a machine-dependant way.
 * XXX: shouldn't be here!
 */
static void
acpi_cst_c1_halt(void)
{
    splz();
    if ((mycpu->gd_reqflags & RQF_IDLECHECK_WK_MASK) == 0)
        __asm __volatile("sti; hlt");
    else
        __asm __volatile("sti; pause");
}

static void
acpi_cst_non_c3(struct acpi_cst_softc *sc)
{
    int i;

    sc->cst_non_c3 = 0;
    for (i = sc->cst_cx_lowest; i >= 0; i--) {
	if (sc->cst_cx_states[i].type < ACPI_STATE_C3) {
	    sc->cst_non_c3 = i;
	    break;
	}
    }
    if (bootverbose)
	device_printf(sc->cst_dev, "non-C3 %d\n", sc->cst_non_c3);
}

/*
 * Update the largest Cx state supported in the global acpi_cst_cx_count.
 * It will be used in the global Cx sysctl handler.
 */
static void
acpi_cst_global_cx_count(void)
{
    struct acpi_cst_softc *sc;
    int i;

    if (acpi_cst_ndevices == 0) {
	acpi_cst_cx_count = 0;
	return;
    }

    sc = device_get_softc(acpi_cst_devices[0]);
    acpi_cst_cx_count = sc->cst_cx_count;

    for (i = 1; i < acpi_cst_ndevices; i++) {
	struct acpi_cst_softc *sc = device_get_softc(acpi_cst_devices[i]);

	if (sc->cst_cx_count < acpi_cst_cx_count)
	    acpi_cst_cx_count = sc->cst_cx_count;
    }
    if (bootverbose)
	kprintf("cpu_cst: global Cx count %d\n", acpi_cst_cx_count);
}

static void
acpi_cst_c1_halt_enter(const struct acpi_cst_cx *cx __unused)
{
    acpi_cst_c1_halt();
}

static void
acpi_cst_cx_io_enter(const struct acpi_cst_cx *cx)
{
    uint64_t dummy;

    /*
     * Read I/O to enter this Cx state
     */
    bus_space_read_1(cx->btag, cx->bhand, 0);
    /*
     * Perform a dummy I/O read.  Since it may take an arbitrary time
     * to enter the idle state, this read makes sure that we are frozen.
     */
    AcpiRead(&dummy, &AcpiGbl_FADT.XPmTimerBlock);
}

static int
acpi_cst_cx_setup(struct acpi_cst_cx *cx)
{
    cx->flags &= ~ACPI_CST_CX_FLAG_BM_STS;
    cx->preamble = ACPI_CST_CX_PREAMBLE_NONE;

    if (cx->type >= ACPI_STATE_C3) {
	/*
	 * Set the required operations for entering C3(+) state.
	 * Later acpi_cst_md_cx_setup() may fix them up.
	 */

	/*
	 * Always check BM_STS.
	 */
	if ((acpi_cst_quirks & ACPI_CST_QUIRK_NO_BM) == 0)
	    cx->flags |= ACPI_CST_CX_FLAG_BM_STS;

	/*
	 * According to the ACPI specification, bus master arbitration
	 * is only available on UP system.  For MP system, cache flushing
	 * is required.
	 */
	if (ncpus == 1 && (acpi_cst_quirks & ACPI_CST_QUIRK_NO_BM) == 0)
	    cx->preamble = ACPI_CST_CX_PREAMBLE_BM_ARB;
	else
	    cx->preamble = ACPI_CST_CX_PREAMBLE_WBINVD;
    }
    return acpi_cst_md_cx_setup(cx);
}

static void
acpi_cst_free_resource(struct acpi_cst_softc *sc, int start)
{
    int i;

    for (i = start; i < MAX_CX_STATES; ++i) {
	struct acpi_cst_cx *cx = &sc->cst_cx_states[i];

	if (cx->res != NULL)
	    bus_release_resource(sc->cst_dev, cx->res_type, cx->rid, cx->res);
	memset(cx, 0, sizeof(*cx));
    }
}
