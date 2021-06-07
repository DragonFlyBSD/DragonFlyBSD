/*	$NetBSD: main.c,v 1.1 2018/09/12 00:00:00 maxv Exp $	*/

/*
 * Copyright (c) 2018-2019 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Maxime Villard.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <machine/segments.h>
#include <machine/psl.h>
#include <machine/specialreg.h>
#include <pthread.h>
#include <nvmm.h>

#include "common.h"

#ifdef __DragonFly__
#define APICBASE_EN	APICBASE_ENABLED	/* 0x00000800: software enable */
#define PSL_MBO		PSL_RESERVED_DEFAULT	/* 0x00000002 */
#define SDT_SYS386BSY	SDT_SYSBSY		/* 11: system 64-bit TSS busy */
#endif

/* -------------------------------------------------------------------------- */

uintptr_t
toyvirt_mem_add(struct nvmm_machine *mach, gpaddr_t gpa, size_t size)
{
	uintptr_t hva;
	void *buf;

	assert(size > 0);

	buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED)
		err(EXIT_FAILURE, "mmap");

	hva = (uintptr_t)buf;
	if (nvmm_hva_map(mach, hva, size) == -1)
		err(EXIT_FAILURE, "nvmm_hva_map");
	if (nvmm_gpa_map(mach, hva, gpa, size, PROT_READ|PROT_WRITE|PROT_EXEC) == -1)
		err(EXIT_FAILURE, "nvmm_gpa_map");

	return hva;
}

/* -------------------------------------------------------------------------- */

static bool can_take_int = false;
static bool can_take_nmi = false;
static bool has_int_pending = false;
static bool has_nmi_pending = false;
static struct nvmm_vcpu_event pending_int;
static struct nvmm_vcpu_event pending_nmi;

static void
toyvirt_event_inject(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu,
    struct nvmm_vcpu_event *event)
{
	memcpy(vcpu->event, event, sizeof(*event));

	/* INT. */
	if (event->vector != 2) {
		if (can_take_int) {
			if (nvmm_vcpu_inject(mach, vcpu) == -1)
				err(EXIT_FAILURE, "nvmm_vcpu_inject");
			has_int_pending = false;
		} else {
			memcpy(&pending_int, event, sizeof(pending_int));
			has_int_pending = true;
		}
	}

	/* NMI. */
	if (event->vector == 2) {
		if (can_take_nmi) {
			if (nvmm_vcpu_inject(mach, vcpu) == -1)
				err(EXIT_FAILURE, "nvmm_vcpu_inject");
			has_nmi_pending = false;
		} else {
			memcpy(&pending_nmi, event, sizeof(pending_nmi));
			has_nmi_pending = true;
		}
	}
}

static void
toyvirt_event_reinject(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_vcpu_exit *exit = vcpu->exit;

	if (exit->reason == NVMM_VCPU_EXIT_INT_READY) {
		if (!has_int_pending)
			errx(EXIT_FAILURE, "no INT pending!");
		toyvirt_event_inject(mach, vcpu, &pending_int);
	} else {
		if (!has_nmi_pending)
			errx(EXIT_FAILURE, "no NMI pending!");
		toyvirt_event_inject(mach, vcpu, &pending_nmi);
	}
}

/* -------------------------------------------------------------------------- */

/* Stolen from x86/pmap.c */
#define	PATENTRY(n, type)	(type << ((n) * 8))
#define	PAT_UC		0x0ULL
#define	PAT_WC		0x1ULL
#define	PAT_WT		0x4ULL
#define	PAT_WP		0x5ULL
#define	PAT_WB		0x6ULL
#define	PAT_UCMINUS	0x7ULL

static void
toyvirt_configure_cpuid(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_vcpu_conf_cpuid cpuid;

	/* Disable PG_NX. No particular reason, just to demonstrate. */
	memset(&cpuid, 0, sizeof(cpuid));
	cpuid.mask = 1;
	cpuid.leaf = 0x80000001;
	cpuid.u.mask.del.edx = CPUID_NOX;

	if (nvmm_vcpu_configure(mach, vcpu, NVMM_VCPU_CONF_CPUID, &cpuid) == -1)
		err(EXIT_FAILURE, "nvmm_vcpu_configure(CPUID)");
}

static void
toyvirt_init_seg(struct nvmm_x64_state_seg *seg, int type, int sel, int limit)
{
	seg->selector = sel;
	seg->attrib.type = type;
	seg->attrib.s = (type & 0b10000) != 0;
	seg->attrib.dpl = 0;
	seg->attrib.p = 1;
	seg->attrib.avl = 1;
	seg->attrib.l = 0;
	seg->attrib.def = 1;
	seg->attrib.g = 1;
	seg->limit = limit;
	seg->base = 0x00000000;
}

static void
toyvirt_init(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu,
    const char *path)
{
	struct nvmm_vcpu_state *state = vcpu->state;
	uint64_t rip;

	if (nvmm_vcpu_getstate(mach, vcpu, NVMM_X64_STATE_ALL) == -1)
		errx(EXIT_FAILURE, "nvmm_vcpu_getstate");

	/* Default. */
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_CS], SDT_MEMERA, 0, 0xFFFFFFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_SS], SDT_MEMRWA, 0, 0xFFFFFFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_DS], SDT_MEMRWA, 0, 0xFFFFFFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_ES], SDT_MEMRWA, 0, 0xFFFFFFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_FS], SDT_MEMRWA, 0, 0xFFFFFFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_GS], SDT_MEMRWA, 0, 0xFFFFFFFF);

	/* Blank. */
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_GDT], 0, 0, 0x0000FFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_IDT], 0, 0, 0x0000FFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_LDT], SDT_SYSLDT, 0, 0xFFFFFFFF);
	toyvirt_init_seg(&state->segs[NVMM_X64_SEG_TR], SDT_SYS386BSY, 0, 0xFFFFFFFF);

	/* Protected mode enabled. */
	state->crs[NVMM_X64_CR_CR0] = CR0_PE | CR0_ET | CR0_NW | CR0_CD;

	state->msrs[NVMM_X64_MSR_PAT] =
	    PATENTRY(0, PAT_WB) | PATENTRY(1, PAT_WT) |
	    PATENTRY(2, PAT_UCMINUS) | PATENTRY(3, PAT_UC) |
	    PATENTRY(4, PAT_WB) | PATENTRY(5, PAT_WT) |
	    PATENTRY(6, PAT_UCMINUS) | PATENTRY(7, PAT_UC);

	/* Map the VM. */
	if (elf_map(mach, path, &rip) != 0)
		errx(EXIT_FAILURE, "unable to map the vm");

	state->gprs[NVMM_X64_GPR_RIP] = rip; /* jump here */
	state->gprs[NVMM_X64_GPR_RFLAGS] = PSL_MBO;

	if (nvmm_vcpu_setstate(mach, vcpu, NVMM_X64_STATE_ALL) == -1)
		err(EXIT_FAILURE, "nvmm_vcpu_setstate");
}

/* -------------------------------------------------------------------------- */

static void
toycpu_io_callback(struct nvmm_io *io)
{
	/* Hand over to toydev. */
	toydev_io(io->port, io->in, io->data, io->size);
}

static void
toyvirt_mem_callback(struct nvmm_mem *mem)
{
	/* Hand over to toydev. */
	toydev_mmio(mem->gpa, mem->write, mem->data, mem->size);
}

static struct nvmm_assist_callbacks callbacks = {
	.io = toycpu_io_callback,
	.mem = toyvirt_mem_callback
};

/* -------------------------------------------------------------------------- */

static uint8_t toyvirt_prio = 0;

static struct {
	struct nvmm_machine *mach;
	struct nvmm_vcpu *vcpu;
} toyvirt;

/*
 * Create mess in the VCPU. Inject random events at regular intervals.
 */
static void *
toyvirt_mess(void *arg __unused)
{
	struct nvmm_machine *mach = toyvirt.mach;
	struct nvmm_vcpu *vcpu = toyvirt.vcpu;
	struct nvmm_vcpu_event event;

	while (1) {
		sleep(3);

		/* Inject a #GP */
		printf("[+] Inject #GP event\n");
		event.type = NVMM_VCPU_EVENT_EXCP;
		event.vector = 13;
		event.u.excp.error = 0;
		toyvirt_event_inject(mach, vcpu, &event);

		sleep(3);

		/* Inject an #NMI */
		printf("[+] Inject #NMI event\n");
		event.type = NVMM_VCPU_EVENT_INTR;
		event.vector = 2;
		toyvirt_event_inject(mach, vcpu, &event);

		sleep(3);

		/* Inject an interrupt */
		if (15 > toyvirt_prio) {
			printf("[+] Inject hardware interrupt event\n");
			event.type = NVMM_VCPU_EVENT_INTR;
			event.vector = 200;
			toyvirt_event_inject(mach, vcpu, &event);
		}
	}

	pthread_exit(NULL);
}

/* -------------------------------------------------------------------------- */

/*
 * Support one MSR: MSR_APICBASE.
 */
static int
toycpu_msr(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_vcpu_state *state = vcpu->state;
	struct nvmm_vcpu_exit *exit = vcpu->exit;
	uint64_t val;

	if (exit->reason != NVMM_VCPU_EXIT_RDMSR) {
		printf("Expected rdmsr!\n");
		return -1;
	}
	if (exit->u.rdmsr.msr != MSR_APICBASE) {
		printf("Unknown MSR!\n");
		return -1;
	}

	if (nvmm_vcpu_getstate(mach, vcpu, NVMM_X64_STATE_GPRS) == -1)
		err(EXIT_FAILURE, "nvmm_vcpu_getstate");

	val = APICBASE_BSP | APICBASE_EN | 0xfee00000;

	state->gprs[NVMM_X64_GPR_RAX] = (val & 0xFFFFFFFF);
	state->gprs[NVMM_X64_GPR_RDX] = (val >> 32);
	state->gprs[NVMM_X64_GPR_RIP] = exit->u.rdmsr.npc;

	if (nvmm_vcpu_setstate(mach, vcpu, NVMM_X64_STATE_GPRS) == -1)
		err(EXIT_FAILURE, "nvmm_vcpu_setstate");

	return 0;
}

static void
toyvirt_invalid(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_vcpu_state *state = vcpu->state;

	if (nvmm_vcpu_getstate(mach, vcpu, NVMM_X64_STATE_GPRS) == -1)
		err(EXIT_FAILURE, "nvmm_vcpu_getstate");

	printf("[!] Invalid exit: rip=%p\n",
	    (void *)state->gprs[NVMM_X64_GPR_RIP]);
}

static void
toyvirt_run(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu)
{
	struct nvmm_vcpu_exit *exit = vcpu->exit;
	pthread_t thid;
	int ret;

	toyvirt.mach = mach;
	toyvirt.vcpu = vcpu;
	pthread_create(&thid, NULL, toyvirt_mess, NULL);

	while (1) {
		if (nvmm_vcpu_run(mach, vcpu) == -1)
			err(EXIT_FAILURE, "nvmm_vcpu_run");

		toyvirt_prio = exit->exitstate.cr8;
		can_take_int = !exit->exitstate.int_window_exiting;
		can_take_nmi = !exit->exitstate.nmi_window_exiting;

		switch (exit->reason) {
		case NVMM_VCPU_EXIT_NONE:
			/*
			 * A VMEXIT caused by whatever internal reason, that
			 * we shouldn't take care of. Keep rolling.
			 */
			continue;

		case NVMM_VCPU_EXIT_IO:
			ret = nvmm_assist_io(mach, vcpu);
			if (ret == -1)
				err(EXIT_FAILURE, "nvmm_assist_io");
			continue;

		case NVMM_VCPU_EXIT_RDMSR:
		case NVMM_VCPU_EXIT_WRMSR:
			toycpu_msr(mach, vcpu);
			continue;

		case NVMM_VCPU_EXIT_MEMORY:
			ret = nvmm_assist_mem(mach, vcpu);
			if (ret == -1)
				err(EXIT_FAILURE, "nvmm_assist_mem");
			continue;

		case NVMM_VCPU_EXIT_INT_READY:
		case NVMM_VCPU_EXIT_NMI_READY:
			printf("[+] Machine ready to INT/NMI\n");
			toyvirt_event_reinject(mach, vcpu);
			return;

		case NVMM_VCPU_EXIT_SHUTDOWN:
			/* Stop the VM here. */
			printf("[+] Machine received shutdown\n");
			return;

		case NVMM_VCPU_EXIT_INVALID:
		default:
			toyvirt_invalid(mach, vcpu);
			return;
		}
	}
}

int main(int argc, char *argv[])
{
	struct nvmm_machine mach;
	struct nvmm_vcpu vcpu;

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s file-path", argv[0]);

	if (nvmm_init() == -1)
		err(EXIT_FAILURE, "nvmm_init");
	printf("[+] NVMM initialization succeeded\n");

	if (nvmm_machine_create(&mach) == -1)
		err(EXIT_FAILURE, "nvmm_machine_create");
	printf("[+] Machine creation succeeded\n");

	if (nvmm_vcpu_create(&mach, 120, &vcpu) == -1)
		err(EXIT_FAILURE, "nvmm_vcpu_create");
	printf("[+] VCPU creation succeeded\n");

	if (nvmm_vcpu_configure(&mach, &vcpu, NVMM_VCPU_CONF_CALLBACKS,
				&callbacks) == -1) {
		err(EXIT_FAILURE, "nvmm_vcpu_configure(CALLBACKS)");
	};
	printf("[+] VCPU callbacks configuration succeeded\n");

	toyvirt_configure_cpuid(&mach, &vcpu);

	toyvirt_init(&mach, &vcpu, argv[1]);
	printf("[+] State set\n");

	toyvirt_run(&mach, &vcpu);
	printf("[+] Machine execution successful\n");

	if (nvmm_machine_destroy(&mach) == -1)
		err(EXIT_FAILURE, "nvmm_machine_destroy");

	printf("[+] Machine destroyed\n");

	return 0;
}
