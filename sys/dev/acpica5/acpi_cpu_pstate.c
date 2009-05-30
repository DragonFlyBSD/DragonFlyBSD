/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include "acpi.h"
#include "acpivar.h"
#include "acpi_cpu.h"

#define ACPI_NPSTATE_MAX	16

#define ACPI_PSS_PX_NENTRY	6

#define ACPI_PSD_COORD_SWALL	0xfc
#define ACPI_PSD_COORD_SWANY	0xfd
#define ACPI_PSD_COORD_HWALL	0xfe
#define ACPI_PSD_COORD_VALID(coord) \
	((coord) == ACPI_PSD_COORD_SWALL || \
	 (coord) == ACPI_PSD_COORD_SWANY || \
	 (coord) == ACPI_PSD_COORD_HWALL)

struct acpi_pst_softc;
LIST_HEAD(acpi_pst_list, acpi_pst_softc);

struct acpi_pst_domain {
	uint32_t		pd_dom;
	uint32_t		pd_coord;
	uint32_t		pd_nproc;
	LIST_ENTRY(acpi_pst_domain) pd_link;

	uint32_t		pd_flags;

	int			pd_state;
	int			pd_sstart;
	struct acpi_pst_list	pd_pstlist;

	struct sysctl_ctx_list	pd_sysctl_ctx;
	struct sysctl_oid	*pd_sysctl_tree;
};
LIST_HEAD(acpi_pst_domlist, acpi_pst_domain);

#define ACPI_PSTDOM_FLAG_STUB	0x1	/* stub domain, no _PSD */

struct acpi_pst_softc {
	struct acpi_cpux_softc	*pst_parent;
	struct acpi_pst_domain	*pst_domain;
	ACPI_RESOURCE_GENERIC_REGISTER pst_creg;
	ACPI_RESOURCE_GENERIC_REGISTER pst_sreg;

	int			pst_state;
	int			pst_sstart;
	int			pst_cpuid;

	ACPI_HANDLE		pst_handle;

	LIST_ENTRY(acpi_pst_softc) pst_link;
};

struct acpi_pstate {
	uint32_t		st_freq;
	uint32_t		st_power;
	uint32_t		st_xsit_lat;
	uint32_t		st_bm_lat;
	uint32_t		st_cval;
	uint32_t		st_sval;
};

static int	acpi_pst_probe(device_t dev);
static int	acpi_pst_attach(device_t dev);

static void	acpi_pst_postattach(void *);
static struct acpi_pst_domain *
		acpi_pst_domain_create(device_t, ACPI_OBJECT *);
static struct acpi_pst_domain *
		acpi_pst_domain_find(uint32_t);
static struct acpi_pst_domain *
		acpi_pst_domain_alloc(uint32_t, uint32_t, uint32_t);

static int	acpi_pst_sysctl_freqs(SYSCTL_HANDLER_ARGS);
static int	acpi_pst_sysctl_members(SYSCTL_HANDLER_ARGS);

static struct acpi_pst_domlist	acpi_pst_domains =
	LIST_HEAD_INITIALIZER(acpi_pst_domains);

static int			acpi_npstates;
static struct acpi_pstate	*acpi_pstates;

static device_method_t acpi_pst_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			acpi_pst_probe),
	DEVMETHOD(device_attach,		acpi_pst_attach),
	DEVMETHOD(device_detach,		bus_generic_detach),
	DEVMETHOD(device_shutdown,		bus_generic_shutdown),
	DEVMETHOD(device_suspend,		bus_generic_suspend),
	DEVMETHOD(device_resume,		bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_add_child,		bus_generic_add_child),
	DEVMETHOD(bus_print_child,		bus_generic_print_child),
	DEVMETHOD(bus_read_ivar,		bus_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,		bus_generic_write_ivar),
	DEVMETHOD(bus_get_resource_list,	bus_generic_get_resource_list),
	DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
	DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
	DEVMETHOD(bus_alloc_resource,		bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_release_resource),
	DEVMETHOD(bus_driver_added,		bus_generic_driver_added),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),

	{ 0, 0 }
};

static driver_t acpi_pst_driver = {
	"cpu_pst",
	acpi_pst_methods,
	sizeof(struct acpi_pst_softc)
};

static devclass_t acpi_pst_devclass;
DRIVER_MODULE(cpu_pst, cpu, acpi_pst_driver, acpi_pst_devclass, 0, 0);
MODULE_DEPEND(cpu_pst, acpi, 1, 1, 1);

static int
acpi_pst_probe(device_t dev)
{
	ACPI_BUFFER buf;
	ACPI_HANDLE handle;
	ACPI_STATUS status;
	ACPI_OBJECT *obj;

	if (acpi_disabled("cpu_pst") ||
	    acpi_get_type(dev) != ACPI_TYPE_PROCESSOR)
		return ENXIO;

	handle = acpi_get_handle(dev);

	/*
	 * Check _PCT package
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(handle, "_PCT", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "Can't get _PCT package - %s\n",
			      AcpiFormatException(status));
		return ENXIO;
	}

	obj = (ACPI_OBJECT *)buf.Pointer;
	if (!ACPI_PKG_VALID_EQ(obj, 2)) {
		device_printf(dev, "Invalid _PCT package\n");
		AcpiOsFree(obj);
		return ENXIO;
	}
	AcpiOsFree(obj);

	/*
	 * Check _PSS package
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(handle, "_PSS", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "Can't get _PSS package - %s\n",
			      AcpiFormatException(status));
		return ENXIO;
	}

	obj = (ACPI_OBJECT *)buf.Pointer;
	if (!ACPI_PKG_VALID(obj, 1)) {
		device_printf(dev, "Invalid _PSS package\n");
		AcpiOsFree(obj);
		return ENXIO;
	}
	AcpiOsFree(obj);

	device_set_desc(dev, "ACPI CPU P-State");
	return 0;
}

static int
acpi_pst_attach(device_t dev)
{
	struct acpi_pst_softc *sc = device_get_softc(dev), *pst;
	struct acpi_pst_domain *dom = NULL;
	ACPI_BUFFER buf;
	ACPI_STATUS status;
	ACPI_OBJECT *obj, *reg;
	struct acpi_pstate *pstate, *p;
	int i, npstate;

	sc->pst_parent = device_get_softc(device_get_parent(dev));
	sc->pst_handle = acpi_get_handle(dev);
	sc->pst_cpuid = acpi_get_magic(dev);

	/*
	 * If there is a _PSD, then we create procossor domain
	 * accordingly.  If there is no _PSD, we just fake a
	 * default processor domain0.
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PSD", NULL, &buf);
	if (!ACPI_FAILURE(status)) {
		obj = (ACPI_OBJECT *)buf.Pointer;
		if (ACPI_PKG_VALID_EQ(obj, 1)) {
			dom = acpi_pst_domain_create(dev,
				&obj->Package.Elements[0]);
			if (dom == NULL) {
				AcpiOsFree(obj);
				return ENXIO;
			}
		} else {
			device_printf(dev, "Invalid _PSD package\n");
			AcpiOsFree(obj);
			return ENXIO;
		}

		/* Free _PSD */
		AcpiOsFree(buf.Pointer);
	} else {
		/* Create a stub one processor domain */
		dom = acpi_pst_domain_alloc(0, ACPI_PSD_COORD_SWANY, 1);
		dom->pd_flags |= ACPI_PSTDOM_FLAG_STUB;
	}

	/* Make sure that adding us will not overflow our domain */
	i = 0;
	LIST_FOREACH(pst, &dom->pd_pstlist, pst_link)
		++i;
	if (i == dom->pd_nproc) {
		device_printf(dev, "Domain%u already contains %d P-States, "
			      "invalid _PSD package\n",
			      dom->pd_dom, dom->pd_nproc);
		return ENXIO;
	}

	/*
	 * Get control/status registers from _PCT
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PCT", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "Can't get _PCT package - %s\n",
			      AcpiFormatException(status));
		return ENXIO;
	}

	obj = (ACPI_OBJECT *)buf.Pointer;
	if (!ACPI_PKG_VALID_EQ(obj, 2)) {
		device_printf(dev, "Invalid _PCT package\n");
		AcpiOsFree(obj);
		return ENXIO;
	}

	/* Save control register */
	reg = &obj->Package.Elements[0];
	if (reg->Type != ACPI_TYPE_BUFFER || reg->Buffer.Pointer == NULL ||
	    reg->Buffer.Length < sizeof(sc->pst_creg) + 3)
		return ENXIO;
	memcpy(&sc->pst_creg, reg->Buffer.Pointer + 3, sizeof(sc->pst_creg));
	if (bootverbose) {
		device_printf(dev, "control reg %d %llx\n",
			      sc->pst_creg.SpaceId, sc->pst_creg.Address);
	}

	/* Save status register */
	reg = &obj->Package.Elements[1];
	if (reg->Type != ACPI_TYPE_BUFFER || reg->Buffer.Pointer == NULL ||
	    reg->Buffer.Length < sizeof(sc->pst_sreg) + 3)
		return ENXIO;
	memcpy(&sc->pst_sreg, reg->Buffer.Pointer + 3, sizeof(sc->pst_sreg));
	if (bootverbose) {
		device_printf(dev, "status reg %d %llx\n",
			      sc->pst_sreg.SpaceId, sc->pst_sreg.Address);
	}

	/* Free _PCT */
	AcpiOsFree(obj);

	/*
	 * Create P-State table according to _PSS
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PSS", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "Can't get _PSS package - %s\n",
			      AcpiFormatException(status));
		return ENXIO;
	}

	obj = (ACPI_OBJECT *)buf.Pointer;
	if (!ACPI_PKG_VALID(obj, 1)) {
		device_printf(dev, "Invalid _PSS package\n");
		AcpiOsFree(obj);
		return ENXIO;
	}

	/* Don't create too many P-States */
	npstate = obj->Package.Count;
	if (npstate > ACPI_NPSTATE_MAX) {
		device_printf(dev, "Too many P-States, %d->%d\n",
			      npstate, ACPI_NPSTATE_MAX);
		npstate = ACPI_NPSTATE_MAX;
	}

	/*
	 * If we have already created P-State table,
	 * we must make sure that number of entries
	 * is consistent.
	 */
	if (acpi_pstates != NULL && acpi_npstates != npstate) {
		device_printf(dev, "Inconsistent # of P-States "
			      "cross Processor objects\n");
		AcpiOsFree(obj);
		return ENXIO;
	}

	/*
	 * Create a temporary P-State table
	 */
	pstate = kmalloc(sizeof(*pstate) * npstate, M_TEMP, M_WAITOK);
	for (i = 0, p = pstate; i < npstate; ++i, ++p) {
		ACPI_OBJECT *pkg;
		uint32_t *ptr[ACPI_PSS_PX_NENTRY] = {
			&p->st_freq, &p->st_power, &p->st_xsit_lat,
			&p->st_bm_lat, &p->st_cval, &p->st_sval
		};
		int j;

		pkg = &obj->Package.Elements[i];
		if (!ACPI_PKG_VALID(pkg, ACPI_PSS_PX_NENTRY)) {
			device_printf(dev, "Invalud _PSS P%d\n", i);
			AcpiOsFree(obj);
			kfree(pstate, M_TEMP);
			return ENXIO;
		}
		for (j = 0; j < ACPI_PSS_PX_NENTRY; ++j) {
			if (acpi_PkgInt32(pkg, j, ptr[j]) != 0) {
				device_printf(dev, "Can't extract "
					      "_PSS P%d %dth entry\n", i, j);
				AcpiOsFree(obj);
				kfree(pstate, M_TEMP);
				return ENXIO;
			}
		}
	}

	/* Free _PSS */
	AcpiOsFree(obj);

	if (acpi_pstates == NULL) {
		/*
		 * If no P-State table is created yet,
		 * save the temporary one we just created.
		 */
		acpi_pstates = pstate;
		acpi_npstates = npstate;
		pstate = NULL;

		if (bootverbose) {
			for (i = 0; i < acpi_npstates; ++i) {
				device_printf(dev,
				"freq %u, pwr %u, xlat %u, blat %u, "
				"cv %08x, sv %08x\n",
				acpi_pstates[i].st_freq,
				acpi_pstates[i].st_power,
				acpi_pstates[i].st_xsit_lat,
				acpi_pstates[i].st_bm_lat,
				acpi_pstates[i].st_cval,
				acpi_pstates[i].st_sval);
			}
		}
	} else {
		/*
		 * Make sure that P-State tables are same
		 * for all processors.
		 */
		if (memcmp(pstate, acpi_pstates,
			   sizeof(*pstate) * npstate) != 0) {
			device_printf(dev, "Inconsistent _PSS "
				      "cross Processor objects\n");
			kfree(pstate, M_TEMP);
			return ENXIO;
		}
		kfree(pstate, M_TEMP);
	}

	/* By default, we start from P-State table's first entry */
	sc->pst_sstart = 0;

	/*
	 * Adjust the usable first entry of P-State table,
	 * if there is _PPC object.
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PPC", NULL, &buf);
	if (!ACPI_FAILURE(status)) {
		obj = (ACPI_OBJECT *)buf.Pointer;
		if (obj->Type == ACPI_TYPE_INTEGER) {
			if (obj->Integer.Value >= acpi_npstates) {
				device_printf(dev, "Invalid _PPC value\n");
				AcpiOsFree(obj);
				return ENXIO;
			}
			sc->pst_sstart = obj->Integer.Value;
			if (bootverbose)
				device_printf(dev, "_PPC %d\n", sc->pst_sstart);

			/* TODO: Install notifiy handler */
		} else {
			device_printf(dev, "Invalid _PPC object\n");
			AcpiOsFree(obj);
			return ENXIO;
		}

		/* Free _PPC */
		AcpiOsFree(obj);
	}

	sc->pst_state = sc->pst_sstart;

	/* Link us with the domain */
	sc->pst_domain = dom;
	LIST_INSERT_HEAD(&dom->pd_pstlist, sc, pst_link);

	if (device_get_unit(dev) == 0)
		AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_pst_postattach, NULL);

	return 0;
}

static struct acpi_pst_domain *
acpi_pst_domain_create(device_t dev, ACPI_OBJECT *obj)
{
	struct acpi_pst_domain *dom;
	uint32_t val, domain, coord, nproc;

	if (!ACPI_PKG_VALID_EQ(obj, 5)) {
		device_printf(dev, "Invalid _PSD package\n");
		return NULL;
	}

	/* NumberOfEntries */
	if (acpi_PkgInt32(obj, 0, &val) != 0 || val != 5) {
		device_printf(dev, "Invalid _PSD NumberOfEntries\n");
		return NULL;
	}

	/* Revision */
	if (acpi_PkgInt32(obj, 1, &val) != 0 || val != 0) {
		device_printf(dev, "Invalid _PSD Revision\n");
		return NULL;
	}

	if (acpi_PkgInt32(obj, 2, &domain) != 0 ||
	    acpi_PkgInt32(obj, 3, &coord) != 0 ||
	    acpi_PkgInt32(obj, 4, &nproc) != 0) {
		device_printf(dev, "Can't extract _PSD package\n");
		return NULL;
	}

	/*
	 * If NumProcessors is greater than MAXCPU,
	 * then we will never start all CPUs within
	 * this domain, and power state transition
	 * will never happen, so we just bail out
	 * here.
	 */
	if (nproc > MAXCPU) {
		device_printf(dev, "Unsupported _PSD NumProcessors (%d)\n",
			      nproc);
		return NULL;
	} else if (nproc == 0) {
		device_printf(dev, "_PSD NumProcessors are zero\n");
		return NULL;
	}

	if (!ACPI_PSD_COORD_VALID(coord)) {
		device_printf(dev, "Invalid _PSD CoordType (%#x)\n", coord);
		return NULL;
	}

	dom = acpi_pst_domain_find(domain);
	if (dom != NULL) {
		if (dom->pd_coord != coord || dom->pd_nproc != nproc) {
			device_printf(dev, "Inconsistent _PSD information "
				      "cross Processor objects\n");
			return NULL;
		}
		return dom;
	}

	dom = acpi_pst_domain_alloc(domain, coord, nproc);
	if (bootverbose)
		device_printf(dev, "create domain %u\n", dom->pd_dom);

	return dom;
}

static struct acpi_pst_domain *
acpi_pst_domain_find(uint32_t domain)
{
	struct acpi_pst_domain *dom;

	LIST_FOREACH(dom, &acpi_pst_domains, pd_link) {
		if (dom->pd_flags & ACPI_PSTDOM_FLAG_STUB)
			continue;
		if (dom->pd_dom == domain)
			return dom;
	}
	return NULL;
}

static struct acpi_pst_domain *
acpi_pst_domain_alloc(uint32_t domain, uint32_t coord, uint32_t nproc)
{
	struct acpi_pst_domain *dom;

	dom = kmalloc(sizeof(*dom), M_DEVBUF, M_WAITOK | M_ZERO);
	dom->pd_dom = domain;
	dom->pd_coord = coord;
	dom->pd_nproc = nproc;
	dom->pd_state = 0; /* XXX */
	dom->pd_sstart = 0; /* XXX */
	LIST_INIT(&dom->pd_pstlist);

	LIST_INSERT_HEAD(&acpi_pst_domains, dom, pd_link);

	return dom;
}

static void
acpi_pst_postattach(void *arg __unused)
{
	struct acpi_pst_domain *dom;
	struct acpi_cpux_softc *cpux;
	device_t *devices;
	int i, ndevices;

	devices = NULL;
	ndevices = 0;
	devclass_get_devices(acpi_pst_devclass, &devices, &ndevices);
	if (ndevices == 0)
		return;

	cpux = NULL;
	for (i = 0; i < ndevices; ++i) {
		cpux = device_get_softc(device_get_parent(devices[i]));
		if (cpux->glob_sysctl_tree != NULL)
			break;
	}
	kfree(devices, M_TEMP);
	KKASSERT(cpux != NULL);

	LIST_FOREACH(dom, &acpi_pst_domains, pd_link) {
		struct acpi_pst_softc *sc;
		char buf[32];

		i = 0;
		LIST_FOREACH(sc, &dom->pd_pstlist, pst_link)
			++i;
		if (i != dom->pd_nproc) {
			/*
			 * Can't activate this domain,
			 * certain processors are missing.
			 */
			continue;
		}
		ksnprintf(buf, sizeof(buf), "px_dom%u", dom->pd_dom);

		sysctl_ctx_init(&dom->pd_sysctl_ctx);
		dom->pd_sysctl_tree =
		SYSCTL_ADD_NODE(&dom->pd_sysctl_ctx,
			SYSCTL_CHILDREN(cpux->glob_sysctl_tree),
			OID_AUTO, buf, CTLFLAG_RD, 0,
			"P-State domain");
		if (dom->pd_sysctl_tree == NULL)
			continue;

		SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
				SYSCTL_CHILDREN(dom->pd_sysctl_tree),
				OID_AUTO, "available",
				CTLTYPE_STRING | CTLFLAG_RD,
				dom, 0, acpi_pst_sysctl_freqs, "A",
				"available frequencies");

		SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
				SYSCTL_CHILDREN(dom->pd_sysctl_tree),
				OID_AUTO, "members",
				CTLTYPE_STRING | CTLFLAG_RD,
				dom, 0, acpi_pst_sysctl_members, "A",
				"member cpus");
	}
}

static int
acpi_pst_sysctl_freqs(SYSCTL_HANDLER_ARGS)
{
	struct acpi_pst_domain *dom = arg1;
	int i, error;

	error = 0;
	for (i = 0; i < acpi_npstates; ++i) {
		if (error == 0 && i)
			error = SYSCTL_OUT(req, " ", 1);
		if (error == 0) {
			const char *pat;
			char buf[32];

			if (i < dom->pd_sstart)
				pat = "(%u)";
			else
				pat = "%u";

			ksnprintf(buf, sizeof(buf), pat,
				  acpi_pstates[i].st_freq);
			error = SYSCTL_OUT(req, buf, strlen(buf));
		}
	}
	return error;
}

static int
acpi_pst_sysctl_members(SYSCTL_HANDLER_ARGS)
{
	struct acpi_pst_domain *dom = arg1;
	const struct acpi_pst_softc *sc;
	int loop, error;

	loop = error = 0;
	LIST_FOREACH(sc, &dom->pd_pstlist, pst_link) {
		if (error == 0 && loop)
			error = SYSCTL_OUT(req, " ", 1);
		if (error == 0) {
			char buf[32];

			ksnprintf(buf, sizeof(buf), "cpu%d", sc->pst_cpuid);
			error = SYSCTL_OUT(req, buf, strlen(buf));
		}
		++loop;
	}
	return error;
}
