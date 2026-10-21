/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Interrupt ingress boundary for the native NVIDIA GPU driver.
 */

#include "nvgpu_intr.h"
#include "nvgpu_chip.h"
#include "nvgpu_device.h"
#include "nvgpu_debug.h"
#include "nvgpu_display.h"
#include "nvgpu_future.h"
#include "nvgpu_sched.h"
#include "nvgsp_channel.h"
#include "nvgsp_event.h"
#include "nvgsp_state.h"

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <sys/bus.h>
#include <sys/globaldata.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>

#define NVGPU_PCI_MSI_REARM		0x68
#define NVGPU_CPU_INTR_TOP		0x00b81600u
#define NVGPU_CPU_INTR_TOP_EN_CLEAR	0x00b81610u
#define NVGPU_CPU_INTR_TOP_EN_SET	0x00b81608u
#define NVGPU_CPU_INTR_LEAF(i)		(0x00b81000u + (i) * 4u)
#define NVGPU_GSP_MSGQ_INTR		0x00000040u
#define NVGPU_INTR_CHID_COUNT		2048u
#define NVGPU_DISPLAY_INTR_HEAD_MASK	0x00611ec0u
#define NVGPU_DISPLAY_INTR_HEAD_STATUS(head) (0x00611c00u + (head) * 4u)
#define NVGPU_DISPLAY_INTR_HEAD_ACK(head)	(0x00611800u + (head) * 4u)
#define NVGPU_DISPLAY_INTR_VBLANK	0x00000002u

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, intr_decode, 16,
    "intr decode stat=0x%x top=0x%x isr=%ju", uint32_t stat,
    uint32_t top, uintmax_t isr_count);
KTR_INFO(KTR_NVGPU, nvgpu, intr_empty, 17,
    "intr empty isr=%ju", uintmax_t isr_count);
KTR_INFO(KTR_NVGPU, nvgpu, intr_exec_complete, 20,
    "intr exec complete sema=%p future=%p chid=%u value=%u target=%u",
    void *sema, void *future, uint32_t chid, uint32_t value,
    uint32_t target);
KTR_INFO(KTR_NVGPU, nvgpu, intr_display_vblank, 21,
    "intr display vblank head=%u status=0x%x", uint32_t head,
    uint32_t status);
KTR_INFO(KTR_NVGPU, nvgpu, intr_park, 22,
    "intr park sema=%p future=%p chid=%u target=%u", void *sema,
    void *future, uint32_t chid, uint32_t target);

MALLOC_DEFINE(M_NVGPU_INTR, "nvgpu_intr", "nvgpu interrupt state");

TAILQ_HEAD(nvgpu_future_list, nvgpu_future);

struct nvgpu_intr_state {
	struct nvgpu_device *gpu;
	int irq_rid;
	bool irq_msi;
	struct resource *irq_res;
	void *irq_cookie;
	struct lwkt_serialize irq_serialize;
	struct spinlock parked_spin;
	struct nvgpu_future_list parked;
	bool display_dispatch_enabled;
	uint64_t isr_count;
	uint64_t empty_count;
	uint64_t msgq_count;
	uint64_t unexpected_count;
	uint32_t unexpected_seen[8];
	uint32_t last_stat;
	uint32_t last_top;
};

static int nvgpu_intr_init(struct nvgpu_device *gpu);
static int nvgpu_intr_enable(struct nvgpu_device *gpu);
static void nvgpu_intr_disable(struct nvgpu_device *gpu);
static void nvgpu_intr_fini(struct nvgpu_device *gpu);

static void
nvgpu_intr_handle(void *arg)
{
	struct nvgpu_device *gpu = arg;
	struct nvgpu_intr_state *intr = nvgpu_device_get_intr(gpu);
	const struct nvgpu_chip_config *chip = nvgpu_device_get_chip(gpu);
	uint32_t intr_reg, mask, stat, top;
	uint32_t unhandled_leaf[8] = {};
	bool handled = false;
	bool display = false;
	bool exec = false;
	bool gsp = false;

	if (intr == NULL)
		return;
	intr->isr_count++;
	nvgpu_device_wr32(gpu, NVGPU_CPU_INTR_TOP_EN_CLEAR, 0x0000000fu);
	if (intr->irq_msi)
		pci_write_config(nvgpu_device_get_newbus_dev(gpu),
		    NVGPU_PCI_MSI_REARM, 0xff, 1);

	intr_reg = nvgpu_device_rd32(gpu, chip->gsp_base + 0x008);
	mask = nvgpu_device_rd32(gpu, chip->gsp_riscv + 0x2b4);
	stat = intr_reg & mask;
	top = nvgpu_device_rd32(gpu, NVGPU_CPU_INTR_TOP);
	intr->last_stat = stat;
	intr->last_top = top;
	KTR_LOG(nvgpu_intr_decode, stat, top, (uintmax_t)intr->isr_count);

	if (stat == 0 && top == 0) {
		intr->empty_count++;
		KTR_LOG(nvgpu_intr_empty, (uintmax_t)intr->isr_count);
		goto done;
	}
	for (uint32_t leaf = 0; leaf < 8; leaf++) {
		struct nvgsp_intr_masks masks;
		uint32_t leaf_stat, nonstall, stall, known, unhandled;

		if ((top & (1u << (leaf / 2u))) == 0)
			continue;
		leaf_stat = nvgpu_device_rd32(gpu, NVGPU_CPU_INTR_LEAF(leaf));
		nvgsp_state_get_intr_masks(gpu, leaf, &masks);
		nonstall = leaf_stat & masks.nonstall;
		stall = leaf_stat & masks.stall;
		known = masks.nonstall | masks.stall;
		unhandled = leaf_stat & ~known;
		if (unhandled != 0) {
			intr->unexpected_count++;
			unhandled_leaf[leaf] = unhandled;
		}
		if (nonstall != 0) {
			handled = true;
			nvgpu_device_wr32(gpu, NVGPU_CPU_INTR_LEAF(leaf), nonstall);
			exec = true;
		}
		if (stall != 0) {
			handled = true;
			if ((stall & masks.display) != 0)
				display = true;
			if ((stall & masks.engine) != 0)
				gsp = true;
			nvgpu_device_wr32(gpu, NVGPU_CPU_INTR_LEAF(leaf), stall);
		}
	}
	if (stat & NVGPU_GSP_MSGQ_INTR) {
		handled = true;
		nvgpu_device_wr32(gpu, chip->gsp_base + 0x004, NVGPU_GSP_MSGQ_INTR);
		intr->msgq_count++;
		gsp = true;
		stat &= ~NVGPU_GSP_MSGQ_INTR;
	}
	if (!handled) {
		for (uint32_t leaf = 0; leaf < 8; leaf++) {
			uint32_t unhandled = unhandled_leaf[leaf];

			if (unhandled == 0 ||
			    (unhandled & ~intr->unexpected_seen[leaf]) == 0)
				continue;
			intr->unexpected_seen[leaf] |= unhandled;
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "unexpected CPU intr leaf=%u unhandled=0x%08x "
			    "top=0x%08x\n", leaf, unhandled, top);
		}
	}
	if (stat != 0) {
		intr->unexpected_count++;
		nvgpu_log(NVGPU_LOG_DEBUG, "unexpected gsp intr stat=0x%08x top=0x%08x\n",
		    stat, top);
		nvgpu_device_wr32(gpu, chip->gsp_base + 0x014, stat);
		nvgpu_device_wr32(gpu, chip->gsp_base + 0x004, stat);
	}

done:
	nvgpu_device_wr32(gpu, NVGPU_CPU_INTR_TOP_EN_SET, 0x0000000fu);
	nvgpu_device_wr32(gpu, chip->gsp_base + 0x3e8, 0x1);
	if (display && intr->display_dispatch_enabled) {
		uint32_t head_mask = nvgpu_device_rd32(gpu,
		    NVGPU_DISPLAY_INTR_HEAD_MASK) & 0xffu;

		for (uint32_t head = 0; head < chip->display_heads; head++) {
			uint32_t status;

			if ((head_mask & (1u << head)) == 0)
				continue;
			status = nvgpu_device_rd32(gpu,
			    NVGPU_DISPLAY_INTR_HEAD_STATUS(head));
			if ((status & NVGPU_DISPLAY_INTR_VBLANK) == 0)
				continue;
			KTR_LOG(nvgpu_intr_display_vblank, head, status);
			nvgpu_device_wr32(gpu, NVGPU_DISPLAY_INTR_HEAD_ACK(head),
			    NVGPU_DISPLAY_INTR_VBLANK);
			nvgpu_display_handle_vblank(gpu, head);
		}
	}
	if (gsp)
		nvgsp_event_dispatch(gpu);
	if (exec) {
		struct nvgpu_future_list completed;
		struct nvgpu_future *future, *next;

		TAILQ_INIT(&completed);
		spin_lock(&intr->parked_spin);
		for (future = TAILQ_FIRST(&intr->parked); future != NULL;
		    future = next) {
			struct nvgpu_sema *sema = future->parked_sema;

			next = TAILQ_NEXT(future, link);
			cpu_lfence();
			if (sema == NULL ||
			    (int32_t)(*sema->address - sema->target) < 0)
				continue;
			TAILQ_REMOVE(&intr->parked, future, link);
			sema->parked = false;
			TAILQ_INSERT_TAIL(&completed, future, link);
		}
		spin_unlock(&intr->parked_spin);
		while ((future = TAILQ_FIRST(&completed)) != NULL) {
			struct nvgpu_sema *sema = future->parked_sema;
			int error;

			TAILQ_REMOVE(&completed, future, link);
			future->parked_sema = NULL;
			if (sema != NULL)
				sema->future = NULL;
			KTR_LOG(nvgpu_intr_exec_complete, sema, future,
			    sema != NULL ? sema->chid : 0u,
			    sema != NULL && sema->address != NULL ?
			    *sema->address : 0u, sema != NULL ? sema->target : 0u);
			error = nvgpu_sched_put(future);
			KASSERT(error == 0,
			    ("completion after scheduler stop: %d", error));
		}
	}
}

static int
nvgpu_intr_init(struct nvgpu_device *gpu)
{
	struct nvgpu_intr_state *intr;
	device_t dev = nvgpu_device_get_newbus_dev(gpu);
	int msi_count;
	int want = 1;

	if (nvgpu_device_get_intr(gpu) != NULL)
		return (0);
	intr = kmalloc(sizeof(*intr), M_NVGPU_INTR, M_WAITOK | M_ZERO);
	intr->gpu = gpu;
	spin_init(&intr->parked_spin, "nvgpu parked futures");
	TAILQ_INIT(&intr->parked);
	msi_count = pci_msi_count(dev);
	if (msi_count >= 1 && pci_alloc_msi(dev, &want, 1, -1) == 0) {
		uint16_t cmd;

		intr->irq_rid = 1;
		intr->irq_msi = true;
		cmd = pci_read_config(dev, PCIR_COMMAND, 2);
		pci_write_config(dev, PCIR_COMMAND, cmd | 0x0400, 2);
	} else {
		intr->irq_rid = 0;
		intr->irq_msi = false;
	}

	intr->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &intr->irq_rid,
	    intr->irq_msi ? RF_ACTIVE : (RF_ACTIVE | RF_SHAREABLE));
	if (intr->irq_res == NULL) {
		if (intr->irq_msi)
			pci_release_msi(dev);
		spin_uninit(&intr->parked_spin);
		kfree(intr, M_NVGPU_INTR);
		return (ENXIO);
	}

	lwkt_serialize_init(&intr->irq_serialize);
	if (bus_setup_intr(dev, intr->irq_res, INTR_MPSAFE, nvgpu_intr_handle,
	    gpu, &intr->irq_cookie, &intr->irq_serialize) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, intr->irq_rid, intr->irq_res);
		if (intr->irq_msi)
			pci_release_msi(dev);
		spin_uninit(&intr->parked_spin);
		kfree(intr, M_NVGPU_INTR);
		return (ENXIO);
	}
	nvgpu_device_set_intr(gpu, intr);
	nvgpu_log(NVGPU_LOG_DEBUG, "irq wired rid=%d msi=%d\n", intr->irq_rid,
	    intr->irq_msi ? 1 : 0);
	return (0);
}

static int
nvgpu_intr_enable(struct nvgpu_device *gpu)
{
	struct nvgpu_intr_state *intr = nvgpu_device_get_intr(gpu);
	const struct nvgpu_chip_config *chip = nvgpu_device_get_chip(gpu);

	if (intr == NULL)
		return (ENXIO);
	if (nvgsp_state_enable_intr(gpu) != 0)
		return (ENXIO);
	nvgpu_device_wr32(gpu, chip->gsp_base + 0x004, NVGPU_GSP_MSGQ_INTR);
	if (intr->irq_msi)
		pci_write_config(nvgpu_device_get_newbus_dev(gpu),
		    NVGPU_PCI_MSI_REARM, 0xff, 1);
	return (0);
}

void
nvgpu_intr_enable_display_dispatch(struct nvgpu_device *gpu)
{
	struct nvgpu_intr_state *intr = nvgpu_device_get_intr(gpu);

	if (intr == NULL)
		return;
	lwkt_serialize_enter(&intr->irq_serialize);
	intr->display_dispatch_enabled = true;
	lwkt_serialize_exit(&intr->irq_serialize);
}

void
nvgpu_intr_disable_display_dispatch(struct nvgpu_device *gpu)
{
	struct nvgpu_intr_state *intr = nvgpu_device_get_intr(gpu);

	if (intr == NULL)
		return;
	lwkt_serialize_enter(&intr->irq_serialize);
	intr->display_dispatch_enabled = false;
	lwkt_serialize_exit(&intr->irq_serialize);
}

static void
nvgpu_intr_disable(struct nvgpu_device *gpu)
{
	const struct nvgpu_chip_config *chip = nvgpu_device_get_chip(gpu);

	if (chip != NULL)
		nvgpu_device_wr32(gpu, chip->gsp_base + 0x004, 0);
}

static void
nvgpu_intr_fini(struct nvgpu_device *gpu)
{
	struct nvgpu_intr_state *intr = nvgpu_device_get_intr(gpu);
	device_t dev = nvgpu_device_get_newbus_dev(gpu);

	if (intr == NULL)
		return;
	if (intr->irq_cookie != NULL) {
		bus_teardown_intr(dev, intr->irq_res, intr->irq_cookie);
		intr->irq_cookie = NULL;
	}
	if (intr->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, intr->irq_rid, intr->irq_res);
		intr->irq_res = NULL;
	}
	if (intr->irq_msi)
		pci_release_msi(dev);
	spin_lock(&intr->parked_spin);
	KASSERT(TAILQ_EMPTY(&intr->parked),
	    ("stopping interrupt state with parked futures"));
	spin_unlock(&intr->parked_spin);
	spin_uninit(&intr->parked_spin);
	nvgpu_device_set_intr(gpu, NULL);
	kfree(intr, M_NVGPU_INTR);
}

void
nvgpu_intr_report_channel_fault(struct nvgpu_device *gpu, uint32_t chid)
{
	struct nvgpu_intr_state *intr = nvgpu_device_get_intr(gpu);
	struct nvgpu_future_list failed;
	struct nvgpu_future *future, *next;

	if (intr == NULL || chid >= NVGPU_INTR_CHID_COUNT)
		return;
	nvgsp_channel_mark_fault(gpu, chid, EIO);
	TAILQ_INIT(&failed);
	spin_lock(&intr->parked_spin);
	for (future = TAILQ_FIRST(&intr->parked); future != NULL;
	    future = next) {
		struct nvgpu_sema *sema = future->parked_sema;

		next = TAILQ_NEXT(future, link);
		if (sema == NULL || sema->chid != chid)
			continue;
		TAILQ_REMOVE(&intr->parked, future, link);
		sema->parked = false;
		sema->error = EIO;
		TAILQ_INSERT_TAIL(&failed, future, link);
	}
	spin_unlock(&intr->parked_spin);
	while ((future = TAILQ_FIRST(&failed)) != NULL) {
		struct nvgpu_sema *sema = future->parked_sema;
		int error;

		TAILQ_REMOVE(&failed, future, link);
		future->parked_sema = NULL;
		if (sema != NULL)
			sema->future = NULL;
		error = nvgpu_sched_put(future);
		KASSERT(error == 0,
		    ("fault after scheduler stop: %d", error));
	}
}

int
nvgpu_intr_start(struct nvgpu_device *gpu)
{
	int error;

	error = nvgpu_intr_init(gpu);
	if (error != 0)
		return (error);
	error = nvgpu_intr_enable(gpu);
	if (error != 0) {
		nvgpu_intr_fini(gpu);
		return (error);
	}
	return (0);
}

void
nvgpu_intr_stop(struct nvgpu_device *gpu)
{
	nvgpu_intr_disable(gpu);
	nvgpu_intr_fini(gpu);
}

int
nvgpu_intr_park(struct nvgpu_sema *sema, struct nvgpu_future *future)
{
	struct nvgpu_intr_state *intr;

	if (sema == NULL || future == NULL || sema->device == NULL ||
	    sema->address == NULL || sema->target == 0)
		return (EINVAL);
	intr = nvgpu_device_get_intr(sema->device);
	if (intr == NULL)
		return (ENODEV);
	spin_lock(&intr->parked_spin);
	KASSERT(!sema->parked && sema->future == NULL &&
	    future->parked_sema == NULL,
	    ("parking one GPU semaphore twice"));
	sema->future = future;
	sema->error = 0;
	sema->parked = true;
	future->parked_sema = sema;
	TAILQ_INSERT_TAIL(&intr->parked, future, link);
	KTR_LOG(nvgpu_intr_park, sema, future, sema->chid, sema->target);
	spin_unlock(&intr->parked_spin);
	return (0);
}
