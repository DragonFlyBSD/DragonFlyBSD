/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP event dispatch boundary for the native NVIDIA GPU driver.
 */

#include "nvgsp_event.h"
#include "nvgpu_intr.h"
#include "nvgsp_disp.h"
#include "nvgsp_priv.h"

#include <sys/ktr.h>

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, gsp_init_done, 18,
    "gsp init done stage=%u polls=%d running=%u", uint32_t stage,
    int polls, uint32_t running);

#define NVGSP_EVENT_GPFIFO_ENTRIES	512u
#define NVGSP_EVENT_USERD_SLOT_SIZE	0x200u
#define NVGSP_EVENT_USERD_GP_GET	0x88u
#define NVGSP_EVENT_USERD_GP_PUT	0x8cu
#define NVGSP_EVENT_POST_DWORDS		13u
#define NVGSP_EVENT_JOURNAL_COMMON_BYTES 40u
#define NVGSP_EVENT_JOURNAL_TYPE_OFFSET	(NVGSP_EVENT_JOURNAL_COMMON_BYTES + 8u)
#define NVGSP_EVENT_JOURNAL_FLAGS_OFFSET (NVGSP_EVENT_JOURNAL_COMMON_BYTES + 12u)
#define NVGSP_EVENT_JOURNAL_COUNT_OFFSET (NVGSP_EVENT_JOURNAL_COMMON_BYTES + 16u)
#define NVGSP_EVENT_JOURNAL_DATA_OFFSET	(NVGSP_EVENT_JOURNAL_COMMON_BYTES + 28u)
#define NVGSP_EVENT_JOURNAL_ENTRY_BYTES	16u
#define NVGSP_EVENT_JOURNAL_MAX_ENTRIES	200u

static void
nvgsp_event_log_queue_snapshot(struct nvgsp_state *gsp, const char *tag)
{
	uint8_t *cmdq, *msgq, *slot;
	uint32_t cmdq_tx, cmdq_rx, msgq_tx, msgq_rx;
	uint32_t fn = 0, len = 0, sig = 0;

	if (gsp->gsp_shm.kva == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp %s: shm is not allocated\n", tag);
		return;
	}
	cmdq = (uint8_t *)gsp->gsp_shm.kva + gsp->gsp_shm_cmdq_off;
	msgq = (uint8_t *)gsp->gsp_shm.kva + gsp->gsp_shm_msgq_off;
	cmdq_tx = *(volatile uint32_t *)(cmdq + 0x10);
	cmdq_rx = *(volatile uint32_t *)(cmdq + 0x20);
	msgq_tx = *(volatile uint32_t *)(msgq + 0x10);
	msgq_rx = *(volatile uint32_t *)(msgq + 0x20);

	if (gsp->gsp_msgq_rptr != msgq_tx) {
		slot = msgq + 0x1000 + gsp->gsp_msgq_rptr * 0x1000;
		sig = *(volatile uint32_t *)(slot + 48 + 4);
		len = *(volatile uint32_t *)(slot + 48 + 8);
		fn = *(volatile uint32_t *)(slot + 48 + 12);
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp %s: cmdq(tx=%u rx=%u) msgq(tx=%u rx=%u host=%u) "
	    "next(sig=0x%08x fn=0x%x len=%u)\n", tag,
	    cmdq_tx, cmdq_rx, msgq_tx, msgq_rx, gsp->gsp_msgq_rptr,
	    sig, fn, len);
}

static int
nvgsp_event_on_init_done(void *priv, uint32_t fn, void *repv, uint32_t repc)
{
	struct nvgsp_state *gsp = priv;

	(void)fn;
	(void)repv;
	(void)repc;
	gsp->gsp_running = true;
	KTR_LOG(nvgpu_gsp_init_done, 1u, 0, 1u);
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp init-done event received\n");
	return (0);
}

static int
nvgsp_event_log_only(void *priv, uint32_t fn, void *repv, uint32_t repc)
{
	(void)priv;
	(void)repv;
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp event fn=0x%x len=%u\n", fn, repc);
	if (fn == 0x1006 && repc > 0) {
		const uint8_t *bytes = repv;
		char text[161];
		uint32_t start = repc >= 12 ? 12 : 0;
		uint32_t n = repc > start ? repc - start : 0;
		uint32_t i;

		if (n >= sizeof(text))
			n = sizeof(text) - 1;
		for (i = 0; i < n; i++) {
			uint8_t c = bytes[start + i];
			text[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
		}
		text[n] = '\0';
		nvgpu_log(NVGPU_LOG_INFO, "gsp os-error-log: %s\n", text);
	}
	return (0);
}

static int
nvgsp_event_on_post_event(void *priv, uint32_t fn, void *repv, uint32_t repc)
{
	struct nvgsp_state *gsp = priv;
	const uint8_t *data = repv;
	uint32_t client_handle;
	uint32_t event_handle;
	uint32_t status;
	uint32_t event_size;
	int error;

	(void)fn;
	if (repc < 32)
		return (EINVAL);
	client_handle = *(const uint32_t *)(const void *)(data + 0);
	event_handle = *(const uint32_t *)(const void *)(data + 4);
	status = *(const uint32_t *)(const void *)(data + 20);
	event_size = *(const uint32_t *)(const void *)(data + 24);
	if (event_size != repc - 32)
		return (EINVAL);
	if (status != 0) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "GSP display event failed client=0x%x event=0x%x status=0x%x\n",
		    client_handle, event_handle, status);
		return (EIO);
	}
	/* eventData starts at byte 29; bytes 29..31 overlap header tail padding. */
	error = nvgsp_disp_dispatch_event(gsp->gpu, client_handle, event_handle,
	    data + 29, event_size);
	if (error == ENOENT)
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "unhandled GSP event client=0x%x event=0x%x size=%u\n",
		    client_handle, event_handle, event_size);
	return (error == ENOENT ? 0 : error);
}

static int
nvgsp_event_on_rc_triggered(void *priv, uint32_t fn, void *repv, uint32_t repc)
{
	struct nvgsp_state *gsp = priv;
	struct nvgsp_channel *chan = NULL;
	struct {
		uint32_t engine_type;
		uint32_t chid;
		uint32_t gfid;
		uint32_t except_level;
		uint32_t except_type;
		uint32_t scope;
		uint16_t partition_id;
		uint8_t pad0[2];
		uint32_t fault_addr_lo;
		uint32_t fault_addr_hi;
		uint32_t fault_type;
		uint8_t callback_needed;
		uint8_t pad1[3];
		uint32_t journal_size;
	} *rc = repv;
	const uint8_t *journal;
	uint32_t journal_bytes;
	uint64_t fault_addr;
	bool dump_fault = false;

	(void)fn;
	if (repc < sizeof(*rc)) {
		nvgpu_log(NVGPU_LOG_INFO, "RC_TRIGGERED short message len=%u\n",
		    repc);
		return (0);
	}
	fault_addr = ((uint64_t)rc->fault_addr_hi << 32) | rc->fault_addr_lo;
	nvgpu_log(NVGPU_LOG_INFO,
	    "RC_TRIGGERED engine=%u chid=%u level=%u type=0x%x scope=%u "
	    "address=0x%jx fault=0x%x journal=%u\n", rc->engine_type,
	    rc->chid, rc->except_level, rc->except_type, rc->scope,
	    (uintmax_t)fault_addr, rc->fault_type, rc->journal_size);

	/* The msgq drain holds gsp_tok, so the borrowed channel cannot be freed. */
	ASSERT_LWKT_TOKEN_HELD(&gsp->gsp_tok);
	if (rc->chid < 2048) {
		lwkt_gettoken(&gsp->chid_tok);
		chan = gsp->chid_channel[rc->chid];
		if (chan != NULL && !chan->fault_dumped) {
			chan->fault_dumped = 1;
			dump_fault = true;
		}
		lwkt_reltoken(&gsp->chid_tok);
	}
	if (dump_fault && chan->userd_bar1_gva != 0 &&
	    chan->submit_gpf.kva != NULL && chan->submit_push.kva != NULL &&
	    chan->submit_sema.kva != NULL) {
		uint32_t *gpf = chan->submit_gpf.kva;
		uint32_t *post = chan->submit_push.kva;
		uint64_t slot = chan->userd_bar1_gva +
		    (uint64_t)(rc->chid % 8u) * NVGSP_EVENT_USERD_SLOT_SIZE;
		uint32_t get = nvgsp_bar_rd32_bar1(gsp,
		    slot + NVGSP_EVENT_USERD_GP_GET) &
		    (NVGSP_EVENT_GPFIFO_ENTRIES - 1);
		uint32_t put = nvgsp_bar_rd32_bar1(gsp,
		    slot + NVGSP_EVENT_USERD_GP_PUT) &
		    (NVGSP_EVENT_GPFIFO_ENTRIES - 1);
		uint32_t shown = 0;

		nvgpu_log(NVGPU_LOG_INFO,
		    "RC channel chid=%u token=0x%x get=%u put=%u shadow_put=%u "
		    "free=%u busy=0x%jx payload=%u gpf=0x%jx push=0x%jx sema=0x%jx\n",
		    rc->chid, chan->gsp_token, get, put, chan->gpf_put,
		    chan->gpf_free, (uintmax_t)chan->submit_post_slots_busy,
		    chan->submit_payload, (uintmax_t)chan->submit_gva_gpf,
		    (uintmax_t)chan->submit_gva_push,
		    (uintmax_t)chan->submit_gva_sema);
		cpu_lfence();
		for (int delta = -2; delta <= 4; delta++) {
			uint32_t index = (get + NVGSP_EVENT_GPFIFO_ENTRIES + delta) &
			    (NVGSP_EVENT_GPFIFO_ENTRIES - 1);
			uint32_t lo = gpf[index * 2];
			uint32_t hi = gpf[index * 2 + 1];
			uint64_t va = (uint64_t)lo | ((uint64_t)(hi & 0xffu) << 32);
			uint32_t bytes = ((hi >> 10) & 0x1fffffu) * 4;

			nvgpu_log(NVGPU_LOG_INFO,
			    "RC gpf[%u]%s lo=0x%08x hi=0x%08x va=0x%jx bytes=%u noprefetch=%u\n",
			    index, delta == 0 ? " GET" : "", lo, hi,
			    (uintmax_t)va, bytes, hi >> 31);
		}
		for (uint32_t i = 0; i < NVGSP_CHANNEL_POST_RING_SLOTS && shown < 8;
		    i++) {
			volatile uint32_t *sema;
			uint32_t *words;

			if ((chan->submit_post_slots_busy & (1ULL << i)) == 0)
				continue;
			sema = (volatile uint32_t *)chan->submit_sema.kva + i;
			words = post + i * NVGSP_EVENT_POST_DWORDS;
			cpu_lfence();
			nvgpu_log(NVGPU_LOG_INFO,
			    "RC post[%u] expected=%u actual=%u words="
			    "%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
			    i, chan->submit_post_payload[i], *sema,
			    words[0], words[1], words[2], words[3], words[4],
			    words[5], words[11], words[12]);
			shown++;
		}
	} else if (dump_fault) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "RC channel snapshot unavailable chid=%u channel=%p\n",
		    rc->chid, chan);
	}
	if (dump_fault && chan != NULL && chan->mthdbuf.kva != NULL) {
		const uint32_t *method = chan->mthdbuf.kva;
		uint32_t shown = 0;

		cpu_lfence();
		for (uint32_t i = 0; i < chan->mthdbuf.size / sizeof(*method) &&
		    shown < 32; i++) {
			if (method[i] == 0)
				continue;
			nvgpu_log(NVGPU_LOG_INFO,
			    "RC method-buffer[%#x]=%08x\n",
			    i * (uint32_t)sizeof(*method), method[i]);
			shown++;
		}
		if (shown == 0)
			nvgpu_log(NVGPU_LOG_INFO, "RC method-buffer is empty\n");
	}
	for (uint32_t i = 0; dump_fault && i < 12; i++) {
		uint32_t base = i * 8192u;
		uint32_t intr0 = nvgsp_rd32(gsp, 0x00040108u + base);
		uint32_t intr1 = nvgsp_rd32(gsp, 0x00040148u + base);
		uint32_t channel = nvgsp_rd32(gsp, 0x00040120u + base);

		if (intr0 == 0 && intr1 == 0 && (channel & 0xfffu) != rc->chid)
			continue;
		nvgpu_log(NVGPU_LOG_INFO,
		    "RC pbdma[%u] intr0=0x%08x intr1=0x%08x status=0x%08x channel=0x%08x\n",
		    i, intr0, intr1, nvgsp_rd32(gsp, 0x00040100u + base), channel);
		nvgpu_log(NVGPU_LOG_INFO,
		    "RC pbdma[%u] gp base=%08x:%08x get=%u put=%u fetch=%u shadow=%08x:%08x\n",
		    i, nvgsp_rd32(gsp, 0x0004004cu + base),
		    nvgsp_rd32(gsp, 0x00040048u + base),
		    nvgsp_rd32(gsp, 0x00040014u + base),
		    nvgsp_rd32(gsp, 0x00040000u + base),
		    nvgsp_rd32(gsp, 0x00040050u + base),
		    nvgsp_rd32(gsp, 0x00040114u + base),
		    nvgsp_rd32(gsp, 0x00040110u + base));
		nvgpu_log(NVGPU_LOG_INFO,
		    "RC pbdma[%u] pb fetch=%08x:%08x get=%08x:%08x put=%08x:%08x "
		    "header=%08x count=%08x\n",
		    i, nvgsp_rd32(gsp, 0x00040058u + base),
		    nvgsp_rd32(gsp, 0x00040054u + base),
		    nvgsp_rd32(gsp, 0x0004001cu + base),
		    nvgsp_rd32(gsp, 0x00040018u + base),
		    nvgsp_rd32(gsp, 0x00040060u + base),
		    nvgsp_rd32(gsp, 0x0004005cu + base),
		    nvgsp_rd32(gsp, 0x00040084u + base),
		    nvgsp_rd32(gsp, 0x00040088u + base));
		nvgpu_log(NVGPU_LOG_INFO,
		    "RC pbdma[%u] method0=%08x data0=%08x\n", i,
		    nvgsp_rd32(gsp, 0x000400c0u + base),
		    nvgsp_rd32(gsp, 0x000400c4u + base));
	}

	journal = (const uint8_t *)(rc + 1);
	journal_bytes = repc - sizeof(*rc);
	if (journal_bytes > rc->journal_size)
		journal_bytes = rc->journal_size;
	for (uint32_t record_off = 0; dump_fault &&
	    record_off + NVGSP_EVENT_JOURNAL_DATA_OFFSET <= journal_bytes;) {
		uint32_t header, record_size, flags;
		uint16_t type, count;
		uint32_t available;

		memcpy(&header, journal + record_off, sizeof(header));
		record_size = header >> 16;
		if (record_size < NVGSP_EVENT_JOURNAL_DATA_OFFSET ||
		    record_size > journal_bytes - record_off) {
			nvgpu_log(NVGPU_LOG_INFO,
			    "RC journal malformed offset=%u header=0x%08x remaining=%u\n",
			    record_off, header, journal_bytes - record_off);
			break;
		}
		memcpy(&type, journal + record_off + NVGSP_EVENT_JOURNAL_TYPE_OFFSET,
		    sizeof(type));
		memcpy(&flags, journal + record_off + NVGSP_EVENT_JOURNAL_FLAGS_OFFSET,
		    sizeof(flags));
		memcpy(&count, journal + record_off + NVGSP_EVENT_JOURNAL_COUNT_OFFSET,
		    sizeof(count));
		available = (record_size - NVGSP_EVENT_JOURNAL_DATA_OFFSET) /
		    NVGSP_EVENT_JOURNAL_ENTRY_BYTES;
		if (count > available)
			count = available;
		if (count > NVGSP_EVENT_JOURNAL_MAX_ENTRIES)
			count = NVGSP_EVENT_JOURNAL_MAX_ENTRIES;
		nvgpu_log(NVGPU_LOG_INFO,
		    "RC journal record offset=%u size=%u type=%u flags=0x%x count=%u\n",
		    record_off, record_size, type, flags, count);
		for (uint32_t i = 0; i < count; i++) {
			uint32_t words[4];
			uint32_t entry_off = record_off + NVGSP_EVENT_JOURNAL_DATA_OFFSET +
			    i * NVGSP_EVENT_JOURNAL_ENTRY_BYTES;

			memcpy(words, journal + entry_off, sizeof(words));
			if (words[2] == 0)
				continue;
			nvgpu_log(NVGPU_LOG_INFO,
			    "RC journal reg[%u] offset=0x%08x tag=0x%08x value=0x%08x attr=0x%08x\n",
			    i, words[0], words[1], words[2], words[3]);
		}
		record_off += record_size;
	}
	nvgpu_intr_report_channel_fault(gsp->gpu, rc->chid);
	return (0);
}

/* Register GSP event handlers before the init-done wait starts. */
int
nvgsp_event_init(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	int error;

	if (gsp == NULL)
		return (ENXIO);
	nvgsp_rpc_init_msg_ntfy(gsp);
	error = nvgsp_rpc_add_msg_ntfy(gsp, NV_VGPU_MSG_EVENT_GSP_INIT_DONE,
	    nvgsp_event_on_init_done, gsp);
	if (error != 0)
		return (error);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x1002, nvgsp_seq_handle_msg, gsp);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x1020, NULL, NULL);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x101c, NULL, NULL);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x1003, nvgsp_event_on_post_event, gsp);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x1004, nvgsp_event_on_rc_triggered,
	    gsp);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x1005, nvgsp_event_log_only, gsp);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x1006, nvgsp_event_log_only, gsp);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x100c, NULL, NULL);
	(void)nvgsp_rpc_add_msg_ntfy(gsp, 0x100f, NULL, NULL);
	return (0);
}

/* Wait for the GSP init-done event. */
int
nvgsp_event_poll_init_done(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	int spin;

	if (gsp == NULL)
		return (ENXIO);
	KTR_LOG(nvgpu_gsp_init_done, 0u, 0,
	    gsp->gsp_running ? 1u : 0u);
	for (spin = 0; spin < 5000 && !gsp->gsp_running; spin++) {
		(void)nvgsp_rpc_dispatch_all_msgs(gsp);
		if (gsp->gsp_running)
			break;
		DELAY(1000);
	}
	if (!gsp->gsp_running)
		nvgsp_event_log_queue_snapshot(gsp, "init-timeout");
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp init-done wait %s after %d ms\n",
	    gsp->gsp_running ? "completed" : "timed out", spin);
	KTR_LOG(nvgpu_gsp_init_done, 2u, spin,
	    gsp->gsp_running ? 1u : 0u);
	return (gsp->gsp_running ? 0 : ETIMEDOUT);
}

/* Dispatch pending GSP events. */
void
nvgsp_event_dispatch(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp != NULL)
		(void)nvgsp_rpc_dispatch_all_msgs(gsp);
}
