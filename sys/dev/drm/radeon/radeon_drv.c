/**
 * \file radeon_drv.c
 * ATI Radeon driver
 *
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * $FreeBSD: head/sys/dev/drm2/radeon/radeon_drv.c 254885 2013-08-25 19:37:15Z dumbbell $
 */

#include <drm/drmP.h>
#include <uapi_drm/radeon_drm.h>
#include "radeon_drv.h"
#include "radeon_gem.h"
#include "radeon_kms.h"
#include "radeon_irq_kms.h"

#include <drm/drm_pciids.h>
#include <linux/module.h>

/*
 * KMS wrapper.
 * - 2.0.0 - initial interface
 * - 2.1.0 - add square tiling interface
 * - 2.2.0 - add r6xx/r7xx const buffer support
 * - 2.3.0 - add MSPOS + 3D texture + r500 VAP regs
 * - 2.4.0 - add crtc id query
 * - 2.5.0 - add get accel 2 to work around ddx breakage for evergreen
 * - 2.6.0 - add tiling config query (r6xx+), add initial HiZ support (r300->r500)
 *   2.7.0 - fixups for r600 2D tiling support. (no external ABI change), add eg dyn gpr regs
 *   2.8.0 - pageflip support, r500 US_FORMAT regs. r500 ARGB2101010 colorbuf, r300->r500 CMASK, clock crystal query
 *   2.9.0 - r600 tiling (s3tc,rgtc) working, SET_PREDICATION packet 3 on r600 + eg, backend query
 *   2.10.0 - fusion 2D tiling
 *   2.11.0 - backend map, initial compute support for the CS checker
 *   2.12.0 - RADEON_CS_KEEP_TILING_FLAGS
 *   2.13.0 - virtual memory support, streamout
 *   2.14.0 - add evergreen tiling informations
 *   2.15.0 - add max_pipes query
 *   2.16.0 - fix evergreen 2D tiled surface calculation
 *   2.17.0 - add STRMOUT_BASE_UPDATE for r7xx
 *   2.18.0 - r600-eg: allow "invalid" DB formats
 *   2.19.0 - r600-eg: MSAA textures
 *   2.20.0 - r600-si: RADEON_INFO_TIMESTAMP query
 *   2.21.0 - r600-r700: FMASK and CMASK
 *   2.22.0 - r600 only: RESOLVE_BOX allowed
 *   2.23.0 - allow STRMOUT_BASE_UPDATE on RS780 and RS880
 *   2.24.0 - eg only: allow MIP_ADDRESS=0 for MSAA textures
 *   2.25.0 - eg+: new info request for num SE and num SH
 *   2.26.0 - r600-eg: fix htile size computation
 *   2.27.0 - r600-SI: Add CS ioctl support for async DMA
 *   2.28.0 - r600-eg: Add MEM_WRITE packet support
 *   2.29.0 - R500 FP16 color clear registers
 *   2.30.0 - fix for FMASK texturing
 *   2.31.0 - Add fastfb support for rs690
 *   2.32.0 - new info request for rings working
 *   2.33.0 - Add SI tiling mode array query
 *   2.34.0 - Add CIK tiling mode array query
 */
#define KMS_DRIVER_MAJOR	2
#define KMS_DRIVER_MINOR	34
#define KMS_DRIVER_PATCHLEVEL	0
int radeon_suspend_kms(struct drm_device *dev);
int radeon_resume_kms(struct drm_device *dev);
extern int radeon_get_crtc_scanoutpos(struct drm_device *dev, int crtc,
				      unsigned int flags,
				      int *vpos, int *hpos, ktime_t *stime,
				      ktime_t *etime);
extern struct drm_ioctl_desc radeon_ioctls_kms[];
extern int radeon_max_kms_ioctl;
#ifdef DUMBBELL_WIP
int radeon_mmap(struct file *filp, struct vm_area_struct *vma);
#endif /* DUMBBELL_WIP */
int radeon_mode_dumb_mmap(struct drm_file *filp,
			  struct drm_device *dev,
			  uint32_t handle, uint64_t *offset_p);
int radeon_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args);
int radeon_mode_dumb_destroy(struct drm_file *file_priv,
			     struct drm_device *dev,
			     uint32_t handle);
struct sg_table *radeon_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *radeon_gem_prime_import_sg_table(struct drm_device *dev,
							size_t size,
							struct sg_table *sg);
int radeon_gem_prime_pin(struct drm_gem_object *obj);
void radeon_gem_prime_unpin(struct drm_gem_object *obj);
void *radeon_gem_prime_vmap(struct drm_gem_object *obj);
void radeon_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);

#if defined(CONFIG_DEBUG_FS)
int radeon_debugfs_init(struct drm_minor *minor);
void radeon_debugfs_cleanup(struct drm_minor *minor);
#endif

/* atpx handler */
#if defined(CONFIG_VGA_SWITCHEROO)
void radeon_register_atpx_handler(void);
void radeon_unregister_atpx_handler(void);
#else
static inline void radeon_register_atpx_handler(void) {}
static inline void radeon_unregister_atpx_handler(void) {}
#endif

int radeon_no_wb;
int radeon_modeset = 1;
int radeon_dynclks = -1;
int radeon_r4xx_atom = 0;
int radeon_agpmode = 0;
int radeon_vram_limit = 0;
int radeon_gart_size = 512; /* default gart size */
int radeon_benchmarking = 0;
int radeon_testing = 0;
int radeon_connector_table = 0;
int radeon_tv = 1;
int radeon_audio = 0;
int radeon_disp_priority = 0;
int radeon_hw_i2c = 0;
int radeon_pcie_gen2 = -1;
int radeon_msi = -1;
int radeon_lockup_timeout = 10000;
int radeon_fastfb = 0;
int radeon_dpm = -1;
int radeon_aspm = -1;

TUNABLE_INT("drm.radeon.no_wb", &radeon_no_wb);
MODULE_PARM_DESC(no_wb, "Disable AGP writeback for scratch registers");
module_param_named(no_wb, radeon_no_wb, int, 0444);

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, radeon_modeset, int, 0400);

TUNABLE_INT("drm.radeon.dynclks", &radeon_dynclks);
MODULE_PARM_DESC(dynclks, "Disable/Enable dynamic clocks");
module_param_named(dynclks, radeon_dynclks, int, 0444);

TUNABLE_INT("drm.radeon.r4xx_atom", &radeon_r4xx_atom);
MODULE_PARM_DESC(r4xx_atom, "Enable ATOMBIOS modesetting for R4xx");
module_param_named(r4xx_atom, radeon_r4xx_atom, int, 0444);

TUNABLE_INT("drm.radeon.vram_limit", &radeon_vram_limit);
MODULE_PARM_DESC(vramlimit, "Restrict VRAM for testing");
module_param_named(vramlimit, radeon_vram_limit, int, 0600);

TUNABLE_INT("drm.radeon.agpmode", &radeon_agpmode);
MODULE_PARM_DESC(agpmode, "AGP Mode (-1 == PCI)");
module_param_named(agpmode, radeon_agpmode, int, 0444);

TUNABLE_INT("drm.radeon.gart_size", &radeon_gart_size);
MODULE_PARM_DESC(gartsize, "Size of PCIE/IGP gart to setup in megabytes (32, 64, etc)");
module_param_named(gartsize, radeon_gart_size, int, 0600);

TUNABLE_INT("drm.radeon.benchmarking", &radeon_benchmarking);
MODULE_PARM_DESC(benchmark, "Run benchmark");
module_param_named(benchmark, radeon_benchmarking, int, 0444);

TUNABLE_INT("drm.radeon.testing", &radeon_testing);
MODULE_PARM_DESC(test, "Run tests");
module_param_named(test, radeon_testing, int, 0444);

TUNABLE_INT("drm.radeon.connector_table", &radeon_connector_table);
MODULE_PARM_DESC(connector_table, "Force connector table");
module_param_named(connector_table, radeon_connector_table, int, 0444);

TUNABLE_INT("drm.radeon.tv", &radeon_tv);
MODULE_PARM_DESC(tv, "TV enable (0 = disable)");
module_param_named(tv, radeon_tv, int, 0444);

TUNABLE_INT("drm.radeon.audio", &radeon_audio);
MODULE_PARM_DESC(audio, "Audio enable (1 = enable)");
module_param_named(audio, radeon_audio, int, 0444);

TUNABLE_INT("drm.radeon.disp_priority", &radeon_disp_priority);
MODULE_PARM_DESC(disp_priority, "Display Priority (0 = auto, 1 = normal, 2 = high)");
module_param_named(disp_priority, radeon_disp_priority, int, 0444);

TUNABLE_INT("drm.radeon.hw_i2c", &radeon_hw_i2c);
MODULE_PARM_DESC(hw_i2c, "hw i2c engine enable (0 = disable)");
module_param_named(hw_i2c, radeon_hw_i2c, int, 0444);

TUNABLE_INT("drm.radeon.pcie_gen2", &radeon_pcie_gen2);
MODULE_PARM_DESC(pcie_gen2, "PCIE Gen2 mode (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(pcie_gen2, radeon_pcie_gen2, int, 0444);

TUNABLE_INT("drm.radeon.msi", &radeon_msi);
MODULE_PARM_DESC(msi, "MSI support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(msi, radeon_msi, int, 0444);

TUNABLE_INT("drm.radeon.lockup_timeout", &radeon_lockup_timeout);
MODULE_PARM_DESC(lockup_timeout, "GPU lockup timeout in ms (defaul 10000 = 10 seconds, 0 = disable)");
module_param_named(lockup_timeout, radeon_lockup_timeout, int, 0444);

TUNABLE_INT("drm.radeon.fastfb", &radeon_fastfb);
MODULE_PARM_DESC(fastfb, "Direct FB access for IGP chips (0 = disable, 1 = enable)");
module_param_named(fastfb, radeon_fastfb, int, 0444);

TUNABLE_INT("drm.radeon.dpm", &radeon_dpm);
MODULE_PARM_DESC(dpm, "DPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(dpm, radeon_dpm, int, 0444);

TUNABLE_INT("drm.radeon.aspm", &radeon_aspm);
MODULE_PARM_DESC(aspm, "ASPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(aspm, radeon_aspm, int, 0444);

static drm_pci_id_list_t pciidlist[] = {
	radeon_PCI_IDS
};

#ifdef CONFIG_DRM_RADEON_UMS

#ifdef DUMBBELL_WIP

static int radeon_suspend(struct drm_device *dev, pm_message_t state)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return 0;

	/* Disable *all* interrupts */
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600)
		RADEON_WRITE(R500_DxMODE_INT_MASK, 0);
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);
	return 0;
}

static int radeon_resume(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return 0;

	/* Restore interrupt registers */
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600)
		RADEON_WRITE(R500_DxMODE_INT_MASK, dev_priv->r500_disp_irq_reg);
	RADEON_WRITE(RADEON_GEN_INT_CNTL, dev_priv->irq_enable_reg);
	return 0;
}
#endif /* DUMBBELL_WIP */

#ifdef DUMBBELL_WIP
static const struct file_operations radeon_driver_old_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = radeon_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver_old = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
	    DRIVER_HAVE_IRQ | DRIVER_HAVE_DMA | DRIVER_IRQ_SHARED,
	.dev_priv_size = sizeof(drm_radeon_buf_priv_t),
	.load = radeon_driver_load,
	.firstopen = radeon_driver_firstopen,
	.open = radeon_driver_open,
	.preclose = radeon_driver_preclose,
	.postclose = radeon_driver_postclose,
	.lastclose = radeon_driver_lastclose,
	.unload = radeon_driver_unload,
#ifdef DUMBBELL_WIP
	.suspend = radeon_suspend,
	.resume = radeon_resume,
#endif /* DUMBBELL_WIP */
	.get_vblank_counter = radeon_get_vblank_counter,
	.enable_vblank = radeon_enable_vblank,
	.disable_vblank = radeon_disable_vblank,
	.master_create = radeon_master_create,
	.master_destroy = radeon_master_destroy,
	.irq_preinstall = radeon_driver_irq_preinstall,
	.irq_postinstall = radeon_driver_irq_postinstall,
	.irq_uninstall = radeon_driver_irq_uninstall,
	.irq_handler = radeon_driver_irq_handler,
	.ioctls = radeon_ioctls,
	.dma_ioctl = radeon_cp_buffers,
	.fops = &radeon_driver_old_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
#endif /* DUMBBELL_WIP */

#endif

static struct drm_driver kms_driver;

#ifdef DUMBBELL_WIP
static int radeon_kick_out_firmware_fb(struct pci_dev *pdev)
{
	struct apertures_struct *ap;
	bool primary = false;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = pci_resource_start(pdev, 0);
	ap->ranges[0].size = pci_resource_len(pdev, 0);

#ifdef CONFIG_X86
	primary = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
#endif
	remove_conflicting_framebuffers(ap, "radeondrmfb", primary);
	kfree(ap);

	return 0;
}

static int radeon_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	int ret;

	/* Get rid of things like offb */
	ret = radeon_kick_out_firmware_fb(pdev);
	if (ret)
		return ret;

	return drm_get_pci_dev(pdev, ent, &kms_driver);
}

static void
radeon_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int
radeon_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	return radeon_suspend_kms(dev, state);
}

static int
radeon_pci_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	return radeon_resume_kms(dev);
}

static const struct file_operations radeon_driver_kms_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = radeon_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = radeon_kms_compat_ioctl,
#endif
};
#endif /* DUMBBELL_WIP */

static struct drm_driver kms_driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
	    DRIVER_HAVE_IRQ | DRIVER_HAVE_DMA | DRIVER_IRQ_SHARED | DRIVER_GEM |
	    DRIVER_PRIME /* | DRIVE_MODESET */,
#ifdef DUMBBELL_WIP
	.dev_priv_size = 0,
#endif /* DUMBBELL_WIP */
	.load = radeon_driver_load_kms,
	.use_msi = radeon_msi_ok,
	.firstopen = radeon_driver_firstopen_kms,
	.open = radeon_driver_open_kms,
	.preclose = radeon_driver_preclose_kms,
	.postclose = radeon_driver_postclose_kms,
	.lastclose = radeon_driver_lastclose_kms,
	.unload = radeon_driver_unload_kms,
#ifdef DUMBBELL_WIP
	.suspend = radeon_suspend_kms,
	.resume = radeon_resume_kms,
#endif /* DUMBBELL_WIP */
	.get_vblank_counter = radeon_get_vblank_counter_kms,
	.enable_vblank = radeon_enable_vblank_kms,
	.disable_vblank = radeon_disable_vblank_kms,
	.get_vblank_timestamp = radeon_get_vblank_timestamp_kms,
	.get_scanout_position = radeon_get_crtc_scanoutpos,
	.irq_preinstall = radeon_driver_irq_preinstall_kms,
	.irq_postinstall = radeon_driver_irq_postinstall_kms,
	.irq_uninstall = radeon_driver_irq_uninstall_kms,
	.irq_handler = radeon_driver_irq_handler_kms,
	.ioctls = radeon_ioctls_kms,
	.gem_free_object = radeon_gem_object_free,
	.gem_open_object = radeon_gem_object_open,
	.gem_close_object = radeon_gem_object_close,
	.dma_ioctl = radeon_dma_ioctl_kms,
	.dumb_create = radeon_mode_dumb_create,
	.dumb_map_offset = radeon_mode_dumb_mmap,
	.dumb_destroy = radeon_mode_dumb_destroy,
#ifdef DUMBBELL_WIP
	.fops = &radeon_driver_kms_fops,
#endif /* DUMBBELL_WIP */

#ifdef DUMBBELL_WIP
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_pin = radeon_gem_prime_pin,
	.gem_prime_unpin = radeon_gem_prime_unpin,
	.gem_prime_get_sg_table = radeon_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = radeon_gem_prime_import_sg_table,
	.gem_prime_vmap = radeon_gem_prime_vmap,
	.gem_prime_vunmap = radeon_gem_prime_vunmap,
#endif /* DUMBBELL_WIP */

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

#ifdef DUMBBELL_WIP
static int __init radeon_init(void)
{
	if (radeon_modeset == 1) {
		DRM_INFO("radeon kernel modesetting enabled.\n");
		driver = &kms_driver;
		pdriver = &radeon_kms_pci_driver;
		driver->driver_features |= DRIVER_MODESET;
		driver->num_ioctls = radeon_max_kms_ioctl;
		radeon_register_atpx_handler();

	} else {
#ifdef CONFIG_DRM_RADEON_UMS
		DRM_INFO("radeon userspace modesetting enabled.\n");
		driver = &driver_old;
		pdriver = &radeon_pci_driver;
		driver->driver_features &= ~DRIVER_MODESET;
		driver->num_ioctls = radeon_max_ioctl;
#else
		DRM_ERROR("No UMS support in radeon module!\n");
		return -EINVAL;
#endif
	}

	/* let modprobe override vga console setting */
	return drm_pci_init(driver, pdriver);
}

static void __exit radeon_exit(void)
{
	drm_pci_exit(driver, pdriver);
	radeon_unregister_atpx_handler();
}
#endif /* DUMBBELL_WIP */

/* =================================================================== */

static int
radeon_probe(device_t kdev)
{

	return drm_probe(kdev, pciidlist);
}

static int
radeon_attach(device_t kdev)
{
	struct drm_device *dev;

	dev = device_get_softc(kdev);
	if (radeon_modeset == 1) {
		kms_driver.driver_features |= DRIVER_MODESET;
		kms_driver.num_ioctls = radeon_max_kms_ioctl;
		radeon_register_atpx_handler();
	}
	dev->driver = &kms_driver;
	return (drm_attach(kdev, pciidlist));
}

static int
radeon_suspend(device_t kdev)
{
	struct drm_device *dev;
	int ret;

	dev = device_get_softc(kdev);
	ret = radeon_suspend_kms(dev);

	return (-ret);
}

static int
radeon_resume(device_t kdev)
{
	struct drm_device *dev;
	int ret;

	dev = device_get_softc(kdev);
	ret = radeon_resume_kms(dev);

	return (-ret);
}

static device_method_t radeon_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		radeon_probe),
	DEVMETHOD(device_attach,	radeon_attach),
	DEVMETHOD(device_suspend,	radeon_suspend),
	DEVMETHOD(device_resume,	radeon_resume),
	DEVMETHOD(device_detach,	drm_release),
	DEVMETHOD_END
};

static driver_t radeon_driver = {
	"drm",
	radeon_methods,
	sizeof(struct drm_device)
};

extern devclass_t drm_devclass;
DRIVER_MODULE_ORDERED(radeonkms, vgapci, radeon_driver, drm_devclass,
    NULL, NULL, SI_ORDER_ANY);
MODULE_DEPEND(radeonkms, drm, 1, 1, 1);
MODULE_DEPEND(radeonkms, agp, 1, 1, 1);
MODULE_DEPEND(radeonkms, iicbus, 1, 1, 1);
MODULE_DEPEND(radeonkms, iic, 1, 1, 1);
MODULE_DEPEND(radeonkms, iicbb, 1, 1, 1);
