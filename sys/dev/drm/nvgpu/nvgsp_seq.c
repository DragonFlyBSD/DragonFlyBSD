/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP RUN_CPU_SEQUENCER event handling.
 *
 * GSP-RM sends a host-side command stream while booting. The stream is
 * rpc_run_cpu_sequencer_v17_00 from r570 nvrm/gsp.h and matches nouveau's
 * r535_gsp_msg_run_cpu_sequencer() handling.
 */

#include "nvgsp_priv.h"
#include "nvgsp_falcon.h"

#define NVGSP_SEQ_OP_REG_WRITE		0
#define NVGSP_SEQ_OP_REG_MODIFY		1
#define NVGSP_SEQ_OP_REG_POLL		2
#define NVGSP_SEQ_OP_DELAY_US		3
#define NVGSP_SEQ_OP_REG_STORE		4
#define NVGSP_SEQ_OP_CORE_RESET		5
#define NVGSP_SEQ_OP_CORE_START		6
#define NVGSP_SEQ_OP_CORE_WAIT_FOR_HALT	7
#define NVGSP_SEQ_OP_CORE_RESUME		8

static const uint8_t nvgsp_seq_payload_dw[] = {
	[NVGSP_SEQ_OP_REG_WRITE]		= 2,
	[NVGSP_SEQ_OP_REG_MODIFY]	= 3,
	[NVGSP_SEQ_OP_REG_POLL]		= 5,
	[NVGSP_SEQ_OP_DELAY_US]		= 1,
	[NVGSP_SEQ_OP_REG_STORE]		= 2,
	[NVGSP_SEQ_OP_CORE_RESET]	= 0,
	[NVGSP_SEQ_OP_CORE_START]	= 0,
	[NVGSP_SEQ_OP_CORE_WAIT_FOR_HALT] = 0,
	[NVGSP_SEQ_OP_CORE_RESUME]	= 0,
};

static bool
nvgsp_seq_poll_reg(struct nvgsp_state *gsp, uint32_t addr, uint32_t mask,
    uint32_t val, uint32_t timeout_us)
{
	uint32_t elapsed = 0;

	if (timeout_us == 0)
		timeout_us = 4000000u;
	while (elapsed < timeout_us) {
		if ((nvgsp_rd32(gsp, addr) & mask) == val)
			return (true);
		DELAY(10);
		elapsed += 10;
	}
	return (false);
}

static int
nvgsp_seq_resume_core(struct nvgsp_state *gsp)
{
	uint32_t gsp_base = gsp->chip->gsp_base;
	uint32_t gsp_riscv = gsp->chip->gsp_riscv;
	uint32_t sec2_base = gsp->chip->sec2_base;
	uint32_t sec2_mb0;
	uint32_t riscv_status;
	uint64_t libos_paddr;

	nvgpu_log(NVGPU_LOG_DEBUG, "seq: CORE_RESUME\n");
	if (gsp->gsp != NULL)
		(void)nvgsp_falcon_reset_eng(gsp->gsp);

	libos_paddr = gsp->gsp_libos.paddr;
	nvgsp_wr32(gsp, gsp_base + 0x040, (uint32_t)libos_paddr);
	nvgsp_wr32(gsp, gsp_base + 0x044, (uint32_t)(libos_paddr >> 32));

	nvgpu_log(NVGPU_LOG_DEBUG, "seq: SEC2 pre-kick CPUCTL=0x%x DMACTL=0x%x MB0=0x%x BOOTVEC=0x%x SCRATCH14=0x%x\n",
	    nvgsp_rd32(gsp, sec2_base + 0x100),
	    nvgsp_rd32(gsp, sec2_base + 0x10c),
	    nvgsp_rd32(gsp, sec2_base + 0x040),
	    nvgsp_rd32(gsp, sec2_base + 0x104),
	    nvgsp_rd32(gsp, 0x1180f8));

	if (gsp->sec2 != NULL)
		nvgsp_falcon_start(gsp->sec2);
	DELAY(100);

	nvgpu_log(NVGPU_LOG_DEBUG, "seq: SEC2 post-kick CPUCTL=0x%x DMACTL=0x%x MB0=0x%x SCRATCH14=0x%x\n",
	    nvgsp_rd32(gsp, sec2_base + 0x100),
	    nvgsp_rd32(gsp, sec2_base + 0x10c),
	    nvgsp_rd32(gsp, sec2_base + 0x040),
	    nvgsp_rd32(gsp, 0x1180f8));

	if (!nvgsp_seq_poll_reg(gsp, 0x1180f8, 0x04000000u, 0x04000000u,
	    2000000u)) {
		nvgpu_log(NVGPU_LOG_DEBUG, "seq: CORE_RESUME timeout waiting for SEC2\n");
		return (ETIMEDOUT);
	}

	sec2_mb0 = nvgsp_rd32(gsp, sec2_base + 0x040);
	if (sec2_mb0 != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "seq: CORE_RESUME SEC2 MB0=0x%x\n", sec2_mb0);
		return (EIO);
	}

	nvgsp_wr32(gsp, gsp_base + 0x080, 0);
	riscv_status = nvgsp_rd32(gsp, gsp_riscv + 0x240);
	if ((riscv_status & 1) == 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "seq: CORE_RESUME failed RISCV_STATUS=0x%x\n", riscv_status);
		return (EIO);
	}

	nvgpu_log(NVGPU_LOG_DEBUG, "seq: CORE_RESUME ok, RISC-V active again\n");
	return (0);
}

int
nvgsp_seq_handle_msg(void *priv, uint32_t fn, void *repv, uint32_t repc)
{
	struct nvgsp_state *gsp = priv;
	const uint32_t *payload = repv;
	uint32_t buf_size_dw, cmd_index;
	uint32_t *reg_save;
	const uint32_t *cmdbuf;
	uint32_t ptr = 0;
	uint32_t op_count = 0;

	(void)fn;
	if (gsp == NULL || repv == NULL)
		return (EINVAL);
	if (repc < (2 + 8) * sizeof(uint32_t)) {
		nvgpu_log(NVGPU_LOG_DEBUG, "seq: payload too small (%u bytes)\n", repc);
		return (EINVAL);
	}

	buf_size_dw = payload[0];
	cmd_index = payload[1];
	reg_save = (uint32_t *)(uintptr_t)&payload[2];
	cmdbuf = &payload[2 + 8];

	nvgpu_log(NVGPU_LOG_DEBUG, "seq: start bufSizeDW=%u cmdIndex=%u\n",
	    buf_size_dw, cmd_index);
	while (ptr < cmd_index) {
		uint32_t opcode = cmdbuf[ptr++];
		uint32_t payload_dw;

		if (opcode >= nitems(nvgsp_seq_payload_dw)) {
			nvgpu_log(NVGPU_LOG_DEBUG, "seq: unknown opcode %u at idx %u\n", opcode, ptr - 1);
			break;
		}
		payload_dw = nvgsp_seq_payload_dw[opcode];
		if (ptr + payload_dw > cmd_index) {
			nvgpu_log(NVGPU_LOG_DEBUG, "seq: truncated opcode %u at idx %u need %u have %u\n",
			    opcode, ptr - 1, payload_dw, cmd_index - ptr);
			break;
		}

		switch (opcode) {
		case NVGSP_SEQ_OP_REG_WRITE:
			nvgsp_wr32(gsp, cmdbuf[ptr], cmdbuf[ptr + 1]);
			break;
		case NVGSP_SEQ_OP_REG_MODIFY: {
			uint32_t addr = cmdbuf[ptr];
			uint32_t mask = cmdbuf[ptr + 1];
			uint32_t val = cmdbuf[ptr + 2];
			uint32_t reg = nvgsp_rd32(gsp, addr);

			nvgsp_wr32(gsp, addr, (reg & ~mask) | (val & mask));
			break;
		}
		case NVGSP_SEQ_OP_REG_POLL:
			if (!nvgsp_seq_poll_reg(gsp, cmdbuf[ptr], cmdbuf[ptr + 1],
			    cmdbuf[ptr + 2], cmdbuf[ptr + 3])) {
				nvgpu_log(NVGPU_LOG_DEBUG, "seq: poll timeout on 0x%06x\n", cmdbuf[ptr]);
			}
			break;
		case NVGSP_SEQ_OP_DELAY_US:
			DELAY(cmdbuf[ptr]);
			break;
		case NVGSP_SEQ_OP_REG_STORE:
			if (cmdbuf[ptr + 1] < 8)
				reg_save[cmdbuf[ptr + 1]] = nvgsp_rd32(gsp, cmdbuf[ptr]);
			break;
		case NVGSP_SEQ_OP_CORE_RESET:
			if (gsp->gsp != NULL)
				(void)nvgsp_falcon_reset_eng(gsp->gsp);
			nvgsp_wr32(gsp, gsp->chip->gsp_base + 0x624,
			    nvgsp_rd32(gsp, gsp->chip->gsp_base + 0x624) | 0x80);
			nvgsp_wr32(gsp, gsp->chip->gsp_base + 0x10c, 0);
			break;
		case NVGSP_SEQ_OP_CORE_START:
			if (nvgsp_rd32(gsp, gsp->chip->gsp_base + 0x100) & 0x40)
				nvgsp_wr32(gsp, gsp->chip->gsp_base + 0x130, 2);
			else
				nvgsp_wr32(gsp, gsp->chip->gsp_base + 0x100, 2);
			break;
		case NVGSP_SEQ_OP_CORE_WAIT_FOR_HALT:
			if (gsp->gsp == NULL ||
			    nvgsp_falcon_wait_for_halt(gsp->gsp, 2000000) != 0) {
				nvgpu_log(NVGPU_LOG_DEBUG, "seq: core wait-halt timeout\n");
			}
			break;
		case NVGSP_SEQ_OP_CORE_RESUME:
			(void)nvgsp_seq_resume_core(gsp);
			break;
		}

		ptr += payload_dw;
		op_count++;
	}

	nvgpu_log(NVGPU_LOG_DEBUG, "seq: done %u ops processed\n", op_count);
	return (0);
}
