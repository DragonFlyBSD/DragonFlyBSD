/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Falcon engine low-level operations. See nvgsp_falcon.h for the API.
 *
 * Implementation modelled after nouveau's nvkm/falcon/base.c and
 * gm200.c (which together cover Turing); register-level details
 * cross-checked against open-rm 570.144 kernel_falcon_tu102.c.
 */

#include "nvgsp_falcon.h"

#include <sys/libkern.h>		/* memcpy */

#define NVGSP_FALCON_WAIT_STEP_US	10

void
nvgsp_falcon_init_core(struct nvgsp_falcon *flcn, struct nvgsp_state *sc,
    const char *name, uint32_t addr, uint32_t addr2, uint32_t fbif)
{
	flcn->sc = sc;
	flcn->name = name;
	flcn->addr = addr;
	flcn->addr2 = addr2;
	flcn->fbif = fbif;
}

int
nvgsp_falcon_reset_eng(struct nvgsp_falcon *flcn)
{
	uint32_t v;

	/*
	 * Reset register lives at base+0x3c0 (NV_PFALCON_FALCON_ENGINE).
	 * Bit 0 is the engine reset / power-up control. We assert it, hold
	 * briefly, then deassert -- this brings the engine out of the
	 * post-OVMF unknown state and gates power on. After deassert the
	 * engine begins scrubbing IMEM/DMEM; wait for it to finish.
	 * Mirrors nouveau gp102_flcn_reset_eng.
	 */
	(void)v;
	/*
	 * Write-only: avoid an initial read of 0x3c0 in case it too is
	 * unsafe in the unknown state. Bit 0 is the only meaningful bit;
	 * other bits read as 0 on TU102 and writing 0 to them is harmless.
	 */
	nvgsp_falcon_wr32(flcn, 0x3c0, 0x1u);
	DELAY(10);
	nvgsp_falcon_wr32(flcn, 0x3c0, 0x0u);

	return (nvgsp_falcon_wait_for_scrub(flcn, 100000)); /* 100 ms */
}

int
nvgsp_falcon_wait_for_scrub(struct nvgsp_falcon *flcn, int timeout_us)
{
	int waited = 0;

	for (;;) {
		uint32_t v = nvgsp_falcon_rd32(flcn, NVGSP_FLCN_DMACTL);

		if ((v & NVGSP_FLCN_DMACTL_SCRUBBING_MASK) == 0)
			return (0);
		if (waited >= timeout_us)
			return (ETIMEDOUT);
		DELAY(NVGSP_FALCON_WAIT_STEP_US);
		waited += NVGSP_FALCON_WAIT_STEP_US;
	}
}

int
nvgsp_falcon_wait_for_halt(struct nvgsp_falcon *flcn, int timeout_us)
{
	int waited = 0;

	for (;;) {
		uint32_t v = nvgsp_falcon_rd32(flcn, NVGSP_FLCN_CPUCTL);

		if (v & NVGSP_FLCN_CPUCTL_HALTED)
			return (0);
		if (waited >= timeout_us)
			return (ETIMEDOUT);
		DELAY(NVGSP_FALCON_WAIT_STEP_US);
		waited += NVGSP_FALCON_WAIT_STEP_US;
	}
}

void
nvgsp_falcon_set_bootvec(struct nvgsp_falcon *flcn, uint32_t bootvec)
{
	nvgsp_falcon_wr32(flcn, NVGSP_FLCN_BOOTVEC, bootvec);
}

/*
 * Start the Falcon CPU. Turing offers two control registers: CPUCTL
 * (privileged) and CPUCTL_ALIAS (NS-accessible). When ALIAS_EN is set in
 * CPUCTL, we must use the alias.
 */
void
nvgsp_falcon_start(struct nvgsp_falcon *flcn)
{
	uint32_t cpuctl = nvgsp_falcon_rd32(flcn, NVGSP_FLCN_CPUCTL);

	if (cpuctl & NVGSP_FLCN_CPUCTL_ALIAS_EN) {
		nvgsp_falcon_wr32(flcn, NVGSP_FLCN_CPUCTL_ALIAS,
		    NVGSP_FLCN_CPUCTL_ALIAS_STARTCPU);
	} else {
		nvgsp_falcon_wr32(flcn, NVGSP_FLCN_CPUCTL,
		    NVGSP_FLCN_CPUCTL_STARTCPU);
	}
}

void
nvgsp_falcon_disable_ctx_req(struct nvgsp_falcon *flcn)
{
	uint32_t v;

	if (flcn->fbif == 0)
		return;
	v = nvgsp_falcon_fbif_rd32(flcn, NVGSP_FBIF_CTL);
	v |= NVGSP_FBIF_CTL_ALLOW_PHYS_NO_CTX;
	nvgsp_falcon_fbif_wr32(flcn, NVGSP_FBIF_CTL, v);

	/*
	 * Per open-rm kflcnDisableCtxReq_TU102: BOTH FBIF_CTL and DMACTL
	 * must be updated. Clearing DMACTL clears REQUIRE_CTX (bit 0)
	 * which otherwise blocks DMA from sysmem when no context is bound.
	 */
	nvgsp_falcon_wr32(flcn, NVGSP_FLCN_DMACTL, 0);
}

/*
 * IMEM load helper. Loads one block (up to NVGSP_FLCN_IMEM_BLKSIZE bytes)
 * via a single IMEMT + N×IMEMD sequence. The starting IMEMC has already
 * been set by the caller; AINCW makes each IMEMD write advance OFFS.
 */
static void
nvgsp_falcon_write_imem_block(struct nvgsp_falcon *flcn, uint8_t port,
    const uint8_t *src, uint32_t bytes, uint16_t tag)
{
	uint32_t i;

	nvgsp_falcon_wr32(flcn, NVGSP_FLCN_IMEMT(port), tag);
	for (i = 0; i + 4 <= bytes; i += 4) {
		uint32_t w = (uint32_t)src[i + 0] |
		    ((uint32_t)src[i + 1] <<  8) |
		    ((uint32_t)src[i + 2] << 16) |
		    ((uint32_t)src[i + 3] << 24);
		nvgsp_falcon_wr32(flcn, NVGSP_FLCN_IMEMD(port), w);
	}
}

int
nvgsp_falcon_load_imem(struct nvgsp_falcon *flcn, const void *data,
    uint32_t imem_offset, uint32_t size, uint16_t start_tag,
    uint8_t port, bool secure)
{
	const uint8_t *src = data;
	uint32_t imemc, remaining, block_bytes;
	uint16_t tag = start_tag;

	if ((imem_offset & (NVGSP_FLCN_IMEM_BLKSIZE - 1)) != 0)
		return (EINVAL);
	if ((size & (NVGSP_FLCN_ACCESS_ALIGN - 1)) != 0)
		return (EINVAL);

	/*
	 * IMEMC layout: BLK in bits 15:8, OFFS (word index) in bits 7:2.
	 * Because imem_offset is block-aligned (low 8 bits zero) the byte
	 * value can be ORed directly: bits 15:8 carry the block, bits 7:2
	 * carry zero offset. Enable AINCW so each IMEMD write advances
	 * the address by 4 bytes.
	 */
	imemc = (imem_offset & 0xffffu) | NVGSP_FLCN_IMEMC_AINCW;
	if (secure)
		imemc |= NVGSP_FLCN_IMEMC_SECURE;
	nvgsp_falcon_wr32(flcn, NVGSP_FLCN_IMEMC(port), imemc);

	remaining = size;
	while (remaining > 0) {
		block_bytes = remaining > NVGSP_FLCN_IMEM_BLKSIZE ?
		    NVGSP_FLCN_IMEM_BLKSIZE : remaining;
		nvgsp_falcon_write_imem_block(flcn, port, src, block_bytes, tag);
		src += block_bytes;
		remaining -= block_bytes;
		tag++;
	}

	return (0);
}

int
nvgsp_falcon_load_dmem(struct nvgsp_falcon *flcn, const void *data,
    uint32_t dmem_offset, uint32_t size, uint8_t port)
{
	const uint8_t *src = data;
	uint32_t dmemc, i;

	if ((dmem_offset & (NVGSP_FLCN_ACCESS_ALIGN - 1)) != 0)
		return (EINVAL);
	if ((size & (NVGSP_FLCN_ACCESS_ALIGN - 1)) != 0)
		return (EINVAL);

	/*
	 * DMEM has no tags; just set up the starting block/offset with
	 * AINCW and stream IMEMD writes. dmem_offset uses the same
	 * encoding as IMEMC for BLK/OFFS.
	 */
	dmemc = (dmem_offset & 0xffffu) | NVGSP_FLCN_DMEMC_AINCW;
	nvgsp_falcon_wr32(flcn, NVGSP_FLCN_DMEMC(port), dmemc);

	for (i = 0; i + 4 <= size; i += 4) {
		uint32_t w = (uint32_t)src[i + 0] |
		    ((uint32_t)src[i + 1] <<  8) |
		    ((uint32_t)src[i + 2] << 16) |
		    ((uint32_t)src[i + 3] << 24);
		nvgsp_falcon_wr32(flcn, NVGSP_FLCN_DMEMD(port), w);
	}

	return (0);
}


static MALLOC_DEFINE(M_NVGSP_FALCON, "nvgsp_falcon", "nvgsp Falcon state");

int
nvgsp_falcon_init_state(struct nvgsp_state *gsp)
{
	struct nvgsp_falcon *sec2;
	struct nvgsp_falcon *gsp_falcon;
	uint32_t hwcfg, hwcfg2;
	uint32_t imem_size_bytes;
	int error;

	sec2 = kmalloc(sizeof(*sec2), M_NVGSP_FALCON, M_WAITOK | M_ZERO);
	nvgsp_falcon_init_core(sec2, gsp, "sec2", gsp->chip->sec2_base, 0,
	    gsp->chip->sec2_fbif);
	gsp->sec2 = sec2;

	hwcfg = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_HWCFG);
	hwcfg2 = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_HWCFG2);
	imem_size_bytes = (hwcfg & NVGSP_FLCN_HWCFG_IMEM_SIZE_MASK) *
	    NVGSP_FLCN_IMEM_BLKSIZE;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "sec2: HWCFG=0x%08x HWCFG2=0x%08x imem=%u bytes riscv=%s\n",
	    hwcfg, hwcfg2, imem_size_bytes,
	    (hwcfg2 & NVGSP_FLCN_HWCFG2_RISCV) ? "yes" : "no");

	error = nvgsp_falcon_wait_for_scrub(sec2, 100000);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "sec2: scrub wait failed (%d), DMACTL=0x%08x\n",
		    error, nvgsp_falcon_rd32(sec2, NVGSP_FLCN_DMACTL));
	}

	gsp_falcon = kmalloc(sizeof(*gsp_falcon), M_NVGSP_FALCON,
	    M_WAITOK | M_ZERO);
	nvgsp_falcon_init_core(gsp_falcon, gsp, "gsp",
	    gsp->chip->gsp_base, gsp->chip->gsp_riscv, gsp->chip->gsp_fbif);
	gsp->gsp = gsp_falcon;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp: Falcon handle allocated base=0x%x riscv=0x%x fbif=0x%x\n",
	    gsp->chip->gsp_base, gsp->chip->gsp_riscv, gsp->chip->gsp_fbif);
	return (0);
}

void
nvgsp_falcon_fini_state(struct nvgsp_state *gsp)
{
	if (gsp->gsp != NULL) {
		kfree(gsp->gsp, M_NVGSP_FALCON);
		gsp->gsp = NULL;
	}
	if (gsp->sec2 != NULL) {
		kfree(gsp->sec2, M_NVGSP_FALCON);
		gsp->sec2 = NULL;
	}
}
