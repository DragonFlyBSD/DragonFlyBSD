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
#include <sys/cpuhelper.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/msgport2.h>
#include <sys/cpu_topology.h>

#include "acpi.h"
#include "acpivar.h"
#include "acpi_cpu.h"
#include "acpi_cpu_pstate.h"

#define ACPI_NPSTATE_MAX	32

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

struct acpi_pst_chmsg {
	struct cpuhelper_msg	ch_msg;
	const struct acpi_pst_res *ch_ctrl;
	const struct acpi_pst_res *ch_status;
};

struct acpi_pst_domain {
	uint32_t		pd_dom;
	uint32_t		pd_coord;
	uint32_t		pd_nproc;
	LIST_ENTRY(acpi_pst_domain) pd_link;

	uint32_t		pd_flags;

	struct lwkt_serialize	pd_serialize;

	int			pd_state;
	struct acpi_pst_list	pd_pstlist;

	struct sysctl_ctx_list	pd_sysctl_ctx;
	struct sysctl_oid	*pd_sysctl_tree;
};
LIST_HEAD(acpi_pst_domlist, acpi_pst_domain);

#define ACPI_PSTDOM_FLAG_STUB	0x1	/* stub domain, no _PSD */
#define ACPI_PSTDOM_FLAG_DEAD	0x2	/* domain can't be started */
#define ACPI_PSTDOM_FLAG_INT	0x4	/* domain created from Integer _PSD */

struct acpi_pst_softc {
	device_t		pst_dev;
	struct acpi_cpu_softc	*pst_parent;
	struct acpi_pst_domain	*pst_domain;
	struct acpi_pst_res	pst_creg;
	struct acpi_pst_res	pst_sreg;

	int			pst_state;
	int			pst_cpuid;

	uint32_t		pst_flags;

	ACPI_HANDLE		pst_handle;

	LIST_ENTRY(acpi_pst_softc) pst_link;
};

#define ACPI_PST_FLAG_PPC	0x1
#define ACPI_PST_FLAG_PDL	0x2

static int	acpi_pst_probe(device_t dev);
static int	acpi_pst_attach(device_t dev);
static void	acpi_pst_notify(device_t dev);

static void	acpi_pst_postattach(void *);
static struct acpi_pst_domain *
		acpi_pst_domain_create_int(device_t, uint32_t);
static struct acpi_pst_domain *
		acpi_pst_domain_create_pkg(device_t, ACPI_OBJECT *);
static struct acpi_pst_domain *
		acpi_pst_domain_find(uint32_t);
static struct acpi_pst_domain *
		acpi_pst_domain_alloc(uint32_t, uint32_t, uint32_t);
static void	acpi_pst_domain_set_pstate_locked(struct acpi_pst_domain *,
		    int, int *);
static void	acpi_pst_domain_set_pstate(struct acpi_pst_domain *, int,
		    int *);
static void	acpi_pst_domain_check_nproc(device_t, struct acpi_pst_domain *);
static void	acpi_pst_global_set_pstate(int);
static void	acpi_pst_global_fixup_pstate(void);

static int	acpi_pst_check_csr(struct acpi_pst_softc *);
static int	acpi_pst_check_pstates(struct acpi_pst_softc *);
static int	acpi_pst_init(struct acpi_pst_softc *);
static int	acpi_pst_set_pstate(struct acpi_pst_softc *,
		    const struct acpi_pstate *);
static const struct acpi_pstate *
		acpi_pst_get_pstate(struct acpi_pst_softc *);
static int	acpi_pst_alloc_resource(device_t, ACPI_OBJECT *, int,
		    struct acpi_pst_res *);
static int	acpi_pst_eval_ppc(struct acpi_pst_softc *, int *);
static int	acpi_pst_eval_pdl(struct acpi_pst_softc *, int *);

static void	acpi_pst_check_csr_handler(struct cpuhelper_msg *);
static void	acpi_pst_check_pstates_handler(struct cpuhelper_msg *);
static void	acpi_pst_init_handler(struct cpuhelper_msg *);
static void	acpi_pst_set_pstate_handler(struct cpuhelper_msg *);
static void	acpi_pst_get_pstate_handler(struct cpuhelper_msg *);

static int	acpi_pst_sysctl_freqs(SYSCTL_HANDLER_ARGS);
static int	acpi_pst_sysctl_freqs_bin(SYSCTL_HANDLER_ARGS);
static int	acpi_pst_sysctl_power(SYSCTL_HANDLER_ARGS);
static int	acpi_pst_sysctl_members(SYSCTL_HANDLER_ARGS);
static int	acpi_pst_sysctl_select(SYSCTL_HANDLER_ARGS);
static int	acpi_pst_sysctl_global(SYSCTL_HANDLER_ARGS);

static struct acpi_pst_domlist	acpi_pst_domains =
	LIST_HEAD_INITIALIZER(acpi_pst_domains);
static int			acpi_pst_domain_id;

static int			acpi_pst_global_state;

static int			acpi_pstate_start = -1;
static int			acpi_pstate_count;
static int			acpi_npstates;
static struct acpi_pstate	*acpi_pstates;

static const struct acpi_pst_md	*acpi_pst_md;

static int			acpi_pst_pdl = -1;
TUNABLE_INT("hw.acpi.cpu.pst.pdl", &acpi_pst_pdl);

static int			acpi_pst_ht_reuse_domain = 1;
TUNABLE_INT("hw.acpi.cpu.pst.ht_reuse_domain", &acpi_pst_ht_reuse_domain);

static int			acpi_pst_force_pkg_domain = 0;
TUNABLE_INT("hw.acpi.cpu.pst.force_pkg_domain", &acpi_pst_force_pkg_domain);

static int			acpi_pst_handle_notify = 1;
TUNABLE_INT("hw.acpi.cpu.pst.handle_notify", &acpi_pst_handle_notify);

/*
 * Force CPU package power domain for Intel CPUs.
 *
 * As of this write (14 July 2015), all Intel CPUs only have CPU package
 * power domain.
 */
static int			acpi_pst_intel_pkg_domain = 1;
TUNABLE_INT("hw.acpi.cpu.pst.intel_pkg_domain", &acpi_pst_intel_pkg_domain);

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

	DEVMETHOD_END
};

static driver_t acpi_pst_driver = {
	"cpu_pst",
	acpi_pst_methods,
	sizeof(struct acpi_pst_softc)
};

static devclass_t acpi_pst_devclass;
DRIVER_MODULE(cpu_pst, cpu, acpi_pst_driver, acpi_pst_devclass, NULL, NULL);
MODULE_DEPEND(cpu_pst, acpi, 1, 1, 1);

static __inline int
acpi_pst_freq2index(int freq)
{
	int i;

	for (i = 0; i < acpi_npstates; ++i) {
		if (acpi_pstates[i].st_freq == freq)
			return i;
	}
	return -1;
}

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

	if (acpi_pst_md == NULL)
		acpi_pst_md = acpi_pst_md_probe();

	handle = acpi_get_handle(dev);

	/*
	 * Check _PSD package
	 *
	 * NOTE:
	 * Some BIOSes do not expose _PCT for the second thread of
	 * CPU cores.  In this case, _PSD should be enough to get the
	 * P-state of the second thread working, since it must have
	 * the same _PCT and _PSS as the first thread in the same
	 * core.
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(handle, "_PSD", NULL, &buf);
	if (!ACPI_FAILURE(status)) {
		AcpiOsFree((ACPI_OBJECT *)buf.Pointer);
		goto done;
	}

	/*
	 * Check _PCT package
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(handle, "_PCT", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		if (bootverbose) {
			device_printf(dev, "Can't get _PCT package - %s\n",
				      AcpiFormatException(status));
		}
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

done:
	device_set_desc(dev, "ACPI CPU P-State");
	return 0;
}

static int
acpi_pst_attach(device_t dev)
{
	struct acpi_pst_softc *sc = device_get_softc(dev);
	struct acpi_pst_domain *dom = NULL;
	ACPI_BUFFER buf;
	ACPI_STATUS status;
	ACPI_OBJECT *obj;
	struct acpi_pstate *pstate, *p;
	int i, npstate, error, sstart, scount;

	sc->pst_dev = dev;
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

		if (acpi_pst_domain_id > 0) {
			device_printf(dev, "Missing _PSD for certain CPUs\n");
			AcpiOsFree(obj);
			return ENXIO;
		}
		acpi_pst_domain_id = -1;

		if (ACPI_PKG_VALID_EQ(obj, 1)) {
			dom = acpi_pst_domain_create_pkg(dev,
				&obj->Package.Elements[0]);
			if (dom == NULL) {
				AcpiOsFree(obj);
				return ENXIO;
			}
		} else {
			if (obj->Type != ACPI_TYPE_INTEGER) {
				device_printf(dev,
				    "Invalid _PSD package, Type 0x%x\n",
				    obj->Type);
				AcpiOsFree(obj);
				return ENXIO;
			} else {
				device_printf(dev, "Integer _PSD %ju\n",
				    (uintmax_t)obj->Integer.Value);
				dom = acpi_pst_domain_create_int(dev,
				    obj->Integer.Value);
				if (dom == NULL) {
					AcpiOsFree(obj);
					return ENXIO;
				}
			}
		}

		/* Free _PSD */
		AcpiOsFree(buf.Pointer);
	} else {
		if (acpi_pst_domain_id < 0) {
			device_printf(dev, "Missing _PSD for cpu%d\n",
			    sc->pst_cpuid);
			return ENXIO;
		}

		/*
		 * Create a stub one processor domain for each processor
		 */
		dom = acpi_pst_domain_alloc(acpi_pst_domain_id,
			ACPI_PSD_COORD_SWANY, 1);
		dom->pd_flags |= ACPI_PSTDOM_FLAG_STUB;

		++acpi_pst_domain_id;
	}

	/* Make sure that adding us will not overflow our domain */
	acpi_pst_domain_check_nproc(dev, dom);

	/*
	 * Get control/status registers from _PCT
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PCT", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		struct acpi_pst_softc *pst;

		/*
		 * No _PCT.  See the comment in acpi_pst_probe() near
		 * _PSD check.
		 *
		 * Use control/status registers of another CPU in the
		 * same domain, or in the same core, if the type of
		 * these registers are "Fixed Hardware", e.g. on most
		 * of the model Intel CPUs.
		 */
		pst = LIST_FIRST(&dom->pd_pstlist);
		if (pst == NULL) {
			cpumask_t mask;

			mask = get_cpumask_from_level(sc->pst_cpuid,
			    CORE_LEVEL);
			if (CPUMASK_TESTNZERO(mask)) {
				struct acpi_pst_domain *dom1;

				LIST_FOREACH(dom1, &acpi_pst_domains, pd_link) {
					LIST_FOREACH(pst, &dom1->pd_pstlist,
					    pst_link) {
						if (CPUMASK_TESTBIT(mask,
						    pst->pst_cpuid))
							break;
					}
					if (pst != NULL)
						break;
				}
				if (pst != NULL && acpi_pst_ht_reuse_domain) {
					/*
					 * Use the same domain for CPUs in the
					 * same core.
					 */
					device_printf(dev, "Destroy domain%u, "
					    "reuse domain%u\n",
					    dom->pd_dom, dom1->pd_dom);
					LIST_REMOVE(dom, pd_link);
					kfree(dom, M_DEVBUF);
					dom = dom1;
					/*
					 * Make sure that adding us will not
					 * overflow the domain containing
					 * siblings in the same core.
					 */
					acpi_pst_domain_check_nproc(dev, dom);
				}
			}
		}
		if (pst != NULL &&
		    pst->pst_creg.pr_res == NULL &&
		    pst->pst_creg.pr_rid == 0 &&
		    pst->pst_creg.pr_gas.SpaceId ==
		    ACPI_ADR_SPACE_FIXED_HARDWARE &&
		    pst->pst_sreg.pr_res == NULL &&
		    pst->pst_sreg.pr_rid == 0 &&
		    pst->pst_sreg.pr_gas.SpaceId ==
		    ACPI_ADR_SPACE_FIXED_HARDWARE) {
			sc->pst_creg = pst->pst_creg;
			sc->pst_sreg = pst->pst_sreg;
			device_printf(dev,
			    "No _PCT; reuse %s control/status regs\n",
			    device_get_nameunit(pst->pst_dev));
			goto fetch_pss;
		}
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

	/* Save and try allocating control register */
	error = acpi_pst_alloc_resource(dev, obj, 0, &sc->pst_creg);
	if (error) {
		AcpiOsFree(obj);
		return error;
	}
	if (bootverbose) {
		device_printf(dev, "control reg %d %jx\n",
			      sc->pst_creg.pr_gas.SpaceId,
			      (uintmax_t)sc->pst_creg.pr_gas.Address);
	}

	/* Save and try allocating status register */
	error = acpi_pst_alloc_resource(dev, obj, 1, &sc->pst_sreg);
	if (error) {
		AcpiOsFree(obj);
		return error;
	}
	if (bootverbose) {
		device_printf(dev, "status reg %d %jx\n",
			      sc->pst_sreg.pr_gas.SpaceId,
			      (uintmax_t)sc->pst_sreg.pr_gas.Address);
	}

	/* Free _PCT */
	AcpiOsFree(obj);

fetch_pss:
	/*
	 * Create P-State table according to _PSS
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PSS", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		/*
		 * No _PSS.  See the comment in acpi_pst_probe() near
		 * _PSD check.
		 *
		 * Assume _PSS are same across all CPUs; well, they
		 * should/have to be so.
		 */
		if (acpi_npstates > 0 && acpi_pstates != NULL) {
			device_printf(dev, "No _PSS\n");
			goto fetch_ppc;
		}
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
		if (p->st_freq & 0x80000000) {
			device_printf(dev, "Invalid _PSS P%d freq: 0x%08x\n",
			    i, p->st_freq);
			AcpiOsFree(obj);
			kfree(pstate, M_TEMP);
			return ENXIO;
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
#if 0
			/*
			 * Some BIOSes create different P-State tables;
			 * just trust the one from the BSP and move on.
			 */
			kfree(pstate, M_TEMP);
			return ENXIO;
#endif
		}
		kfree(pstate, M_TEMP);
	}

fetch_ppc:
	/* By default, we start from P-State table's first entry */
	sstart = 0;

	/*
	 * Adjust the usable first entry of P-State table,
	 * if there is _PPC object.
	 */
	error = acpi_pst_eval_ppc(sc, &sstart);
	if (error && error != ENOENT)
		return error;
	else if (!error)
		sc->pst_flags |= ACPI_PST_FLAG_PPC;
	if (acpi_pstate_start < 0) {
		acpi_pstate_start = sstart;
	} else if (acpi_pstate_start != sstart) {
		device_printf(dev, "_PPC mismatch, was %d, now %d\n",
		    acpi_pstate_start, sstart);
		if (acpi_pstate_start < sstart) {
			device_printf(dev, "_PPC %d -> %d\n",
			    acpi_pstate_start, sstart);
			acpi_pstate_start = sstart;
		}
	}

	/*
	 * By default, we assume number of usable P-States is same as
	 * number of P-States.
	 */
	scount = acpi_npstates;

	/*
	 * Allow users to override or set _PDL
	 */
	if (acpi_pst_pdl >= 0) {
		if (acpi_pst_pdl < acpi_npstates) {
			if (bootverbose) {
				device_printf(dev, "_PDL override %d\n",
				    acpi_pst_pdl);
			}
			scount = acpi_pst_pdl + 1;
			goto proc_pdl;
		} else {
			device_printf(dev, "Invalid _PDL override %d, "
			    "must be less than %d\n", acpi_pst_pdl,
			    acpi_npstates);
		}
	}

	/*
	 * Adjust the number of usable entries in P-State table,
	 * if there is _PDL object.
	 */
	error = acpi_pst_eval_pdl(sc, &scount);
	if (error && error != ENOENT)
		return error;
	else if (!error)
		sc->pst_flags |= ACPI_PST_FLAG_PDL;
proc_pdl:
	if (acpi_pstate_count == 0) {
		acpi_pstate_count = scount;
	} else if (acpi_pstate_count != scount) {
		device_printf(dev, "_PDL mismatch, was %d, now %d\n",
		    acpi_pstate_count, scount);
		if (acpi_pstate_count > scount) {
			device_printf(dev, "_PDL %d -> %d\n",
			    acpi_pstate_count, scount);
			acpi_pstate_count = scount;
		}
	}

	/*
	 * Some CPUs only have package P-states, but some BIOSes put each
	 * hyperthread to its own P-state domain; allow user to override.
	 */
	if (LIST_EMPTY(&dom->pd_pstlist) &&
	    (acpi_pst_force_pkg_domain ||
	     (cpu_vendor_id == CPU_VENDOR_INTEL &&
	      acpi_pst_intel_pkg_domain))) {
		cpumask_t mask;

		mask = get_cpumask_from_level(sc->pst_cpuid, CHIP_LEVEL);
		if (CPUMASK_TESTNZERO(mask)) {
			struct acpi_pst_softc *pst = NULL;
			struct acpi_pst_domain *dom1;

			LIST_FOREACH(dom1, &acpi_pst_domains, pd_link) {
				LIST_FOREACH(pst, &dom1->pd_pstlist,
				    pst_link) {
					if (CPUMASK_TESTBIT(mask,
					    pst->pst_cpuid))
						break;
				}
				if (pst != NULL)
					break;
			}
			if (pst != NULL &&
			    memcmp(&pst->pst_creg, &sc->pst_creg,
			        sizeof(sc->pst_creg)) == 0 &&
			    memcmp(&pst->pst_sreg, &sc->pst_sreg,
			        sizeof(sc->pst_sreg)) == 0) {
				/*
				 * Use the same domain for CPUs in the
				 * same package.
				 */
				device_printf(dev, "Destroy domain%u, "
				    "force pkg domain%u\n",
				    dom->pd_dom, dom1->pd_dom);
				LIST_REMOVE(dom, pd_link);
				kfree(dom, M_DEVBUF);
				dom = dom1;
				/*
				 * Make sure that adding us will not
				 * overflow the domain containing
				 * siblings in the same package.
				 */
				acpi_pst_domain_check_nproc(dev, dom);
			}
		}
	}

	/* Link us with the domain */
	sc->pst_domain = dom;
	LIST_INSERT_HEAD(&dom->pd_pstlist, sc, pst_link);

	if (device_get_unit(dev) == 0)
		AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_pst_postattach, NULL);

	if (sc->pst_flags & (ACPI_PST_FLAG_PPC | ACPI_PST_FLAG_PDL))
		sc->pst_parent->cpu_pst_notify = acpi_pst_notify;

	return 0;
}

static struct acpi_pst_domain *
acpi_pst_domain_create_pkg(device_t dev, ACPI_OBJECT *obj)
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

	if (!ACPI_PSD_COORD_VALID(coord)) {
		device_printf(dev, "Invalid _PSD CoordType (%#x)\n", coord);
		return NULL;
	}

	if (nproc > MAXCPU) {
		/*
		 * If NumProcessors is greater than MAXCPU
		 * and domain's coordination is SWALL, then
		 * we will never be able to start all CPUs
		 * within this domain, and power state
		 * transition will never be completed, so we
		 * just bail out here.
		 */
		if (coord == ACPI_PSD_COORD_SWALL) {
			device_printf(dev, "Unsupported _PSD NumProcessors "
				      "(%d)\n", nproc);
			return NULL;
		}
	} else if (nproc == 0) {
		device_printf(dev, "_PSD NumProcessors are zero\n");
		return NULL;
	}

	dom = acpi_pst_domain_find(domain);
	if (dom != NULL) {
		if (dom->pd_flags & ACPI_PSTDOM_FLAG_INT) {
			device_printf(dev, "Mixed Integer _PSD and "
			    "Package _PSD\n");
			return NULL;
		}
		if (dom->pd_coord != coord) {
			device_printf(dev, "Inconsistent _PSD coord "
			    "information cross Processor objects\n");
			return NULL;
		}
		if (dom->pd_nproc != nproc) {
			device_printf(dev, "Inconsistent _PSD nproc "
			    "information cross Processor objects\n");
			/*
			 * Some stupid BIOSes will set wrong "# of processors",
			 * e.g. 1 for CPU w/ hyperthreading; Be lenient here.
			 */
		}
		return dom;
	}

	dom = acpi_pst_domain_alloc(domain, coord, nproc);
	if (bootverbose) {
		device_printf(dev, "create pkg domain%u, coord %#x\n",
		    dom->pd_dom, dom->pd_coord);
	}

	return dom;
}

static struct acpi_pst_domain *
acpi_pst_domain_create_int(device_t dev, uint32_t domain)
{
	struct acpi_pst_domain *dom;

	dom = acpi_pst_domain_find(domain);
	if (dom != NULL) {
		if ((dom->pd_flags & ACPI_PSTDOM_FLAG_INT) == 0) {
			device_printf(dev, "Mixed Package _PSD and "
			    "Integer _PSD\n");
			return NULL;
		}
		KKASSERT(dom->pd_coord == ACPI_PSD_COORD_SWALL);

		dom->pd_nproc++;
		return dom;
	}

	dom = acpi_pst_domain_alloc(domain, ACPI_PSD_COORD_SWALL, 1);
	dom->pd_flags |= ACPI_PSTDOM_FLAG_INT;

	if (bootverbose)
		device_printf(dev, "create int domain%u\n", dom->pd_dom);

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
	LIST_INIT(&dom->pd_pstlist);
	lwkt_serialize_init(&dom->pd_serialize);

	LIST_INSERT_HEAD(&acpi_pst_domains, dom, pd_link);

	return dom;
}

static void
acpi_pst_domain_set_pstate_locked(struct acpi_pst_domain *dom, int i, int *global)
{
	const struct acpi_pstate *pstate;
	struct acpi_pst_softc *sc;
	int done, error;

	ASSERT_SERIALIZED(&dom->pd_serialize);

	KKASSERT(i >= 0 && i < acpi_npstates);
	pstate = &acpi_pstates[i];

	done = 0;
	LIST_FOREACH(sc, &dom->pd_pstlist, pst_link) {
		if (!done) {
			error = acpi_pst_set_pstate(sc, pstate);
			if (error) {
				device_printf(sc->pst_dev, "can't set "
					      "freq %d\n", pstate->st_freq);
				/* XXX error cleanup? */
			}
			if (dom->pd_coord == ACPI_PSD_COORD_SWANY)
				done = 1;
		}
		sc->pst_state = i;
	}
	dom->pd_state = i;

	if (global != NULL)
		*global = i;
}

static void
acpi_pst_domain_set_pstate(struct acpi_pst_domain *dom, int i, int *global)
{
	lwkt_serialize_enter(&dom->pd_serialize);
	acpi_pst_domain_set_pstate_locked(dom, i, global);
	lwkt_serialize_exit(&dom->pd_serialize);
}

static void
acpi_pst_global_set_pstate(int i)
{
	struct acpi_pst_domain *dom;
	int *global = &acpi_pst_global_state;

	LIST_FOREACH(dom, &acpi_pst_domains, pd_link) {
		/* Skip dead domain */
		if (dom->pd_flags & ACPI_PSTDOM_FLAG_DEAD)
			continue;
		acpi_pst_domain_set_pstate(dom, i, global);
		global = NULL;
	}
}

static void
acpi_pst_global_fixup_pstate(void)
{
	struct acpi_pst_domain *dom;
	int *global = &acpi_pst_global_state;
	int sstart, scount;

	sstart = acpi_pstate_start;
	scount = acpi_pstate_count;

	LIST_FOREACH(dom, &acpi_pst_domains, pd_link) {
		int i = -1;

		/* Skip dead domain */
		if (dom->pd_flags & ACPI_PSTDOM_FLAG_DEAD)
			continue;

		lwkt_serialize_enter(&dom->pd_serialize);

		if (global != NULL) {
			if (*global < sstart)
				*global = sstart;
			else if (*global >= scount)
				*global = scount - 1;
			global = NULL;
		}
		if (dom->pd_state < sstart)
			i = sstart;
		else if (dom->pd_state >= scount)
			i = scount - 1;
		if (i >= 0)
			acpi_pst_domain_set_pstate_locked(dom, i, NULL);

		lwkt_serialize_exit(&dom->pd_serialize);
	}
}

static void
acpi_pst_postattach(void *arg __unused)
{
	struct acpi_pst_domain *dom;
	struct acpi_cpu_softc *cpu;
	device_t *devices;
	int i, ndevices, error, has_domain;

	devices = NULL;
	ndevices = 0;
	error = devclass_get_devices(acpi_pst_devclass, &devices, &ndevices);
	if (error)
		return;

	if (ndevices == 0)
		return;

	cpu = NULL;
	for (i = 0; i < ndevices; ++i) {
		cpu = device_get_softc(device_get_parent(devices[i]));
		if (cpu->glob_sysctl_tree != NULL)
			break;
	}
	kfree(devices, M_TEMP);
	KKASSERT(cpu != NULL);

	if (acpi_pst_md == NULL)
		kprintf("ACPI: no P-State CPU driver\n");

	has_domain = 0;
	LIST_FOREACH(dom, &acpi_pst_domains, pd_link) {
		struct acpi_pst_softc *sc;
		char buf[32];

		dom->pd_state = acpi_pstate_start;

		/*
		 * Make sure that all processors belonging to this
		 * domain are located.
		 */
		i = 0;
		LIST_FOREACH(sc, &dom->pd_pstlist, pst_link) {
			sc->pst_state = acpi_pstate_start;
			++i;
		}
		if (i != dom->pd_nproc) {
			KKASSERT(i < dom->pd_nproc);

			kprintf("ACPI: domain%u misses processors, "
				"should be %d, got %d\n", dom->pd_dom,
				dom->pd_nproc, i);
			if (dom->pd_coord == ACPI_PSD_COORD_SWALL) {
				/*
				 * If this domain's coordination is
				 * SWALL and we don't see all of the
				 * member CPUs of this domain, then
				 * the P-State transition will never
				 * be completed, so just leave this
				 * domain out.
				 */
				dom->pd_flags |= ACPI_PSTDOM_FLAG_DEAD;
				continue;
			}
			dom->pd_nproc = i;
		}

		/*
		 * Validate P-State configurations for this domain
		 */
		LIST_FOREACH(sc, &dom->pd_pstlist, pst_link) {
			error = acpi_pst_check_csr(sc);
			if (error)
				break;

			error = acpi_pst_check_pstates(sc);
			if (error)
				break;
		}
		if (sc != NULL) {
			kprintf("ACPI: domain%u P-State configuration "
				"check failed\n", dom->pd_dom);
			dom->pd_flags |= ACPI_PSTDOM_FLAG_DEAD;
			continue;
		}

		/*
		 * Do necssary P-State initialization
		 */
		LIST_FOREACH(sc, &dom->pd_pstlist, pst_link) {
			error = acpi_pst_init(sc);
			if (error)
				break;
		}
		if (sc != NULL) {
			kprintf("ACPI: domain%u P-State initialization "
				"check failed\n", dom->pd_dom);
			dom->pd_flags |= ACPI_PSTDOM_FLAG_DEAD;
			continue;
		}

		has_domain = 1;

		ksnprintf(buf, sizeof(buf), "px_dom%u", dom->pd_dom);

		sysctl_ctx_init(&dom->pd_sysctl_ctx);
		dom->pd_sysctl_tree =
		SYSCTL_ADD_NODE(&dom->pd_sysctl_ctx,
			SYSCTL_CHILDREN(cpu->glob_sysctl_tree),
			OID_AUTO, buf, CTLFLAG_RD, 0,
			"P-State domain");
		if (dom->pd_sysctl_tree == NULL) {
			kprintf("ACPI: Can't create sysctl tree for domain%u",
				dom->pd_dom);
			continue;
		}

		SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
				SYSCTL_CHILDREN(dom->pd_sysctl_tree),
				OID_AUTO, "available",
				CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_SKIP,
				dom, 0, acpi_pst_sysctl_freqs, "A",
				"available frequencies");

		SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
				SYSCTL_CHILDREN(dom->pd_sysctl_tree),
				OID_AUTO, "avail",
				CTLTYPE_OPAQUE | CTLFLAG_RD,
				dom, 0, acpi_pst_sysctl_freqs_bin, "IU",
				"available frequencies");

		SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
				SYSCTL_CHILDREN(dom->pd_sysctl_tree),
				OID_AUTO, "power",
				CTLTYPE_OPAQUE | CTLFLAG_RD,
				dom, 0, acpi_pst_sysctl_power, "IU",
				"power of available frequencies");

		SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
				SYSCTL_CHILDREN(dom->pd_sysctl_tree),
				OID_AUTO, "members",
				CTLTYPE_STRING | CTLFLAG_RD,
				dom, 0, acpi_pst_sysctl_members, "A",
				"member cpus");

		if (acpi_pst_md != NULL &&
		    acpi_pst_md->pmd_set_pstate != NULL) {
			SYSCTL_ADD_PROC(&dom->pd_sysctl_ctx,
					SYSCTL_CHILDREN(dom->pd_sysctl_tree),
					OID_AUTO, "select",
					CTLTYPE_UINT | CTLFLAG_RW,
					dom, 0, acpi_pst_sysctl_select,
					"IU", "select freq");
		}
	}

	if (has_domain && acpi_pst_md != NULL &&
	    acpi_pst_md->pmd_set_pstate != NULL) {
		SYSCTL_ADD_PROC(&cpu->glob_sysctl_ctx,
				SYSCTL_CHILDREN(cpu->glob_sysctl_tree),
				OID_AUTO, "px_global",
				CTLTYPE_UINT | CTLFLAG_RW,
				NULL, 0, acpi_pst_sysctl_global,
				"IU", "select freq for all domains");
		SYSCTL_ADD_INT(&cpu->glob_sysctl_ctx,
			       SYSCTL_CHILDREN(cpu->glob_sysctl_tree),
			       OID_AUTO, "px_handle_notify", CTLFLAG_RW,
			       &acpi_pst_handle_notify, 0,
			       "handle type 0x80 notify");

		acpi_pst_global_set_pstate(acpi_pstate_start);
	}
}

static int
acpi_pst_sysctl_freqs(SYSCTL_HANDLER_ARGS)
{
	int i, error, sstart, scount;

	error = 0;
	sstart = acpi_pstate_start;
	scount = acpi_pstate_count;
	for (i = 0; i < acpi_npstates; ++i) {
		if (error == 0 && i)
			error = SYSCTL_OUT(req, " ", 1);
		if (error == 0) {
			const char *pat;
			char buf[32];

			if (i < sstart || i >= scount)
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
acpi_pst_sysctl_freqs_bin(SYSCTL_HANDLER_ARGS)
{
	uint32_t freqs[ACPI_NPSTATE_MAX];
	int cnt, i, sstart, scount;

	sstart = acpi_pstate_start;
	scount = acpi_pstate_count;

	cnt = scount - sstart;
	for (i = 0; i < cnt; ++i)
		freqs[i] = acpi_pstates[sstart + i].st_freq;

	return sysctl_handle_opaque(oidp, freqs, cnt * sizeof(freqs[0]), req);
}

static int
acpi_pst_sysctl_power(SYSCTL_HANDLER_ARGS)
{
	uint32_t power[ACPI_NPSTATE_MAX];
	int cnt, i, sstart, scount;

	sstart = acpi_pstate_start;
	scount = acpi_pstate_count;

	cnt = scount - sstart;
	for (i = 0; i < cnt; ++i)
		power[i] = acpi_pstates[sstart + i].st_power;

	return sysctl_handle_opaque(oidp, power, cnt * sizeof(power[0]), req);
}

static int
acpi_pst_sysctl_members(SYSCTL_HANDLER_ARGS)
{
	struct acpi_pst_domain *dom = arg1;
	struct acpi_pst_softc *sc;
	int loop, error;

	loop = error = 0;
	LIST_FOREACH(sc, &dom->pd_pstlist, pst_link) {
		char buf[32];

		if (error == 0 && loop)
			error = SYSCTL_OUT(req, " ", 1);
		if (error == 0) {
			ksnprintf(buf, sizeof(buf), "cpu%d", sc->pst_cpuid);
			error = SYSCTL_OUT(req, buf, strlen(buf));
		}

		if (error == 0 && acpi_pst_md && acpi_pst_md->pmd_get_pstate) {
			const struct acpi_pstate *pstate;
			const char *str;

			pstate = acpi_pst_get_pstate(sc);
			if (pstate == NULL) {
				str = "(*)";
			} else {
				ksnprintf(buf, sizeof(buf), "(%d)",
					  pstate->st_freq);
				str = buf;
			}
			error = SYSCTL_OUT(req, str, strlen(str));
		}
		++loop;
	}
	return error;
}

static int
acpi_pst_sysctl_select(SYSCTL_HANDLER_ARGS)
{
	struct acpi_pst_domain *dom = arg1;
	int error, i, freq;

	KKASSERT(dom->pd_state >= 0 && dom->pd_state < acpi_npstates);

	freq = acpi_pstates[dom->pd_state].st_freq;

	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error || req->newptr == NULL)
		return error;

	i = acpi_pst_freq2index(freq);
	if (i < 0)
		return EINVAL;

	acpi_pst_domain_set_pstate(dom, i, NULL);
	return 0;
}

static int
acpi_pst_sysctl_global(SYSCTL_HANDLER_ARGS)
{
	int error, i, freq;

	KKASSERT(acpi_pst_global_state >= 0 &&
		 acpi_pst_global_state < acpi_npstates);

	freq = acpi_pstates[acpi_pst_global_state].st_freq;

	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error || req->newptr == NULL)
		return error;

	i = acpi_pst_freq2index(freq);
	if (i < 0)
		return EINVAL;

	acpi_pst_global_set_pstate(i);

	return 0;
}

static void
acpi_pst_check_csr_handler(struct cpuhelper_msg *msg)
{
	struct acpi_pst_chmsg *pmsg = (struct acpi_pst_chmsg *)msg;
	int error;

	error = acpi_pst_md->pmd_check_csr(pmsg->ch_ctrl, pmsg->ch_status);
	cpuhelper_replymsg(msg, error);
}

static int
acpi_pst_check_csr(struct acpi_pst_softc *sc)
{
	struct acpi_pst_chmsg msg;

	if (acpi_pst_md == NULL)
		return 0;

	cpuhelper_initmsg(&msg.ch_msg, &curthread->td_msgport,
	    acpi_pst_check_csr_handler, NULL, MSGF_PRIORITY);
	msg.ch_ctrl = &sc->pst_creg;
	msg.ch_status = &sc->pst_sreg;

	return (cpuhelper_domsg(&msg.ch_msg, sc->pst_cpuid));
}

static void
acpi_pst_check_pstates_handler(struct cpuhelper_msg *msg)
{
	int error;

	error = acpi_pst_md->pmd_check_pstates(acpi_pstates, acpi_npstates);
	cpuhelper_replymsg(msg, error);
}

static int
acpi_pst_check_pstates(struct acpi_pst_softc *sc)
{
	struct cpuhelper_msg msg;

	if (acpi_pst_md == NULL)
		return 0;

	cpuhelper_initmsg(&msg, &curthread->td_msgport,
	    acpi_pst_check_pstates_handler, NULL, MSGF_PRIORITY);
	return (cpuhelper_domsg(&msg, sc->pst_cpuid));
}

static void
acpi_pst_init_handler(struct cpuhelper_msg *msg)
{
	struct acpi_pst_chmsg *pmsg = (struct acpi_pst_chmsg *)msg;
	int error;

	error = acpi_pst_md->pmd_init(pmsg->ch_ctrl, pmsg->ch_status);
	cpuhelper_replymsg(msg, error);
}

static int
acpi_pst_init(struct acpi_pst_softc *sc)
{
	struct acpi_pst_chmsg msg;

	if (acpi_pst_md == NULL)
		return 0;

	cpuhelper_initmsg(&msg.ch_msg, &curthread->td_msgport,
	    acpi_pst_init_handler, NULL, MSGF_PRIORITY);
	msg.ch_ctrl = &sc->pst_creg;
	msg.ch_status = &sc->pst_sreg;

	return (cpuhelper_domsg(&msg.ch_msg, sc->pst_cpuid));
}

static void
acpi_pst_set_pstate_handler(struct cpuhelper_msg *msg)
{
	struct acpi_pst_chmsg *pmsg = (struct acpi_pst_chmsg *)msg;
	int error;

	error = acpi_pst_md->pmd_set_pstate(pmsg->ch_ctrl, pmsg->ch_status,
	    msg->ch_cbarg);
	cpuhelper_replymsg(msg, error);
}

static int
acpi_pst_set_pstate(struct acpi_pst_softc *sc, const struct acpi_pstate *pstate)
{
	struct acpi_pst_chmsg msg;

	KKASSERT(acpi_pst_md != NULL);

	cpuhelper_initmsg(&msg.ch_msg, &curthread->td_msgport,
	    acpi_pst_set_pstate_handler, __DECONST(void *, pstate),
	    MSGF_PRIORITY);
	msg.ch_ctrl = &sc->pst_creg;
	msg.ch_status = &sc->pst_sreg;

	return (cpuhelper_domsg(&msg.ch_msg, sc->pst_cpuid));
}

static void
acpi_pst_get_pstate_handler(struct cpuhelper_msg *msg)
{
	struct acpi_pst_chmsg *pmsg = (struct acpi_pst_chmsg *)msg;
	const struct acpi_pstate *pstate;

	pstate = acpi_pst_md->pmd_get_pstate(pmsg->ch_status, acpi_pstates,
	    acpi_npstates);
	msg->ch_cbarg = __DECONST(void *, pstate);
	cpuhelper_replymsg(msg, 0);
}

static const struct acpi_pstate *
acpi_pst_get_pstate(struct acpi_pst_softc *sc)
{
	struct acpi_pst_chmsg msg;

	if (acpi_pst_md == NULL)
		return 0;

	cpuhelper_initmsg(&msg.ch_msg, &curthread->td_msgport,
	    acpi_pst_get_pstate_handler, NULL, MSGF_PRIORITY);
	msg.ch_status = &sc->pst_sreg;

	cpuhelper_domsg(&msg.ch_msg, sc->pst_cpuid);
	return (msg.ch_msg.ch_cbarg);
}

static int
acpi_pst_alloc_resource(device_t dev, ACPI_OBJECT *obj, int idx,
			struct acpi_pst_res *res)
{
	struct acpi_pst_softc *sc = device_get_softc(dev);
	int error, type;

	/* Save GAS */
	error = acpi_PkgRawGas(obj, idx, &res->pr_gas);
	if (error)
		return error;

	/* Allocate resource, if possible */
	res->pr_rid = sc->pst_parent->cpu_next_rid;
	acpi_bus_alloc_gas(dev, &type, &res->pr_rid, &res->pr_gas, &res->pr_res, 0);
	if (res->pr_res != NULL) {
		sc->pst_parent->cpu_next_rid++;
		res->pr_bt = rman_get_bustag(res->pr_res);
		res->pr_bh = rman_get_bushandle(res->pr_res);
	} else {
		res->pr_rid = 0;
	}
	return 0;
}

static void
acpi_pst_domain_check_nproc(device_t dev, struct acpi_pst_domain *dom)
{
	struct acpi_pst_softc *pst;
	int i;

	i = 0;
	LIST_FOREACH(pst, &dom->pd_pstlist, pst_link)
		++i;
	if (i == dom->pd_nproc) {
		/*
		 * Some stupid BIOSes will set wrong "# of processors",
		 * e.g. 1 for CPU w/ hyperthreading; Be lenient here.
		 */
		if (bootverbose) {
			device_printf(dev, "domain%u already contains %d "
			    "P-States\n", dom->pd_dom, dom->pd_nproc);
		}
		dom->pd_nproc++;
	}
	KKASSERT(i < dom->pd_nproc);
}

static int
acpi_pst_eval_ppc(struct acpi_pst_softc *sc, int *sstart)
{
	ACPI_BUFFER buf;
	ACPI_STATUS status;
	ACPI_OBJECT *obj;

	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PPC", NULL, &buf);
	if (!ACPI_FAILURE(status)) {
		ACPI_OBJECT_LIST arglist;
		ACPI_OBJECT arg[2];

		obj = (ACPI_OBJECT *)buf.Pointer;
		if (obj->Type == ACPI_TYPE_INTEGER) {
			if (obj->Integer.Value >= acpi_npstates) {
				device_printf(sc->pst_dev,
				    "Invalid _PPC value\n");
				AcpiOsFree(obj);
				return ENXIO;
			}
			*sstart = obj->Integer.Value;
			if (bootverbose) {
				device_printf(sc->pst_dev, "_PPC %d\n",
				    *sstart);
			}
		} else {
			device_printf(sc->pst_dev, "Invalid _PPC object\n");
			AcpiOsFree(obj);
			return ENXIO;
		}

		/* Free _PPC */
		AcpiOsFree(obj);

		/* _PPC has been successfully processed */
		arglist.Pointer = arg;
		arglist.Count = 2;
		arg[0].Type = ACPI_TYPE_INTEGER;
		arg[0].Integer.Value = 0x80;
		arg[1].Type = ACPI_TYPE_INTEGER;
		arg[1].Integer.Value = 0;
		AcpiEvaluateObject(sc->pst_handle, "_OST", &arglist, NULL);

		return 0;
	}
	return ENOENT;
}

static int
acpi_pst_eval_pdl(struct acpi_pst_softc *sc, int *scount)
{
	ACPI_BUFFER buf;
	ACPI_STATUS status;
	ACPI_OBJECT *obj;

	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->pst_handle, "_PDL", NULL, &buf);
	if (!ACPI_FAILURE(status)) {
		obj = (ACPI_OBJECT *)buf.Pointer;
		if (obj->Type == ACPI_TYPE_INTEGER) {
			if (obj->Integer.Value >= acpi_npstates) {
				device_printf(sc->pst_dev,
				    "Invalid _PDL value\n");
				AcpiOsFree(obj);
				return ENXIO;
			}
			if (obj->Integer.Value >= acpi_pstate_start) {
				*scount = obj->Integer.Value + 1;
				if (bootverbose) {
					device_printf(sc->pst_dev, "_PDL %d\n",
					    *scount);
				}
			} else {
				/* Prefer _PPC as stated in ACPI 5.1 8.4.4.6 */
				device_printf(sc->pst_dev, "conflict _PDL %ju "
				    "and _PPC %d, ignore\n",
				    (uintmax_t)obj->Integer.Value,
				    acpi_pstate_start);
			}
		} else {
			device_printf(sc->pst_dev, "Invalid _PDL object\n");
			AcpiOsFree(obj);
			return ENXIO;
		}

		/* Free _PDL */
		AcpiOsFree(obj);

		return 0;
	}
	return ENOENT;
}

/*
 * Notify is serialized by acpi task thread.
 */
static void
acpi_pst_notify(device_t dev)
{
	struct acpi_pst_softc *sc = device_get_softc(dev);
	boolean_t fixup = FALSE;

	if (!acpi_pst_handle_notify)
		return;

	/*
	 * NOTE:
	 * _PPC and _PDL evaluation order is critical.  _PDL
	 * evaluation depends on _PPC evaluation.
	 */
	if (sc->pst_flags & ACPI_PST_FLAG_PPC) {
		int sstart = acpi_pstate_start;

		acpi_pst_eval_ppc(sc, &sstart);
		if (acpi_pstate_start != sstart && sc->pst_cpuid == 0) {
			acpi_pstate_start = sstart;
			fixup = TRUE;
		}
	}
	if (sc->pst_flags & ACPI_PST_FLAG_PDL) {
		int scount = acpi_pstate_count;

		acpi_pst_eval_pdl(sc, &scount);
		if (acpi_pstate_count != scount && sc->pst_cpuid == 0) {
			acpi_pstate_count = scount;
			fixup = TRUE;
		}
	}

	if (fixup && acpi_pst_md != NULL &&
	    acpi_pst_md->pmd_set_pstate != NULL)
		acpi_pst_global_fixup_pstate();
}
