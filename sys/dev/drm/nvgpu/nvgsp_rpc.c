/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP-RM RPC mechanism (r570; uses r535-shared layout).
 *
 * Mirrors nouveau drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/rpc.c.
 * r570 inherits this verbatim via .rpc = &r535_rpc in r570/rm.c.
 *
 * Layering:
 *   public API (declared in nvgsp_priv.h):
 *     nvgsp_rpc_get(fn, argc)     — alloc buffer, init headers, return params ptr
 *     nvgsp_rpc_push(params, policy, repc) — send + handle reply per policy
 *     nvgsp_rpc_done(buf)         — kfree the buffer
 *     nvgsp_msg_ntfy_add(fn, h, p) — register event handler
 *   inline convenience helpers (in nvgsp_priv.h):
 *     nvgsp_rpc_rd(fn, argc) = get + push(RECV, argc)
 *     nvgsp_rpc_wr(params, policy) = push(policy, 0) + done
 *
 * Buffer lifecycle (matches nouveau):
 *   [r535_gsp_msg outer hdr 48B][nvfw_gsp_rpc inner hdr 32B][params argc B]
 *   rpc_get   → alloc + init hdrs, return ptr to params (offset 80)
 *   caller    → fills params
 *   rpc_push  → cmdq_push (consumes/frees the buffer), then handle_reply
 *   for RECV: handle_reply → msg_recv allocates NEW buffer, copies reply,
 *             returns params ptr of reply buffer
 *   rpc_done → kfree the reply buffer
 *
 * Doorbell decision: rung iff sc->gsp_running is true (i.e. GSP_INIT_DONE
 * already seen). Pre-init RPCs sit in cmdq and GSP-RM polls them itself.
 */

#include "nvgsp_priv.h"
#include "nvgsp_falcon.h"
#include "nvgsp_abi.h"

#include <sys/libkern.h>
#include <sys/time.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#define NVGSP_PAGE_SIZE	4096u
#define NVGSP_MSG_HDR_SIZE	48		/* r535_gsp_msg outer */
#define NVGSP_RPC_HDR_SIZE	32		/* nvfw_gsp_rpc inner */
#define NVGSP_HDR_TOTAL	(NVGSP_MSG_HDR_SIZE + NVGSP_RPC_HDR_SIZE)
#define NVGSP_MSGCOUNT	63u		/* fixed: (256K - 4K) / 4K */
#define NVGSP_MAX_MSG_PAGES	16u		/* GSP_MSG_MAX_SIZE / PAGE */
#define NVGSP_MAX_PAYLOAD	\
	(NVGSP_PAGE_SIZE * NVGSP_MAX_MSG_PAGES - NVGSP_HDR_TOTAL)
#define NVGSP_SIGNATURE	0x43505256u	/* 'C''P''R''V' LE */
#define NVGSP_RPC_DEBUG_QUEUES	1
#define NVGSP_RPC_FAST_POLL_US	25000
#define NVGSP_RPC_FAST_POLL_STEP_US	10

struct nvgsp_msg_env {
	uint8_t  auth_tag_buffer[16];
	uint8_t  aad_buffer[16];
	uint32_t checksum;
	uint32_t sequence;
	uint32_t elem_count;
	uint32_t pad;
	uint8_t  data[];
} __packed;

struct nvgsp_nvfw_gsp_rpc {
	uint32_t header_version;
	uint32_t signature;
	uint32_t length;
	uint32_t function;
	uint32_t rpc_result;
	uint32_t rpc_result_private;
	uint32_t sequence;
	uint32_t spare;
	uint8_t  data[];
} __packed;

/* container_of-style: from params ptr (after the two headers) walk back. */
static inline struct nvgsp_nvfw_gsp_rpc *
params_to_rpc(void *params)
{
	return (struct nvgsp_nvfw_gsp_rpc *)((uint8_t *)params -
	    NVGSP_RPC_HDR_SIZE);
}
static inline struct nvgsp_msg_env *
rpc_to_msg(struct nvgsp_nvfw_gsp_rpc *rpc)
{
	return (struct nvgsp_msg_env *)((uint8_t *)rpc -
	    NVGSP_MSG_HDR_SIZE);
}

static void
nvgsp_rpc_dump_diag_queues(struct nvgsp_state *sc, const char *tag,
    uint32_t fn, uint32_t seq)
{
#if NVGSP_RPC_DEBUG_QUEUES
	uint8_t *cmdq, *msgq;
	uint32_t cmdq_tx, cmdq_rx, msgq_tx, msgq_rx;

	if (sc->gsp_shm.kva == NULL)
		return;

	cmdq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_cmdq_off;
	msgq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_msgq_off;
	cmdq_tx = *(volatile uint32_t *)(cmdq + 0x10);
	cmdq_rx = *(volatile uint32_t *)(cmdq + 0x20);
	msgq_tx = *(volatile uint32_t *)(msgq + 0x10);
	msgq_rx = *(volatile uint32_t *)(msgq + 0x20);

	nvgpu_log(NVGPU_LOG_DEBUG, "gsp_rpc: %s fn=%u seq=%u cmdq(tx=%u rx=%u) "
	    "msgq(tx=%u rx=%u host_msgq_rptr=%u)\n",
	    tag, fn, seq, cmdq_tx, cmdq_rx, msgq_tx, msgq_rx,
	    sc->gsp_msgq_rptr);
#else
	(void)sc;
	(void)tag;
	(void)fn;
	(void)seq;
#endif
}

/* ===================================================================
 * Notify (event handler) registry. Fixed-size table.
 * =================================================================== */

void
nvgsp_rpc_init_msg_ntfy(struct nvgsp_state *sc)
{
	sc->gsp_ntfy.cnt = 0;
}

int
nvgsp_rpc_add_msg_ntfy(struct nvgsp_state *sc, uint32_t fn,
    nvgsp_msg_ntfy_func handler, void *priv)
{
	uint32_t i;

	if (sc->gsp_ntfy.cnt >= NVGSP_NTFY_MAX)
		return (ENOSPC);
	for (i = 0; i < sc->gsp_ntfy.cnt; i++) {
		if (sc->gsp_ntfy.tab[i].fn == fn)
			return (EEXIST);
	}
	sc->gsp_ntfy.tab[sc->gsp_ntfy.cnt].fn   = fn;
	sc->gsp_ntfy.tab[sc->gsp_ntfy.cnt].func = handler;
	sc->gsp_ntfy.tab[sc->gsp_ntfy.cnt].priv = priv;
	sc->gsp_ntfy.cnt++;
	return (0);
}

static int
nvgsp_rpc_handle_msg(struct nvgsp_state *sc, uint32_t fn,
    void *repv, uint32_t repc)
{
	uint32_t i;

	for (i = 0; i < sc->gsp_ntfy.cnt; i++) {
		if (sc->gsp_ntfy.tab[i].fn != fn)
			continue;
		if (sc->gsp_ntfy.tab[i].func == NULL)
			return (0);	/* stub: drain silently */
		return sc->gsp_ntfy.tab[i].func(
		    sc->gsp_ntfy.tab[i].priv, fn, repv, repc);
	}
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp_rpc: unhandled event fn=0x%x len=%u\n",
	    fn, repc);
	return (0);
}

static bool
nvgsp_rpc_is_null_event_msg(struct nvgsp_state *sc, uint32_t fn)
{
	uint32_t i;

	for (i = 0; i < sc->gsp_ntfy.cnt; i++) {
		if (sc->gsp_ntfy.tab[i].fn == fn)
			return (sc->gsp_ntfy.tab[i].func == NULL);
	}
	return (false);
}

static uint64_t
nvgsp_rpc_read_time_us(void)
{
	struct timeval tv;

	microuptime(&tv);
	return ((uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec);
}

static void
nvgsp_rpc_get_trace_aux(struct nvgsp_nvfw_gsp_rpc *rpc, uint32_t *aux,
    uint32_t *aux2)
{
	const uint32_t *d = (const uint32_t *)rpc->data;

	*aux = rpc->length;
	*aux2 = 0;
	if (rpc->length < NVGSP_RPC_HDR_SIZE + sizeof(uint32_t))
		return;

	switch (rpc->function) {
	case 103:	/* NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC */
		if (rpc->length >= NVGSP_RPC_HDR_SIZE + 16) {
			*aux = d[3];	/* hClass */
			*aux2 = d[2];	/* hObject */
		}
		break;
	case 76:	/* NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL */
		if (rpc->length >= NVGSP_RPC_HDR_SIZE + 12) {
			*aux = d[2];	/* cmd */
			*aux2 = d[1];	/* hObject */
		}
		break;
	case 10:	/* NV_VGPU_MSG_FUNCTION_FREE */
		if (rpc->length >= NVGSP_RPC_HDR_SIZE + 12)
			*aux = d[2];	/* hObjectOld */
		break;
	default:
		break;
	}
}

/* Record one entry in the GSP RPC ring trace (debug). Gated by
 * gsp_rpc_trace_on; lock-free circular write (single producer per dir under
 * gsp_tok / ithread, head races are benign for a debug ring). */
static void
nvgsp_rpc_add_trace(struct nvgsp_state *sc, uint8_t dir, uint32_t fn,
    uint32_t seq, uint32_t aux, uint32_t aux2, uint32_t latency_us)
{
	uint32_t i;

	if (!sc->gsp_rpc_trace_on)
		return;
	i = sc->gsp_rpc_trace_head++ % NVGSP_RPC_TRACE_N;
	sc->gsp_rpc_trace[i].time_us = nvgsp_rpc_read_time_us();
	sc->gsp_rpc_trace[i].dir = dir;
	sc->gsp_rpc_trace[i].fn = fn;
	sc->gsp_rpc_trace[i].seq = seq;
	sc->gsp_rpc_trace[i].aux = aux;
	sc->gsp_rpc_trace[i].aux2 = aux2;
	sc->gsp_rpc_trace[i].latency_us = latency_us;
}

/* ===================================================================
 * Layer 1: cmdq write + msgq read primitives.
 * =================================================================== */

static uint32_t
nvgsp_rpc_get_msgq_pages(uint32_t len)
{
	uint32_t total_bytes = NVGSP_MSG_HDR_SIZE + len;
	uint32_t pages = (total_bytes + NVGSP_PAGE_SIZE - 1) /
	    NVGSP_PAGE_SIZE;

	if (pages == 0)
		pages = 1;
	if (pages > 16)
		pages = 16;
	return (pages);
}

static void
nvgsp_rpc_publish_msgq_rptr(struct nvgsp_state *sc)
{
	uint8_t *cmdq;

	cpu_mfence();
	cmdq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_cmdq_off;
	*(volatile uint32_t *)(cmdq + 32) = sc->gsp_msgq_rptr;
}

static bool
nvgsp_rpc_peek_msgq_meta(struct nvgsp_state *sc, uint32_t *out_fn,
    uint32_t *out_len, uint32_t *out_pages)
{
	uint8_t *msgq, *slot;
	struct nvgsp_nvfw_gsp_rpc *rpc;
	uint32_t wptr, len;

	msgq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_msgq_off;
	wptr = *(volatile uint32_t *)(msgq + 0x10);
	if (sc->gsp_msgq_rptr == wptr)
		return (false);

	cpu_lfence();
	slot = msgq + NVGSP_PAGE_SIZE +
	    sc->gsp_msgq_rptr * NVGSP_PAGE_SIZE;
	rpc = (struct nvgsp_nvfw_gsp_rpc *)(slot + NVGSP_MSG_HDR_SIZE);
	if (rpc->signature != NVGSP_SIGNATURE)
		return (false);

	len = rpc->length;
	if (out_fn != NULL)
		*out_fn = rpc->function;
	if (out_len != NULL)
		*out_len = len;
	if (out_pages != NULL)
		*out_pages = nvgsp_rpc_get_msgq_pages(len);
	return (true);
}

static void
nvgsp_rpc_skip_msgq_pages(struct nvgsp_state *sc, uint32_t pages)
{
	sc->gsp_msgq_rptr = (sc->gsp_msgq_rptr + pages) %
	    NVGSP_MSGCOUNT;
	nvgsp_rpc_publish_msgq_rptr(sc);
}

static int
nvgsp_rpc_push_cmdq(struct nvgsp_state *sc, void *params)
{
	struct nvgsp_nvfw_gsp_rpc *rpc = params_to_rpc(params);
	struct nvgsp_msg_env *msg = rpc_to_msg(rpc);

	if (sc->gsp_rpc_trace_on) {
		uint32_t aux, aux2;

		nvgsp_rpc_get_trace_aux(rpc, &aux, &aux2);
		nvgsp_rpc_add_trace(sc, NVGSP_RPC_TX, rpc->function,
		    rpc->sequence, aux, aux2, 0);
	}
	uint8_t *cmdq, *msgq;
	uint32_t rpc_len, hdr_total, padded;
	uint32_t wptr, rptr, free_slots;
	uint64_t csum;
	const uint64_t *cp;
	uint32_t nu64, i, copied;
	int retries;

	if (sc->gsp_shm.kva == NULL) {
		kfree(msg, M_TEMP);
		return (ENXIO);
	}

	rpc_len   = rpc->length;	/* hdr + payload */
	hdr_total = sizeof(*msg) + rpc_len;
	padded    = roundup(hdr_total, NVGSP_PAGE_SIZE);
	if (padded / NVGSP_PAGE_SIZE > NVGSP_MAX_MSG_PAGES) {
		nvgpu_log(NVGPU_LOG_DEBUG, "cmdq_push: fn=%u len=%u exceeds %u pages\n",
		    rpc->function, rpc_len, NVGSP_MAX_MSG_PAGES);
		kfree(msg, M_TEMP);
		return (EINVAL);
	}
	if (padded / NVGSP_PAGE_SIZE >= NVGSP_MSGCOUNT) {
		nvgpu_log(NVGPU_LOG_DEBUG, "cmdq_push: fn=%u len=%u too large for ring\n",
		    rpc->function, rpc_len);
		kfree(msg, M_TEMP);
		return (EINVAL);
	}

	msg->sequence   = sc->gsp_cmdq_seq++;
	msg->elem_count = padded / NVGSP_PAGE_SIZE;
	msg->checksum   = 0;
	msg->pad        = 0;

	nu64 = padded / sizeof(uint64_t);
	cp = (const uint64_t *)msg;
	csum = 0;
	for (i = 0; i < nu64; i++)
		csum ^= cp[i];
	msg->checksum = (uint32_t)(csum >> 32) ^ (uint32_t)(csum & 0xffffffffu);

	cmdq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_cmdq_off;
	msgq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_msgq_off;

	wptr = *(volatile uint32_t *)(cmdq + 0x10);
	for (retries = 1000; retries > 0; retries--) {
		/* GSP's cmdq read cursor lives in msgq.rx (nouveau crossover) */
		rptr = *(volatile uint32_t *)(msgq + 32);
		free_slots = rptr + NVGSP_MSGCOUNT - wptr - 1;
		if (free_slots >= NVGSP_MSGCOUNT)
			free_slots -= NVGSP_MSGCOUNT;
		if (free_slots >= msg->elem_count)
			break;
		DELAY(10);
	}
	if (retries == 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "cmdq_push: timeout waiting for slot\n");
		kfree(msg, M_TEMP);
		return (ETIMEDOUT);
	}

	for (copied = 0; copied < padded; copied += NVGSP_PAGE_SIZE) {
		uint32_t slot = wptr + copied / NVGSP_PAGE_SIZE;
		uint32_t chunk = padded - copied;

		if (slot >= NVGSP_MSGCOUNT)
			slot -= NVGSP_MSGCOUNT;
		if (chunk > NVGSP_PAGE_SIZE)
			chunk = NVGSP_PAGE_SIZE;
		memcpy(cmdq + NVGSP_PAGE_SIZE + slot * NVGSP_PAGE_SIZE,
		    (const uint8_t *)msg + copied, chunk);
	}

	wptr += msg->elem_count;
	if (wptr >= NVGSP_MSGCOUNT)
		wptr -= NVGSP_MSGCOUNT;
	/* The ring payload (memcpy above) must be visible to GSP before the
	 * write pointer that exposes it. cmdq lives in GSP-shared memory and
	 * GSP is a separate processor, so order this explicitly rather than
	 * relying on x86 store ordering (needed on aarch64). */
	cpu_sfence();
	*(volatile uint32_t *)(cmdq + 0x10) = wptr;

	/* Doorbell iff GSP-RM has come up and is event-driven. Fence so the
	 * updated write pointer is visible before the doorbell MMIO write. */
	if (sc->gsp_running) {
		cpu_sfence();
		nvgsp_wr32(sc, sc->chip->gsp_base + 0xc00, 0);
	}

#ifdef NVGSP_DEBUG_RPC_TRACE
	nvgpu_log(NVGPU_LOG_DEBUG, "cmdq_push: fn=%u len=%u wptr=%u seq=%u ring=%d\n",
	    rpc->function, rpc_len, wptr, msg->sequence, sc->gsp_running);
#endif
	kfree(msg, M_TEMP);
	return (0);
}

/*
 * Read one msgq slot into a fresh kmalloc buffer (sized to fit at least
 * gsp_rpc_len bytes). Returns kvbuf on success (caller frees via
 * nvgsp_rpc_done), NULL if no message available, or ERR_PTR-style
 * negative-errno cast as void *.
 *
 * For the common single-element case this is just a memcpy from the
 * cmdq slot. Multi-page continuation is not yet supported; we'll
 * extend later.
 */
static void *
nvgsp_rpc_recv_msgq_elem(struct nvgsp_state *sc, uint32_t want_len,
    uint32_t *out_fn, uint32_t *out_len)
{
	uint8_t *msgq, *slot;
	struct nvgsp_nvfw_gsp_rpc *rpc;
	uint32_t wptr;
	uint32_t fn, len, sig;
	uint32_t alloc_sz, copy_len, copied, msg_off;
	uint8_t *buf;

	msgq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_msgq_off;
	wptr = *(volatile uint32_t *)(msgq + 0x10);
	if (sc->gsp_msgq_rptr == wptr)
		return (NULL);

	/* GSP writes the slot payload before advancing the write pointer we
	 * just read; load-fence so we observe that payload and not a stale
	 * slot (acquire side of the producer's release; needed on aarch64). */
	cpu_lfence();

	slot = msgq + NVGSP_PAGE_SIZE +
	    sc->gsp_msgq_rptr * NVGSP_PAGE_SIZE;
	rpc = (struct nvgsp_nvfw_gsp_rpc *)(slot + NVGSP_MSG_HDR_SIZE);
	sig = rpc->signature;
	len = rpc->length;
	fn  = rpc->function;

	if (sig != NVGSP_SIGNATURE) {
		nvgpu_log(NVGPU_LOG_DEBUG, "msgq[%u]: bad signature 0x%08x (slot=%p) - skipping\n",
		    sc->gsp_msgq_rptr, sig, slot);
		/* Dump first 96 bytes of slot to identify format. */
		for (int i = 0; i < 96; i += 16) {
			nvgpu_log(NVGPU_LOG_DEBUG, "  slot[+%02d]: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\n",
			    i,
			    slot[i+0], slot[i+1], slot[i+2], slot[i+3],
			    slot[i+4], slot[i+5], slot[i+6], slot[i+7],
			    slot[i+8], slot[i+9], slot[i+10], slot[i+11],
			    slot[i+12], slot[i+13], slot[i+14], slot[i+15]);
		}
		/* Skip slot. Per r535: advance + mfence before publishing rptr. */
		sc->gsp_msgq_rptr = (sc->gsp_msgq_rptr + 1) % NVGSP_MSGCOUNT;
		cpu_mfence();
		{
			uint8_t *cmdq = (uint8_t *)sc->gsp_shm.kva + sc->gsp_shm_cmdq_off;
			*(volatile uint32_t *)(cmdq + 32) = sc->gsp_msgq_rptr;
		}
		return (NULL);
	}

	if (len < NVGSP_RPC_HDR_SIZE ||
	    len > NVGSP_PAGE_SIZE * NVGSP_MAX_MSG_PAGES - NVGSP_MSG_HDR_SIZE) {
		nvgpu_log(NVGPU_LOG_DEBUG, "msgq[%u]: invalid rpc length %u fn=0x%x sig=0x%08x - skipping one page\n",
		    sc->gsp_msgq_rptr, len, fn, sig);
		nvgsp_rpc_skip_msgq_pages(sc, 1);
		return (NULL);
	}

	/* Allocate buffer sized max(rpc->length, want_len) so caller can
	 * see at least the requested length even if reply is short. */
	alloc_sz = (len > want_len) ? len : want_len;
	if (alloc_sz < NVGSP_RPC_HDR_SIZE)
		alloc_sz = NVGSP_RPC_HDR_SIZE;
	buf = kmalloc(alloc_sz, M_TEMP, M_WAITOK | M_ZERO);
	copy_len = (len > alloc_sz) ? alloc_sz : len;
	msg_off = NVGSP_MSG_HDR_SIZE;
	for (copied = 0; copied < copy_len;) {
		uint32_t slot_idx = sc->gsp_msgq_rptr + msg_off / NVGSP_PAGE_SIZE;
		uint32_t slot_off = msg_off % NVGSP_PAGE_SIZE;
		uint32_t chunk = NVGSP_PAGE_SIZE - slot_off;

		while (slot_idx >= NVGSP_MSGCOUNT)
			slot_idx -= NVGSP_MSGCOUNT;
		if (chunk > copy_len - copied)
			chunk = copy_len - copied;
		memcpy(buf + copied,
		    msgq + NVGSP_PAGE_SIZE + slot_idx * NVGSP_PAGE_SIZE +
		    slot_off, chunk);
		copied += chunk;
		msg_off += chunk;
	}

	/* Per nouveau r535_gsp_msgq_recv_one_elem: page count comes from
	 * DIV_ROUND_UP(GSP_MSG_HDR_SIZE + rpc->length, GSP_PAGE_SIZE),
	 * not from elemCount header field.
	 */
	nvgsp_rpc_skip_msgq_pages(sc, nvgsp_rpc_get_msgq_pages(len));

	if (out_fn)  *out_fn = fn;
	if (out_len) *out_len = len;
	return (buf);
}

/* Drain the msgq while gsp_tok is held. For each message:
 *   - Function < 0x1000 + matches a pending->fn -> set pending->done + wakeup.
 *   - Otherwise treat as an unsolicited event and dispatch via ntfy table.
 *
 * The pending->reply_buf takes ownership of the kmalloc'd buffer returned
 * by nvgsp_msgq_recv_one_elem.
 */
static void
nvgsp_rpc_drain_msgq_locked(struct nvgsp_state *sc)
{
	for (;;) {
		uint32_t fn = 0, len = 0;
		uint32_t pages = 0;
		void *buf;

		if (nvgsp_rpc_peek_msgq_meta(sc, &fn, &len, &pages) &&
		    fn >= 0x1000 && nvgsp_rpc_is_null_event_msg(sc, fn)) {
			uint32_t plen = (len > NVGSP_RPC_HDR_SIZE) ?
			    len - NVGSP_RPC_HDR_SIZE : 0;

			nvgsp_rpc_add_trace(sc, NVGSP_RPC_EVENT, fn, 0,
			    plen, 0, 0);
			sc->gsp_msgq_null_event_drop_count++;
			sc->gsp_msgq_null_event_drop_bytes += plen;
			sc->gsp_msgq_null_event_last_fn = fn;
			nvgsp_rpc_skip_msgq_pages(sc, pages);
			continue;
		}

		buf = nvgsp_rpc_recv_msgq_elem(sc, 0, &fn, &len);
		if (buf == NULL)
			return;

		if (fn < 0x1000) {
			struct nvgsp_nvfw_gsp_rpc *r =
			    (struct nvgsp_nvfw_gsp_rpc *)buf;
			struct nvgsp_pending *p;
			bool matched = false;

			/*
			 * Route the reply by its echoed sequence, not by
			 * function: with concurrent submitters several RPCs of
			 * the same function can be in flight, and only the
			 * sequence identifies which waiter this reply belongs
			 * to. seq is unique (assigned under gsp_tok) and nonzero
			 * for every awaited request.
			 */
			LIST_FOREACH(p, &sc->gsp_pending, link) {
				if (p->seq == r->sequence) {
					if (sc->gsp_rpc_trace_on) {
						uint64_t now_us =
						    nvgsp_rpc_read_time_us();
						uint32_t latency_us = 0;

						if (p->tx_us != 0 &&
						    now_us >= p->tx_us) {
							uint64_t delta =
							    now_us - p->tx_us;
							latency_us =
							    (delta > UINT32_MAX) ?
							    UINT32_MAX :
							    (uint32_t)delta;
						}
						nvgsp_rpc_add_trace(sc,
						    NVGSP_RPC_RX, fn,
						    r->sequence, len, 0,
						    latency_us);
					}
					p->reply_buf = buf;
					p->reply_len = len;
					/* Release: the reply_buf/_len stores must
					 * precede the done publication so the
					 * waiter (which reads them only after an
					 * acquire-load of done) observes them on
					 * weak-memory archs, not just x86 TSO. */
					atomic_store_rel_int(&p->done, 1);
					wakeup(p);
					matched = true;
					break;
				}
			}
			if (!matched) {
				nvgsp_rpc_add_trace(sc, NVGSP_RPC_STALE,
				    fn, r->sequence, len, 0, 0);
				nvgpu_log(NVGPU_LOG_DEBUG, "gsp_rpc: stale reply fn=%u seq=%u (dropped)\n",
				    fn, r->sequence);
				kfree(buf, M_TEMP);
			}
			continue;
		}

		/* Event >= 0x1000: dispatch via ntfy table, then free. */
		{
			uint32_t plen = (len > NVGSP_RPC_HDR_SIZE) ?
			    len - NVGSP_RPC_HDR_SIZE : 0;
			uint8_t *params = (uint8_t *)buf + NVGSP_RPC_HDR_SIZE;
			nvgsp_rpc_add_trace(sc, NVGSP_RPC_EVENT, fn, 0,
			    plen, 0, 0);
			(void)nvgsp_rpc_handle_msg(sc, fn, params, plen);
		}
		kfree(buf, M_TEMP);
	}
}

/* Public drain entry: takes gsp_tok and runs msgq_drain_locked. Safe to
 * call from any lwkt (ISR or ioctl). */
int
nvgsp_rpc_dispatch_all_msgs(struct nvgsp_state *sc)
{
	lwkt_gettoken(&sc->gsp_tok);
	nvgsp_rpc_drain_msgq_locked(sc);
	lwkt_reltoken(&sc->gsp_tok);
	return (0);
}

/* ===================================================================
 * Layer 2/3: public RPC API.
 * =================================================================== */

/* Allocate request buffer (hdr+argc), init headers. Returns ptr to
 * params area (caller fills in argc bytes). */
void *
nvgsp_rpc_get(struct nvgsp_state *sc, uint32_t fn, uint32_t argc)
{
	struct nvgsp_msg_env *msg;
	struct nvgsp_nvfw_gsp_rpc *rpc;
	uint32_t alloc_sz;

	if (argc > NVGSP_MAX_PAYLOAD) {
		nvgpu_log(NVGPU_LOG_DEBUG, "rpc_get: fn=%u argc=%u exceeds max payload %u\n",
		    fn, argc, NVGSP_MAX_PAYLOAD);
		return (NULL);
	}
	alloc_sz = roundup(NVGSP_HDR_TOTAL + argc, NVGSP_PAGE_SIZE);
	msg = kmalloc(alloc_sz, M_TEMP, M_WAITOK | M_ZERO);
	rpc = (struct nvgsp_nvfw_gsp_rpc *)msg->data;

	rpc->header_version     = 0x03000000;
	rpc->signature          = NVGSP_SIGNATURE;
	rpc->length             = NVGSP_RPC_HDR_SIZE + argc;
	rpc->function           = fn;
	rpc->rpc_result         = 0xffffffffu;
	rpc->rpc_result_private = 0xffffffffu;
	rpc->sequence           = 0;
	rpc->spare              = 0;
	return (rpc->data);
}

/* Send the request buffer. Returns reply params ptr (for RECV) or
 * non-NULL sentinel (for NOWAIT/NOSEQ — buffer freed by cmdq_push), or
 * NULL on error. */
void *
nvgsp_rpc_push(struct nvgsp_state *sc, void *params, int policy,
    uint32_t repc)
{
	struct nvgsp_nvfw_gsp_rpc *rpc = params_to_rpc(params);
	uint32_t fn = rpc->function;
	uint32_t seq = 0;
	int err;

	switch (policy) {
	case NVGSP_RPC_REPLY_NOWAIT:
	case NVGSP_RPC_REPLY_NOSEQ:
		/* Wrap cmdq_push with gsp_tok so concurrent senders serialise.
		 * Assign the inner sequence under the token too, so the counter
		 * is not raced by concurrent senders and the value published in
		 * the request is unique. */
		lwkt_gettoken(&sc->gsp_tok);
		if (policy != NVGSP_RPC_REPLY_NOSEQ) {
			do {
				seq = ++sc->gsp_rpc_seq;
			} while (seq == 0);	/* 0 is reserved for no-reply */
			rpc->sequence = seq;
		}
		if (!sc->gsp_running || (fn == 103 || fn == 76))
			nvgsp_rpc_dump_diag_queues(sc, "before-push", fn, seq);
		err = nvgsp_rpc_push_cmdq(sc, params);	/* frees the buffer */
		if (!sc->gsp_running || (fn == 103 || fn == 76))
			nvgsp_rpc_dump_diag_queues(sc, "after-push", fn, seq);
		lwkt_reltoken(&sc->gsp_tok);
		if (err != 0)
			return (NULL);
		/* Caller passes NULL params in their rpc_wr wrapper.
		 * Return a non-NULL sentinel so the wrapper sees success.
		 * The pointer is never dereferenced. */
		return ((void *)(uintptr_t)1);

	case NVGSP_RPC_REPLY_RECV: {
		struct nvgsp_pending p = { .fn = fn };
		int fast_poll_us;
		int ticks_to_wait;
		int waited_ticks = 0;
		int timeout_ticks = 5 * hz;

		/* Assign a unique sequence under gsp_tok and publish it in the
		 * request before the command becomes visible to GSP. The reply
		 * echoes this sequence, so the drainer routes it to exactly this
		 * waiter even when several concurrent submitters have RPCs of
		 * the same function in flight. Install the pending entry before
		 * pushing so an early drain cannot discard the reply. */
		lwkt_gettoken(&sc->gsp_tok);
		do {
			seq = ++sc->gsp_rpc_seq;
		} while (seq == 0);	/* 0 is reserved for no-reply */
		rpc->sequence = seq;
		p.seq = seq;
		if (sc->gsp_rpc_trace_on)
			p.tx_us = nvgsp_rpc_read_time_us();
		LIST_INSERT_HEAD(&sc->gsp_pending, &p, link);
		if (!sc->gsp_running || (fn == 103 || fn == 76))
			nvgsp_rpc_dump_diag_queues(sc, "before-push", fn, seq);
		err = nvgsp_rpc_push_cmdq(sc, params);	/* frees the buffer */
		if (!sc->gsp_running || (fn == 103 || fn == 76))
			nvgsp_rpc_dump_diag_queues(sc, "after-push", fn, seq);
		if (err != 0) {
			LIST_REMOVE(&p, link);
			lwkt_reltoken(&sc->gsp_tok);
			return (NULL);
		}

		/*
		 * Ownership:
		 *   p is stack-owned by this RPC call.  While it is linked on
		 *   sc->gsp_pending, msgq drainers may borrow it only to store
		 *   the reply pointer, publish done, and wake this waiter.
		 *
		 * Lifetime:
		 *   The pending entry remains valid until we remove it below.
		 *   cmdq_push consumed the request buffer; a RECV reply owns a
		 *   fresh kmalloc buffer that is returned to the caller.
		 *
		 * Threading:
		 *   Keep gsp_tok held across send+reply, matching nouveau's
		 *   cmdq mutex serialization for RM RPCs.  Because the token
		 *   holder is also the only guaranteed msgq consumer for this
		 *   awaited reply, poll the msgq at microsecond granularity
		 *   before falling back to a scheduler tick.  This mirrors
		 *   Linux r535_gsp_msgq_wait()'s fast wptr/rptr polling and
		 *   avoids quantizing every RM RPC to hz/10.
		 */
		if ((fn == 103 || fn == 76))
			nvgsp_rpc_dump_diag_queues(sc, "await-start", fn, p.seq);
		while (!atomic_load_acq_int(&p.done) && timeout_ticks > 0) {
			if ((fn == 103 || fn == 76))
				nvgsp_rpc_dump_diag_queues(sc, "drain-before", fn, p.seq);
			nvgsp_rpc_drain_msgq_locked(sc);
			if ((fn == 103 || fn == 76))
				nvgsp_rpc_dump_diag_queues(sc, "drain-after", fn, p.seq);
			if (atomic_load_acq_int(&p.done))
				break;

			if ((fn == 103 || fn == 76))
				nvgsp_rpc_dump_diag_queues(sc, "fast-start", fn, p.seq);
			for (fast_poll_us = 0;
			    fast_poll_us < NVGSP_RPC_FAST_POLL_US;
			    fast_poll_us += NVGSP_RPC_FAST_POLL_STEP_US) {
				DELAY(NVGSP_RPC_FAST_POLL_STEP_US);
				nvgsp_rpc_drain_msgq_locked(sc);
				if (atomic_load_acq_int(&p.done))
					break;
			}
			if ((fn == 103 || fn == 76))
				nvgsp_rpc_dump_diag_queues(sc, "fast-end", fn, p.seq);
			if (atomic_load_acq_int(&p.done))
				break;

			ticks_to_wait = (timeout_ticks > 1) ? 1 : timeout_ticks;
			if ((fn == 103 || fn == 76))
				nvgsp_rpc_dump_diag_queues(sc, "sleep-before", fn, p.seq);
			(void)tsleep(&p, 0, "gsprpc", ticks_to_wait);
			if ((fn == 103 || fn == 76))
				nvgsp_rpc_dump_diag_queues(sc, "sleep-after", fn, p.seq);
			waited_ticks += ticks_to_wait;
			timeout_ticks -= ticks_to_wait;
			if (hz > 0 && waited_ticks % hz == 0)
				nvgsp_rpc_dump_diag_queues(sc, "wait", fn, p.seq);
		}

		LIST_REMOVE(&p, link);
		lwkt_reltoken(&sc->gsp_tok);

		if (!atomic_load_acq_int(&p.done)) {
			nvgsp_rpc_dump_diag_queues(sc, "timeout", fn, p.seq);
			nvgpu_log(NVGPU_LOG_DEBUG, "rpc_push: timeout waiting for fn=%u seq=%u reply\n",
			    fn, p.seq);
			return (NULL);
		}

		return ((uint8_t *)p.reply_buf + NVGSP_RPC_HDR_SIZE);
	}
	}
	return (NULL);
}

void
nvgsp_rpc_complete(struct nvgsp_state *sc, void *params)
{
	(void)sc;
	if (params == NULL || params == (void *)(uintptr_t)1)
		return;
	/* Reply buffer was kmalloc'd in msgq_recv_one_elem and given to
	 * caller offset by NVGSP_RPC_HDR_SIZE. Walk back to base. */
	kfree((uint8_t *)params - NVGSP_RPC_HDR_SIZE, M_TEMP);
}

/* ===================================================================
 * Convenience: set_system_info, set_registry (NOSEQ pre-boot).
 * =================================================================== */

int
nvgsp_rpc_set_system_info(struct nvgsp_state *sc)
{
	GspSystemInfo *info;

	if (nvgpu_device_get_bar(sc->gpu, 0) == NULL ||
	    nvgpu_device_get_bar(sc->gpu, 1) == NULL ||
	    nvgpu_device_get_bar(sc->gpu, 3) == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "set_system_info: skipped (BARs not allocated)\n");
		return (ENXIO);
	}

	info = nvgsp_rpc_get(sc,
	    NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO, sizeof(*info));
	if (info == NULL)
		return (ENOMEM);

	info->gpuPhysAddr     = rman_get_start(nvgpu_device_get_bar(sc->gpu, 0));
	info->gpuPhysFbAddr   = rman_get_start(nvgpu_device_get_bar(sc->gpu, 1));
	info->gpuPhysInstAddr = rman_get_start(nvgpu_device_get_bar(sc->gpu, 3));
	info->gpuPhysIoAddr   = 0;
	{
		/* Open-rm encoding (g_gpu_nvoc.h:476-478):
		 *   bits 63:32 DOMAIN, bits 15:8 BUS, bits 7:0 DEVICE (devfn).
		 * Matches Linux pci_dev_id() = (bus<<8) | devfn,
		 * where devfn = (slot<<3) | func. */
		uint32_t bus    = pci_get_bus(sc->dev);
		uint32_t slot   = pci_get_slot(sc->dev);
		uint32_t func   = pci_get_function(sc->dev);
		uint32_t dom    = pci_get_domain(sc->dev);
		uint32_t devfn  = ((slot & 0x1f) << 3) | (func & 0x7);
		info->nvDomainBusDeviceFunc =
		    ((uint64_t)dom << 32) | ((bus & 0xff) << 8) | (devfn & 0xff);
	}
	info->maxUserVa          = (1ULL << 47);
	info->pciConfigMirrorBase= 0x88000;
	info->pciConfigMirrorSize= 0x1000;
	info->PCIDeviceID        =
	    ((uint32_t)pci_get_device(sc->dev) << 16) |
	     (uint32_t)pci_get_vendor(sc->dev);
	info->PCISubDeviceID     =
	    ((uint32_t)pci_get_subdevice(sc->dev) << 16) |
	     (uint32_t)pci_get_subvendor(sc->dev);
	info->PCIRevisionID      = pci_get_revid(sc->dev);

	return (nvgsp_rpc_wr(sc, info, NVGSP_RPC_REPLY_NOSEQ));
}

/* rpc_unloading_guest_driver_v1F_07 (layout per nouveau-vendored r570). */
struct nvgsp_rpc_unloading_guest_driver {
	uint8_t  bInPMTransition;
	uint8_t  bGc6Entering;
	uint8_t  pad02[2];
	uint32_t newLevel;
};

int
nvgsp_rpc_get_unloading_guest_driver_state(struct nvgsp_state *sc)
{
	struct nvgsp_rpc_unloading_guest_driver *rpc;

	rpc = nvgsp_rpc_get(sc,
	    NV_VGPU_MSG_FUNCTION_UNLOADING_GUEST_DRIVER, sizeof(*rpc));
	if (rpc == NULL)
		return (ENOMEM);
	rpc->bInPMTransition = 0;	/* driver unload, not suspend */
	rpc->bGc6Entering = 0;
	rpc->pad02[0] = 0;
	rpc->pad02[1] = 0;
	rpc->newLevel = 0;	/* NV2080_..._SET_POWER_STATE_GPU_LEVEL_0 */
	/*
	 * Fire-and-forget: r570 GSP-RM acts on this RPC and halts without
	 * writing a reply (a REPLY_RECV wait just burns its full timeout
	 * while MAILBOX0 already reads 0x80000000).  The authoritative
	 * completion barrier is the MB0 halt poll in nvgsp_shutdown().
	 */
	return (nvgsp_rpc_wr(sc, rpc, NVGSP_RPC_REPLY_NOSEQ));
}

int
nvgsp_rpc_set_registry(struct nvgsp_state *sc)
{
	PACKED_REGISTRY_TABLE *reg;

	reg = nvgsp_rpc_get(sc,
	    NV_VGPU_MSG_FUNCTION_SET_REGISTRY, sizeof(*reg));
	if (reg == NULL)
		return (ENOMEM);
	reg->size = sizeof(*reg);
	reg->numEntries = 0;
	return (nvgsp_rpc_wr(sc, reg, NVGSP_RPC_REPLY_NOSEQ));
}


/* Initialize RPC transport state after nvgsp_state_init. */
int
nvgsp_rpc_init(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return (ENXIO);
	LIST_INIT(&gsp->gsp_pending);
	gsp->gsp_rpc_seq = 0;
	return (0);
}
