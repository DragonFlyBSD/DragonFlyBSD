/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Physical GPU lifetime boundary for the native NVIDIA driver.
 */

#include "nvgpu_device.h"
#include "nvgpu_chip.h"
#include "nvgpu_debug.h"
#include "nvdrm_drv.h"
#include "nvgpu_intr.h"
#include "nvgpu_sched.h"
#include "nvgpu_unload.h"
#include "nvgsp_bar.h"
#include "nvgsp_boot.h"
#include "nvgsp_channel.h"
#include "nvgsp_disp.h"
#include "nvgsp_event.h"
#include "nvgsp_rpc.h"
#include "nvgsp_state.h"
#include "nvgsp_vmm.h"
#include "nvgsp_vram.h"

#include <drm/drmP.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/thread.h>
#include <sys/time.h>
#include <machine/atomic.h>

#define NVGPU_PMC_BOOT_0	0x00000000u

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, device_boot_stage, 18,
    "device boot stage=%u error=%d", uint32_t stage, int error);
KTR_INFO(KTR_NVGPU, nvgpu, device_attach, 23,
    "device attach gpu=%p", void *gpu);
KTR_INFO(KTR_NVGPU, nvgpu, device_boot_thread, 24,
    "device boot thread gpu=%p attach_elapsed_us=%ju", void *gpu,
    uintmax_t elapsed);

static MALLOC_DEFINE(M_NVGPU_DEVICE, "nvgpu_device", "nvgpu physical device");

enum nvgpu_boot_phase {
	NVGPU_BOOT_BEGIN = 0,
	NVGPU_BOOT_BARS,
	NVGPU_BOOT_UNLOAD,
	NVGPU_BOOT_THREADED,
	NVGPU_BOOT_FIRMWARE,
	NVGPU_BOOT_STATE,
	NVGPU_BOOT_RPC,
	NVGPU_BOOT_EVENT,
	NVGPU_BOOT_GSP,
	NVGPU_BOOT_VRAM,
	NVGPU_BOOT_BAR2,
	NVGPU_BOOT_BAR1,
	NVGPU_BOOT_VMM,
	NVGPU_BOOT_BOOTSTRAP_CHANNEL,
	NVGPU_BOOT_CHANNEL_CAPS,
	NVGPU_BOOT_DISPLAY,
	NVGPU_BOOT_INTR,
	NVGPU_BOOT_DRM,
	NVGPU_BOOT_COMPLETE,
};

#define NVGPU_BOOT_THREAD_ACTIVE(gpu) \
	((gpu)->boot_phase >= NVGPU_BOOT_THREADED && \
	 (gpu)->boot_phase < NVGPU_BOOT_COMPLETE)

/*
 * Physical GPU root.  Owned by PCI attach and borrowed by DRM, GSP, display,
 * interrupt, and per-open process state while detach is excluded.
 */
struct nvgpu_device {
	device_t dev;
	const struct nvgpu_pci_device *pci_device;
	const struct nvgpu_chip_config *chip;
	int bar_rid[NVGPU_NUM_BARS];
	struct resource *bar_res[NVGPU_NUM_BARS];
	struct thread *boot_td;
	struct lwkt_token boot_token;
	bool boot_stop_requested;
	struct timespec boot_started;
	int boot_result;
	uint32_t boot_phase;
	uint32_t boot0;
	struct nvgsp_state *gsp;
	struct nvgpu_intr_state *intr;
	struct pci_dev *drm_pdev;
	struct drm_device *drm_dev;
	struct nvgpu_ttm *ttm;
	struct nvgpu_display *display;
	struct nvdrm_kms *kms;
	struct nvgpu_unload_state *unload;
	uint32_t channel_limit;
	volatile u_int channel_used;
};

/*
 * DragonFly DRM expects device_get_softc(dev) to start with drm_softc.
 * Keep the GPU root in our own tail field; drm_softc::drm_driver_data is owned
 * by DRM core and points at struct drm_device after drm_dev_alloc().
 */
struct nvgpu_pci_softc {
	struct drm_softc drm;
	struct nvgpu_device *gpu;
};

static int nvgpu_device_probe_pci(device_t dev);
static int nvgpu_device_attach_pci(device_t dev);
static int nvgpu_device_detach_pci(device_t dev);
static const struct nvgpu_pci_device *nvgpu_device_match_pci(device_t dev);
static struct nvgpu_device *nvgpu_device_from_newbus(device_t dev);
static void nvgpu_device_store_newbus(device_t dev, struct nvgpu_device *gpu);
static int nvgpu_device_alloc_bars(struct nvgpu_device *gpu);
static void nvgpu_device_release_bars(struct nvgpu_device *gpu);
static int nvgpu_device_start_boot(struct nvgpu_device *gpu);
static void nvgpu_device_run_boot(void *arg);
static int nvgpu_device_run_boot_sequence(struct nvgpu_device *gpu);
static int nvgpu_device_identify_boot(struct nvgpu_device *gpu);
static int nvgpu_device_query_channel_capacity(struct nvgpu_device *gpu);
static void nvgpu_device_teardown(struct nvgpu_device *gpu);
static void nvgpu_device_fini(struct nvgpu_device *gpu);
static int nvgpu_driver_init(void);
static void nvgpu_driver_release(void);

static device_method_t nvgpu_device_pci_methods[] = {
	DEVMETHOD(device_probe,	nvgpu_device_probe_pci),
	DEVMETHOD(device_attach,	nvgpu_device_attach_pci),
	DEVMETHOD(device_detach,	nvgpu_device_detach_pci),
	DEVMETHOD_END
};

/*
 * DragonFly's vga_pci parent pre-creates a child named "drm". The driver name
 * must stay "drm" so the native GPU driver binds to that child, matching
 * amdgpu, i915, and radeon.
 */
static driver_t nvgpu_device_pci_driver = {
	"drm",
	nvgpu_device_pci_methods,
	sizeof(struct nvgpu_pci_softc),
};

static devclass_t nvgpu_device_devclass;
static struct lwkt_token nvgpu_driver_token =
    LWKT_TOKEN_INITIALIZER(nvgpu_driver_token);
static u_int nvgpu_driver_refs;

DRIVER_MODULE(nvgpu, vgapci, nvgpu_device_pci_driver,
    nvgpu_device_devclass, NULL, NULL);
MODULE_DEPEND(nvgpu, drm, 1, 1, 1);

/* Acquire one device lifetime reference and start shared state if needed. */
static int
nvgpu_driver_init(void)
{
	int error = 0;

	lwkt_gettoken(&nvgpu_driver_token);
	nvgpu_driver_refs++;
	if (nvgpu_driver_refs == 1) {
		error = nvgpu_debug_init();
		if (error == 0)
			error = nvgpu_sched_start();
		if (error != 0)
			nvgpu_driver_release();
	}
	lwkt_reltoken(&nvgpu_driver_token);
	return (error);
}

/* Release one device lifetime reference and stop shared state at zero. */
static void
nvgpu_driver_release(void)
{
	lwkt_gettoken(&nvgpu_driver_token);
	KASSERT(nvgpu_driver_refs != 0,
	    ("nvgpu driver references underflow"));
	nvgpu_driver_refs--;
	if (nvgpu_driver_refs == 0) {
		KASSERT(nvgpu_sched_busy_count() == 0,
		    ("final nvgpu detach with active futures"));
		nvgpu_sched_stop();
		nvgpu_debug_fini();
	}
	lwkt_reltoken(&nvgpu_driver_token);
}

/* Look up static chip metadata for a PCI device. */
static const struct nvgpu_pci_device *
nvgpu_device_match_pci(device_t dev)
{
	if (pci_get_vendor(dev) != NVGPU_PCI_VENDOR_NVIDIA)
		return (NULL);
	return (nvgpu_chip_lookup_pci(pci_get_device(dev)));
}

/* Tell newbus whether this NVIDIA PCI function is supported. */
static int
nvgpu_device_probe_pci(device_t dev)
{
	const struct nvgpu_pci_device *id;

	id = nvgpu_device_match_pci(dev);
	if (id == NULL || id->chip == NULL)
		return (ENXIO);

	device_set_desc(dev, id->name);
	return (BUS_PROBE_DEFAULT);
}

/* Recover the GPU object stored in the PCI/newbus softc tail. */
static struct nvgpu_device *
nvgpu_device_from_newbus(device_t dev)
{
	struct nvgpu_pci_softc *sc;

	sc = device_get_softc(dev);
	if (sc == NULL)
		return (NULL);
	return (sc->gpu);
}

/* Publish or clear the GPU object in the PCI/newbus softc tail. */
static void
nvgpu_device_store_newbus(device_t dev, struct nvgpu_device *gpu)
{
	struct nvgpu_pci_softc *sc;

	sc = device_get_softc(dev);
	if (sc != NULL)
		sc->gpu = gpu;
}

/* Map the PCI BAR resources required for early MMIO. */
static int
nvgpu_device_alloc_bars(struct nvgpu_device *gpu)
{
	int i;

	for (i = 0; i < NVGPU_NUM_BARS; i++) {
		gpu->bar_rid[i] = PCIR_BAR(i);
		gpu->bar_res[i] = bus_alloc_resource_any(gpu->dev,
		    SYS_RES_MEMORY, &gpu->bar_rid[i], RF_ACTIVE);
		if (gpu->bar_res[i] == NULL)
			continue;

		nvgpu_log(NVGPU_LOG_DEBUG, "BAR%d: %#jx-%#jx (%ju MiB)\n", i,
		    (uintmax_t)rman_get_start(gpu->bar_res[i]),
		    (uintmax_t)rman_get_end(gpu->bar_res[i]),
		    (uintmax_t)rman_get_size(gpu->bar_res[i]) >> 20);
	}

	if (gpu->bar_res[0] == NULL) {
		nvgpu_log(NVGPU_LOG_INFO, "BAR0 missing; cannot proceed\n");
		nvgpu_device_release_bars(gpu);
		return (ENXIO);
	}

	return (0);
}

/* Release all PCI BAR resources owned by the GPU. */
static void
nvgpu_device_release_bars(struct nvgpu_device *gpu)
{
	int i;

	for (i = 0; i < NVGPU_NUM_BARS; i++) {
		if (gpu->bar_res[i] == NULL)
			continue;
		bus_release_resource(gpu->dev, SYS_RES_MEMORY, gpu->bar_rid[i],
		    gpu->bar_res[i]);
		gpu->bar_res[i] = NULL;
	}
}

/* Return gpu's borrowed DragonFly device. */
device_t
nvgpu_device_get_newbus_dev(struct nvgpu_device *gpu)
{
	if (gpu == NULL)
		return (NULL);
	return (gpu->dev);
}

uint32_t
nvgpu_device_rd32(struct nvgpu_device *gpu, uint32_t offset)
{
	return (bus_read_4(gpu->bar_res[0], offset));
}


const struct nvgpu_chip_config *
nvgpu_device_get_chip(struct nvgpu_device *gpu)
{
	return (gpu->chip);
}

const char *
nvgpu_device_get_name(struct nvgpu_device *gpu)
{
	if (gpu == NULL || gpu->pci_device == NULL)
		return (NULL);
	return (gpu->pci_device->name);
}

struct resource *
nvgpu_device_get_bar(struct nvgpu_device *gpu, unsigned int bar)
{
	if (bar >= NVGPU_NUM_BARS)
		return (NULL);
	return (gpu->bar_res[bar]);
}

void
nvgpu_device_wr32(struct nvgpu_device *gpu, uint32_t offset, uint32_t val)
{
	bus_write_4(gpu->bar_res[0], offset, val);
}

struct drm_device *
nvgpu_device_get_drm_dev(struct nvgpu_device *gpu)
{
	return (gpu->drm_dev);
}

struct pci_dev *
nvgpu_device_get_drm_pdev(struct nvgpu_device *gpu)
{
	return (gpu->drm_pdev);
}

void
nvgpu_device_set_drm(struct nvgpu_device *gpu, struct drm_device *ddev,
    struct pci_dev *pdev)
{
	gpu->drm_dev = ddev;
	gpu->drm_pdev = pdev;
}

struct nvgpu_unload_state *
nvgpu_device_get_unload_state(struct nvgpu_device *gpu)
{
	return (gpu->unload);
}

void
nvgpu_device_set_unload_state(struct nvgpu_device *gpu,
    struct nvgpu_unload_state *state)
{
	gpu->unload = state;
}

struct nvgpu_ttm *
nvgpu_device_get_ttm(struct nvgpu_device *gpu)
{
	return (gpu != NULL ? gpu->ttm : NULL);
}

void
nvgpu_device_set_ttm(struct nvgpu_device *gpu, struct nvgpu_ttm *ttm)
{
	if (gpu != NULL)
		gpu->ttm = ttm;
}

struct nvgpu_display *
nvgpu_device_get_display(struct nvgpu_device *gpu)
{
	return (gpu != NULL ? gpu->display : NULL);
}

void
nvgpu_device_set_display(struct nvgpu_device *gpu,
    struct nvgpu_display *display)
{
	if (gpu != NULL)
		gpu->display = display;
}

struct nvdrm_kms *
nvgpu_device_get_kms(struct nvgpu_device *gpu)
{
	return (gpu != NULL ? gpu->kms : NULL);
}

void
nvgpu_device_set_kms(struct nvgpu_device *gpu, struct nvdrm_kms *kms)
{
	if (gpu != NULL)
		gpu->kms = kms;
}

int
nvgpu_device_reserve_channel(struct nvgpu_device *gpu)
{
	u_int old;
	u_int next;

	if (gpu == NULL || gpu->channel_limit == 0)
		return (ENOSPC);
	for (;;) {
		old = gpu->channel_used;
		if (old >= gpu->channel_limit)
			return (ENOSPC);
		next = old + 1;
		if (atomic_cmpset_int(&gpu->channel_used, old, next))
			return (0);
	}
}

void
nvgpu_device_release_channel(struct nvgpu_device *gpu)
{
	u_int old;

	if (gpu == NULL)
		return;
	old = atomic_fetchadd_int(&gpu->channel_used, -1);
	KASSERT(old != 0, ("nvgpu channel quota underflow"));
}

uint32_t
nvgpu_device_get_channel_limit(struct nvgpu_device *gpu)
{
	return (gpu != NULL ? gpu->channel_limit : 0);
}

uint32_t
nvgpu_device_get_channel_used(struct nvgpu_device *gpu)
{
	return (gpu != NULL ? gpu->channel_used : 0);
}

struct nvgsp_state *
nvgpu_device_get_gsp(struct nvgpu_device *gpu)
{
	return (gpu->gsp);
}

void
nvgpu_device_set_gsp(struct nvgpu_device *gpu, struct nvgsp_state *gsp)
{
	gpu->gsp = gsp;
}

struct nvgpu_intr_state *
nvgpu_device_get_intr(struct nvgpu_device *gpu)
{
	return (gpu->intr);
}

void
nvgpu_device_set_intr(struct nvgpu_device *gpu,
    struct nvgpu_intr_state *intr)
{
	gpu->intr = intr;
}


/* Create the boot LWKT that performs long GSP bring-up. */
static int
nvgpu_device_start_boot(struct nvgpu_device *gpu)
{
	int error;

	error = kthread_create(nvgpu_device_run_boot, gpu, &gpu->boot_td,
	    "nvgpu-boot");
	if (error == 0)
		gpu->boot_phase = NVGPU_BOOT_THREADED;
	else
		gpu->boot_td = NULL;
	return (error);
}

/* Run the device boot sequence inside the boot LWKT. */
static void
nvgpu_device_run_boot(void *arg)
{
	struct nvgpu_device *gpu = arg;
	struct timespec now;
	struct timespec elapsed;
	int error;

	getnanouptime(&now);
	timespecsub(&now, &gpu->boot_started, &elapsed);
	KTR_LOG(nvgpu_device_boot_thread, gpu,
	    (uintmax_t)elapsed.tv_sec * 1000000u +
	    (uintmax_t)elapsed.tv_nsec / 1000u);
	lwkt_gettoken(&gpu->boot_token);
	if (gpu->boot_stop_requested) {
		error = EINTR;
		goto finish_locked;
	}
	lwkt_reltoken(&gpu->boot_token);

	error = nvgpu_device_run_boot_sequence(gpu);

	lwkt_gettoken(&gpu->boot_token);
finish_locked:
	gpu->boot_result = error;
	if (error != 0)
		nvgpu_device_teardown(gpu);
	else
		gpu->boot_phase = NVGPU_BOOT_COMPLETE;
	if (gpu->boot_stop_requested)
		wakeup(&gpu->boot_phase);
	lwkt_reltoken(&gpu->boot_token);
	kthread_exit();
}

/* Read basic GPU identity registers and log the attachment. */
static int
nvgpu_device_identify_boot(struct nvgpu_device *gpu)
{
	gpu->boot0 = nvgpu_device_rd32(gpu, NVGPU_PMC_BOOT_0);
	nvgpu_log(NVGPU_LOG_INFO, "%s attached, PMC_BOOT_0=0x%08x\n",
	    gpu->pci_device->name, gpu->boot0);
	return (0);
}

/* Execute the ordered device boot chain before DRM publication. */
static int
nvgpu_device_run_boot_sequence(struct nvgpu_device *gpu)
{
	int error;

	error = nvgpu_device_identify_boot(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_FIRMWARE;
	error = nvgsp_state_init(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_STATE;
	error = nvgsp_rpc_init(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_RPC;
	error = nvgsp_event_init(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_EVENT;
	error = nvgsp_boot(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_GSP;
	error = nvgsp_vram_init(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_VRAM;
	error = nvgsp_bar_init_bar2(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_BAR2;
	error = nvgsp_bar_init_bar1(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_BAR1;
	error = nvgsp_vmm_init_kernel(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_VMM;
	error = nvgsp_channel_create_bootstrap(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_BOOTSTRAP_CHANNEL;
	error = nvgpu_device_query_channel_capacity(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_CHANNEL_CAPS;
	error = nvgsp_disp_init(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_DISPLAY;
	error = nvgpu_intr_start(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_INTR;
	error = nvdrm_register(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	gpu->boot_phase = NVGPU_BOOT_DRM;
	/*
	 * Initial KMS is already live.  Prewarm the graphics template here so
	 * the first userspace graphics channel does not pay this latency.
	 */
	error = nvgsp_channel_create_golden(gpu);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, error);
	if (error != 0)
		return (error);
	KTR_LOG(nvgpu_device_boot_stage, gpu->boot_phase, 0);
	nvgpu_log(NVGPU_LOG_DEBUG, "boot completed\n");
	return (0);
}


static int
nvgpu_device_query_channel_capacity(struct nvgpu_device *gpu)
{
	uint32_t total;
	uint32_t reserved;
	int error;

	error = nvgsp_channel_query_capacity(gpu, &total, &reserved);
	if (error != 0)
		return (error);
	if (total <= reserved)
		return (ENOSPC);
	gpu->channel_limit = total - reserved;
	gpu->channel_used = 0;
	nvgpu_log(NVGPU_LOG_INFO,
	    "channel capacity total=%u reserved=%u user_limit=%u\n",
	    total, reserved, gpu->channel_limit);
	return (0);
}

/* Release initialized resources in reverse boot order. */
static void
nvgpu_device_teardown(struct nvgpu_device *gpu)
{
	uint32_t phase;

	phase = gpu->boot_phase;
	nvgpu_log(NVGPU_LOG_DEBUG, "teardown device phase=%u\n", phase);

	if (phase >= NVGPU_BOOT_DRM)
		nvdrm_unregister(gpu);
	/* RM object frees use synchronous RPC, so keep the interrupt path alive. */
	if (phase >= NVGPU_BOOT_DISPLAY)
		nvgsp_disp_fini(gpu);
	if (phase >= NVGPU_BOOT_BOOTSTRAP_CHANNEL)
		nvgsp_channel_destroy_golden(gpu);
	if (phase >= NVGPU_BOOT_BOOTSTRAP_CHANNEL)
		nvgsp_channel_destroy_bootstrap(gpu);
	if (phase >= NVGPU_BOOT_VMM)
		nvgsp_vmm_fini_kernel(gpu);

	/* No display, channel, or VMM path remains to consume GSP events now. */
	if (phase >= NVGPU_BOOT_INTR)
		nvgpu_intr_stop(gpu);

	if (phase >= NVGPU_BOOT_BAR1)
		nvgsp_bar_fini_bar1(gpu);
	if (phase >= NVGPU_BOOT_BAR2)
		nvgsp_bar_fini_bar2(gpu);
	if (phase >= NVGPU_BOOT_VRAM)
		nvgsp_vram_fini(gpu);
	if (phase >= NVGPU_BOOT_GSP)
		nvgsp_shutdown(gpu);
	if (phase >= NVGPU_BOOT_STATE)
		nvgsp_state_fini(gpu);
	if (phase >= NVGPU_BOOT_UNLOAD)
		nvgpu_unload_fini(gpu);
	if (phase >= NVGPU_BOOT_BARS)
		nvgpu_device_release_bars(gpu);

	gpu->boot_phase = NVGPU_BOOT_BEGIN;
}

/* Release final device resources and free the GPU object. */
static void
nvgpu_device_fini(struct nvgpu_device *gpu)
{
	nvgpu_log(NVGPU_LOG_DEBUG, "finish device\n");
	nvgpu_log(NVGPU_LOG_INFO, "detached\n");
	lwkt_token_uninit(&gpu->boot_token);
	_kfree(gpu, M_NVGPU_DEVICE);
}

/* Allocate the GPU object and start asynchronous device boot. */
static int
nvgpu_device_attach_pci(device_t dev)
{
	const struct nvgpu_pci_device *id;
	struct nvgpu_device *gpu;
	int error;

	id = nvgpu_device_match_pci(dev);
	if (id == NULL || id->chip == NULL)
		return (ENXIO);

	gpu = kmalloc(sizeof(*gpu), M_NVGPU_DEVICE, M_WAITOK | M_ZERO);

	gpu->dev = dev;
	gpu->pci_device = id;
	gpu->chip = id->chip;
	gpu->boot_phase = NVGPU_BOOT_BEGIN;
	gpu->boot_stop_requested = false;
	lwkt_token_init(&gpu->boot_token, "nvgpubt");
	getnanouptime(&gpu->boot_started);
	KTR_LOG(nvgpu_device_attach, gpu);

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "vendor=0x%04x device=0x%04x rev=0x%02x subsys=0x%04x:0x%04x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev),
	    pci_get_subvendor(dev), pci_get_subdevice(dev));

	lwkt_gettoken(&gpu->boot_token);
	error = nvgpu_device_alloc_bars(gpu);
	if (error != 0)
		goto fail_locked;
	gpu->boot_phase = NVGPU_BOOT_BARS;

	error = nvgpu_unload_init(gpu);
	if (error != 0)
		goto fail_locked;
	gpu->boot_phase = NVGPU_BOOT_UNLOAD;

	lwkt_reltoken(&gpu->boot_token);

	error = nvgpu_driver_init();
	if (error != 0)
		goto fail;

	nvgpu_device_store_newbus(dev, gpu);
	lwkt_gettoken(&gpu->boot_token);
	error = nvgpu_device_start_boot(gpu);
	if (error != 0)
		goto fail_boot_locked;

	lwkt_reltoken(&gpu->boot_token);
	return (0);

fail_boot_locked:
	nvgpu_device_teardown(gpu);
	lwkt_reltoken(&gpu->boot_token);
	nvgpu_driver_release();
	goto fail;

fail_locked:
	nvgpu_device_teardown(gpu);
	lwkt_reltoken(&gpu->boot_token);
fail:
	nvgpu_device_store_newbus(dev, NULL);
	nvgpu_device_fini(gpu);
	return (error);
}

/* Admit unload and tear down the GPU object if idle. */
static int
nvgpu_device_detach_pci(device_t dev)
{
	struct nvgpu_device *gpu;
	int error;

	gpu = nvgpu_device_from_newbus(dev);
	nvgpu_log(NVGPU_LOG_DEBUG, "pci detach gpu=%p\n", gpu);
	if (gpu == NULL)
		return (0);

	nvgpu_log(NVGPU_LOG_DEBUG, "unload try begin gpu=%p ddev=%p\n", gpu,
	    nvgpu_device_get_drm_dev(gpu));
	error = nvgpu_unload_try_begin(gpu);
	if (error != 0)
		return (error);

	lwkt_gettoken(&gpu->boot_token);
	gpu->boot_stop_requested = true;
	while (NVGPU_BOOT_THREAD_ACTIVE(gpu)) {
		error = tsleep(&gpu->boot_phase, PCATCH, "nvgpubt", 0);
		if (error != 0) {
			gpu->boot_stop_requested = false;
			nvgpu_unload_abort(gpu);
			lwkt_reltoken(&gpu->boot_token);
			return (error);
		}
	}
	nvgpu_device_teardown(gpu);
	lwkt_reltoken(&gpu->boot_token);

	nvgpu_device_store_newbus(dev, NULL);
	nvgpu_device_fini(gpu);
	nvgpu_driver_release();
	return (0);
}
