/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * VBIOS reading via the BAR0 PROM aperture.
 *
 * The on-card SPI ROM is exposed in BAR0 at offset NV_PROM (0x300000),
 * 1 MiB wide. Clearing bit 0 of the ROM-shadow control register at
 * BAR0 + 0x088050 enables reading; the bit is restored afterwards so
 * the host's option-ROM emulator continues to work.
 *
 * PCI Option ROMs are chained. Each image starts with the standard
 * 0x55 0xAA signature, carries a PCIR "PCI Data Structure" at the
 * pointer in offset 0x18, and (for NVIDIA images) follows that with a
 * private "NPDE" extension at the next 16-byte boundary. NPDE, when
 * present, supplies the actual sub-image length and the last-image flag,
 * overriding the equivalent PCIR fields. See nvgsp_rom.h for the
 * authoritative definitions copied from open-rm.
 *
 * Source references:
 *   linux/drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadowrom.c     (PROM)
 *   linux/drivers/gpu/drm/nouveau/nvkm/subdev/pci/base.c           (rom_shadow)
 *   open-rm/src/nvidia/src/kernel/gpu/gsp/arch/turing/
 *      kernel_gsp_vbios_tu102.c                                     (NPDE chain)
 */

#include "nvgsp_priv.h"

#include <sys/ktr.h>
#include <sys/sysctl.h>
#include "nvgsp_rom.h"

static MALLOC_DEFINE(M_NVGSP_VBIOS, "nvgsp_vbios", "nvgsp VBIOS image cache");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, gsp_bios_stage, 18,
    "gsp bios stage=%u", uint32_t stage);

static void
nvgsp_bios_shadow_rom(struct nvgsp_state *sc, bool enable)
{
	uint32_t v = nvgsp_rd32(sc, NV_PMC_ROM_SHADOW);

	if (enable)
		v |= NV_PMC_ROM_SHADOW_EN;
	else
		v &= ~NV_PMC_ROM_SHADOW_EN;
	nvgsp_wr32(sc, NV_PMC_ROM_SHADOW, v);
}

static void
nvgsp_bios_read_prom(struct nvgsp_state *sc, uint8_t *buf, uint32_t offset,
    uint32_t length)
{
	uint32_t i, word;

	KASSERT((offset & 3) == 0, ("PROM offset not 4-byte aligned"));
	KASSERT((length & 3) == 0, ("PROM length not 4-byte aligned"));

	for (i = 0; i < length; i += 4) {
		word = nvgsp_rd32(sc, NV_PROM + offset + i);
		buf[i + 0] = (uint8_t)(word >>  0);
		buf[i + 1] = (uint8_t)(word >>  8);
		buf[i + 2] = (uint8_t)(word >> 16);
		buf[i + 3] = (uint8_t)(word >> 24);
	}
}

/*
 * Configure the PRAMIN window to expose the GPU's working VBIOS copy in
 * VRAM, then read length bytes from offset 0 of that copy. The window is
 * limited to NV_PRAMIN_SIZE (1 MiB). The previous PBUS_PRAMIN value is
 * restored before returning.
 *
 * Returns 0 on success; otherwise an errno explaining why PRAMIN was
 * unavailable (display block off, aperture pointing elsewhere, etc).
 */
static int
nvgsp_bios_read_pramin(struct nvgsp_state *sc, uint8_t *buf, uint32_t length)
{
	uint32_t vga_cr, dctl, saved_window;
	uint64_t vram_addr;
	uint32_t i, word;
	int error = 0;

	if (length > NV_PRAMIN_SIZE)
		return (ENOMEM);

	/* Bail out if the display engine reports itself disabled. */
	dctl = nvgsp_rd32(sc, NV_PDISP_GENERAL_CTL);
	if (dctl & NV_PDISP_GENERAL_CTL_DISABLED) {
		nvgpu_log(NVGPU_LOG_DEBUG, "PRAMIN: display disabled (0x021c04=0x%x)\n", dctl);
		return (ENODEV);
	}

	/* Discover where the GPU staged the active VBIOS in VRAM. */
	vga_cr = nvgsp_rd32(sc, NV_PDISP_VGA_CR);
	if (!(vga_cr & NV_PDISP_VGA_CR_ENABLED) ||
	    (vga_cr & NV_PDISP_VGA_CR_TARGET_MASK) !=
	    NV_PDISP_VGA_CR_TARGET_VRAM) {
		nvgpu_log(NVGPU_LOG_DEBUG, "PRAMIN: VGA aperture not in VRAM (0x625f04=0x%x)\n",
		    vga_cr);
		return (ENODEV);
	}
	vram_addr = ((uint64_t)(vga_cr & 0xffffff00u)) << 8;
	if (vram_addr == 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "PRAMIN: vga_cr has no staged address (0x625f04=0x%x)\n",
		    vga_cr);
		return (ENODEV);
	}

	/* Point the PRAMIN window at the VBIOS staging area. */
	saved_window = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, (uint32_t)(vram_addr >> 16));

	nvgpu_log(NVGPU_LOG_DEBUG, "PRAMIN: vga_cr=0x%x vram_addr=%#jx window saved=0x%x\n",
	    vga_cr, (uintmax_t)vram_addr, saved_window);

	for (i = 0; i < length; i += 4) {
		word = nvgsp_rd32(sc, NV_PRAMIN + i);
		buf[i + 0] = (uint8_t)(word >>  0);
		buf[i + 1] = (uint8_t)(word >>  8);
		buf[i + 2] = (uint8_t)(word >> 16);
		buf[i + 3] = (uint8_t)(word >> 24);
	}

	/* Restore the PRAMIN window. */
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved_window);

	return (error);
}

/*
 * Parse a single PCI Option ROM image at sc->vbios[offset].
 *
 * On success returns 0 and fills *size with the byte size to advance to
 * reach the next image, *last with the is-last-image flag, *npde with
 * 1 if an NPDE extension was used to override PCIR fields.
 */
static int
nvgsp_bios_parse_image(struct nvgsp_state *sc, uint32_t offset, int idx,
    uint32_t *size, int *last, int *npde_used)
{
	uint16_t pcir_rel, pcir_struct_len, vendor, device;
	uint32_t pcir, class_code, image_bytes, advance_bytes;
	uint8_t  code_type, indicator;
	uint32_t npde_off;

	*npde_used = 0;

	if (offset + 0x20 > sc->vbios_size)
		return (ENOSPC);

	{
		uint16_t sig = nvgsp_le16(&sc->vbios[offset]);

		if (sig != NVGSP_ROM_SIG_STD &&
		    sig != NVGSP_ROM_SIG_NV &&
		    sig != NVGSP_ROM_SIG_NV2) {
			nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS image %d: bad ROM signature 0x%04x at 0x%05x\n",
			    idx, sig, offset);
			return (EIO);
		}
	}

	pcir_rel = nvgsp_le16(&sc->vbios[offset + NVGSP_ROM_HDR_OFF_PCIR_PTR]);
	pcir = offset + pcir_rel;

	if (pcir + 0x18 > sc->vbios_size) {
		nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS image %d: PCIR beyond buffer (off=0x%05x rel=0x%04x)\n",
		    idx, offset, pcir_rel);
		return (EIO);
	}
	{
		uint32_t pcir_sig = nvgsp_le32(&sc->vbios[pcir + NVGSP_PCIR_OFF_SIG]);

		if (pcir_sig != NVGSP_PCIR_SIG_PCIR &&
		    pcir_sig != NVGSP_PCIR_SIG_NPDS &&
		    pcir_sig != NVGSP_PCIR_SIG_RGIS) {
			nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS image %d: bad PCIR signature 0x%08x at 0x%05x\n",
			    idx, pcir_sig, pcir);
			return (EIO);
		}
	}

	pcir_struct_len = nvgsp_le16(&sc->vbios[pcir + NVGSP_PCIR_OFF_STRUCT_LEN]);
	vendor          = nvgsp_le16(&sc->vbios[pcir + NVGSP_PCIR_OFF_VENDOR_ID]);
	device          = nvgsp_le16(&sc->vbios[pcir + NVGSP_PCIR_OFF_DEVICE_ID]);
	class_code      = nvgsp_le24(&sc->vbios[pcir + NVGSP_PCIR_OFF_CLASS_CODE]);
	image_bytes     = (uint32_t)nvgsp_le16(&sc->vbios[pcir + NVGSP_PCIR_OFF_IMAGE_LEN])
	    * NVGSP_ROM_BLOCK_SIZE;
	code_type       = sc->vbios[pcir + NVGSP_PCIR_OFF_CODE_TYPE];
	indicator       = sc->vbios[pcir + NVGSP_PCIR_OFF_INDICATOR];

	advance_bytes = image_bytes;
	*last = (indicator & NVGSP_PCIR_LAST_IMAGE_BIT) ? 1 : 0;

	/*
	 * Look for an NPDE extension at the 16-byte-aligned offset right
	 * after the PCIR. NPDE.subimage_len overrides PCIR.image_len for
	 * the purpose of advancing to the next image; NPDE.last_image (if
	 * within NPDE's length) overrides PCIR's last-image bit.
	 */
	npde_off = (pcir + pcir_struct_len + 0xFu) & ~0xFu;
	if (npde_off + 0x10u <= sc->vbios_size &&
	    nvgsp_le32(&sc->vbios[npde_off + NVGSP_NPDE_OFF_SIG]) == NVGSP_NPDE_SIG) {
		uint16_t rev = nvgsp_le16(&sc->vbios[npde_off + NVGSP_NPDE_OFF_REV]);
		uint16_t npde_len = nvgsp_le16(&sc->vbios[npde_off + NVGSP_NPDE_OFF_LEN]);

		if (rev == NVGSP_NPDE_REV_10 || rev == NVGSP_NPDE_REV_11) {
			uint32_t sub_bytes = (uint32_t)nvgsp_le16(
			    &sc->vbios[npde_off + NVGSP_NPDE_OFF_SUBIMAGE_LEN]) *
			    NVGSP_ROM_BLOCK_SIZE;

			*npde_used = 1;
			advance_bytes = sub_bytes;

			if (NVGSP_NPDE_OFF_LAST_IMAGE + 1u <= npde_len) {
				*last = (sc->vbios[npde_off +
				    NVGSP_NPDE_OFF_LAST_IMAGE] &
				    NVGSP_PCIR_LAST_IMAGE_BIT) ? 1 : 0;
			} else if (sub_bytes < image_bytes) {
				/*
				 * NPDE didn't carry the last-image bit but
				 * sub-image is smaller than full image: by
				 * convention, more images follow.
				 */
				*last = 0;
			}
		}
	}

	nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS image %d at 0x%05x: %u bytes vendor=0x%04x device=0x%04x "
	    "class=0x%06x code_type=0x%02x last=%d%s\n",
	    idx, offset, advance_bytes,
	    vendor, device, class_code, code_type, *last,
	    *npde_used ? " (NPDE)" : "");

	*size = advance_bytes;
	return (0);
}

int
nvgsp_bios_init(struct nvgsp_state *sc)
{
	uint32_t offset, size;
	int idx, last, npde_used, error;

	KTR_LOG(nvgpu_gsp_bios_stage, 0u);
	sc->vbios = kmalloc(NVGSP_VBIOS_MAX_SIZE, M_NVGSP_VBIOS,
	    M_WAITOK | M_ZERO);
	sc->vbios_size = NVGSP_VBIOS_MAX_SIZE;

	nvgsp_bios_shadow_rom(sc, false);
	KTR_LOG(nvgpu_gsp_bios_stage, 1u);
	nvgsp_bios_read_prom(sc, sc->vbios, 0, NVGSP_VBIOS_MAX_SIZE);
	KTR_LOG(nvgpu_gsp_bios_stage, 2u);
	nvgsp_bios_shadow_rom(sc, true);
	KTR_LOG(nvgpu_gsp_bios_stage, 3u);

	/*
	 * Phase 0.2.3d follow-up diagnostics: with OVMF's GOP driver actually
	 * executed (image 1 from romfile), the GPU displays an EFI framebuffer
	 * but legacy VBIOS stitching to VRAM does NOT happen (that's the
	 * legacy x86 image's job, not GOP's). So PRAMIN+BIT is still out.
	 *
	 * Two replacement findings made here:
	 *   1. WPR2_LO/HI (0x1fa824/8) are PLM-locked from PRI -- a kernel
	 *      write does not stick, so we cannot fake WPR2 setup directly.
	 *   2. FALCON_UCODE_DESC_V2 records for FwSec ARE present in raw PROM
	 *      data in the NV-private (code_type 0xE0) extension images. They
	 *      can be located by content scan (4-byte magic pattern) rather
	 *      than by following BIT's broken ucode_table_ptr. Once located,
	 *      the FwSec body can be staged and run on SEC2 like booter_load.
	 */
	{
		uint8_t probe[64];
		(void)nvgsp_bios_read_pramin(sc, probe, sizeof(probe));
	}
	{
		uint32_t orig_lo, orig_hi, after_lo;
		orig_lo = nvgsp_rd32(sc, 0x001fa824);
		orig_hi = nvgsp_rd32(sc, 0x001fa828);
		nvgsp_wr32(sc, 0x001fa824, 0xdeadbe00u);
		after_lo = nvgsp_rd32(sc, 0x001fa824);
		nvgsp_wr32(sc, 0x001fa824, orig_lo);
		nvgpu_log(NVGPU_LOG_DEBUG, "WPR2: lo=0x%08x hi=0x%08x, test-write -> readback=0x%08x %s\n",
		    orig_lo, orig_hi, after_lo,
		    after_lo == 0xdeadbe00u ? "(WRITABLE)" : "(LOCKED)");
	}
	{
		uint32_t a, hits = 0;

		for (a = 0; a + 60 <= sc->vbios_size && hits < 8; a += 4) {
			uint8_t flags = sc->vbios[a + 0];
			uint8_t ver   = sc->vbios[a + 1];
			uint8_t sz_lo = sc->vbios[a + 2];
			uint8_t sz_hi = sc->vbios[a + 3];

			if ((flags & 0x01) == 0) continue;
			if (ver != 2) continue;
			if (sz_lo != 0x3c || sz_hi != 0x00) continue;

			nvgpu_log(NVGPU_LOG_DEBUG, "FwSec desc V2 @ 0x%05x: flags=0x%02x enc=%d "
			    "imem_phys=0x%x imem_load=%u imem_virt=0x%x "
			    "dmem_offset=0x%x dmem_phys=0x%x dmem_load=%u "
			    "intf=0x%x ventry=0x%x\n",
			    a, flags, (flags & 0x04) ? 1 : 0,
			    nvgsp_le32(&sc->vbios[a + 0x14]),
			    nvgsp_le32(&sc->vbios[a + 0x18]),
			    nvgsp_le32(&sc->vbios[a + 0x1c]),
			    nvgsp_le32(&sc->vbios[a + 0x28]),
			    nvgsp_le32(&sc->vbios[a + 0x2c]),
			    nvgsp_le32(&sc->vbios[a + 0x30]),
			    nvgsp_le32(&sc->vbios[a + 0x10]),
			    nvgsp_le32(&sc->vbios[a + 0x0c]));
			hits++;
		}
		if (hits == 0)
			nvgpu_log(NVGPU_LOG_DEBUG, "FwSec: no V2 desc candidates in %u bytes\n",
			    sc->vbios_size);
	}
	KTR_LOG(nvgpu_gsp_bios_stage, 4u);

	{
		uint16_t sig = nvgsp_le16(&sc->vbios[0]);

		if (sig != NVGSP_ROM_SIG_STD &&
		    sig != NVGSP_ROM_SIG_NV &&
		    sig != NVGSP_ROM_SIG_NV2) {
			nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS: no valid ROM signature at offset 0 (got 0x%04x)\n",
			    sig);
			kfree(sc->vbios, M_NVGSP_VBIOS);
			sc->vbios = NULL;
			sc->vbios_size = 0;
			return (EIO);
		}
	}

	offset = 0;
	idx = 0;
	last = 0;
	size = 0;
	while (!last) {
		error = nvgsp_bios_parse_image(sc, offset, idx, &size, &last,
		    &npde_used);
		if (error != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS: chain truncated at image %d (offset 0x%05x)\n",
			    idx, offset);
			break;
		}
		idx++;
		if (last)
			break;
		offset += size;
	}

	if (last)
		sc->vbios_size = offset + size;

	nvgpu_log(NVGPU_LOG_DEBUG, "VBIOS: %d image%s, total %u bytes\n",
	    idx, idx == 1 ? "" : "s", sc->vbios_size);
	KTR_LOG(nvgpu_gsp_bios_stage, 5u);

	return (0);
}

static int
nvgsp_bios_dump_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct nvgsp_state *sc = arg1;

	if (sc->vbios == NULL || sc->vbios_size == 0)
		return (ENXIO);
	return (SYSCTL_OUT(req, sc->vbios, sc->vbios_size));
}

void
nvgsp_bios_publish_sysctl(struct nvgsp_state *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid *parent)
{
	if (sc->vbios == NULL)
		return;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "vbios_size", CTLFLAG_RD,
	    &sc->vbios_size, 0, "VBIOS image size in bytes");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "vbios", CTLTYPE_OPAQUE | CTLFLAG_RD,
	    sc, 0, nvgsp_bios_dump_sysctl, "S",
	    "Full VBIOS image dump");
}

void
nvgsp_bios_fini(struct nvgsp_state *sc)
{
	if (sc->vbios != NULL) {
		kfree(sc->vbios, M_NVGSP_VBIOS);
		sc->vbios = NULL;
		sc->vbios_size = 0;
	}
}
