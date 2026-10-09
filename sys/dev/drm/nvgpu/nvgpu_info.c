/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau GETPARAM implementation.
 */

#include "nvdrm_nouveau_abi.h"
#include "nvgpu_channel.h"
#include "nvgpu_chip.h"
#include "nvgpu_device.h"
#include "nvgpu_info.h"
#include "nvgpu_proc.h"
#include "nvgsp_state.h"

#include <sys/errno.h>
#include <sys/rman.h>
#include <bus/pci/pcivar.h>

/* Fill one nouveau GETPARAM value for a live process.  proc is borrowed. */
int
nvgpu_info_get_param(struct nvgpu_proc *proc, uint64_t param, uint64_t *value)
{
	struct nvgpu_device *gpu;
	const struct nvgpu_chip_config *chip;
	struct resource *bar1;
	device_t dev;
	uint32_t hi, lo, hi2;

	if (proc == NULL || value == NULL)
		return (EINVAL);
	gpu = nvgpu_proc_get_device(proc);
	if (gpu == NULL)
		return (ENXIO);
	dev = nvgpu_device_get_newbus_dev(gpu);
	chip = nvgpu_device_get_chip(gpu);

	switch (param) {
	case NOUVEAU_GETPARAM_PCI_VENDOR:
		*value = pci_get_vendor(dev);
		return (0);
	case NOUVEAU_GETPARAM_PCI_DEVICE:
		*value = pci_get_device(dev);
		return (0);
	case NOUVEAU_GETPARAM_CHIPSET_ID:
		*value = chip != NULL ? chip->chipset : 0;
		return (0);
	case NOUVEAU_GETPARAM_BUS_TYPE:
		*value = 2;
		return (0);
	case NOUVEAU_GETPARAM_FB_SIZE:
		*value = nvgsp_state_get_fb_usable_size(gpu);
		return (0);
	case NOUVEAU_GETPARAM_VRAM_BAR_SIZE:
		bar1 = nvgpu_device_get_bar(gpu, 1);
		*value = bar1 != NULL ? rman_get_size(bar1) : 0;
		return (0);
	case NOUVEAU_GETPARAM_EXEC_PUSH_MAX:
		*value = NVGPU_CHANNEL_GPFIFO_ENTRIES / 2 - 1;
		return (0);
	case NOUVEAU_GETPARAM_GRAPH_UNITS:
		*value = chip != NULL ? chip->graph_units : 0;
		return (0);
	case NOUVEAU_GETPARAM_VRAM_USED:
		return (EINVAL);
	case NOUVEAU_GETPARAM_PTIMER_TIME:
		do {
			hi = nvgpu_device_rd32(gpu, 0x009410);
			lo = nvgpu_device_rd32(gpu, 0x009400);
			hi2 = nvgpu_device_rd32(gpu, 0x009410);
		} while (hi != hi2);
		*value = ((uint64_t)hi << 32) | lo;
		return (0);
	case NOUVEAU_GETPARAM_HAS_VMA_TILEMODE:
		*value = 1;
		return (0);
	default:
		return (EINVAL);
	}
}
