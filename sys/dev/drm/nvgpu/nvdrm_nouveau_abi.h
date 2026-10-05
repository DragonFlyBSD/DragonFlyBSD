/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau-compatible userspace ABI structs used by the DRM shim.
 */

#ifndef _NVDRM_NOUVEAU_ABI_H_
#define _NVDRM_NOUVEAU_ABI_H_

#include <sys/stdint.h>

#include <drm/drmP.h>
#include <drm/drm_ioctl.h>

#define DRM_NOUVEAU_GETPARAM		0x00
#define DRM_NOUVEAU_CHANNEL_ALLOC	0x02
#define DRM_NOUVEAU_CHANNEL_FREE	0x03
#define DRM_NOUVEAU_NVIF		0x07
#define DRM_NOUVEAU_VM_INIT		0x10
#define DRM_NOUVEAU_VM_BIND		0x11
#define DRM_NOUVEAU_EXEC		0x12
#define DRM_NOUVEAU_GEM_NEW		0x40
#define DRM_NOUVEAU_GEM_CPU_PREP	0x42
#define DRM_NOUVEAU_GEM_CPU_FINI	0x43
#define DRM_NOUVEAU_GEM_INFO		0x44

#define NOUVEAU_GETPARAM_PCI_VENDOR	3
#define NOUVEAU_GETPARAM_PCI_DEVICE	4
#define NOUVEAU_GETPARAM_BUS_TYPE	5
#define NOUVEAU_GETPARAM_FB_SIZE	8
#define NOUVEAU_GETPARAM_AGP_SIZE	9
#define NOUVEAU_GETPARAM_CHIPSET_ID	11
#define NOUVEAU_GETPARAM_VM_VRAM_BASE	12
#define NOUVEAU_GETPARAM_GRAPH_UNITS	13
#define NOUVEAU_GETPARAM_PTIMER_TIME	14
#define NOUVEAU_GETPARAM_HAS_BO_USAGE	15
#define NOUVEAU_GETPARAM_HAS_PAGEFLIP	16
#define NOUVEAU_GETPARAM_EXEC_PUSH_MAX	17
#define NOUVEAU_GETPARAM_VRAM_BAR_SIZE	18
#define NOUVEAU_GETPARAM_VRAM_USED	19
#define NOUVEAU_GETPARAM_HAS_VMA_TILEMODE 20

#define NOUVEAU_FIFO_ENGINE_GR		0x01
#define NOUVEAU_FIFO_ENGINE_CE		0x30

#define NOUVEAU_GEM_DOMAIN_CPU		(1 << 0)
#define NOUVEAU_GEM_DOMAIN_VRAM	(1 << 1)
#define NOUVEAU_GEM_DOMAIN_GART	(1 << 2)
#define NOUVEAU_GEM_DOMAIN_MAPPABLE	(1 << 3)
#define NOUVEAU_GEM_DOMAIN_COHERENT	(1 << 4)
#define NOUVEAU_GEM_DOMAIN_NO_SHARE	(1 << 5)

#define NOUVEAU_GEM_CPU_PREP_NOWAIT	0x00000001
#define NOUVEAU_GEM_CPU_PREP_WRITE	0x00000004

struct drm_nouveau_getparam {
	uint64_t param;
	uint64_t value;
};

struct drm_nouveau_vm_init {
	uint64_t kernel_managed_addr;
	uint64_t kernel_managed_size;
};

struct drm_nouveau_channel_alloc {
	uint32_t fb_ctxdma_handle;
	uint32_t tt_ctxdma_handle;
	int32_t channel;
	uint32_t pushbuf_domains;
	uint32_t notifier_handle;
	struct {
		uint32_t handle;
		uint32_t grclass;
	} subchan[8];
	uint32_t nr_subchan;
};

struct drm_nouveau_channel_free {
	int32_t channel;
};

struct drm_nouveau_gem_info {
	uint32_t handle;
	uint32_t domain;
	uint64_t size;
	uint64_t offset;
	uint64_t map_handle;
	uint32_t tile_mode;
	uint32_t tile_flags;
};

struct drm_nouveau_gem_new {
	struct drm_nouveau_gem_info info;
	uint32_t channel_hint;
	uint32_t align;
};

struct drm_nouveau_gem_cpu_prep {
	uint32_t handle;
	uint32_t flags;
};

struct drm_nouveau_gem_cpu_fini {
	uint32_t handle;
};

struct drm_nouveau_sync {
	uint32_t flags;
#define DRM_NOUVEAU_SYNC_SYNCOBJ		0x0
#define DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ	0x1
#define DRM_NOUVEAU_SYNC_TYPE_MASK		0xf
	uint32_t handle;
	uint64_t timeline_value;
};

struct drm_nouveau_vm_bind_op {
	uint32_t op;
#define DRM_NOUVEAU_VM_BIND_OP_MAP	0x0
#define DRM_NOUVEAU_VM_BIND_OP_UNMAP	0x1
	uint32_t flags;
#define DRM_NOUVEAU_VM_BIND_SPARSE	(1 << 8)
	uint32_t handle;
	uint32_t pad;
	uint64_t addr;
	uint64_t bo_offset;
	uint64_t range;
};

struct drm_nouveau_vm_bind {
	uint32_t op_count;
	uint32_t flags;
#define DRM_NOUVEAU_VM_BIND_RUN_ASYNC	0x1
	uint32_t wait_count;
	uint32_t sig_count;
	uint64_t wait_ptr;
	uint64_t sig_ptr;
	uint64_t op_ptr;
};

struct drm_nouveau_exec {
	uint32_t channel_id;
	uint32_t push_count;
	uint32_t wait_count;
	uint32_t sig_count;
	uint64_t wait_ptr;
	uint64_t sig_ptr;
	uint64_t push_ptr;
};

struct nvif_ioctl_v0 {
	uint8_t version;
	uint8_t type;
#define NVIF_IOCTL_V0_SCLASS	0x01
#define NVIF_IOCTL_V0_NEW	0x02
#define NVIF_IOCTL_V0_DEL	0x03
#define NVIF_IOCTL_V0_MTHD	0x04
	uint8_t path_nr;
	uint8_t pad03[3];
	uint8_t owner;
	uint8_t route;
	uint64_t token;
	uint64_t object;
	uint8_t data[];
} __packed;

struct nvif_ioctl_new_v0 {
	uint8_t version;
	uint8_t pad01[2];
	uint8_t route;
	uint32_t pad04;
	uint64_t token;
	uint64_t object;
	uint32_t handle;
#define NV_DEVICE	0x0080
	uint32_t oclass;
	uint8_t data[];
} __packed;

struct nv_device_v0 {
	uint8_t version;
	uint8_t pad01[7];
	uint64_t device;
	uint32_t priv;
	uint32_t pad14;
} __packed;

struct nvif_ioctl_mthd_v0 {
	uint8_t version;
	uint8_t method;
#define NV_DEVICE_V0_INFO	0x00
	uint8_t pad02[6];
	uint8_t data[];
} __packed;

struct nv_device_info_v0 {
	uint8_t version;
	uint8_t platform;
#define NV_DEVICE_INFO_V0_PCIE	0x03
	uint16_t chipset;
	uint8_t revision;
	uint8_t family;
	uint8_t pad06[2];
	uint64_t ram_size;
	uint64_t ram_user;
	char chip[16];
	char name[64];
} __packed;

struct nvif_ioctl_sclass_oclass_v0 {
	int32_t oclass;
	int16_t minver;
	int16_t maxver;
};

struct nvif_ioctl_sclass_v0 {
	uint8_t version;
	uint8_t count;
	uint8_t pad02[6];
	struct nvif_ioctl_sclass_oclass_v0 oclass[];
} __packed;

#define DRM_IOCTL_NOUVEAU_GETPARAM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GETPARAM, struct drm_nouveau_getparam)
#define DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_CHANNEL_ALLOC, struct drm_nouveau_channel_alloc)
#define DRM_IOCTL_NOUVEAU_CHANNEL_FREE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_CHANNEL_FREE, struct drm_nouveau_channel_free)
#define DRM_IOCTL_NOUVEAU_NVIF \
	_IOC(IOC_INOUT, DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_NOUVEAU_NVIF, 0)
#define DRM_IOCTL_NOUVEAU_VM_INIT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_VM_INIT, struct drm_nouveau_vm_init)
#define DRM_IOCTL_NOUVEAU_VM_BIND \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_VM_BIND, struct drm_nouveau_vm_bind)
#define DRM_IOCTL_NOUVEAU_EXEC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_EXEC, struct drm_nouveau_exec)
#define DRM_IOCTL_NOUVEAU_GEM_NEW \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GEM_NEW, struct drm_nouveau_gem_new)
#define DRM_IOCTL_NOUVEAU_GEM_CPU_PREP \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_GEM_CPU_PREP, struct drm_nouveau_gem_cpu_prep)
#define DRM_IOCTL_NOUVEAU_GEM_CPU_FINI \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_GEM_CPU_FINI, struct drm_nouveau_gem_cpu_fini)
#define DRM_IOCTL_NOUVEAU_GEM_INFO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GEM_INFO, struct drm_nouveau_gem_info)

#endif /* _NVDRM_NOUVEAU_ABI_H_ */
