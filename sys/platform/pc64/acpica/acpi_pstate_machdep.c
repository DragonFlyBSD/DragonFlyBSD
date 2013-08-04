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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/globaldata.h>

#include <machine/md_var.h>
#include <machine/cpufunc.h>
#include <machine/cpufreq.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>

#include "acpi.h"
#include "acpi_cpu_pstate.h"

#define AMD_APMI_HWPSTATE		0x80

#define AMD_MSR_PSTATE_CSR_MASK		0x7ULL
#define AMD1X_MSR_PSTATE_CTL		0xc0010062
#define AMD1X_MSR_PSTATE_ST		0xc0010063

#define AMD_MSR_PSTATE_EN		0x8000000000000000ULL

#define AMD10_MSR_PSTATE_START		0xc0010064
#define AMD10_MSR_PSTATE_COUNT		5

#define AMD0F_PST_CTL_FID(cval)		(((cval) >> 0)  & 0x3f)
#define AMD0F_PST_CTL_VID(cval)		(((cval) >> 6)  & 0x1f)
#define AMD0F_PST_CTL_VST(cval)		(((cval) >> 11) & 0x7f)
#define AMD0F_PST_CTL_MVS(cval)		(((cval) >> 18) & 0x3)
#define AMD0F_PST_CTL_PLLTIME(cval)	(((cval) >> 20) & 0x7f)
#define AMD0F_PST_CTL_RVO(cval)		(((cval) >> 28) & 0x3)
#define AMD0F_PST_CTL_IRT(cval)		(((cval) >> 30) & 0x3)

#define AMD0F_PST_ST_FID(sval)		(((sval) >> 0) & 0x3f)
#define AMD0F_PST_ST_VID(sval)		(((sval) >> 6) & 0x3f)

#define INTEL_MSR_MISC_ENABLE		0x1a0
#define INTEL_MSR_MISC_EST_EN		0x10000ULL

#define INTEL_MSR_PERF_STATUS		0x198
#define INTEL_MSR_PERF_CTL		0x199
#define INTEL_MSR_PERF_MASK		0xffffULL

static const struct acpi_pst_md *
		acpi_pst_amd_probe(void);
static int	acpi_pst_amd_check_csr(const struct acpi_pst_res *,
		    const struct acpi_pst_res *);
static int	acpi_pst_amd1x_check_pstates(const struct acpi_pstate *, int,
		    uint32_t, uint32_t);
static int	acpi_pst_amd10_check_pstates(const struct acpi_pstate *, int);
static int	acpi_pst_amd0f_check_pstates(const struct acpi_pstate *, int);
static int	acpi_pst_amd_init(const struct acpi_pst_res *,
		    const struct acpi_pst_res *);
static int	acpi_pst_amd1x_set_pstate(const struct acpi_pst_res *,
		    const struct acpi_pst_res *, const struct acpi_pstate *);
static int	acpi_pst_amd0f_set_pstate(const struct acpi_pst_res *,
		    const struct acpi_pst_res *, const struct acpi_pstate *);
static const struct acpi_pstate *
		acpi_pst_amd1x_get_pstate(const struct acpi_pst_res *,
		    const struct acpi_pstate *, int);
static const struct acpi_pstate *
		acpi_pst_amd0f_get_pstate(const struct acpi_pst_res *,
		    const struct acpi_pstate *, int);

static const struct acpi_pst_md *
		acpi_pst_intel_probe(void);
static int	acpi_pst_intel_check_csr(const struct acpi_pst_res *,
		    const struct acpi_pst_res *);
static int	acpi_pst_intel_check_pstates(const struct acpi_pstate *, int);
static int	acpi_pst_intel_init(const struct acpi_pst_res *,
		    const struct acpi_pst_res *);
static int	acpi_pst_intel_set_pstate(const struct acpi_pst_res *,
		    const struct acpi_pst_res *, const struct acpi_pstate *);
static const struct acpi_pstate *
		acpi_pst_intel_get_pstate(const struct acpi_pst_res *,
		    const struct acpi_pstate *, int);

static int	acpi_pst_md_gas_asz(const ACPI_GENERIC_ADDRESS *);
static int	acpi_pst_md_gas_verify(const ACPI_GENERIC_ADDRESS *);
static uint32_t	acpi_pst_md_res_read(const struct acpi_pst_res *);
static void	acpi_pst_md_res_write(const struct acpi_pst_res *, uint32_t);

static const struct acpi_pst_md	acpi_pst_amd10 = {
	.pmd_check_csr		= acpi_pst_amd_check_csr,
	.pmd_check_pstates	= acpi_pst_amd10_check_pstates,
	.pmd_init		= acpi_pst_amd_init,
	.pmd_set_pstate		= acpi_pst_amd1x_set_pstate,
	.pmd_get_pstate		= acpi_pst_amd1x_get_pstate
};

static const struct acpi_pst_md	acpi_pst_amd0f = {
	.pmd_check_csr		= acpi_pst_amd_check_csr,
	.pmd_check_pstates	= acpi_pst_amd0f_check_pstates,
	.pmd_init		= acpi_pst_amd_init,
	.pmd_set_pstate		= acpi_pst_amd0f_set_pstate,
	.pmd_get_pstate		= acpi_pst_amd0f_get_pstate
};

static const struct acpi_pst_md acpi_pst_intel = {
	.pmd_check_csr		= acpi_pst_intel_check_csr,
	.pmd_check_pstates	= acpi_pst_intel_check_pstates,
	.pmd_init		= acpi_pst_intel_init,
	.pmd_set_pstate		= acpi_pst_intel_set_pstate,
	.pmd_get_pstate		= acpi_pst_intel_get_pstate
};

static int acpi_pst_stringent_check = 1;
TUNABLE_INT("hw.acpi.cpu.pstate.strigent_check", &acpi_pst_stringent_check);

const struct acpi_pst_md *
acpi_pst_md_probe(void)
{
	if (cpu_vendor_id == CPU_VENDOR_AMD)
		return acpi_pst_amd_probe();
	else if (cpu_vendor_id == CPU_VENDOR_INTEL)
		return acpi_pst_intel_probe();
	return NULL;
}

static const struct acpi_pst_md *
acpi_pst_amd_probe(void)
{
	uint32_t regs[4];

	/* Only Family >= 0fh has P-State support */
	if (CPUID_TO_FAMILY(cpu_id) < 0xf)
		return NULL;

	/* Check whether APMI exists */
	if (cpu_exthigh < 0x80000007)
		return NULL;

	/* Fetch APMI */
	do_cpuid(0x80000007, regs);

	if (CPUID_TO_FAMILY(cpu_id) == 0xf) {		/* Family 0fh */
		if ((regs[3] & 0x06) == 0x06)
			return &acpi_pst_amd0f;
	} else if (CPUID_TO_FAMILY(cpu_id) >= 0x10) {	/* Family >= 10h */
		if (regs[3] & 0x80)
			return &acpi_pst_amd10;
	}
	return NULL;
}

static int
acpi_pst_amd_check_csr(const struct acpi_pst_res *ctrl,
		       const struct acpi_pst_res *status)
{
	if (ctrl->pr_gas.SpaceId != ACPI_ADR_SPACE_FIXED_HARDWARE) {
		kprintf("cpu%d: Invalid P-State control register\n", mycpuid);
		return EINVAL;
	}
	if (status->pr_gas.SpaceId != ACPI_ADR_SPACE_FIXED_HARDWARE) {
		kprintf("cpu%d: Invalid P-State status register\n", mycpuid);
		return EINVAL;
	}
	return 0;
}

static int
acpi_pst_amd1x_check_pstates(const struct acpi_pstate *pstates, int npstates,
			     uint32_t msr_start, uint32_t msr_end)
{
	int i;

	/*
	 * Make sure that related MSR P-State registers are enabled.
	 *
	 * NOTE:
	 * We don't check status register value here;
	 * it will not be used.
	 */
	for (i = 0; i < npstates; ++i) {
		uint64_t pstate;
		uint32_t msr;

		msr = msr_start +
		      (pstates[i].st_cval & AMD_MSR_PSTATE_CSR_MASK);
		if (msr >= msr_end) {
			kprintf("cpu%d: MSR P-State register %#08x "
				"does not exist\n", mycpuid, msr);
			return EINVAL;
		}

		pstate = rdmsr(msr);
		if ((pstate & AMD_MSR_PSTATE_EN) == 0) {
			kprintf("cpu%d: MSR P-State register %#08x "
				"is not enabled\n", mycpuid, msr);
			return EINVAL;
		}
	}
	return 0;
}

static int
acpi_pst_amd10_check_pstates(const struct acpi_pstate *pstates, int npstates)
{
	/* Only P0-P4 are supported */
	if (npstates > AMD10_MSR_PSTATE_COUNT) {
		kprintf("cpu%d: only P0-P4 is allowed\n", mycpuid);
		return EINVAL;
	}

	return acpi_pst_amd1x_check_pstates(pstates, npstates,
			AMD10_MSR_PSTATE_START,
			AMD10_MSR_PSTATE_START + AMD10_MSR_PSTATE_COUNT);
}

static int
acpi_pst_amd1x_set_pstate(const struct acpi_pst_res *ctrl __unused,
			  const struct acpi_pst_res *status __unused,
			  const struct acpi_pstate *pstate)
{
	uint64_t cval;

	cval = pstate->st_cval & AMD_MSR_PSTATE_CSR_MASK;
	wrmsr(AMD1X_MSR_PSTATE_CTL, cval);

	/*
	 * Don't check AMD1X_MSR_PSTATE_ST here, since it is
	 * affected by various P-State limits.
	 *
	 * For details:
	 * AMD Family 10h Processor BKDG Rev 3.20 (#31116)
	 * 2.4.2.4 P-state Transition Behavior
	 */

	return 0;
}

static const struct acpi_pstate *
acpi_pst_amd1x_get_pstate(const struct acpi_pst_res *status __unused,
			  const struct acpi_pstate *pstates, int npstates)
{
	uint64_t sval;
	int i;

	sval = rdmsr(AMD1X_MSR_PSTATE_ST) & AMD_MSR_PSTATE_CSR_MASK;
	for (i = 0; i < npstates; ++i) {
		if ((pstates[i].st_sval & AMD_MSR_PSTATE_CSR_MASK) == sval)
			return &pstates[i];
	}
	return NULL;
}

static int
acpi_pst_amd0f_check_pstates(const struct acpi_pstate *pstates, int npstates)
{
	struct amd0f_fidvid fv_max, fv_min;
	int i;

	amd0f_fidvid_limit(&fv_min, &fv_max);

	if (fv_min.fid == fv_max.fid && fv_min.vid == fv_max.vid) {
		kprintf("cpu%d: only one P-State is supported\n", mycpuid);
		if (acpi_pst_stringent_check)
			return EOPNOTSUPP;
	}

	for (i = 0; i < npstates; ++i) {
		const struct acpi_pstate *p = &pstates[i];
		uint32_t fid, vid, mvs, rvo;
		int mvs_mv, rvo_mv;

		fid = AMD0F_PST_CTL_FID(p->st_cval);
		vid = AMD0F_PST_CTL_VID(p->st_cval);

		if (i == 0) {
			if (vid != fv_max.vid) {
				kprintf("cpu%d: max VID mismatch "
					"real %u, lim %d\n", mycpuid,
					vid, fv_max.vid);
			}
			if (fid != fv_max.fid) {
				kprintf("cpu%d: max FID mismatch "
					"real %u, lim %d\n", mycpuid,
					fid, fv_max.fid);
			}
		} else if (i == npstates - 1) {
			if (vid != fv_min.vid) {
				kprintf("cpu%d: min VID mismatch "
					"real %u, lim %d\n", mycpuid,
					vid, fv_min.vid);
			}
			if (fid != fv_min.fid) {
				kprintf("cpu%d: min FID mismatch "
					"real %u, lim %d\n", mycpuid,
					fid, fv_min.fid);
			}
		} else {
			if (fid >= fv_max.fid || fid < (fv_min.fid + 0x8)) {
				kprintf("cpu%d: Invalid FID %#x, "
					"out [%#x, %#x]\n", mycpuid, fid,
					fv_min.fid + 0x8, fv_max.fid);
				if (acpi_pst_stringent_check)
					return EINVAL;
			}
			if (vid < fv_max.vid || vid > fv_min.vid) {
				kprintf("cpu%d: Invalid VID %#x, "
					"in [%#x, %#x]\n", mycpuid, vid,
					fv_max.vid, fv_min.vid);
				if (acpi_pst_stringent_check)
					return EINVAL;
			}
		}

		mvs = AMD0F_PST_CTL_MVS(p->st_cval);
		rvo = AMD0F_PST_CTL_RVO(p->st_cval);

		/* Only 0 is allowed, i.e. 25mV stepping */
		if (mvs != 0) {
			kprintf("cpu%d: Invalid MVS %#x\n", mycpuid, mvs);
			return EINVAL;
		}

		/* -> mV */
		mvs_mv = 25 * (1 << mvs);
		rvo_mv = 25 * rvo;
		if (rvo_mv % mvs_mv != 0) {
			kprintf("cpu%d: Invalid MVS/RVO (%#x/%#x)\n",
				mycpuid, mvs, rvo);
			return EINVAL;
		}
	}
	return 0;
}

static int
acpi_pst_amd0f_set_pstate(const struct acpi_pst_res *ctrl __unused,
			  const struct acpi_pst_res *status __unused,
			  const struct acpi_pstate *pstate)
{
	struct amd0f_fidvid fv;
	struct amd0f_xsit xsit;

	fv.fid = AMD0F_PST_CTL_FID(pstate->st_cval);
	fv.vid = AMD0F_PST_CTL_VID(pstate->st_cval);

	xsit.rvo = AMD0F_PST_CTL_RVO(pstate->st_cval);
	xsit.mvs = AMD0F_PST_CTL_MVS(pstate->st_cval);
	xsit.vst = AMD0F_PST_CTL_VST(pstate->st_cval);
	xsit.pll_time = AMD0F_PST_CTL_PLLTIME(pstate->st_cval);
	xsit.irt = AMD0F_PST_CTL_IRT(pstate->st_cval);

	return amd0f_set_fidvid(&fv, &xsit);
}

static const struct acpi_pstate *
acpi_pst_amd0f_get_pstate(const struct acpi_pst_res *status __unused,
			  const struct acpi_pstate *pstates, int npstates)
{
	struct amd0f_fidvid fv;
	int error, i;

	error = amd0f_get_fidvid(&fv);
	if (error)
		return NULL;

	for (i = 0; i < npstates; ++i) {
		const struct acpi_pstate *p = &pstates[i];

		if (fv.fid == AMD0F_PST_ST_FID(p->st_sval) &&
		    fv.vid == AMD0F_PST_ST_VID(p->st_sval))
			return p;
	}
	return NULL;
}

static int
acpi_pst_amd_init(const struct acpi_pst_res *ctrl __unused,
		  const struct acpi_pst_res *status __unused)
{
	return 0;
}

static const struct acpi_pst_md *
acpi_pst_intel_probe(void)
{
	uint32_t family;

	if ((cpu_feature2 & CPUID2_EST) == 0)
		return NULL;

	family = cpu_id & 0xf00;
	if (family != 0xf00 && family != 0x600)
		return NULL;
	return &acpi_pst_intel;
}

static int
acpi_pst_intel_check_csr(const struct acpi_pst_res *ctrl,
			 const struct acpi_pst_res *status)
{
	int error;

	if (ctrl->pr_gas.SpaceId != status->pr_gas.SpaceId) {
		kprintf("cpu%d: P-State control(%d)/status(%d) registers have "
			"different SpaceId", mycpuid,
			ctrl->pr_gas.SpaceId, status->pr_gas.SpaceId);
		return EINVAL;
	}

	switch (ctrl->pr_gas.SpaceId) {
	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		if (ctrl->pr_res != NULL || status->pr_res != NULL) {
			/* XXX should panic() */
			kprintf("cpu%d: Allocated resource for fixed hardware "
				"registers\n", mycpuid);
			return EINVAL;
		}
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:
		if (ctrl->pr_res == NULL) {
			kprintf("cpu%d: ioport allocation failed for control "
				"register\n", mycpuid);
			return ENXIO;
		}
		error = acpi_pst_md_gas_verify(&ctrl->pr_gas);
		if (error) {
			kprintf("cpu%d: Invalid control register GAS\n",
				mycpuid);
			return error;
		}

		if (status->pr_res == NULL) {
			kprintf("cpu%d: ioport allocation failed for status "
				"register\n", mycpuid);
			return ENXIO;
		}
		error = acpi_pst_md_gas_verify(&status->pr_gas);
		if (error) {
			kprintf("cpu%d: Invalid status register GAS\n",
				mycpuid);
			return error;
		}
		break;

	default:
		kprintf("cpu%d: Invalid P-State control/status register "
			"SpaceId %d\n", mycpuid, ctrl->pr_gas.SpaceId);
		return EOPNOTSUPP;
	}
	return 0;
}

static int
acpi_pst_intel_check_pstates(const struct acpi_pstate *pstates __unused,
			     int npstates __unused)
{
	return 0;
}

static int
acpi_pst_intel_init(const struct acpi_pst_res *ctrl __unused,
		    const struct acpi_pst_res *status __unused)
{
	uint32_t family, model;
	uint64_t misc_enable;

	family = cpu_id & 0xf00;
	if (family == 0xf00) {
		/* EST enable bit is reserved in INTEL_MSR_MISC_ENABLE */
		return 0;
	}
	KKASSERT(family == 0x600);

	model = ((cpu_id & 0xf0000) >> 12) | ((cpu_id & 0xf0) >> 4);
	if (model < 0xd) {
		/* EST enable bit is reserved in INTEL_MSR_MISC_ENABLE */
		return 0;
	}

	misc_enable = rdmsr(INTEL_MSR_MISC_ENABLE);
	if ((misc_enable & INTEL_MSR_MISC_EST_EN) == 0) {
		misc_enable |= INTEL_MSR_MISC_EST_EN;
		wrmsr(INTEL_MSR_MISC_ENABLE, misc_enable);

		misc_enable = rdmsr(INTEL_MSR_MISC_ENABLE);
		if ((misc_enable & INTEL_MSR_MISC_EST_EN) == 0) {
			kprintf("cpu%d: Can't enable EST\n", mycpuid);
			return EIO;
		}
	}
	return 0;
}

static int
acpi_pst_intel_set_pstate(const struct acpi_pst_res *ctrl,
			  const struct acpi_pst_res *status __unused,
			  const struct acpi_pstate *pstate)
{
	if (ctrl->pr_res != NULL) {
		acpi_pst_md_res_write(ctrl, pstate->st_cval);
	} else {
		uint64_t ctl;

		ctl = rdmsr(INTEL_MSR_PERF_CTL);
		ctl &= ~INTEL_MSR_PERF_MASK;
		ctl |= (pstate->st_cval & INTEL_MSR_PERF_MASK);
		wrmsr(INTEL_MSR_PERF_CTL, ctl);
	}
	return 0;
}

static const struct acpi_pstate *
acpi_pst_intel_get_pstate(const struct acpi_pst_res *status,
			  const struct acpi_pstate *pstates, int npstates)
{
	int i;

	if (status->pr_res != NULL) {
		uint32_t st;

		st = acpi_pst_md_res_read(status);
		for (i = 0; i < npstates; ++i) {
			if (pstates[i].st_sval == st)
				return &pstates[i];
		}
	} else {
		uint64_t sval;

		sval = rdmsr(INTEL_MSR_PERF_STATUS) & INTEL_MSR_PERF_MASK;
		for (i = 0; i < npstates; ++i) {
			if ((pstates[i].st_sval & INTEL_MSR_PERF_MASK) == sval)
				return &pstates[i];
		}
	}
	return NULL;
}

static int
acpi_pst_md_gas_asz(const ACPI_GENERIC_ADDRESS *gas)
{
	int asz;

	if (gas->AccessWidth != 0)
		asz = gas->AccessWidth;
	else
		asz = gas->BitWidth / NBBY;
	switch (asz) {
	case 1:
	case 2:
	case 4:
		break;
	default:
		asz = 0;
		break;
	}
	return asz;
}

static int
acpi_pst_md_gas_verify(const ACPI_GENERIC_ADDRESS *gas)
{
	int reg, end, asz;

	if (gas->BitOffset % NBBY != 0)
		return EINVAL;

	end = gas->BitWidth / NBBY;
	reg = gas->BitOffset / NBBY;

	if (reg >= end)
		return EINVAL;

	asz = acpi_pst_md_gas_asz(gas);
	if (asz == 0)
		return EINVAL;

	if (reg + asz > end)
		return EINVAL;
	return 0;
}

static uint32_t
acpi_pst_md_res_read(const struct acpi_pst_res *res)
{
	int asz, reg;

	KKASSERT(res->pr_res != NULL);
	asz = acpi_pst_md_gas_asz(&res->pr_gas);
	reg = res->pr_gas.BitOffset / NBBY;

	switch (asz) {
	case 1:
		return bus_space_read_1(res->pr_bt, res->pr_bh, reg);
	case 2:
		return bus_space_read_2(res->pr_bt, res->pr_bh, reg);
	case 4:
		return bus_space_read_4(res->pr_bt, res->pr_bh, reg);
	}
	panic("unsupported access width %d", asz);

	/* NEVER REACHED */
	return 0;
}

static void
acpi_pst_md_res_write(const struct acpi_pst_res *res, uint32_t val)
{
	int asz, reg;

	KKASSERT(res->pr_res != NULL);
	asz = acpi_pst_md_gas_asz(&res->pr_gas);
	reg = res->pr_gas.BitOffset / NBBY;

	switch (asz) {
	case 1:
		bus_space_write_1(res->pr_bt, res->pr_bh, reg, val);
		break;
	case 2:
		bus_space_write_2(res->pr_bt, res->pr_bh, reg, val);
		break;
	case 4:
		bus_space_write_4(res->pr_bt, res->pr_bh, reg, val);
		break;
	default:
		panic("unsupported access width %d", asz);
	}
}
