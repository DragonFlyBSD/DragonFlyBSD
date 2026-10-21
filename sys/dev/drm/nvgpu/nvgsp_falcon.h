/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Falcon engine abstraction.
 *
 * A "Falcon" is an NVIDIA on-die microcontroller. Different GPU engines
 * (GSP, SEC2, NVDEC, NVENC) each have their own Falcon instance at a
 * distinct BAR0 offset. From Turing onward some Falcons can run RISC-V
 * code in place of the legacy Falcon ISA.
 *
 * This module exposes the lowest-level Falcon operations: register R/W
 * through three per-engine bases, IMEM/DMEM PIO load, reset wait, start,
 * halt wait. Higher-level concepts (ucode signing, ACR, GSP boot) build
 * on top of these.
 *
 * Source references:
 *   linux/drivers/gpu/drm/nouveau/nvkm/falcon/base.c   (algorithm)
 *   linux/drivers/gpu/drm/nouveau/nvkm/falcon/gm200.c  (Maxwell+ impl)
 *   linux/drivers/gpu/drm/nouveau/nvkm/falcon/tu102.c  (Turing extras)
 *   open-rm/src/nvidia/src/kernel/gpu/falcon/arch/turing/
 *       kernel_falcon_tu102.c                          (TU102 sequences)
 */

#ifndef _NVGSP_FALCON_H_
#define _NVGSP_FALCON_H_

#include "nvgsp_priv.h"
#include "nvgsp_reg_falcon.h"

struct nvgsp_falcon {
	struct nvgsp_state	*sc;
	const char		*name;

	/* BAR0 offsets for the three register blocks belonging to this
	 * Falcon engine. addr2 (the RISC-V control block) and fbif may be
	 * 0 for engines that don't expose them. */
	uint32_t		addr;
	uint32_t		addr2;
	uint32_t		fbif;
};

void	nvgsp_falcon_init_core(struct nvgsp_falcon *flcn, struct nvgsp_state *sc,
	    const char *name, uint32_t addr, uint32_t addr2, uint32_t fbif);

static __inline uint32_t
nvgsp_falcon_rd32(struct nvgsp_falcon *flcn, uint32_t off)
{
	return (nvgsp_rd32(flcn->sc, flcn->addr + off));
}

static __inline void
nvgsp_falcon_wr32(struct nvgsp_falcon *flcn, uint32_t off, uint32_t val)
{
	nvgsp_wr32(flcn->sc, flcn->addr + off, val);
}

static __inline void
nvgsp_falcon_mask(struct nvgsp_falcon *flcn, uint32_t off,
    uint32_t mask, uint32_t val)
{
	uint32_t v = nvgsp_falcon_rd32(flcn, off);

	nvgsp_falcon_wr32(flcn, off, (v & ~mask) | (val & mask));
}

static __inline void
nvgsp_falcon_fbif_wr32(struct nvgsp_falcon *flcn, uint32_t off, uint32_t val)
{
	nvgsp_wr32(flcn->sc, flcn->fbif + off, val);
}

static __inline uint32_t
nvgsp_falcon_fbif_rd32(struct nvgsp_falcon *flcn, uint32_t off)
{
	return (nvgsp_rd32(flcn->sc, flcn->fbif + off));
}

int	nvgsp_falcon_wait_for_scrub(struct nvgsp_falcon *flcn, int timeout_us);
int	nvgsp_falcon_wait_for_halt(struct nvgsp_falcon *flcn, int timeout_us);

void	nvgsp_falcon_set_bootvec(struct nvgsp_falcon *flcn, uint32_t bootvec);
void	nvgsp_falcon_start(struct nvgsp_falcon *flcn);

void	nvgsp_falcon_disable_ctx_req(struct nvgsp_falcon *flcn);

/*
 * PIO-load instruction memory.
 *
 * imem_offset must be 256-byte (block) aligned. size must be 4-byte
 * aligned. start_tag is written into IMEMT for the first block and then
 * auto-incremented per block. secure=true sets IMEMC.SECURE for HS code.
 */
int	nvgsp_falcon_load_imem(struct nvgsp_falcon *flcn, const void *data,
	    uint32_t imem_offset, uint32_t size, uint16_t start_tag,
	    uint8_t port, bool secure);

/*
 * PIO-load data memory. dmem_offset must be 4-byte aligned; size must be
 * 4-byte aligned.
 */
int	nvgsp_falcon_load_dmem(struct nvgsp_falcon *flcn, const void *data,
	    uint32_t dmem_offset, uint32_t size, uint8_t port);

/*
 * Reset and re-enable the Falcon engine (NV_PFALCON_FALCON_ENGINE @
 * base+0x3c0, bit 0 = ENGINE_RESET). After the pulse, waits for IMEM/
 * DMEM scrubbing to complete. Must be the first thing done on a
 * Falcon whose post-OVMF state is unknown -- on TU102 GSP-Falcon a
 * naive PRI read can hang the PRI hub before the engine is reset.
 * Returns 0 on success, nonzero if the scrub-wait timed out.
 *
 * Reference: nouveau gp102_flcn_reset_eng (falcon/gp102.c).
 */
int	nvgsp_falcon_reset_eng(struct nvgsp_falcon *flcn);

#endif /* _NVGSP_FALCON_H_ */
