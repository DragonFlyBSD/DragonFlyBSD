/*
 * Copyright (c) 2018-2021 Maxime Villard, m00nbsd.net
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

#include "smallkern.h"
#include "pdir.h"
#include "trap.h"

#include <sys/bitops.h>
#include <machine/reg.h>
#include <machine/specialreg.h>
#include <machine/frame.h>
#include <machine/tss.h>
#include <machine/segments.h>

/* GDT offsets */
#define SMALLKERN_GDT_NUL_OFF	(0 * 8)
#define SMALLKERN_GDT_CS_OFF	(1 * 8)
#define SMALLKERN_GDT_DS_OFF	(2 * 8)
#define SMALLKERN_GDT_TSS_OFF	(3 * 8)

#ifdef __DragonFly__
#define SDT_SYS386TSS		SDT_SYSTSS	/*  9: system 64-bit TSS available */
#define SDT_SYS386IGT		SDT_SYSIGT	/* 14: system 64-bit interrupt gate */
#define APICBASE_PHYSADDR	APICBASE_ADDRESS /* 0xfffff000: physical address */
#define sys_segment_descriptor	system_segment_descriptor
#define x86_64_tss		x86_64tss
#define __arraycount(x)		nitems(x)
#endif /* __DragonFly__ */

void fatal(char *msg)
{
	print("\n");
	print_ext(RED_ON_BLACK, "********** FATAL ***********\n");
	print_ext(RED_ON_BLACK, msg);
	print("\n");
	print_ext(RED_ON_BLACK, "****************************\n");

	while (1);
}

/* -------------------------------------------------------------------------- */

struct smallframe {
	uint64_t sf_trapno;
	uint64_t sf_err;
	uint64_t sf_rip;
	uint64_t sf_cs;
	uint64_t sf_rflags;
	uint64_t sf_rsp;
	uint64_t sf_ss;
};

static void setregion(struct region_descriptor *, void *, uint16_t);
static void setgate(struct gate_descriptor *, void *, int, int, int, int);
static void set_sys_segment(struct sys_segment_descriptor *, void *,
    size_t, int, int, int);
static void set_sys_gdt(int, void *, size_t, int, int, int);
static void init_tss(void);
static void init_idt(void);

static char *trap_type[] = {
	"privileged instruction fault",		/*  0 T_PRIVINFLT */
	"breakpoint trap",			/*  1 T_BPTFLT */
	"arithmetic trap",			/*  2 T_ARITHTRAP */
	"asynchronous system trap",		/*  3 T_ASTFLT */
	"protection fault",			/*  4 T_PROTFLT */
	"trace trap",				/*  5 T_TRCTRAP */
	"page fault",				/*  6 T_PAGEFLT */
	"alignment fault",			/*  7 T_ALIGNFLT */
	"integer divide fault",			/*  8 T_DIVIDE */
	"non-maskable interrupt",		/*  9 T_NMI */
	"overflow trap",			/* 10 T_OFLOW */
	"bounds check fault",			/* 11 T_BOUND */
	"FPU not available fault",		/* 12 T_DNA */
	"double fault",				/* 13 T_DOUBLEFLT */
	"FPU operand fetch fault",		/* 14 T_FPOPFLT */
	"invalid TSS fault",			/* 15 T_TSSFLT */
	"segment not present fault",		/* 16 T_SEGNPFLT */
	"stack fault",				/* 17 T_STKFLT */
	"machine check fault",			/* 18 T_MCA */
	"SSE FP exception",			/* 19 T_XMM */
	"hardware interrupt",			/* 20 T_RESERVED */
};
size_t	trap_types = __arraycount(trap_type);

static uint8_t idtstore[PAGE_SIZE] __aligned(PAGE_SIZE);
static uint8_t faultstack[PAGE_SIZE] __aligned(PAGE_SIZE);
static struct x86_64_tss smallkern_tss;

static void
triple_fault(void)
{
	char *p = NULL;
	memset(&idtstore, 0, PAGE_SIZE);
	*p = 0;
}

/*
 * Trap handler.
 */
void
trap(struct smallframe *sf)
{
	uint64_t trapno = sf->sf_trapno;
	static int ntrap = 0;
	static float f = 0.0;
	char *buf;

	f += 1.0f;
	if (ntrap++ == 6) {
		triple_fault();
	}
	if (ntrap != (int)f) {
		print_ext(RED_ON_BLACK, "!!! FPU BUG !!!\n");
	}

	if (trapno < trap_types) {
		buf = trap_type[trapno];
	} else {
		buf = "unknown trap";
	}

	if (trapno == T_RESERVED) {
		/* Disable external interrupts. */
		lcr8(15);
	}

	print("\n");
	print_ext(RED_ON_BLACK, "****** FAULT OCCURRED ******\n");
	print_ext(RED_ON_BLACK, buf);
	print("\n");
	print_ext(RED_ON_BLACK, "****************************\n");
	print("\n");

	sti();

	while (1);
}

static void
setregion(struct region_descriptor *rd, void *base, uint16_t limit)
{
	rd->rd_limit = limit;
	rd->rd_base = (uint64_t)base;
}

static void
setgate(struct gate_descriptor *gd, void *func, int ist, int type, int dpl,
	int sel)
{
	gd->gd_looffset = (uint64_t)func & 0xffff;
	gd->gd_selector = sel;
	gd->gd_ist = ist;
	gd->gd_type = type;
	gd->gd_dpl = dpl;
	gd->gd_p = 1;
	gd->gd_hioffset = (uint64_t)func >> 16;
	gd->gd_xx1 = 0;
#ifdef __NetBSD__
	gd->gd_zero = 0;
	gd->gd_xx2 = 0;
	gd->gd_xx3 = 0;
#endif
}

static void
set_sys_segment(struct sys_segment_descriptor *sd, void *base, size_t limit,
	int type, int dpl, int gran)
{
	memset(sd, 0, sizeof(*sd));
	sd->sd_lolimit = (unsigned)limit;
	sd->sd_lobase = (uint64_t)base;
	sd->sd_type = type;
	sd->sd_dpl = dpl;
	sd->sd_p = 1;
	sd->sd_hilimit = (unsigned)limit >> 16;
	sd->sd_gran = gran;
	sd->sd_hibase = (uint64_t)base >> 24;
}

static void
set_sys_gdt(int slotoff, void *base, size_t limit, int type, int dpl, int gran)
{
	struct sys_segment_descriptor sd;

	set_sys_segment(&sd, base, limit, type, dpl, gran);

	memcpy(&gdt64_start + slotoff, &sd, sizeof(sd));
}

static void init_tss(void)
{
	memset(&smallkern_tss, 0, sizeof(smallkern_tss));
#ifdef __NetBSD__
	smallkern_tss.tss_ist[0] = (uintptr_t)(&faultstack[PAGE_SIZE-1]) & ~0xf;
#else /* DragonFly */
	smallkern_tss.tss_ist1 = (uintptr_t)(&faultstack[PAGE_SIZE-1]) & ~0xf;
#endif

	set_sys_gdt(SMALLKERN_GDT_TSS_OFF, &smallkern_tss,
	    sizeof(struct x86_64_tss) - 1, SDT_SYS386TSS, SEL_KPL, 0);
}

static void init_idt(void)
{
	struct region_descriptor region;
	struct gate_descriptor *idt;
	size_t i;

	idt = (struct gate_descriptor *)&idtstore;
	for (i = 0; i < NCPUIDT; i++) {
		setgate(&idt[i], x86_exceptions[i], 0, SDT_SYS386IGT,
		    SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}
	for (i = NCPUIDT; i < 256; i++) {
		setgate(&idt[i], &Xintr, 0, SDT_SYS386IGT,
		    SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}

	setregion(&region, &idtstore, PAGE_SIZE - 1);
	lidt(&region);
}

/* -------------------------------------------------------------------------- */

/*
 * Main entry point of the kernel.
 */
void
main(paddr_t pa_start __unused)
{
	u_int descs[4];
	uint32_t *reg, val;

	print_banner();

	/*
	 * Init the TSS and IDT. We mostly don't care about this, they are just
	 * here to properly handle traps.
	 */
	init_tss();
	print_state(true, "TSS created");
	init_idt();
	print_state(true, "IDT created");

	/* Reset CR8. */
	lcr8(0);

	/* Enable FPU. */
	clts();

	/* Enable interrupts. */
	sti();

	/* Ensure APICBASE is correct (default). */
	if ((rdmsr(MSR_APICBASE) & APICBASE_PHYSADDR) == 0xfee00000) {
		print_state(true, "APICBASE is correct");
	} else {
		print_state(false, "wrong APICBASE");
	}

	/* Ensure PG_NX is disabled. */
	if (!nox_flag) {
		print_state(true, "PG_NX is disabled");
	} else {
		print_state(false, "PG_NX is enabled!");
	}

	/* Ensure we are on cpu120. */
	cpuid(1, 0, descs);
	if (__SHIFTOUT(descs[1], CPUID_LOCAL_APIC_ID) == 120) {
		print_state(true, "Running on cpu120");
	} else {
		print_state(false, "Not running on cpu120!");
	}

	/* Ensure the LAPIC information matches. */
#define	LAPIC_ID		0x020
#	define LAPIC_ID_MASK		0xff000000
#	define LAPIC_ID_SHIFT		24
	reg = (uint32_t *)lapicbase;
	val = reg[LAPIC_ID/4];
	if (__SHIFTOUT(val, LAPIC_ID_MASK) == 120) {
		print_state(true, "LAPIC information matches");
	} else {
		print_state(false, "LAPIC information does not match!");
	}

	/*
	 * Will cause a #UD.
	 */
	vmmcall();
}
