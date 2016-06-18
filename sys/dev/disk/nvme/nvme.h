/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Primary header file
 * NVME softc and structural definitions
 */

#if defined(__DragonFly__)
#include "nvme_dragonfly.h"
#else
#error "build for OS unknown"
#endif
#include "nvme_fw.h"
#include "nvme_log.h"
#include "nvme_ident.h"
#include "nvme_ns.h"
#include "nvme_chipset.h"
#include "nvme_ioctl.h"

MALLOC_DECLARE(M_NVME);

/*
 * Choose some reasonable limit, even if the hardware supports more.
 */
#define NVME_MAX_QUEUES		1024
#define NVME_MAX_NAMESPACES	1024

struct nvme_queue;
struct nvme_softc;
struct nvme_softns;

/*
 * Device matching array for special attach/detach codings.
 */
typedef struct {
	pci_vendor_id_t	 vendor;
	pci_product_id_t product;
	int		(*attach)(device_t dev);
	int		(*detach)(device_t dev);
	char		*name;
} nvme_device_t;

/*
 * Kernel-level request structure.  This structure allows the driver to
 * construct, issue, wait for, and process responses to commands.  Each
 * queue manages its own request bank.
 *
 * In order to disconnect the request structure from the hardware queue
 * mechanism itself, to reduce SMP conflicts and interactions, and allow
 * command/response processing to block without interfering with queue
 * operations, this structure embeds a copy of the HW command and response
 * structures instead of referencing the ones in the actual hardware queues.
 * These will be copied to/from the actual queue entries by lower-level
 * chipset code.
 *
 * Requests are associated with particular queues, completions can occur on
 * any queue.  Requests associated with the admin queue conveniently include
 * an additional 4K 'info' block suitable for DMA.
 */
typedef struct nvme_request {
	struct nvme_request *next_avail;
	struct nvme_subqueue *subq;	/* which queue is submission on */
	struct nvme_comqueue *comq;	/* which queue is completion on */
	uint32_t	state;
	uint32_t	cmd_id;		/* reqary[] index */
	int		waiting;
	nvme_allcmd_t	cmd;		/* hw submit structure for entry */
	nvme_allres_t	res;		/* hw completion structure for entry */
	nvme_admin_data_t *info;	/* DMA data (admin request only) */
	bus_addr_t	pinfo;		/* phys address for PRP */

	/*
	 * Aux fields to keep track of bio and other data, depending on
	 * the callback.  If the callback is NULL the caller will poll for
	 * completion.
	 */
	void		(*callback)(struct nvme_request *req, struct lock *lk);
	struct nvme_softns *nsc;
	struct bio	*bio;
} nvme_request_t;

#define NVME_REQ_AVAIL		0
#define NVME_REQ_ALLOCATED	1
#define NVME_REQ_SUBMITTED	2
#define NVME_REQ_COMPLETED	3

typedef struct nvme_subqueue {
	/*
	 * Driver stuff
	 */
	struct lock	lk;		/* queue lock controls access */
	struct nvme_softc *sc;
	nvme_request_t	*first_avail;
	nvme_request_t	*reqary;
	uint32_t	nqe;		/* #of queue entries */
	uint16_t	qid;		/* which queue# are we on? */
	uint16_t	comqid;		/* we are tied to this completion qu */
	uint32_t	subq_doorbell_reg;
	uint32_t	subq_head;
	uint32_t	subq_tail;	/* new requests */
	int		signal_requeue;

	/*
	 * DMA resources
	 */
	bus_dmamap_t	sque_map;
	bus_dmamap_t	prps_map;
	nvme_allcmd_t	*ksubq;		/* kernel-mapped addresses */
	uint64_t	*kprps;		/* enough PRPs per request for */
					/* MAXPHYS bytes worth of mappings */
	bus_addr_t	psubq;		/* physical addresses */
	bus_addr_t	pprps;

	/*
	 * Additional DMA resources for admin queue (A NVME_MAX_ADMIN_BUFFER
	 * sized buffer for each queue entry).
	 */
	bus_dmamap_t	adm_map;
	bus_addr_t	pdatapgs;
	nvme_admin_data_t *kdatapgs;
} nvme_subqueue_t;

typedef struct nvme_comqueue {
	/*
	 * Driver stuff
	 */
	struct lock	lk;		/* queue lock controls access */
	struct nvme_softc *sc;
	uint32_t	nqe;		/* #of queue entries */
	uint16_t	phase;		/* phase to match (res->tail.status) */
	uint16_t	qid;		/* which queue# are we on? */
	uint32_t	comq_doorbell_reg;
	uint32_t	comq_head;	/* consume responses */
	uint32_t	comq_tail;

	/*
	 * DMA resources
	 */
	bus_dmamap_t	cque_map;
	nvme_allres_t	*kcomq;
	bus_addr_t	pcomq;
} nvme_comqueue_t;

typedef struct nvme_softns {
	struct nvme_softc *sc;
	nvme_ident_ns_data_t idns;
	struct bio_queue_head bioq;	/* excess BIOs */
	struct lock	lk;		/* mostly for bioq handling */
	int		state;
	int		unit;
	uint32_t	nsid;
	uint32_t	blksize;
	struct disk	disk;		/* disk attachment */
	struct devstat	stats;
	cdev_t		cdev;		/* disk device (cdev) */
} nvme_softns_t;

#define NVME_NSC_STATE_UNATTACHED	0
#define NVME_NSC_STATE_ATTACHED		1


typedef struct nvme_softc {
	TAILQ_ENTRY(nvme_softc) entry;	/* global list */
	device_t	dev;		/* core device */
	const nvme_device_t *ad;	/* quirk detection etc */
	thread_t	admintd;
	uint32_t	maxqe;
	uint64_t	cap;
	uint32_t	vers;
	uint32_t	dstrd4;		/* doorbell stride */
	uint32_t	entimo;		/* enable timeout in ticks */
	uint16_t	niosubqs;	/* #of I/O submission queues */
	uint16_t	niocomqs;	/* #of I/O completion queues */
	uint16_t	dumpqno;
	uint16_t	eventqno;
	uint16_t	qmap[SMP_MAXCPU][2];
	uint32_t	flags;
	nvme_subqueue_t	subqueues[NVME_MAX_QUEUES];
	nvme_comqueue_t	comqueues[NVME_MAX_QUEUES];
	int		cputovect[SMP_MAXCPU];

	/*
	 * bio/disk layer tracking
	 */
	ulong		opencnt;

	/*
	 * admin queue irq resources
	 * register map resources
	 */
	struct resource	*regs;
	struct resource	*bar4;
	bus_space_tag_t	iot;
	bus_space_handle_t ioh;
	int		rid_regs;
	int		rid_bar4;

	int		nirqs;
	int		irq_type;
	struct resource	*irq[NVME_MAX_QUEUES];
	int		rid_irq[NVME_MAX_QUEUES];
	void		*irq_handle[NVME_MAX_QUEUES];

	/*
	 * dma tags
	 */
	bus_dma_tag_t	prps_tag;	/* worst-case PRP table(s) per queue */
	bus_dma_tag_t	sque_tag;	/* submission queue */
	bus_dma_tag_t	cque_tag;	/* completion queue */
	bus_dma_tag_t	adm_tag;	/* DMA data buffers for admin cmds */
	size_t		prp_bytes;
	size_t		cmd_bytes;
	size_t		res_bytes;
	size_t		adm_bytes;

	/*
	 * Admin thread and controller identify data
	 */
	uint32_t	admin_signal;
	struct lock	admin_lk;
	struct lock	ioctl_lk;
	int		(*admin_func)(struct nvme_softc *);
	nvme_ident_ctlr_data_t idctlr;
	nvme_softns_t	*nscary[NVME_MAX_NAMESPACES];
	int		nscmax;
} nvme_softc_t;

#define NVME_SC_ATTACHED	0x00000001
#define NVME_SC_UNLOADING	0x00000002

#define ADMIN_SIG_STOP		0x00000001
#define ADMIN_SIG_RUNNING	0x00000002
#define ADMIN_SIG_PROBED	0x00000004
#define ADMIN_SIG_REQUEUE	0x00000008
#define ADMIN_SIG_RUN_MASK	(ADMIN_SIG_STOP | ADMIN_SIG_REQUEUE)

#define NVME_QMAP_RD		0
#define NVME_QMAP_WR		1

/*
 * Prototypes
 */
const nvme_device_t *nvme_lookup_device(device_t dev);
void nvme_os_sleep(int ms);
int nvme_os_softsleep(void);
void nvme_os_hardsleep(int us);
u_int32_t nvme_read(nvme_softc_t *sc, bus_size_t r);
u_int64_t nvme_read8(nvme_softc_t *sc, bus_size_t r);
void nvme_write(nvme_softc_t *sc, bus_size_t r, u_int32_t v);
void nvme_write8(nvme_softc_t *sc, bus_size_t r, u_int64_t v);
int nvme_enable(nvme_softc_t *sc, int enable);
int nvme_alloc_subqueue(nvme_softc_t *sc, uint16_t qid);
int nvme_alloc_comqueue(nvme_softc_t *sc, uint16_t qid);
void nvme_free_subqueue(nvme_softc_t *sc, uint16_t qid);
void nvme_free_comqueue(nvme_softc_t *sc, uint16_t qid);

nvme_request_t *nvme_get_admin_request(nvme_softc_t *sc, uint8_t opcode);
nvme_request_t *nvme_get_request(nvme_subqueue_t *queue, uint8_t opcode,
			char *kva, size_t bytes);
void nvme_submit_request(nvme_request_t *req);
int nvme_wait_request(nvme_request_t *req, int ticks);
void nvme_put_request(nvme_request_t *req);
void nvme_poll_completions(nvme_comqueue_t *queue, struct lock *lk);

int nvme_start_admin_thread(nvme_softc_t *sc);
void nvme_stop_admin_thread(nvme_softc_t *sc);

int nvme_create_subqueue(nvme_softc_t *sc, uint16_t qid);
int nvme_create_comqueue(nvme_softc_t *sc, uint16_t qid);
int nvme_delete_subqueue(nvme_softc_t *sc, uint16_t qid);
int nvme_delete_comqueue(nvme_softc_t *sc, uint16_t qid);
int nvme_issue_shutdown(nvme_softc_t *sc);

void nvme_disk_attach(nvme_softns_t *nsc);
void nvme_disk_detach(nvme_softns_t *nsc);
void nvme_disk_requeues(nvme_softc_t *sc);
int nvme_alloc_disk_unit(void);

int nvme_getlog_ioctl(nvme_softc_t *sc, nvme_getlog_ioctl_t *ioc);

void nvme_intr(void *arg);
size_t string_cleanup(char *str, int domiddle);
