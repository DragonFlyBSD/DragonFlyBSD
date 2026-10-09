/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP-RM libos init args + message queue + RM args setup.
 *
 * After the booter on SEC2 stages GSP-RM into VRAM and releases the
 * RISC-V core, GSP-RM looks at GSP-Falcon MAILBOX0/MAILBOX1 for the
 * sysmem PA of a 4 KiB "libos init args" page. That page is an array
 * of LibosMemoryRegionInitArgument entries (32 bytes each) by which
 * GSP-RM finds:
 *
 *   id8 = "LOGINIT"  -> early-boot printf log buffer (64 KiB)
 *   id8 = "LOGINTR"  -> interrupt-context log buffer (64 KiB)
 *   id8 = "LOGRM"    -> general RM log buffer        (64 KiB)
 *   id8 = "RMARGS"   -> GSP_ARGUMENTS_CACHED         ( 4 KiB)
 *
 * GSP_ARGUMENTS_CACHED carries:
 *   - sharedMemPhysAddr / pageTableEntryCount / cmdQueueOffset /
 *     statQueueOffset -- describes the message-queue shared memory
 *     region (cmdq + msgq + PTE array) in sysmem so the RPC channel
 *     is usable as soon as GSP-RM starts.
 *   - srInitArguments (suspend/resume state -- zeros on first boot).
 *   - bDmemStack = 1.
 *
 * Without these in place, GSP-RM starts on RISC-V, immediately tries
 * to bring up its RPC channel, finds no RMARGS, halts. The booter
 * still reports mb0 = 0 (because its own job -- staging WPR2 -- was
 * successful), but GSP-Falcon RISCV_STATUS reads 0 from the host
 * side because the RISC-V core ran for microseconds and halted.
 *
 * References:
 *   nouveau drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/gsp.c:
 *     r535_gsp_libos_init  (libos args page)
 *     r535_gsp_shared_init (cmdq/msgq SHM region + msgqTxHeader)
 *     r535_gsp_rmargs_init
 *   nouveau drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/gsp.c:
 *     r570_gsp_set_rmargs (THIS variant -- r570 differs from r535)
 *   open-rm src/common/shared/msgq/inc/msgq/msgq_priv.h:
 *     msgqTxHeader / msgqRxHeader layouts
 */

#include "nvgsp_priv.h"

static MALLOC_DEFINE(M_NVGSP_LIBOS, "nvgsp_libos", "nvgsp GSP libos/shm state");

#define NVGSP_PAGE_SIZE	4096u
#define NVGSP_PAGE_SHIFT	12

/* Region sizes per nouveau. */
#define NVGSP_LOG_BUF_SIZE	0x10000u	/* 64 KiB each for LOGINIT/INTR/RM */
#define NVGSP_RMARGS_SIZE	0x1000u		/* 4 KiB */
#define NVGSP_LIBOS_SIZE	0x1000u		/* 4 KiB args page */
#define NVGSP_CMDQ_SIZE	0x40000u	/* 256 KiB */
#define NVGSP_MSGQ_SIZE	0x40000u	/* 256 KiB */

/* ----- Wire-level structs (MIT-derived from open-rm / nouveau) ------- */

/*
 * msgqTxHeader (open-rm common/shared/msgq/inc/msgq/msgq_priv.h).
 * 32 bytes; written by the producer at the start of its ring.
 */
struct nvgsp_msgq_tx_header {
	uint32_t version;	/* 0 = current */
	uint32_t size;		/* ring size in bytes, page aligned */
	uint32_t msgSize;	/* entry size, power of 2, >= 16 */
	uint32_t msgCount;	/* number of slots (= (size - entryOff) / msgSize) */
	uint32_t writePtr;	/* next slot to write */
	uint32_t flags;		/* nouveau uses 1 = swap_rx */
	uint32_t rxHdrOff;	/* offset of the consumer rxHdr in this ring */
	uint32_t entryOff;	/* offset of first message slot in this ring */
} __packed;

struct nvgsp_msgq_rx_header {
	uint32_t readPtr;
} __packed;

/* GSP_ARGUMENTS_CACHED layout (r570 nvrm/gsp.h). 4 KiB page in sysmem. */
/* Natural alignment (not __packed) -- NVIDIA r570 ABI relies on
 * compiler padding after pageTableEntryCount to align cmdQueueOffset
 * to 8 bytes. Total size = 32 bytes including 4 bytes pad. */
struct nvgsp_msgq_init_args {
	uint64_t sharedMemPhysAddr;
	uint32_t pageTableEntryCount;
	uint64_t cmdQueueOffset;	/* NvLength (u64 on 64-bit) */
	uint64_t statQueueOffset;
};

struct nvgsp_sr_init_args {
	uint32_t oldLevel;
	uint32_t flags;
	uint32_t bInPMTransition;
} __packed;

/* Natural alignment (not __packed) -- profilerArgs at offset 56 after
 * 4-byte pad inserted between bDmemStack and profilerArgs.pa. */
struct nvgsp_arguments_cached {
	struct nvgsp_msgq_init_args  messageQueueInitArguments;
	struct nvgsp_sr_init_args srInitArguments;
	uint32_t gpuInstance;
	uint32_t bDmemStack;
	struct {
		uint64_t pa;
		uint64_t size;
	} profilerArgs;
};

/*
 * LibosMemoryRegionInitArgument (per nova-core bindings, 32 bytes):
 *   id8   : 8-byte ASCII tag, big-endian pack of name (first char in MSB)
 *   pa    : sysmem PA
 *   size  : region size in bytes
 *   kind  : 1 = CONTIGUOUS, 2 = RADIX3
 *   loc   : 1 = SYSMEM,     2 = FB
 *   pad   : 6 bytes
 */
struct nvgsp_libos_region {
	uint64_t id8;
	uint64_t pa;
	uint64_t size;
	uint8_t  kind;
	uint8_t  loc;
	uint8_t  pad[6];
} __packed;

#define NVGSP_LIBOS_KIND_CONTIGUOUS	1u
#define NVGSP_LIBOS_LOC_SYSMEM		1u

static uint64_t
nvgsp_libos_get_id8(const char *name)
{
	uint64_t id = 0;
	int i;

	for (i = 0; i < 8 && name[i] != '\0'; i++)
		id = (id << 8) | (uint8_t)name[i];
	return (id);
}

/*
 * Build the PTE array for a contiguous DMA-coherent buffer: one u64
 * entry per 4 KiB page, holding the sysmem PA of that page. GSP-RM
 * uses these to MMU-map the buffer through libos.
 */
static void
nvgsp_libos_get_pte_array(uint64_t *ptes, uint64_t paddr, uint32_t size)
{
	uint32_t i, npages = (size + NVGSP_PAGE_SIZE - 1) >>
	    NVGSP_PAGE_SHIFT;

	for (i = 0; i < npages; i++)
		ptes[i] = paddr + ((uint64_t)i << NVGSP_PAGE_SHIFT);
}

int
nvgsp_libos_prepare(struct nvgsp_state *sc)
{
	struct nvgsp_libos_region *args;
	struct nvgsp_msgq_tx_header *cmdq_tx;
	struct nvgsp_arguments_cached *rma;
	uint32_t ptes_nr, ptes_size;
	int error;

	/*
	 * 1. SHM allocation: PTE array + cmdq + msgq in one contiguous
	 *    sysmem region.
	 *
	 *    ptes_nr = pages of (cmdq + msgq), plus the pages the PTE
	 *              array itself occupies (each PTE is 8 bytes).
	 */
	ptes_nr  = (NVGSP_CMDQ_SIZE + NVGSP_MSGQ_SIZE) >>
	    NVGSP_PAGE_SHIFT;
	ptes_nr += (ptes_nr * sizeof(uint64_t) + NVGSP_PAGE_SIZE - 1) >>
	    NVGSP_PAGE_SHIFT;
	ptes_size = roundup(ptes_nr * sizeof(uint64_t), NVGSP_PAGE_SIZE);

	error = nvgsp_dma_alloc_dmamem(sc,
	    ptes_size + NVGSP_CMDQ_SIZE + NVGSP_MSGQ_SIZE,
	    NVGSP_PAGE_SIZE, &sc->gsp_shm);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "libos: shm alloc failed (%d)\n", error);
		return (error);
	}
	sc->gsp_shm_ptes_nr   = ptes_nr;
	sc->gsp_shm_ptes_size = ptes_size;
	sc->gsp_shm_cmdq_off  = ptes_size;
	sc->gsp_shm_msgq_off  = ptes_size + NVGSP_CMDQ_SIZE;

	/* Fill PTE array at offset 0. */
	nvgsp_libos_get_pte_array((uint64_t *)sc->gsp_shm.kva,
	    sc->gsp_shm.paddr,
	    ptes_size + NVGSP_CMDQ_SIZE + NVGSP_MSGQ_SIZE);

	/*
	 * Fill the cmdq tx header. Matches nouveau r535_gsp_shared_init:
	 *   version  = 0
	 *   size     = 0x40000
	 *   entryOff = 0x1000
	 *   msgSize  = 0x1000
	 *   msgCount = (size - entryOff) / msgSize  = 63
	 *   writePtr = 0
	 *   flags    = 1
	 *   rxHdrOff = sizeof(tx) (= 32; the rx header follows tx in-ring)
	 *
	 * The msgq side stays zero -- GSP-RM writes its own tx header
	 * when it produces the first message.
	 */
	cmdq_tx = (struct nvgsp_msgq_tx_header *)
	    ((uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_cmdq_off);
	memset(cmdq_tx, 0, sizeof(*cmdq_tx));
	cmdq_tx->version  = 0;
	cmdq_tx->size     = NVGSP_CMDQ_SIZE;
	cmdq_tx->msgSize  = NVGSP_PAGE_SIZE;
	cmdq_tx->msgCount = (NVGSP_CMDQ_SIZE - NVGSP_PAGE_SIZE) /
	    NVGSP_PAGE_SIZE;
	cmdq_tx->writePtr = 0;
	cmdq_tx->flags    = 1;
	cmdq_tx->rxHdrOff = sizeof(*cmdq_tx);
	cmdq_tx->entryOff = NVGSP_PAGE_SIZE;

	nvgpu_log(NVGPU_LOG_DEBUG, "libos: shm paddr=0x%llx pteSz=%u pte#=%u cmdq@+0x%x msgq@+0x%x\n",
	    (unsigned long long)sc->gsp_shm.paddr,
	    sc->gsp_shm_ptes_size, sc->gsp_shm_ptes_nr,
	    sc->gsp_shm_cmdq_off, sc->gsp_shm_msgq_off);

	/* 2. RMARGS — 4 KiB sysmem holding GSP_ARGUMENTS_CACHED. */
	error = nvgsp_dma_alloc_dmamem(sc, NVGSP_RMARGS_SIZE,
	    NVGSP_PAGE_SIZE, &sc->gsp_rmargs);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "libos: rmargs alloc failed (%d)\n", error);
		goto err_shm;
	}
	rma = (struct nvgsp_arguments_cached *)sc->gsp_rmargs.kva;
	memset(rma, 0, NVGSP_RMARGS_SIZE);
	rma->messageQueueInitArguments.sharedMemPhysAddr  = sc->gsp_shm.paddr;
	rma->messageQueueInitArguments.pageTableEntryCount = ptes_nr;
	rma->messageQueueInitArguments.cmdQueueOffset  = sc->gsp_shm_cmdq_off;
	rma->messageQueueInitArguments.statQueueOffset = sc->gsp_shm_msgq_off;
	rma->srInitArguments.oldLevel       = 0;
	rma->srInitArguments.flags          = 0;
	rma->srInitArguments.bInPMTransition = 0;
	rma->bDmemStack = 1;

	/* 3. Log buffers (64 KiB each). First u64 = "put" pointer = 0. */
	error = nvgsp_dma_alloc_dmamem(sc, NVGSP_LOG_BUF_SIZE,
	    NVGSP_PAGE_SIZE, &sc->gsp_loginit);
	if (error != 0) goto err_rmargs;
	memset(sc->gsp_loginit.kva, 0, NVGSP_LOG_BUF_SIZE);
	nvgsp_libos_get_pte_array(
	    (uint64_t *)((uint8_t *)sc->gsp_loginit.kva + sizeof(uint64_t)),
	    sc->gsp_loginit.paddr, NVGSP_LOG_BUF_SIZE);

	error = nvgsp_dma_alloc_dmamem(sc, NVGSP_LOG_BUF_SIZE,
	    NVGSP_PAGE_SIZE, &sc->gsp_logintr);
	if (error != 0) goto err_loginit;
	memset(sc->gsp_logintr.kva, 0, NVGSP_LOG_BUF_SIZE);
	nvgsp_libos_get_pte_array(
	    (uint64_t *)((uint8_t *)sc->gsp_logintr.kva + sizeof(uint64_t)),
	    sc->gsp_logintr.paddr, NVGSP_LOG_BUF_SIZE);

	error = nvgsp_dma_alloc_dmamem(sc, NVGSP_LOG_BUF_SIZE,
	    NVGSP_PAGE_SIZE, &sc->gsp_logrm);
	if (error != 0) goto err_logintr;
	memset(sc->gsp_logrm.kva, 0, NVGSP_LOG_BUF_SIZE);
	nvgsp_libos_get_pte_array(
	    (uint64_t *)((uint8_t *)sc->gsp_logrm.kva + sizeof(uint64_t)),
	    sc->gsp_logrm.paddr, NVGSP_LOG_BUF_SIZE);

	/*
	 * 4. LibOS init args page -- replaces our prior all-zero placeholder.
	 *    4 entries pointing at the buffers above. GSP-RM looks them up by
	 *    id8 tag at startup.
	 */
	if (sc->gsp_libos.kva == NULL) {
		error = nvgsp_dma_alloc_dmamem(sc, NVGSP_LIBOS_SIZE,
		    NVGSP_PAGE_SIZE, &sc->gsp_libos);
		if (error != 0) goto err_logrm;
	}
	args = (struct nvgsp_libos_region *)sc->gsp_libos.kva;
	memset(args, 0, NVGSP_LIBOS_SIZE);

	args[0].id8  = nvgsp_libos_get_id8("LOGINIT");
	args[0].pa   = sc->gsp_loginit.paddr;
	args[0].size = NVGSP_LOG_BUF_SIZE;
	args[0].kind = NVGSP_LIBOS_KIND_CONTIGUOUS;
	args[0].loc  = NVGSP_LIBOS_LOC_SYSMEM;

	args[1].id8  = nvgsp_libos_get_id8("LOGINTR");
	args[1].pa   = sc->gsp_logintr.paddr;
	args[1].size = NVGSP_LOG_BUF_SIZE;
	args[1].kind = NVGSP_LIBOS_KIND_CONTIGUOUS;
	args[1].loc  = NVGSP_LIBOS_LOC_SYSMEM;

	args[2].id8  = nvgsp_libos_get_id8("LOGRM");
	args[2].pa   = sc->gsp_logrm.paddr;
	args[2].size = NVGSP_LOG_BUF_SIZE;
	args[2].kind = NVGSP_LIBOS_KIND_CONTIGUOUS;
	args[2].loc  = NVGSP_LIBOS_LOC_SYSMEM;

	args[3].id8  = nvgsp_libos_get_id8("RMARGS");
	args[3].pa   = sc->gsp_rmargs.paddr;
	args[3].size = NVGSP_RMARGS_SIZE;
	args[3].kind = NVGSP_LIBOS_KIND_CONTIGUOUS;
	args[3].loc  = NVGSP_LIBOS_LOC_SYSMEM;

	nvgpu_log(NVGPU_LOG_DEBUG, "libos: args page @0x%llx, LOGINIT=0x%llx LOGINTR=0x%llx LOGRM=0x%llx RMARGS=0x%llx\n",
	    (unsigned long long)sc->gsp_libos.paddr,
	    (unsigned long long)args[0].pa,
	    (unsigned long long)args[1].pa,
	    (unsigned long long)args[2].pa,
	    (unsigned long long)args[3].pa);
	nvgpu_log(NVGPU_LOG_DEBUG, "libos: rmargs paddr=0x%llx shm=0x%llx ptes=%u cmdq+0x%x msgq+0x%x\n",
	    (unsigned long long)sc->gsp_rmargs.paddr,
	    (unsigned long long)sc->gsp_shm.paddr,
	    ptes_nr, sc->gsp_shm_cmdq_off, sc->gsp_shm_msgq_off);
	return (0);

err_logrm:
	nvgsp_dma_free_dmamem(sc, &sc->gsp_logrm);
err_logintr:
	nvgsp_dma_free_dmamem(sc, &sc->gsp_logintr);
err_loginit:
	nvgsp_dma_free_dmamem(sc, &sc->gsp_loginit);
err_rmargs:
	nvgsp_dma_free_dmamem(sc, &sc->gsp_rmargs);
err_shm:
	nvgsp_dma_free_dmamem(sc, &sc->gsp_shm);
	return (error);
}

void
nvgsp_libos_release(struct nvgsp_state *sc)
{
	if (sc->gsp_logrm.kva   != NULL) nvgsp_dma_free_dmamem(sc, &sc->gsp_logrm);
	if (sc->gsp_logintr.kva != NULL) nvgsp_dma_free_dmamem(sc, &sc->gsp_logintr);
	if (sc->gsp_loginit.kva != NULL) nvgsp_dma_free_dmamem(sc, &sc->gsp_loginit);
	if (sc->gsp_rmargs.kva  != NULL) nvgsp_dma_free_dmamem(sc, &sc->gsp_rmargs);
	if (sc->gsp_shm.kva     != NULL) nvgsp_dma_free_dmamem(sc, &sc->gsp_shm);
}
