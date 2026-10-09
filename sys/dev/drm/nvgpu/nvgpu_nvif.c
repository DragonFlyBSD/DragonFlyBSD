/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau NVIF syscall implementation.
 */

#include "nvdrm_nouveau_abi.h"
#include "nvgpu_chip.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_nvif.h"
#include "nvgpu_proc.h"
#include "nvgsp_state.h"

#include <sys/errno.h>
#include <sys/systm.h>
#include <bus/pci/pcivar.h>

#define NVGPU_NVIF_DEVICE_FAMILY_TURING	0x0au

static int
nvgpu_nvif_new(struct nvgpu_proc *proc, const struct nvif_ioctl_v0 *hdr)
{
	struct nvif_ioctl_new_v0 *req;

	req = (void *)hdr->data;
	if (req->version != 0)
		return (ENOSYS);
	if (req->oclass == NV_DEVICE)
		return (0);
	return (nvgpu_proc_create_channel_object(proc, hdr->token, req->object,
	    req->handle, req->oclass));
}

static int
nvgpu_nvif_device_info(struct nvgpu_proc *proc, struct nvif_ioctl_mthd_v0 *mthd)
{
	struct nvgpu_device *gpu;
	const struct nvgpu_chip_config *chip;
	struct nv_device_info_v0 *info;
	const char *chip_name;
	const char *device_name;
	device_t dev;

	gpu = nvgpu_proc_get_device(proc);
	chip = nvgpu_device_get_chip(gpu);
	dev = nvgpu_device_get_newbus_dev(gpu);
	if (chip == NULL || dev == NULL)
		return (ENXIO);

	info = (void *)mthd->data;
	memset(info, 0, sizeof(*info));
	info->version = 0;
	info->platform = NV_DEVICE_INFO_V0_PCIE;
	info->chipset = chip->chipset;
	info->revision = pci_get_revid(dev);
	info->family = NVGPU_NVIF_DEVICE_FAMILY_TURING;
	info->ram_size = nvgsp_state_get_fb_usable_size(gpu);
	info->ram_user = info->ram_size;

	chip_name = chip->chip != NULL ? chip->chip : "";
	device_name = nvgpu_device_get_name(gpu);
	if (device_name == NULL)
		device_name = chip->fallback_name != NULL ? chip->fallback_name :
		    chip_name;
	strncpy(info->chip, chip_name, sizeof(info->chip) - 1);
	strncpy(info->name, device_name, sizeof(info->name) - 1);
	return (0);
}

static int
nvgpu_nvif_mthd(struct nvgpu_proc *proc, const struct nvif_ioctl_v0 *hdr)
{
	struct nvif_ioctl_mthd_v0 *mthd;

	mthd = (void *)hdr->data;
	if (mthd->version != 0)
		return (ENOSYS);
	if (mthd->method == NV_DEVICE_V0_INFO)
		return (nvgpu_nvif_device_info(proc, mthd));
	return (EINVAL);
}

static int
nvgpu_nvif_sclass(struct nvgpu_proc *proc, const struct nvif_ioctl_v0 *hdr)
{
	struct nvif_ioctl_sclass_v0 *req;
	uint32_t classes[5];
	uint32_t count, fill, want;
	int error;

	req = (void *)hdr->data;
	if (req->version != 0)
		return (ENOSYS);
	error = nvgpu_proc_get_channel_classes(proc, hdr->token, classes,
	    &count);
	if (error != 0)
		return (error);
	want = req->count;
	fill = want < count ? want : count;
	for (uint32_t i = 0; i < fill; i++) {
		req->oclass[i].oclass = classes[i];
		req->oclass[i].minver = 0;
		req->oclass[i].maxver = 0;
	}
	req->count = fill;
	return (0);
}

/* Handle one variable-size NVIF ioctl payload copied by the DRM shim. */
int
nvgpu_nvif_ioctl(struct nvgpu_proc *proc, void *data, size_t size)
{
	struct nvif_ioctl_v0 *hdr = data;
	size_t required;

	if (proc == NULL || hdr == NULL)
		return (EINVAL);
	if (size < sizeof(*hdr))
		return (EINVAL);
	if (hdr->version != 0)
		return (ENOSYS);

	switch (hdr->type) {
	case NVIF_IOCTL_V0_NEW:
		required = sizeof(*hdr) + sizeof(struct nvif_ioctl_new_v0);
		if (size < required)
			return (EINVAL);
		return (nvgpu_nvif_new(proc, hdr));
	case NVIF_IOCTL_V0_MTHD:
		required = sizeof(*hdr) + sizeof(struct nvif_ioctl_mthd_v0);
		if (size < required)
			return (EINVAL);
		if (((struct nvif_ioctl_mthd_v0 *)hdr->data)->method ==
		    NV_DEVICE_V0_INFO) {
			required += sizeof(struct nv_device_info_v0);
			if (size < required)
				return (EINVAL);
		}
		return (nvgpu_nvif_mthd(proc, hdr));
	case NVIF_IOCTL_V0_SCLASS: {
		struct nvif_ioctl_sclass_v0 *req = (void *)hdr->data;

		required = sizeof(*hdr) + sizeof(*req);
		if (size < required)
			return (EINVAL);
		required += (req->count < 5u ? req->count : 5u) *
		    sizeof(struct nvif_ioctl_sclass_oclass_v0);
		if (size < required)
			return (EINVAL);
		return (nvgpu_nvif_sclass(proc, hdr));
	}
	case NVIF_IOCTL_V0_DEL:
		return (nvgpu_proc_destroy_channel_object(proc, hdr->object));
	default:
		nvgpu_log(NVGPU_LOG_DEBUG, "nvif type %u unhandled\n",
		    hdr->type);
		return (EINVAL);
	}
}
