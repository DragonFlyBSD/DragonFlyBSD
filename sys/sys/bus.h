/*-
 * Copyright (c) 1997,1998 Doug Rabson
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
 * $FreeBSD: src/sys/sys/bus.h,v 1.30.2.5 2004/03/17 17:54:25 njl Exp $
 */

#ifndef _SYS_BUS_H_
#define _SYS_BUS_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_ 
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_KOBJ_H_
#include <sys/kobj.h>
#endif
#ifdef _KERNEL
#include <sys/serialize.h>
#endif
#ifndef _SYS_BUS_DMA_H_
#include <sys/bus_dma.h>
#endif
#ifndef _SYS_BUS_RESOURCE_H_
#include <sys/bus_resource.h>
#endif

/*
 * Forward declarations
 */
typedef struct bsd_device	*device_t;
typedef struct kobj_class	driver_t;
typedef struct devclass		*devclass_t;
#define	device_method_t		kobj_method_t

typedef int driver_filter_t(void*);
typedef void driver_intr_t(void*);

/*
 * Interface information structure.
 */
struct u_businfo {
	int	ub_version;		/* interface version */
#define BUS_USER_VERSION	1
	int	ub_generation;		/* generation count */
};

/*
 * State of the device.
 */
typedef enum device_state {
	DS_NOTPRESENT,			/* not probed or probe failed */
	DS_ALIVE,			/* probe succeeded */
	DS_INPROGRESS,			/* attach in progress */
	DS_ATTACHED,			/* attach method called */
	DS_BUSY				/* device is open */
} device_state_t;

/*
 * Device information exported to userspace.
 */
struct u_device {
	uintptr_t	dv_handle;
	uintptr_t	dv_parent;

	char		dv_name[32];		/* Name of device in tree. */
	char		dv_desc[32];		/* Driver description */
	char		dv_drivername[32];	/* Driver name */
	char		dv_pnpinfo[128];	/* Plug and play info */
	char		dv_location[128];	/* Where is the device? */
	uint32_t	dv_devflags;		/* API Flags for device */
	uint16_t	dv_flags;		/* flags for dev date */
	device_state_t	dv_state;		/* State of attachment */
	/* XXX more driver info? */
};

/*
 * Interrupt features mask.  Note that DragonFly no longer implements
 * INTR_TYPE_* flags.
 *
 * NOTE: INTR_FAST is no longer supported, all device interrupts are threaded
 *	 now.  Only clock interrupts are 'fast'.
 */
#define	INTR_HIFREQ	0x0040	/* high frequency interrupt */
#define	INTR_CLOCK	0x0080	/* (was INTR_FAST) */
#define	INTR_EXCL	0x0100
#define	INTR_MPSAFE	0x0200
#define	INTR_NOENTROPY	0x0400
#define INTR_NOPOLL	0x0800	/* interrupt cannot be polled (e.g. ata) */

enum intr_trigger {
    INTR_TRIGGER_CONFORM = 0,
    INTR_TRIGGER_EDGE = 1,
    INTR_TRIGGER_LEVEL = 2
};

enum intr_polarity {
    INTR_POLARITY_CONFORM = 0,
    INTR_POLARITY_HIGH = 1,
    INTR_POLARITY_LOW = 2
};

typedef int (*devop_t)(void);

/*
 * Definitions for drivers which need to keep simple lists of resources
 * for their child devices.
 */
struct	resource;

struct resource_list_entry {
    SLIST_ENTRY(resource_list_entry) link;
    int			type;		/* type argument to alloc_resource */
    int			rid;		/* resource identifier */
    struct resource	*res;		/* the real resource when allocated */
    u_long		start;		/* start of resource range */
    u_long		end;		/* end of resource range */
    u_long		count;		/* count within range */
    int			cpuid;		/* owner cpuid */
};
SLIST_HEAD(resource_list, resource_list_entry);

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#ifdef _KERNEL

const char	*intr_str_polarity(enum intr_polarity);
const char	*intr_str_trigger(enum intr_trigger);

/**
 * devctl hooks.  Typically one should use the devctl_notify
 * hook to send the message.  However, devctl_queue_data is also
 * included in case devctl_notify isn't sufficiently general.
 */
boolean_t devctl_process_running(void);
void devctl_notify(const char *__system, const char *__subsystem,
    const char *__type, const char *__data);
void devctl_queue_data(char *__data);

/*
 * Initialise a resource list.
 */
void	resource_list_init(struct resource_list *rl);

/*
 * Reclaim memory used by a resource list.
 */
void	resource_list_free(struct resource_list *rl);

/*
 * Add a resource entry or modify an existing entry if one exists with 
 * the same type and rid.
 */
void	resource_list_add(struct resource_list *rl, int type, int rid,
	    u_long start, u_long end, u_long count, int cpuid);

/*
 * Find a resource entry by type and rid.
 */
struct resource_list_entry*
	resource_list_find(struct resource_list *rl,
			   int type, int rid);

/*
 * Delete a resource entry.
 */
void	resource_list_delete(struct resource_list *rl,
			     int type, int rid);

/*
 * Implement BUS_ALLOC_RESOURCE by looking up a resource from the list 
 * and passing the allocation up to the parent of bus. This assumes
 * that the first entry of device_get_ivars(child) is a struct
 * resource_list. This also handles 'passthrough' allocations where a
 * child is a remote descendant of bus by passing the allocation up to 
 * the parent of bus.
 */
struct resource *
	resource_list_alloc(struct resource_list *rl,
			    device_t bus, device_t child,
			    int type, int *rid,
			    u_long start, u_long end,
			    u_long count, u_int flags, int cpuid);

/*
 * Implement BUS_RELEASE_RESOURCE.
 */
int	resource_list_release(struct resource_list *rl,
			      device_t bus, device_t child,
			      int type, int rid, struct resource *res);

/*
 * Print all resources of a specified type, for use in bus_print_child.
 * The name is printed if at least one resource of the given type is available.
 * The format ist used to print resource start and end.
 */
int	resource_list_print_type(struct resource_list *rl,
				 const char *name, int type,
				 const char *format);

/*
 * The root bus, to which all top-level busses are attached.
 */
extern device_t root_bus;
void	root_bus_configure(void);

/*
 * Useful functions for implementing busses.
 */

int	bus_generic_activate_resource(device_t dev, device_t child, int type,
				      int rid, struct resource *r);
struct resource *
	bus_generic_alloc_resource(device_t bus, device_t child,
				    int type, int *rid,
				    u_long start, u_long end,
				    u_long count, u_int flags, int cpuid);
struct resource_list *
	bus_generic_get_resource_list (device_t, device_t);

int     bus_generic_config_intr(device_t, device_t, int, enum intr_trigger,
					enum intr_polarity);
int	bus_generic_attach(device_t dev);
int	bus_generic_attach_gpri(device_t dev, u_int gpri);
int	bus_generic_child_present(device_t dev, device_t child);
int	bus_generic_deactivate_resource(device_t dev, device_t child, int type,
					int rid, struct resource *r);
int	bus_generic_detach(device_t dev);
int	bus_generic_disable_intr(device_t dev, device_t child, void *cookie);
void	bus_generic_driver_added(device_t dev, driver_t *driver);
bus_dma_tag_t
	bus_generic_get_dma_tag(device_t dev, device_t child);
void	bus_generic_enable_intr(device_t dev, device_t child, void *cookie);
int	bus_print_child_header(device_t dev, device_t child);
int	bus_print_child_footer(device_t dev, device_t child);
int	bus_generic_print_child(device_t dev, device_t child);
int	bus_generic_identify(driver_t *driver, device_t parent);
int	bus_generic_identify_sameunit(driver_t *driver, device_t parent);
int	bus_generic_probe(device_t dev);
int	bus_generic_probe_hack(device_t dev);
device_t bus_generic_add_child(device_t, device_t, int, const char *, int);
int	bus_generic_read_ivar(device_t dev, device_t child, int which,
			      uintptr_t *result);
int	bus_generic_release_resource(device_t bus, device_t child,
				     int type, int rid, struct resource *r);
int	bus_generic_get_resource(device_t dev, device_t child, int type, 
				     int rid, u_long *startp, u_long *countp);
int	bus_generic_set_resource(device_t dev, device_t child,
	    int type, int rid, u_long start, u_long count, int cpuid);
void	bus_generic_delete_resource(device_t dev, device_t child, 
				     int type, int rid);
int	bus_generic_resume(device_t dev);
int	bus_generic_setup_intr(device_t dev, device_t child,
	    struct resource *irq, int flags, driver_intr_t *intr, void *arg,
	    void **cookiep, lwkt_serialize_t serializer, const char *desc);
int	bus_generic_shutdown(device_t dev);
int	bus_generic_suspend(device_t dev);
int	bus_generic_teardown_intr(device_t dev, device_t child,
				  struct resource *irq, void *cookie);
int	bus_generic_write_ivar(device_t dev, device_t child, int which,
			       uintptr_t value);

struct resource *
	bus_generic_rl_alloc_resource (device_t, device_t, int, int *,
				    u_long, u_long, u_long, u_int, int);
void	bus_generic_rl_delete_resource (device_t, device_t, int, int);
int	bus_generic_rl_get_resource (device_t, device_t, int, int, u_long *,
				    u_long *);
int	bus_generic_rl_set_resource (device_t, device_t, int, int, u_long,
				    u_long, int);
int	bus_generic_rl_release_resource (device_t, device_t, int, int,
				    struct resource *);

/*
 * Wrapper functions for the BUS_*_RESOURCE methods to make client code
 * a little simpler.
 */
struct resource_spec {
	int     type;
	int     rid;
	int     flags;
};

int bus_alloc_resources(device_t dev, struct resource_spec *rs,
			struct resource **res);
void bus_release_resources(device_t dev, const struct resource_spec *rs,
			   struct resource **res);

struct	resource *bus_alloc_resource(device_t dev, int type, int *rid,
				     u_long start, u_long end, u_long count,
				     u_int flags);
struct	resource *bus_alloc_legacy_irq_resource(device_t dev, int *rid,
				     u_long irq, u_int flags);
int	bus_activate_resource(device_t dev, int type, int rid, 
			      struct resource *r);
int	bus_deactivate_resource(device_t dev, int type, int rid,
				struct resource *r);
bus_dma_tag_t bus_get_dma_tag(device_t dev);
int	bus_disable_intr(device_t dev, void *cookie);
void	bus_enable_intr(device_t dev, void *cookie);
int	bus_release_resource(device_t dev, int type, int rid, 
			     struct resource *r);
int	bus_setup_intr(device_t dev, struct resource *r, int flags,
		       driver_intr_t handler, void *arg,
		       void **cookiep, lwkt_serialize_t serializer);
int	bus_setup_intr_descr(device_t dev, struct resource *r, int flags,
	    driver_intr_t handler, void *arg, void **cookiep,
	    lwkt_serialize_t serializer, const char *desc);
int	bus_teardown_intr(device_t dev, struct resource *r, void *cookie);
int	bus_set_resource(device_t dev, int type, int rid,
			 u_long start, u_long count, int cpuid);
int	bus_get_resource(device_t dev, int type, int rid,
			 u_long *startp, u_long *countp);
u_long	bus_get_resource_start(device_t dev, int type, int rid);
u_long	bus_get_resource_count(device_t dev, int type, int rid);
void	bus_delete_resource(device_t dev, int type, int rid);
int	bus_child_present(device_t child);
int	bus_child_pnpinfo_str(device_t child, char *buf, size_t buflen);
int	bus_child_location_str(device_t child, char *buf, size_t buflen);

static __inline struct resource *
bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags)
{
	return (bus_alloc_resource(dev, type, rid, 0ul, ~0ul, 1, flags));
}

/*
 * Access functions for device.
 */
device_t	device_add_child(device_t dev, const char *name, int unit);
device_t	device_add_child_ordered(device_t dev, int order,
					 const char *name, int unit);
void	device_busy(device_t dev);
int	device_delete_child(device_t dev, device_t child);
int	device_delete_children(device_t dev);
int	device_detach(device_t dev);
void	device_disable(device_t dev);
void	device_enable(device_t dev);
device_t	device_find_child(device_t dev, const char *classname,
				  int unit);
const char 	*device_get_desc(device_t dev);
devclass_t	device_get_devclass(device_t dev);
driver_t	*device_get_driver(device_t dev);
u_int32_t	device_get_flags(device_t dev);
device_t	device_get_parent(device_t dev);
int	device_get_children(device_t dev, device_t **listp, int *countp);
void	*device_get_ivars(device_t dev);
void	device_set_ivars(device_t dev, void *ivars);
const	char *device_get_name(device_t dev);
const	char *device_get_nameunit(device_t dev);
void	*device_get_softc(device_t dev);
device_state_t	device_get_state(device_t dev);
int	device_get_unit(device_t dev);
struct sysctl_ctx_list *device_get_sysctl_ctx(device_t dev);
struct sysctl_oid *device_get_sysctl_tree(device_t dev);
int	device_is_alive(device_t dev); /* did probe succeed? */
int	device_is_attached(device_t dev);	/* did attach succeed? */
int	device_is_enabled(device_t dev);
int	device_is_quiet(device_t dev);
int	device_print_prettyname(device_t dev);
int	device_printf(device_t dev, const char *, ...) __printflike(2, 3);
int	device_log(device_t dev, int pri, const char *, ...) __printflike(3, 4);
int	device_probe_and_attach(device_t dev);
int	device_probe_and_attach_gpri(device_t dev, u_int gpri);
int	device_probe_child(device_t dev, device_t child);
int	device_probe_child_gpri(device_t dev, device_t child, u_int gpri);
void	device_quiet(device_t dev);
void	device_set_desc(device_t dev, const char* desc);
void	device_set_desc_copy(device_t dev, const char* desc);
int	device_set_devclass(device_t dev, const char *classname);
int	device_set_driver(device_t dev, driver_t *driver);
void	device_set_flags(device_t dev, u_int32_t flags);
void	device_set_softc(device_t dev, void *softc);
void	device_set_async_attach(device_t dev, int enable);
int	device_set_unit(device_t dev, int unit);	/* XXX DONT USE XXX */
int	device_shutdown(device_t dev);
void	device_unbusy(device_t dev);
void	device_verbose(device_t dev);
int	device_getenv_int(device_t dev, const char *knob, int def);
void	device_getenv_string(device_t dev, const char *knob,
	    char * __restrict data, int dlen, const char * __restrict def);

/*
 * Access functions for devclass.
 */
int	devclass_add_driver(devclass_t dc, kobj_class_t driver);
int	devclass_delete_driver(devclass_t dc, kobj_class_t driver);
devclass_t	devclass_create(const char *classname);
devclass_t	devclass_find(const char *classname);
device_t	devclass_find_unit(const char *classname, int unit);
kobj_class_t	devclass_find_driver(devclass_t dc, const char *classname);
const char 	*devclass_get_name(devclass_t dc);
device_t	devclass_get_device(devclass_t dc, int unit);
void	*devclass_get_softc(devclass_t dc, int unit);
int	devclass_get_devices(devclass_t dc, device_t **listp, int *countp);
int	devclass_get_drivers(devclass_t dc, driver_t ***listp, int *countp);
int	devclass_get_count(devclass_t dc);
int	devclass_get_maxunit(devclass_t dc);
void	devclass_set_parent(devclass_t dc, devclass_t pdc);
devclass_t	devclass_get_parent(devclass_t dc);

/*
 * Access functions for device resources.
 */

int	resource_int_value(const char *name, int unit, const char *resname,
			   int *result);
int	resource_long_value(const char *name, int unit, const char *resname,
			    long *result);
int	resource_string_value(const char *name, int unit, const char *resname,
			      const char **result);
int     resource_disabled(const char *name, int unit);
int	resource_query_string(int i, const char *resname, const char *value);
char	*resource_query_name(int i);
int	resource_query_unit(int i);
int	resource_locate(int i, const char *resname);
int	resource_set_int(const char *name, int unit, const char *resname,
			 int value);
int	resource_set_long(const char *name, int unit, const char *resname,
			  long value);
int	resource_set_string(const char *name, int unit, const char *resname,
			    const char *value);
int	resource_count(void);

/*
 * Functions for maintaining and checking consistency of
 * bus information exported to userspace.
 */
int	bus_data_generation_check(int generation);
void	bus_data_generation_update(void);

/**
 * Some convenience defines for probe routines to return.  These are just
 * suggested values, and there's nothing magical about them.
 * BUS_PROBE_SPECIFIC is for devices that cannot be reprobed, and that no
 * possible other driver may exist (typically legacy drivers who don't fallow
 * all the rules, or special needs drivers).  BUS_PROBE_VENDOR is the
 * suggested value that vendor supplied drivers use.  This is for source or
 * binary drivers that are not yet integrated into the FreeBSD tree.  Its use
 * in the base OS is prohibited.  BUS_PROBE_DEFAULT is the normal return value
 * for drivers to use.  It is intended that nearly all of the drivers in the
 * tree should return this value.  BUS_PROBE_LOW_PRIORITY are for drivers that
 * have special requirements like when there are two drivers that support
 * overlapping series of hardware devices.  In this case the one that supports
 * the older part of the line would return this value, while the one that
 * supports the newer ones would return BUS_PROBE_DEFAULT.  BUS_PROBE_GENERIC
 * is for drivers that wish to have a generic form and a specialized form,
 * like is done with the pci bus and the acpi pci bus.  BUS_PROBE_HOOVER is
 * for those busses that implement a generic device place-holder for devices on
 * the bus that have no more specific driver for them (aka ugen).
 */
#define BUS_PROBE_SPECIFIC      0       /* Only I can use this device */
#if 0 /* notyet */
#define BUS_PROBE_VENDOR        (-10)   /* Vendor supplied driver */
#define BUS_PROBE_DEFAULT       (-20)   /* Base OS default driver */
#define BUS_PROBE_LOW_PRIORITY  (-40)   /* Older, less desirable drivers */
#define BUS_PROBE_GENERIC       (-100)  /* generic driver for dev */
#define BUS_PROBE_HOOVER        (-500)  /* Generic dev for all devs on bus */
#else
#define BUS_PROBE_VENDOR        0
#define BUS_PROBE_DEFAULT       0
#define BUS_PROBE_LOW_PRIORITY  0
#define BUS_PROBE_GENERIC       0
#define BUS_PROBE_HOOVER        0
#endif

/*
 * Shorthand for constructing method tables.
 */
#define DEVMETHOD	KOBJMETHOD
#define DEVMETHOD_END	KOBJMETHOD_END

/*
 * Some common device interfaces.
 */
#include "device_if.h"
#include "bus_if.h"

struct	module;

int	driver_module_handler(struct module *, int, void *);

/*
 * Module support for automatically adding drivers to busses.
 */
struct driver_module_data {
	int		(*dmd_chainevh)(struct module *, int, void *);
	void		*dmd_chainarg;
	const char	*dmd_busname;
	kobj_class_t	dmd_driver;
	devclass_t	*dmd_devclass;
};

#define DRIVER_MODULE_ORDERED(name, busname, driver, devclassp, evh, arg,\
                              order)					\
									\
static struct driver_module_data name##_##busname##_driver_mod = {	\
	evh, arg,							\
	#busname,							\
	(kobj_class_t) &driver,						\
	devclassp							\
};									\
									\
static moduledata_t name##_##busname##_mod = {				\
	#busname "/" #name,						\
	driver_module_handler,						\
	&name##_##busname##_driver_mod					\
};									\
DECLARE_MODULE(name##_##busname, name##_##busname##_mod,		\
	       SI_SUB_DRIVERS, order)

#define DRIVER_MODULE(name, busname, driver, devclass, evh, arg)	\
	DRIVER_MODULE_ORDERED(name, busname, driver, &devclass, evh, arg,\
	                      SI_ORDER_MIDDLE)
/**
 * Generic ivar accessor generation macros for bus drivers
 */
#define __BUS_ACCESSOR(varp, var, ivarp, ivar, type)			\
									\
static __inline type varp ## _get_ ## var(device_t dev)			\
{									\
	uintptr_t v;							\
	BUS_READ_IVAR(device_get_parent(dev), dev,			\
	    ivarp ## _IVAR_ ## ivar, &v);				\
	return ((type) v);						\
}									\
									\
static __inline void varp ## _set_ ## var(device_t dev, type t)		\
{									\
	uintptr_t v = (uintptr_t) t;					\
	BUS_WRITE_IVAR(device_get_parent(dev), dev,			\
	    ivarp ## _IVAR_ ## ivar, v);				\
}

/**
 * Shorthand macros, taking resource argument
 */

#define bus_barrier(r, o, l, f) \
	bus_space_barrier((r)->r_bustag, (r)->r_bushandle, (o), (l), (f))
#define bus_read_1(r, o) \
	bus_space_read_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_1(r, o, d, c) \
	bus_space_read_multi_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_1(r, o, d, c) \
	bus_space_read_region_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_1(r, o, v, c) \
	bus_space_set_multi_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_1(r, o, v, c) \
	bus_space_set_region_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_1(r, o, v) \
	bus_space_write_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_1(r, o, d, c) \
	bus_space_write_multi_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_1(r, o, d, c) \
	bus_space_write_region_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_1(r, o) \
	bus_space_read_stream_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_1(r, o, d, c) \
	bus_space_read_multi_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_1(r, o, d, c) \
	bus_space_read_region_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_1(r, o, v, c) \
	bus_space_set_multi_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_1(r, o, v, c) \
	bus_space_set_region_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_1(r, o, v) \
	bus_space_write_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_1(r, o, d, c) \
	bus_space_write_multi_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_1(r, o, d, c) \
	bus_space_write_region_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_2(r, o) \
	bus_space_read_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_2(r, o, d, c) \
	bus_space_read_multi_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_2(r, o, d, c) \
	bus_space_read_region_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_2(r, o, v, c) \
	bus_space_set_multi_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_2(r, o, v, c) \
	bus_space_set_region_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_2(r, o, v) \
	bus_space_write_2((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_2(r, o, d, c) \
	bus_space_write_multi_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_2(r, o, d, c) \
	bus_space_write_region_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_2(r, o) \
	bus_space_read_stream_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_2(r, o, d, c) \
	bus_space_read_multi_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_2(r, o, d, c) \
	bus_space_read_region_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_2(r, o, v, c) \
	bus_space_set_multi_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_2(r, o, v, c) \
	bus_space_set_region_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_2(r, o, v) \
	bus_space_write_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_2(r, o, d, c) \
	bus_space_write_multi_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_2(r, o, d, c) \
	bus_space_write_region_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_4(r, o) \
	bus_space_read_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_4(r, o, d, c) \
	bus_space_read_multi_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_4(r, o, d, c) \
	bus_space_read_region_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_4(r, o, v, c) \
	bus_space_set_multi_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_4(r, o, v, c) \
	bus_space_set_region_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_4(r, o, v) \
	bus_space_write_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_4(r, o, d, c) \
	bus_space_write_multi_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_4(r, o, d, c) \
	bus_space_write_region_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_4(r, o) \
	bus_space_read_stream_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_4(r, o, d, c) \
	bus_space_read_multi_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_4(r, o, d, c) \
	bus_space_read_region_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_4(r, o, v, c) \
	bus_space_set_multi_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_4(r, o, v, c) \
	bus_space_set_region_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_4(r, o, v) \
	bus_space_write_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_4(r, o, d, c) \
	bus_space_write_multi_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_4(r, o, d, c) \
	bus_space_write_region_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_8(r, o) \
	bus_space_read_8((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_8(r, o, d, c) \
	bus_space_read_multi_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_8(r, o, d, c) \
	bus_space_read_region_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_8(r, o, v, c) \
	bus_space_set_multi_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_8(r, o, v, c) \
	bus_space_set_region_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_8(r, o, v) \
	bus_space_write_8((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_8(r, o, d, c) \
	bus_space_write_multi_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_8(r, o, d, c) \
	bus_space_write_region_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_8(r, o) \
	bus_space_read_stream_8((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_8(r, o, d, c) \
	bus_space_read_multi_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_8(r, o, d, c) \
	bus_space_read_region_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_8(r, o, v, c) \
	bus_space_set_multi_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_8(r, o, v, c) \
	bus_space_set_region_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_8(r, o, v) \
	bus_space_write_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_8(r, o, d, c) \
	bus_space_write_multi_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_8(r, o, d, c) \
	bus_space_write_region_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#endif /* _KERNEL */

#endif /* !_SYS_BUS_H_ */
