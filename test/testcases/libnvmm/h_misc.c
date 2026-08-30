/*
 * Copyright (c) 2026 Maxime Villard, m00nbsd.net
 * All rights reserved.
 *
 * This code is part of the NVMM hypervisor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include <nvmm.h>

#include "h_os.h"

static uint8_t *instbuf;

static uint64_t
run_machine(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_vcpu_exit *exit = vcpu->exit;

	while (1) {
		if (nvmm_vcpu_run(mach, vcpu) == -1)
			err(errno, "nvmm_vcpu_run");

		switch (exit->reason) {
		case NVMM_VCPU_EXIT_NONE:
			break;

		default:
			return exit->reason;
		}
	}
}

/* -------------------------------------------------------------------------- */

static void
init_seg(struct nvmm_x64_state_seg *seg, int type, int sel)
{
	seg->selector = sel;
	seg->attrib.type = type;
	seg->attrib.s = (type & 0b10000) != 0;
	seg->attrib.dpl = 0;
	seg->attrib.p = 1;
	seg->attrib.avl = 1;
	seg->attrib.l = 1;
	seg->attrib.def = 0;
	seg->attrib.g = 1;
	seg->limit = 0x0000FFFF;
	seg->base = 0x00000000;
}

static void
reset_machine64(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_x64_state *state = vcpu->state;

	if (nvmm_vcpu_getstate(mach, vcpu, NVMM_X64_STATE_ALL) == -1)
		err(errno, "nvmm_vcpu_getstate");

	memset(state, 0, sizeof(*state));

	/* Default. */
	state->gprs[NVMM_X64_GPR_RFLAGS] = PSL_MBO;
	init_seg(&state->segs[NVMM_X64_SEG_CS], SDT_MEMERA, GSEL(GCODE_SEL, SEL_KPL));
	init_seg(&state->segs[NVMM_X64_SEG_SS], SDT_MEMRWA, GSEL(GDATA_SEL, SEL_KPL));
	init_seg(&state->segs[NVMM_X64_SEG_DS], SDT_MEMRWA, GSEL(GDATA_SEL, SEL_KPL));
	init_seg(&state->segs[NVMM_X64_SEG_ES], SDT_MEMRWA, GSEL(GDATA_SEL, SEL_KPL));
	init_seg(&state->segs[NVMM_X64_SEG_FS], SDT_MEMRWA, GSEL(GDATA_SEL, SEL_KPL));
	init_seg(&state->segs[NVMM_X64_SEG_GS], SDT_MEMRWA, GSEL(GDATA_SEL, SEL_KPL));

	/* Blank. */
	init_seg(&state->segs[NVMM_X64_SEG_GDT], 0, 0);
	init_seg(&state->segs[NVMM_X64_SEG_IDT], 0, 0);
	init_seg(&state->segs[NVMM_X64_SEG_LDT], SDT_SYSLDT, 0);
	init_seg(&state->segs[NVMM_X64_SEG_TR], SDT_SYS386BSY, 0);

	/* Protected mode enabled. */
	state->crs[NVMM_X64_CR_CR0] = CR0_PG|CR0_PE|CR0_NE|CR0_TS|CR0_MP|CR0_WP|CR0_AM;

	/* 64bit mode enabled. */
	state->crs[NVMM_X64_CR_CR4] = CR4_PAE;
	state->msrs[NVMM_X64_MSR_EFER] = EFER_LME | EFER_SCE | EFER_LMA;

	/* Reset TPR. */
	state->crs[NVMM_X64_CR_CR8] = 0;

	state->msrs[NVMM_X64_MSR_PAT] = MSR_PAT_VALUE;

	/* Page tables. */
	state->crs[NVMM_X64_CR_CR3] = 0x3000;

	state->gprs[NVMM_X64_GPR_RIP] = 0x2000;

	if (nvmm_vcpu_setstate(mach, vcpu, NVMM_X64_STATE_ALL) == -1)
		err(errno, "nvmm_vcpu_setstate");
}

static void
map_pages64(struct nvmm_machine *mach)
{
	pt_entry_t *L4, *L3, *L2, *L1;
	int ret;

	instbuf = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
	    -1, 0);
	if (instbuf == MAP_FAILED)
		err(errno, "mmap");

	if (nvmm_hva_map(mach, (uintptr_t)instbuf, PAGE_SIZE) == -1)
		err(errno, "nvmm_hva_map");
	ret = nvmm_gpa_map(mach, (uintptr_t)instbuf, 0x2000, PAGE_SIZE,
	    PROT_READ|PROT_EXEC);
	if (ret == -1)
		err(errno, "nvmm_gpa_map");

	L4 = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
	    -1, 0);
	if (L4 == MAP_FAILED)
		err(errno, "mmap");
	L3 = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
	    -1, 0);
	if (L3 == MAP_FAILED)
		err(errno, "mmap");
	L2 = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
	    -1, 0);
	if (L2 == MAP_FAILED)
		err(errno, "mmap");
	L1 = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
	    -1, 0);
	if (L1 == MAP_FAILED)
		err(errno, "mmap");

	if (nvmm_hva_map(mach, (uintptr_t)L4, PAGE_SIZE) == -1)
		err(errno, "nvmm_hva_map");
	if (nvmm_hva_map(mach, (uintptr_t)L3, PAGE_SIZE) == -1)
		err(errno, "nvmm_hva_map");
	if (nvmm_hva_map(mach, (uintptr_t)L2, PAGE_SIZE) == -1)
		err(errno, "nvmm_hva_map");
	if (nvmm_hva_map(mach, (uintptr_t)L1, PAGE_SIZE) == -1)
		err(errno, "nvmm_hva_map");

	ret = nvmm_gpa_map(mach, (uintptr_t)L4, 0x3000, PAGE_SIZE,
	    PROT_READ|PROT_WRITE);
	if (ret == -1)
		err(errno, "nvmm_gpa_map");
	ret = nvmm_gpa_map(mach, (uintptr_t)L3, 0x4000, PAGE_SIZE,
	    PROT_READ|PROT_WRITE);
	if (ret == -1)
		err(errno, "nvmm_gpa_map");
	ret = nvmm_gpa_map(mach, (uintptr_t)L2, 0x5000, PAGE_SIZE,
	    PROT_READ|PROT_WRITE);
	if (ret == -1)
		err(errno, "nvmm_gpa_map");
	ret = nvmm_gpa_map(mach, (uintptr_t)L1, 0x6000, PAGE_SIZE,
	    PROT_READ|PROT_WRITE);
	if (ret == -1)
		err(errno, "nvmm_gpa_map");

	memset(L4, 0, PAGE_SIZE);
	memset(L3, 0, PAGE_SIZE);
	memset(L2, 0, PAGE_SIZE);
	memset(L1, 0, PAGE_SIZE);

	L4[0] = PTE_P | PTE_W | 0x4000;
	L3[0] = PTE_P | PTE_W | 0x5000;
	L2[0] = PTE_P | PTE_W | 0x6000;
	L1[0x2000 / PAGE_SIZE] = PTE_P | PTE_W | 0x2000;
	L1[0x1000 / PAGE_SIZE] = PTE_P | PTE_W | 0x1000;
}

/* -------------------------------------------------------------------------- */

extern uint8_t test_tpr_begin, test_tpr_next_rip, test_tpr_end;

/*
 * 0x2000: Instructions, mapped
 * 0x3000: L4
 * 0x4000: L3
 * 0x5000: L2
 * 0x6000: L1
 */
static int
test_tpr(void)
{
	const char *test_name = "TPR";
	struct nvmm_capability cap;
	struct nvmm_machine mach;
	struct nvmm_vcpu vcpu;
	struct nvmm_vcpu_conf_tpr tpr;
	size_t size;
	uint64_t next_rip;
	uint64_t reason;
	int ret;

	if (nvmm_capability(&cap) == -1)
		err(errno, "nvmm_capability");

	if ((cap.arch.vcpu_conf_support & NVMM_CAP_ARCH_VCPU_CONF_TPR) == 0) {
		printf("Test '%s' skipped\n", test_name);
		return 0;
	}

	if (nvmm_machine_create(&mach) == -1)
		err(errno, "nvmm_machine_create");
	if (nvmm_vcpu_create(&mach, 0, &vcpu) == -1)
		err(errno, "nvmm_vcpu_create");

	map_pages64(&mach);

	size = (size_t)&test_tpr_end - (size_t)&test_tpr_begin;
	memcpy(instbuf, &test_tpr_begin, size);

	memset(&tpr, 0, sizeof(tpr));

	/*
	 * Set exit_changed and ensure we get a NVMM_VCPU_EXIT_TPR_CHANGED at
	 * the expected RIP.
	 */

	tpr.exit_changed = 1;
	if (nvmm_vcpu_configure(&mach, &vcpu, NVMM_VCPU_CONF_TPR, &tpr) == -1)
		err(errno, "nvmm_vcpu_configure");

	reset_machine64(&mach, &vcpu);
	reason = run_machine(&mach, &vcpu);

	if (reason != NVMM_VCPU_EXIT_TPR_CHANGED) {
		printf("*** Test '%s' failed, did not get VMEXIT\n", test_name);
		ret = 1;
		goto done;
	}

	if (nvmm_vcpu_getstate(&mach, &vcpu, NVMM_X64_STATE_ALL) == -1)
		err(errno, "nvmm_vcpu_getstate");
	if (vcpu.state->crs[NVMM_X64_CR_CR8] != 0x3) {
		printf("*** Test '%s' failed, CR8=%#lx\n", test_name,
		    vcpu.state->crs[NVMM_X64_CR_CR8]);
		ret = 1;
		goto done;
	}

	next_rip = 0x2000 + (size_t)&test_tpr_next_rip - (size_t)&test_tpr_begin;

	if (vcpu.state->gprs[NVMM_X64_GPR_RIP] != next_rip) {
		printf("*** Test '%s' failed, RIP=%#lx\n", test_name,
		    vcpu.state->gprs[NVMM_X64_GPR_RIP]);
		ret = 1;
		goto done;
	}

	/*
	 * Now unset exit_changed and ensure the VM just finishes.
	 */

	tpr.exit_changed = 0;
	if (nvmm_vcpu_configure(&mach, &vcpu, NVMM_VCPU_CONF_TPR, &tpr) == -1)
		err(errno, "nvmm_vcpu_configure");

	reset_machine64(&mach, &vcpu);
	reason = run_machine(&mach, &vcpu);

	if (reason != NVMM_VCPU_EXIT_RDMSR) {
		printf("*** Test '%s' failed, VM did not finish\n", test_name);
		ret = 1;
		goto done;
	}

	if (nvmm_vcpu_getstate(&mach, &vcpu, NVMM_X64_STATE_ALL) == -1)
		err(errno, "nvmm_vcpu_getstate");
	if (vcpu.state->crs[NVMM_X64_CR_CR8] != 0x3) {
		printf("*** Test '%s' failed, CR8=%#lx\n", test_name,
		    vcpu.state->crs[NVMM_X64_CR_CR8]);
		ret = 1;
		goto done;
	}

	printf("Test '%s' passed\n", test_name);
	ret = 0;

done:
	if (nvmm_vcpu_destroy(&mach, &vcpu) == -1)
		err(errno, "nvmm_vcpu_destroy");
	if (nvmm_machine_destroy(&mach) == -1)
		err(errno, "nvmm_machine_destroy");

	return ret;
}

/* -------------------------------------------------------------------------- */

int main(int argc __unused, char *argv[] __unused)
{
	int nfail = 0;

	if (nvmm_init() == -1)
		err(errno, "nvmm_init");

	nfail += test_tpr();

	if (nfail == 0) {
		printf("All tests passed.\n");
	} else {
		printf("*** %d tests failed.\n", nfail);
	}

	return (nfail == 0 ? 0 : -1);
}
