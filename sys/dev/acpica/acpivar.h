/*-
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@jp.freebsd.org>
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
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
 * $FreeBSD: src/sys/dev/acpica/acpivar.h,v 1.108.8.1 2009/04/15 03:14:26 kensmith Exp $
 */

#ifndef _ACPIVAR_H_
#define _ACPIVAR_H_

#ifdef _KERNEL


#include "acpi_if.h"
#include "bus_if.h"
#include <sys/eventhandler.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/bus.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/serialize.h>
#include <contrib/dev/acpica/source/include/acmacros.h>
#include <contrib/dev/acpica/source/include/acconfig.h>
#include <contrib/dev/acpica/source/include/aclocal.h>
#include <contrib/dev/acpica/source/include/acobject.h>
#include <contrib/dev/acpica/source/include/acstruct.h>
#include <contrib/dev/acpica/source/include/acutils.h>

struct apm_clone_data;
struct acpi_softc {
    device_t		acpi_dev;
    cdev_t		acpi_dev_t;

    struct resource	*acpi_irq;
    int			acpi_irq_rid;
    void		*acpi_irq_handle;

    int			acpi_enabled;
    int			acpi_sstate;
    int			acpi_sleep_disabled;

    struct sysctl_ctx_list acpi_sysctl_ctx;
    struct sysctl_oid	*acpi_sysctl_tree;
    int			acpi_power_button_sx;
    int			acpi_sleep_button_sx;
    int			acpi_lid_switch_sx;

    int			acpi_standby_sx;
    int			acpi_suspend_sx;

    int			acpi_sleep_delay;
    int			acpi_s4bios;
    int			acpi_do_disable;
    int			acpi_verbose;
    int			acpi_handle_reboot;

    bus_dma_tag_t	acpi_waketag;
    bus_dmamap_t	acpi_wakemap;
    vm_offset_t		acpi_wakeaddr;
    vm_paddr_t		acpi_wakephys;

    int			acpi_next_sstate;	/* Next suspend Sx state. */
    struct apm_clone_data *acpi_clone;		/* Pseudo-dev for devd(8). */
    STAILQ_HEAD(,apm_clone_data) apm_cdevs;	/* All apm/apmctl/acpi cdevs. */
    struct callout	susp_force_to;		/* Force suspend if no acks. */
};

struct acpi_device {
    /* ACPI ivars */
    ACPI_HANDLE			ad_handle;
    uintptr_t			ad_magic;
    void			*ad_private;
    int				ad_flags;

    /* Resources */
    struct resource_list	ad_rl;
};

/* Track device (/dev/{apm,apmctl} and /dev/acpi) notification status. */
struct apm_clone_data {
    STAILQ_ENTRY(apm_clone_data) entries;
    struct cdev 	*cdev;
    int			flags;
#define	ACPI_EVF_NONE	0	/* /dev/apm semantics */
#define	ACPI_EVF_DEVD	1	/* /dev/acpi is handled via devd(8) */
#define	ACPI_EVF_WRITE	2	/* Device instance is opened writable. */
    int			notify_status;
#define	APM_EV_NONE	0	/* Device not yet aware of pending sleep. */
#define	APM_EV_NOTIFIED	1	/* Device saw next sleep state. */
#define	APM_EV_ACKED	2	/* Device agreed sleep can occur. */
    struct acpi_softc	*acpi_sc;
};

#define ACPI_PRW_MAX_POWERRES	8

struct acpi_prw_data {
    ACPI_HANDLE		gpe_handle;
    int			gpe_bit;
    int			lowest_wake;
    ACPI_OBJECT		power_res[ACPI_PRW_MAX_POWERRES];
    int			power_res_count;
};

/* Flags for each device defined in the AML namespace. */
#define ACPI_FLAG_WAKE_ENABLED	0x1

/* Macros for extracting parts of a PCI address from an _ADR value. */
#define	ACPI_ADR_PCI_SLOT(adr)	(((adr) & 0xffff0000) >> 16)
#define	ACPI_ADR_PCI_FUNC(adr)	((adr) & 0xffff)

/*
 * Entry points to ACPI from above are global functions defined in this
 * file, sysctls, and I/O on the control device.  Entry points from below
 * are interrupts (the SCI), notifies, task queue threads, and the thermal
 * zone polling thread.
 *
 * ACPI tables and global shared data are protected by a global lock
 * (acpi_lock).
 *
 * Each ACPI device can have its own driver-specific mutex for protecting
 * shared access to local data.  The ACPI_LOCK macros handle mutexes.
 *
 * Drivers that need to serialize access to functions (e.g., to route
 * interrupts, get/set control paths, etc.) should use the sx lock macros
 * (ACPI_SERIAL).
 *
 * ACPICA handles its own locking and should not be called with locks held.
 *
 * The most complicated path is:
 *     GPE -> EC runs _Qxx -> _Qxx reads EC space -> GPE
 */
extern struct lock acpi_lock;
/* acpi_thermal does lock recurs on purpose */
/* I bet I should use some other locks here */
#define ACPI_LOCK(sys)                  lockmgr(&sys##_lock, LK_EXCLUSIVE|LK_RETRY|LK_CANRECURSE)
#define ACPI_UNLOCK(sys)                lockmgr(&sys##_lock, LK_RELEASE)
#define ACPI_LOCK_ASSERT(sys)           KKASSERT(lockstatus(&sys##_lock, curthread) == LK_EXCLUSIVE)
#define ACPI_ASSERTLOCK ACPI_LOCK_ASSERT
#define ACPI_LOCK_DECL(sys, name)       static struct lock sys##_lock
#define ACPI_LOCK_INIT(sys, name)       lockinit(&sys##_lock, name, 0, 0)

#define ACPI_SERIAL_INIT(sys)           lockinit(&sys##_serial, #sys, 0, 0)
#define ACPI_SERIAL_BEGIN(sys)          lockmgr(&sys##_serial, LK_EXCLUSIVE|LK_RETRY)
#define ACPI_SERIAL_END(sys)            lockmgr(&sys##_serial, LK_RELEASE)
#define ACPI_SERIAL_ASSERT(sys)         KKASSERT(lockstatus(&sys##_serial, curthread) == LK_EXCLUSIVE)
#define ACPI_SERIAL_DECL(sys, name)     static struct lock sys##_serial

/*
 * ACPICA does not define layers for non-ACPICA drivers.
 * We define some here within the range provided.
 */
#define	ACPI_AC_ADAPTER		0x00010000
#define	ACPI_BATTERY		0x00020000
#define	ACPI_BUS		0x00040000
#define	ACPI_BUTTON		0x00080000
#define	ACPI_EC			0x00100000
#define	ACPI_FAN		0x00200000
#define	ACPI_POWERRES		0x00400000
#define	ACPI_PROCESSOR		0x00800000
#define	ACPI_THERMAL		0x01000000
#define	ACPI_TIMER		0x02000000
#define	ACPI_OEM		0x04000000

/*
 * Constants for different interrupt models used with acpi_SetIntrModel().
 */
#define	ACPI_INTR_PIC		0
#define	ACPI_INTR_APIC		1
#define	ACPI_INTR_SAPIC		2

/*
 * Various features and capabilities for the acpi_get_features() method.
 * In particular, these are used for the ACPI 3.0 _PDC and _OSC methods.
 * See the Intel document titled "Intel Processor Vendor-Specific ACPI",
 * number 302223-005.
 */
#define ACPI_PDC_PX_MSR		(1 << 0) /* Intel SpeedStep PERF_CTL MSRs */
#define ACPI_PDC_MP_C1_IO_HALT	(1 << 1) /* Intel C1 "IO then halt" sequence */
#define ACPI_PDC_TX_MSR		(1 << 2) /* Intel OnDemand throttling MSRs */
#define ACPI_PDC_MP_C1PXTX	(1 << 3) /* MP C1, Px, and Tx */
#define ACPI_PDC_MP_C2C3	(1 << 4) /* MP C2 and C3 */
#define ACPI_PDC_MP_PX_SWCOORD	(1 << 5) /* MP Px, using _PSD */
#define ACPI_PDC_MP_CX_SWCOORD	(1 << 6) /* MP Cx, using _CSD */
#define ACPI_PDC_MP_TX_SWCOORD	(1 << 7) /* MP Tx, using _TSD */
#define ACPI_PDC_MP_C1_NATIVE	(1 << 8) /* MP C1 support other than halt */
#define ACPI_PDC_MP_C2C3_NATIVE	(1 << 9) /* MP C2 and C3 support */
#define ACPI_PDC_PX_HWCOORD	(1 << 11)/* Hardware coordination of Px */

#define ACPI_OSC_QUERY_SUPPORT	(1 << 0) /* Query Support Flag */

#define ACPI_OSCERR_OSCFAIL	(1 << 1) /* _OSC failure */
#define ACPI_OSCERR_UUID	(1 << 2) /* Unrecognized UUID */
#define ACPI_OSCERR_REVISION	(1 << 3) /* Unrecognized revision ID */
#define ACPI_OSCERR_CAPSMASKED	(1 << 4) /* Capabilities have been cleared */

/*
 * Quirk flags.
 *
 * ACPI_Q_BROKEN: Disables all ACPI support.
 * ACPI_Q_TIMER: Disables support for the ACPI timer.
 * ACPI_Q_MADT_IRQ0: Specifies that ISA IRQ 0 is wired up to pin 0 of the
 *	first APIC and that the MADT should force that by ignoring the PC-AT
 *	compatible flag and ignoring overrides that redirect IRQ 0 to pin 2.
 * ACPI_Q_BATT_RATE_ABS: Specifies that the DSDT reports a negative 16-bit
 *	value for charging/discharging current and/or 0 as 65536.
 */
extern int	acpi_quirks;
#define ACPI_Q_OK		0
#define ACPI_Q_BROKEN		(1 << 0)
#define ACPI_Q_TIMER		(1 << 1)
#define ACPI_Q_MADT_IRQ0	(1 << 2)
#define ACPI_Q_BATT_RATE_ABS	(1 << 3)

/*
 * Note that the low ivar values are reserved to provide
 * interface compatibility with ISA drivers which can also
 * attach to ACPI.
 */
#define ACPI_IVAR_HANDLE	0x100
#define ACPI_IVAR_MAGIC		0x101
#define ACPI_IVAR_PRIVATE	0x102
#define ACPI_IVAR_FLAGS		0x103

/*
 * Accessor functions for our ivars.  Default value for BUS_READ_IVAR is
 * (type) 0.  The <sys/bus.h> accessor functions don't check return values.
 */
#define __ACPI_BUS_ACCESSOR(varp, var, ivarp, ivar, type)	\
								\
static __inline type varp ## _get_ ## var(device_t dev)		\
{								\
    uintptr_t v = 0;						\
    BUS_READ_IVAR(device_get_parent(dev), dev,			\
	ivarp ## _IVAR_ ## ivar, &v);				\
    return ((type) v);						\
}								\
								\
static __inline void varp ## _set_ ## var(device_t dev, type t)	\
{								\
    uintptr_t v = (uintptr_t) t;				\
    BUS_WRITE_IVAR(device_get_parent(dev), dev,			\
	ivarp ## _IVAR_ ## ivar, v);				\
}

__ACPI_BUS_ACCESSOR(acpi, handle, ACPI, HANDLE, ACPI_HANDLE)
__ACPI_BUS_ACCESSOR(acpi, magic, ACPI, MAGIC, uintptr_t)
__ACPI_BUS_ACCESSOR(acpi, private, ACPI, PRIVATE, void *)
__ACPI_BUS_ACCESSOR(acpi, flags, ACPI, FLAGS, int)

void acpi_fake_objhandler(ACPI_HANDLE h, void *data);
static __inline device_t
acpi_get_device(ACPI_HANDLE handle)
{
    void *dev = NULL;
    AcpiGetData(handle, acpi_fake_objhandler, &dev);
    return ((device_t)dev);
}

static __inline ACPI_OBJECT_TYPE
acpi_get_type(device_t dev)
{
    ACPI_HANDLE		h;
    ACPI_OBJECT_TYPE	t;

    if ((h = acpi_get_handle(dev)) == NULL)
	return (ACPI_TYPE_NOT_FOUND);
    if (ACPI_FAILURE(AcpiGetType(h, &t)))
	return (ACPI_TYPE_NOT_FOUND);
    return (t);
}

/* Find the difference between two PM tick counts. */
static __inline uint32_t
acpi_TimerDelta(uint32_t end, uint32_t start)
{

	if (end < start && (AcpiGbl_FADT.Flags & ACPI_FADT_32BIT_TIMER) == 0)
		end |= 0x01000000;
	return (end - start);
}

#ifdef ACPI_DEBUGGER
void		acpi_EnterDebugger(void);
#endif

#ifdef ACPI_DEBUG
#include <sys/cons.h>
#define STEP(x)		do {printf x, printf("\n"); cngetc();} while (0)
#else
#define STEP(x)
#endif

#define ACPI_VPRINT(dev, acpi_sc, x...) do {			\
    if (acpi_get_verbose(acpi_sc))				\
	device_printf(dev, x);					\
} while (0)

/* Values for the device _STA (status) method. */
#define ACPI_STA_PRESENT	(1 << 0)
#define ACPI_STA_ENABLED	(1 << 1)
#define ACPI_STA_SHOW_IN_UI	(1 << 2)
#define ACPI_STA_FUNCTIONAL	(1 << 3)
#define ACPI_STA_BATT_PRESENT	(1 << 4)

#define ACPI_DEVINFO_PRESENT(x, flags)					\
	(((x) & (flags)) == (flags))
#define ACPI_DEVICE_PRESENT(x)						\
	ACPI_DEVINFO_PRESENT(x, ACPI_STA_PRESENT | ACPI_STA_FUNCTIONAL)
#define ACPI_BATTERY_PRESENT(x)						\
	ACPI_DEVINFO_PRESENT(x, ACPI_STA_PRESENT | ACPI_STA_FUNCTIONAL | \
	    ACPI_STA_BATT_PRESENT)

BOOLEAN		acpi_DeviceIsPresent(device_t dev);
BOOLEAN		acpi_BatteryIsPresent(device_t dev);
ACPI_STATUS	acpi_GetHandleInScope(ACPI_HANDLE parent, char *path,
		    ACPI_HANDLE *result);
ACPI_BUFFER	*acpi_AllocBuffer(int size);
ACPI_STATUS	acpi_ConvertBufferToInteger(ACPI_BUFFER *bufp,
		    UINT32 *number);
ACPI_STATUS	acpi_GetInteger(ACPI_HANDLE handle, char *path,
		    UINT32 *number);
ACPI_STATUS	acpi_SetInteger(ACPI_HANDLE handle, char *path,
		    UINT32 number);
ACPI_STATUS	acpi_ForeachPackageObject(ACPI_OBJECT *obj, 
		    void (*func)(ACPI_OBJECT *comp, void *arg), void *arg);
ACPI_STATUS	acpi_FindIndexedResource(ACPI_BUFFER *buf, int index,
		    ACPI_RESOURCE **resp);
ACPI_STATUS	acpi_AppendBufferResource(ACPI_BUFFER *buf,
		    ACPI_RESOURCE *res);
ACPI_STATUS	acpi_SetIntrModel(int model);
int		acpi_ReqSleepState(struct acpi_softc *sc, int state);
int		acpi_AckSleepState(struct apm_clone_data *clone, int error);
ACPI_STATUS	acpi_SetSleepState(struct acpi_softc *sc, int state);
int		acpi_wake_set_enable(device_t dev, int enable);
int		acpi_parse_prw(ACPI_HANDLE h, struct acpi_prw_data *prw);
ACPI_STATUS	acpi_Startup(void);
void		acpi_UserNotify(const char *subsystem, ACPI_HANDLE h,
		    uint8_t notify);
int		acpi_bus_alloc_gas(device_t dev, int *type, int *rid,
		    ACPI_GENERIC_ADDRESS *gas, struct resource **res,
		    u_int flags);
ACPI_STATUS	acpi_eval_osc(device_t dev, ACPI_HANDLE handle,
		    const char *uuidstr, int revision, uint32_t *buf,
		    int count);

struct acpi_parse_resource_set {
    void	(*set_init)(device_t dev, void *arg, void **context);
    void	(*set_done)(device_t dev, void *context);
    void	(*set_ioport)(device_t dev, void *context, uint64_t base,
		    uint64_t length);
    void	(*set_iorange)(device_t dev, void *context, uint64_t low,
		    uint64_t high, uint64_t length, uint64_t align);
    void	(*set_memory)(device_t dev, void *context, uint64_t base,
		    uint64_t length);
    void	(*set_memoryrange)(device_t dev, void *context, uint64_t low,
		    uint64_t high, uint64_t length, uint64_t align);
    void	(*set_irq)(device_t dev, void *context, uint8_t *irq,
		    int count, int trig, int pol);
    void	(*set_ext_irq)(device_t dev, void *context, uint32_t *irq,
		    int count, int trig, int pol);
    void	(*set_drq)(device_t dev, void *context, uint8_t *drq,
		    int count);
    void	(*set_start_dependent)(device_t dev, void *context,
		    int preference);
    void	(*set_end_dependent)(device_t dev, void *context);
};

extern struct	acpi_parse_resource_set acpi_res_parse_set;

void		acpi_config_intr(device_t dev, ACPI_RESOURCE *res);
ACPI_STATUS	acpi_lookup_irq_resource(device_t dev, int rid,
		    struct resource *res, ACPI_RESOURCE *acpi_res);
ACPI_STATUS	acpi_parse_resources(device_t dev, ACPI_HANDLE handle,
		    struct acpi_parse_resource_set *set, void *arg);

/* ACPI event handling */
UINT32		acpi_event_power_button_sleep(void *context);
UINT32		acpi_event_power_button_wake(void *context);
UINT32		acpi_event_sleep_button_sleep(void *context);
UINT32		acpi_event_sleep_button_wake(void *context);

#define ACPI_EVENT_PRI_FIRST      0
#define ACPI_EVENT_PRI_DEFAULT    10000
#define ACPI_EVENT_PRI_LAST       20000

typedef void (*acpi_event_handler_t)(void *, int);

EVENTHANDLER_DECLARE(acpi_sleep_event, acpi_event_handler_t);
EVENTHANDLER_DECLARE(acpi_wakeup_event, acpi_event_handler_t);

/* Device power control. */
ACPI_STATUS	acpi_pwr_wake_enable(ACPI_HANDLE consumer, int enable);
ACPI_STATUS	acpi_pwr_switch_consumer(ACPI_HANDLE consumer, int state);

/* Misc. */
static __inline struct acpi_softc *
acpi_device_get_parent_softc(device_t child)
{
    device_t	parent;

    parent = device_get_parent(child);
    if (parent == NULL)
	return (NULL);
    return (device_get_softc(parent));
}

static __inline int
acpi_get_verbose(struct acpi_softc *sc)
{
    if (sc)
	return (sc->acpi_verbose);
    return (0);
}

char		*acpi_name(ACPI_HANDLE handle);
int		acpi_avoid(ACPI_HANDLE handle);
int		acpi_disabled(char *subsys);
int		acpi_enabled(char *subsys);
int		acpi_machdep_init(device_t dev);
void		acpi_install_wakeup_handler(struct acpi_softc *sc);
int		acpi_sleep_machdep(struct acpi_softc *sc, int state);
int		acpi_table_quirks(int *quirks);
int		acpi_machdep_quirks(int *quirks);

/* Battery Abstraction. */
struct acpi_battinfo;

int		acpi_battery_register(device_t dev);
int		acpi_battery_remove(device_t dev);
int		acpi_battery_get_units(void);
int		acpi_battery_get_info_expire(void);
int		acpi_battery_bst_valid(struct acpi_bst *bst);
int		acpi_battery_bif_valid(struct acpi_bif *bif);
int		acpi_battery_get_battinfo(device_t dev,
		    struct acpi_battinfo *info);

/* Embedded controller. */
void		acpi_ec_ecdt_probe(device_t);

/* AC adapter interface. */
int		acpi_acad_get_acline(int *);

/* Package manipulation convenience functions. */
#define ACPI_PKG_VALID(pkg, size)				\
    ((pkg) != NULL && (pkg)->Type == ACPI_TYPE_PACKAGE &&	\
     (pkg)->Package.Count >= (size))
#define ACPI_PKG_VALID_EQ(pkg, size)    \
    (ACPI_PKG_VALID((pkg), (size)) && (pkg)->Package.Count == (size))
int		acpi_PkgInt(ACPI_OBJECT *res, int idx, UINT64 *dst);
int		acpi_PkgInt32(ACPI_OBJECT *res, int idx, uint32_t *dst);
int		acpi_PkgStr(ACPI_OBJECT *res, int idx, void *dst, size_t size);
int		acpi_PkgGas(device_t dev, ACPI_OBJECT *res, int idx, int *type,
		    int *rid, struct resource **dst, u_int flags);
int		acpi_PkgRawGas(ACPI_OBJECT *res, int idx,
			       ACPI_GENERIC_ADDRESS *gas);
ACPI_HANDLE	acpi_GetReference(ACPI_HANDLE scope, ACPI_OBJECT *obj);
/* ACPI task kernel thread initialization. */
int		acpi_task_thread_init(void);
void		acpi_task_thread_schedule(void);
extern BOOLEAN acpi_MatchHid(ACPI_HANDLE h, const char *hid);
/*
 * Base level for BUS_ADD_CHILD.  Special devices are added at orders less
 * than this, and normal devices at or above this level.  This keeps the
 * probe order sorted so that things like sysresource are available before
 * their children need them.
 */
#define	ACPI_DEV_BASE_ORDER	10

/* Default number of task queue threads to start. */
#ifndef ACPI_MAX_THREADS
#define ACPI_MAX_THREADS	3
#endif

SYSCTL_DECL(_debug_acpi);

#endif /* _KERNEL */
#endif /* !_ACPIVAR_H_ */
