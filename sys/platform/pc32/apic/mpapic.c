/*
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/mpapic.c,v 1.37.2.7 2003/01/25 02:31:47 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/machintr.h>
#include <machine/globaldata.h>
#include <machine/smp.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/pmap.h>
#include <machine_base/apic/mpapic.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine/segments.h>
#include <sys/thread2.h>

#include <machine/intr_machdep.h>

/* XXX */
extern pt_entry_t *SMPpt;

/* EISA Edge/Level trigger control registers */
#define ELCR0	0x4d0			/* eisa irq 0-7 */
#define ELCR1	0x4d1			/* eisa irq 8-15 */

struct ioapic_info {
	int		io_idx;
	int		io_apic_id;
	void		*io_addr;
	int		io_npin;
	int		io_gsi_base;

	TAILQ_ENTRY(ioapic_info) io_link;
};
TAILQ_HEAD(ioapic_info_list, ioapic_info);

struct ioapic_conf {
	struct ioapic_info_list ioc_list;
	int		ioc_intsrc[16];	/* XXX magic number */
};

static void	lapic_timer_calibrate(void);
static void	lapic_timer_set_divisor(int);
static void	lapic_timer_fixup_handler(void *);
static void	lapic_timer_restart_handler(void *);

void		lapic_timer_process(void);
void		lapic_timer_process_frame(struct intrframe *);

static int	lapic_timer_enable = 1;
TUNABLE_INT("hw.lapic_timer_enable", &lapic_timer_enable);

static void	lapic_timer_intr_reload(struct cputimer_intr *, sysclock_t);
static void	lapic_timer_intr_enable(struct cputimer_intr *);
static void	lapic_timer_intr_restart(struct cputimer_intr *);
static void	lapic_timer_intr_pmfixup(struct cputimer_intr *);

static void	ioapic_setup(const struct ioapic_info *);
static void	ioapic_set_apic_id(const struct ioapic_info *);
static void	ioapic_gsi_setup(int);
static const struct ioapic_info *
		ioapic_gsi_search(int);
static void	ioapic_pin_prog(void *, int, int,
		    enum intr_trigger, enum intr_polarity, uint32_t);

static struct cputimer_intr lapic_cputimer_intr = {
	.freq = 0,
	.reload = lapic_timer_intr_reload,
	.enable = lapic_timer_intr_enable,
	.config = cputimer_intr_default_config,
	.restart = lapic_timer_intr_restart,
	.pmfixup = lapic_timer_intr_pmfixup,
	.initclock = cputimer_intr_default_initclock,
	.next = SLIST_ENTRY_INITIALIZER,
	.name = "lapic",
	.type = CPUTIMER_INTR_LAPIC,
	.prio = CPUTIMER_INTR_PRIO_LAPIC,
	.caps = CPUTIMER_INTR_CAP_NONE
};

/*
 * pointers to pmapped apic hardware.
 */

volatile ioapic_t	**ioapic;

static int		lapic_timer_divisor_idx = -1;
static const uint32_t	lapic_timer_divisors[] = {
	APIC_TDCR_2,	APIC_TDCR_4,	APIC_TDCR_8,	APIC_TDCR_16,
	APIC_TDCR_32,	APIC_TDCR_64,	APIC_TDCR_128,	APIC_TDCR_1
};
#define APIC_TIMER_NDIVISORS (int)(NELEM(lapic_timer_divisors))

int			lapic_id_max;

static struct ioapic_conf	ioapic_conf;

/*
 * Enable LAPIC, configure interrupts.
 */
void
lapic_init(boolean_t bsp)
{
	uint32_t timer;
	u_int   temp;

	/*
	 * Install vectors
	 *
	 * Since IDT is shared between BSP and APs, these vectors
	 * only need to be installed once; we do it on BSP.
	 */
	if (bsp) {
		/* Install a 'Spurious INTerrupt' vector */
		setidt(XSPURIOUSINT_OFFSET, Xspuriousint,
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));

		/* Install an inter-CPU IPI for TLB invalidation */
		setidt(XINVLTLB_OFFSET, Xinvltlb,
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));

		/* Install an inter-CPU IPI for IPIQ messaging */
		setidt(XIPIQ_OFFSET, Xipiq,
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));

		/* Install a timer vector */
		setidt(XTIMER_OFFSET, Xtimer,
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
		
		/* Install an inter-CPU IPI for CPU stop/restart */
		setidt(XCPUSTOP_OFFSET, Xcpustop,
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}

	/*
	 * Setup LINT0 as ExtINT on the BSP.  This is theoretically an
	 * aggregate interrupt input from the 8259.  The INTA cycle
	 * will be routed to the external controller (the 8259) which
	 * is expected to supply the vector.
	 *
	 * Must be setup edge triggered, active high.
	 *
	 * Disable LINT0 on BSP, if I/O APIC is enabled.
	 *
	 * Disable LINT0 on the APs.  It doesn't matter what delivery
	 * mode we use because we leave it masked.
	 */
	temp = lapic.lvt_lint0;
	temp &= ~(APIC_LVT_MASKED | APIC_LVT_TRIG_MASK | 
		  APIC_LVT_POLARITY_MASK | APIC_LVT_DM_MASK);
	if (bsp) {
		temp |= APIC_LVT_DM_EXTINT;
		if (apic_io_enable)
			temp |= APIC_LVT_MASKED;
	} else {
		temp |= APIC_LVT_DM_FIXED | APIC_LVT_MASKED;
	}
	lapic.lvt_lint0 = temp;

	/*
	 * Setup LINT1 as NMI.
	 *
	 * Must be setup edge trigger, active high.
	 *
	 * Enable LINT1 on BSP, if I/O APIC is enabled.
	 *
	 * Disable LINT1 on the APs.
	 */
	temp = lapic.lvt_lint1;
	temp &= ~(APIC_LVT_MASKED | APIC_LVT_TRIG_MASK | 
		  APIC_LVT_POLARITY_MASK | APIC_LVT_DM_MASK);
	temp |= APIC_LVT_MASKED | APIC_LVT_DM_NMI;
	if (bsp && apic_io_enable)
		temp &= ~APIC_LVT_MASKED;
	lapic.lvt_lint1 = temp;

	/*
	 * Mask the LAPIC error interrupt, LAPIC performance counter
	 * interrupt.
	 */
	lapic.lvt_error = lapic.lvt_error | APIC_LVT_MASKED;
	lapic.lvt_pcint = lapic.lvt_pcint | APIC_LVT_MASKED;

	/*
	 * Set LAPIC timer vector and mask the LAPIC timer interrupt.
	 */
	timer = lapic.lvt_timer;
	timer &= ~APIC_LVTT_VECTOR;
	timer |= XTIMER_OFFSET;
	timer |= APIC_LVTT_MASKED;
	lapic.lvt_timer = timer;

	/*
	 * Set the Task Priority Register as needed.   At the moment allow
	 * interrupts on all cpus (the APs will remain CLId until they are
	 * ready to deal).  We could disable all but IPIs by setting
	 * temp |= TPR_IPI for cpu != 0.
	 */
	temp = lapic.tpr;
	temp &= ~APIC_TPR_PRIO;		/* clear priority field */
#ifdef SMP /* APIC-IO */
if (!apic_io_enable) {
#endif
	/*
	 * If we are NOT running the IO APICs, the LAPIC will only be used
	 * for IPIs.  Set the TPR to prevent any unintentional interrupts.
	 */
	temp |= TPR_IPI;
#ifdef SMP /* APIC-IO */
}
#endif

	lapic.tpr = temp;

	/* 
	 * Enable the LAPIC 
	 */
	temp = lapic.svr;
	temp |= APIC_SVR_ENABLE;	/* enable the LAPIC */
	temp &= ~APIC_SVR_FOCUS_DISABLE; /* enable lopri focus processor */

	/*
	 * Set the spurious interrupt vector.  The low 4 bits of the vector
	 * must be 1111.
	 */
	if ((XSPURIOUSINT_OFFSET & 0x0F) != 0x0F)
		panic("bad XSPURIOUSINT_OFFSET: 0x%08x", XSPURIOUSINT_OFFSET);
	temp &= ~APIC_SVR_VECTOR;
	temp |= XSPURIOUSINT_OFFSET;

	lapic.svr = temp;

	/*
	 * Pump out a few EOIs to clean out interrupts that got through
	 * before we were able to set the TPR.
	 */
	lapic.eoi = 0;
	lapic.eoi = 0;
	lapic.eoi = 0;

	if (bsp) {
		lapic_timer_calibrate();
		if (lapic_timer_enable) {
			cputimer_intr_register(&lapic_cputimer_intr);
			cputimer_intr_select(&lapic_cputimer_intr, 0);
		}
	} else {
		lapic_timer_set_divisor(lapic_timer_divisor_idx);
	}

	if (bootverbose)
		apic_dump("apic_initialize()");
}

static void
lapic_timer_set_divisor(int divisor_idx)
{
	KKASSERT(divisor_idx >= 0 && divisor_idx < APIC_TIMER_NDIVISORS);
	lapic.dcr_timer = lapic_timer_divisors[divisor_idx];
}

static void
lapic_timer_oneshot(u_int count)
{
	uint32_t value;

	value = lapic.lvt_timer;
	value &= ~APIC_LVTT_PERIODIC;
	lapic.lvt_timer = value;
	lapic.icr_timer = count;
}

static void
lapic_timer_oneshot_quick(u_int count)
{
	lapic.icr_timer = count;
}

static void
lapic_timer_calibrate(void)
{
	sysclock_t value;

	/* Try to calibrate the local APIC timer. */
	for (lapic_timer_divisor_idx = 0;
	     lapic_timer_divisor_idx < APIC_TIMER_NDIVISORS;
	     lapic_timer_divisor_idx++) {
		lapic_timer_set_divisor(lapic_timer_divisor_idx);
		lapic_timer_oneshot(APIC_TIMER_MAX_COUNT);
		DELAY(2000000);
		value = APIC_TIMER_MAX_COUNT - lapic.ccr_timer;
		if (value != APIC_TIMER_MAX_COUNT)
			break;
	}
	if (lapic_timer_divisor_idx >= APIC_TIMER_NDIVISORS)
		panic("lapic: no proper timer divisor?!\n");
	lapic_cputimer_intr.freq = value / 2;

	kprintf("lapic: divisor index %d, frequency %u Hz\n",
		lapic_timer_divisor_idx, lapic_cputimer_intr.freq);
}

static void
lapic_timer_process_oncpu(struct globaldata *gd, struct intrframe *frame)
{
	sysclock_t count;

	gd->gd_timer_running = 0;

	count = sys_cputimer->count();
	if (TAILQ_FIRST(&gd->gd_systimerq) != NULL)
		systimer_intr(&count, 0, frame);
}

void
lapic_timer_process(void)
{
	lapic_timer_process_oncpu(mycpu, NULL);
}

void
lapic_timer_process_frame(struct intrframe *frame)
{
	lapic_timer_process_oncpu(mycpu, frame);
}

static void
lapic_timer_intr_reload(struct cputimer_intr *cti, sysclock_t reload)
{
	struct globaldata *gd = mycpu;

	reload = (int64_t)reload * cti->freq / sys_cputimer->freq;
	if (reload < 2)
		reload = 2;

	if (gd->gd_timer_running) {
		if (reload < lapic.ccr_timer)
			lapic_timer_oneshot_quick(reload);
	} else {
		gd->gd_timer_running = 1;
		lapic_timer_oneshot_quick(reload);
	}
}

static void
lapic_timer_intr_enable(struct cputimer_intr *cti __unused)
{
	uint32_t timer;

	timer = lapic.lvt_timer;
	timer &= ~(APIC_LVTT_MASKED | APIC_LVTT_PERIODIC);
	lapic.lvt_timer = timer;

	lapic_timer_fixup_handler(NULL);
}

static void
lapic_timer_fixup_handler(void *arg)
{
	int *started = arg;

	if (started != NULL)
		*started = 0;

	if (cpu_vendor_id == CPU_VENDOR_AMD) {
		/*
		 * Detect the presence of C1E capability mostly on latest
		 * dual-cores (or future) k8 family.  This feature renders
		 * the local APIC timer dead, so we disable it by reading
		 * the Interrupt Pending Message register and clearing both
		 * C1eOnCmpHalt (bit 28) and SmiOnCmpHalt (bit 27).
		 * 
		 * Reference:
		 *   "BIOS and Kernel Developer's Guide for AMD NPT
		 *    Family 0Fh Processors"
		 *   #32559 revision 3.00
		 */
		if ((cpu_id & 0x00000f00) == 0x00000f00 &&
		    (cpu_id & 0x0fff0000) >= 0x00040000) {
			uint64_t msr;

			msr = rdmsr(0xc0010055);
			if (msr & 0x18000000) {
				struct globaldata *gd = mycpu;

				kprintf("cpu%d: AMD C1E detected\n",
					gd->gd_cpuid);
				wrmsr(0xc0010055, msr & ~0x18000000ULL);

				/*
				 * We are kinda stalled;
				 * kick start again.
				 */
				gd->gd_timer_running = 1;
				lapic_timer_oneshot_quick(2);

				if (started != NULL)
					*started = 1;
			}
		}
	}
}

static void
lapic_timer_restart_handler(void *dummy __unused)
{
	int started;

	lapic_timer_fixup_handler(&started);
	if (!started) {
		struct globaldata *gd = mycpu;

		gd->gd_timer_running = 1;
		lapic_timer_oneshot_quick(2);
	}
}

/*
 * This function is called only by ACPI-CA code currently:
 * - AMD C1E fixup.  AMD C1E only seems to happen after ACPI
 *   module controls PM.  So once ACPI-CA is attached, we try
 *   to apply the fixup to prevent LAPIC timer from hanging.
 */
static void
lapic_timer_intr_pmfixup(struct cputimer_intr *cti __unused)
{
	lwkt_send_ipiq_mask(smp_active_mask,
			    lapic_timer_fixup_handler, NULL);
}

static void
lapic_timer_intr_restart(struct cputimer_intr *cti __unused)
{
	lwkt_send_ipiq_mask(smp_active_mask, lapic_timer_restart_handler, NULL);
}


/*
 * dump contents of local APIC registers
 */
void
apic_dump(char* str)
{
	kprintf("SMP: CPU%d %s:\n", mycpu->gd_cpuid, str);
	kprintf("     lint0: 0x%08x lint1: 0x%08x TPR: 0x%08x SVR: 0x%08x\n",
		lapic.lvt_lint0, lapic.lvt_lint1, lapic.tpr, lapic.svr);
}


#ifdef SMP /* APIC-IO */

/*
 * IO APIC code,
 */

#define IOAPIC_ISA_INTS		16
#define REDIRCNT_IOAPIC(A) \
	    ((int)((io_apic_versions[(A)] & IOART_VER_MAXREDIR) >> MAXREDIRSHIFT) + 1)

static int trigger (int apic, int pin, u_int32_t * flags);
static void polarity (int apic, int pin, u_int32_t * flags, int level);

#define DEFAULT_FLAGS		\
	((u_int32_t)		\
	 (IOART_INTMSET |	\
	  IOART_DESTPHY |	\
	  IOART_DELLOPRI))

#define DEFAULT_ISA_FLAGS	\
	((u_int32_t)		\
	 (IOART_INTMSET |	\
	  IOART_TRGREDG |	\
	  IOART_INTAHI |	\
	  IOART_DESTPHY |	\
	  IOART_DELLOPRI))

void
io_apic_set_id(int apic, int id)
{
	u_int32_t ux;
	
	ux = ioapic_read(ioapic[apic], IOAPIC_ID);	/* get current contents */
	if (((ux & APIC_ID_MASK) >> 24) != id) {
		kprintf("Changing APIC ID for IO APIC #%d"
		       " from %d to %d on chip\n",
		       apic, ((ux & APIC_ID_MASK) >> 24), id);
		ux &= ~APIC_ID_MASK;	/* clear the ID field */
		ux |= (id << 24);
		ioapic_write(ioapic[apic], IOAPIC_ID, ux);	/* write new value */
		ux = ioapic_read(ioapic[apic], IOAPIC_ID);	/* re-read && test */
		if (((ux & APIC_ID_MASK) >> 24) != id)
			panic("can't control IO APIC #%d ID, reg: 0x%08x",
			      apic, ux);
	}
}


int
io_apic_get_id(int apic)
{
  return (ioapic_read(ioapic[apic], IOAPIC_ID) & APIC_ID_MASK) >> 24;
}
  


/*
 * Setup the IO APIC.
 */
void
io_apic_setup_intpin(int apic, int pin)
{
	int bus, bustype, irq;
	u_char		select;		/* the select register is 8 bits */
	u_int32_t	flags;		/* the window register is 32 bits */
	u_int32_t	target;		/* the window register is 32 bits */
	u_int32_t	vector;		/* the window register is 32 bits */
	int		level;
	int		cpuid;
	char		envpath[32];

	select = pin * 2 + IOAPIC_REDTBL0;	/* register */

	/*
	 * Always clear an IO APIC pin before [re]programming it.  This is
	 * particularly important if the pin is set up for a level interrupt
	 * as the IOART_REM_IRR bit might be set.   When we reprogram the
	 * vector any EOI from pending ints on this pin could be lost and
	 * IRR might never get reset.
	 *
	 * To fix this problem, clear the vector and make sure it is 
	 * programmed as an edge interrupt.  This should theoretically
	 * clear IRR so we can later, safely program it as a level 
	 * interrupt.
	 */
	imen_lock();

	flags = ioapic_read(ioapic[apic], select) & IOART_RESV;
	flags |= IOART_INTMSET | IOART_TRGREDG | IOART_INTAHI;
	flags |= IOART_DESTPHY | IOART_DELFIXED;

	target = ioapic_read(ioapic[apic], select + 1) & IOART_HI_DEST_RESV;
	target |= 0;	/* fixed mode cpu mask of 0 - don't deliver anywhere */

	vector = 0;

	ioapic_write(ioapic[apic], select, flags | vector);
	ioapic_write(ioapic[apic], select + 1, target);

	imen_unlock();

	/*
	 * We only deal with vectored interrupts here.  ? documentation is
	 * lacking, I'm guessing an interrupt type of 0 is the 'INT' type,
	 * vs ExTINT, etc.
	 *
	 * This test also catches unconfigured pins.
	 */
	if (apic_int_type(apic, pin) != 0)
		return;

	/*
	 * Leave the pin unprogrammed if it does not correspond to
	 * an IRQ.
	 */
	irq = apic_irq(apic, pin);
	if (irq < 0)
		return;
	
	/* determine the bus type for this pin */
	bus = apic_src_bus_id(apic, pin);
	if (bus < 0)
		return;
	bustype = apic_bus_type(bus);
	
	if ((bustype == ISA) &&
	    (pin < IOAPIC_ISA_INTS) && 
	    (irq == pin) &&
	    (apic_polarity(apic, pin) == 0x1) &&
	    (apic_trigger(apic, pin) == 0x3)) {
		/* 
		 * A broken BIOS might describe some ISA 
		 * interrupts as active-high level-triggered.
		 * Use default ISA flags for those interrupts.
		 */
		flags = DEFAULT_ISA_FLAGS;
	} else {
		/* 
		 * Program polarity and trigger mode according to 
		 * interrupt entry.
		 */
		flags = DEFAULT_FLAGS;
		level = trigger(apic, pin, &flags);
		if (level == 1)
			int_to_apicintpin[irq].flags |= IOAPIC_IM_FLAG_LEVEL;
		polarity(apic, pin, &flags, level);
	}

	cpuid = 0;
	ksnprintf(envpath, sizeof(envpath), "hw.irq.%d.dest", irq);
	kgetenv_int(envpath, &cpuid);

	/* ncpus may not be available yet */
	if (cpuid > mp_naps)
		cpuid = 0;

	if (bootverbose) {
		kprintf("IOAPIC #%d intpin %d -> irq %d (CPU%d)\n",
		       apic, pin, irq, cpuid);
	}

	/*
	 * Program the appropriate registers.  This routing may be 
	 * overridden when an interrupt handler for a device is
	 * actually added (see register_int(), which calls through
	 * the MACHINTR ABI to set up an interrupt handler/vector).
	 *
	 * The order in which we must program the two registers for
	 * safety is unclear! XXX
	 */
	imen_lock();

	vector = IDT_OFFSET + irq;			/* IDT vec */
	target = ioapic_read(ioapic[apic], select + 1) & IOART_HI_DEST_RESV;
	/* Deliver all interrupts to CPU0 (BSP) */
	target |= (CPU_TO_ID(cpuid) << IOART_HI_DEST_SHIFT) &
		  IOART_HI_DEST_MASK;
	flags |= ioapic_read(ioapic[apic], select) & IOART_RESV;
	ioapic_write(ioapic[apic], select, flags | vector);
	ioapic_write(ioapic[apic], select + 1, target);

	imen_unlock();
}

int
io_apic_setup(int apic)
{
	int		maxpin;
	int		pin;

	maxpin = REDIRCNT_IOAPIC(apic);		/* pins in APIC */
	kprintf("Programming %d pins in IOAPIC #%d\n", maxpin, apic);
	
	for (pin = 0; pin < maxpin; ++pin) {
		io_apic_setup_intpin(apic, pin);
	}
	while (pin < 32) {
		if (apic_int_type(apic, pin) >= 0) {
			kprintf("Warning: IOAPIC #%d pin %d does not exist,"
				" cannot program!\n", apic, pin);
		}
		++pin;
	}

	/* return GOOD status */
	return 0;
}
#undef DEFAULT_ISA_FLAGS
#undef DEFAULT_FLAGS


#define DEFAULT_EXTINT_FLAGS	\
	((u_int32_t)		\
	 (IOART_INTMSET |	\
	  IOART_TRGREDG |	\
	  IOART_INTAHI |	\
	  IOART_DESTPHY |	\
	  IOART_DELLOPRI))

/*
 * XXX this function is only used by 8254 setup
 * Setup the source of External INTerrupts.
 */
int
ext_int_setup(int apic, int intr)
{
	u_char  select;		/* the select register is 8 bits */
	u_int32_t flags;	/* the window register is 32 bits */
	u_int32_t target;	/* the window register is 32 bits */
	u_int32_t vector;	/* the window register is 32 bits */
	int cpuid;
	char envpath[32];

	if (apic_int_type(apic, intr) != 3)
		return -1;

	cpuid = 0;
	ksnprintf(envpath, sizeof(envpath), "hw.irq.%d.dest", intr);
	kgetenv_int(envpath, &cpuid);

	/* ncpus may not be available yet */
	if (cpuid > mp_naps)
		cpuid = 0;

	/* Deliver interrupts to CPU0 (BSP) */
	target = (CPU_TO_ID(cpuid) << IOART_HI_DEST_SHIFT) &
		 IOART_HI_DEST_MASK;
	select = IOAPIC_REDTBL0 + (2 * intr);
	vector = IDT_OFFSET + intr;
	flags = DEFAULT_EXTINT_FLAGS;

	ioapic_write(ioapic[apic], select, flags | vector);
	ioapic_write(ioapic[apic], select + 1, target);

	return 0;
}
#undef DEFAULT_EXTINT_FLAGS


/*
 * Set the trigger level for an IO APIC pin.
 */
static int
trigger(int apic, int pin, u_int32_t * flags)
{
	int     id;
	int     eirq;
	int     level;
	static int intcontrol = -1;

	switch (apic_trigger(apic, pin)) {

	case 0x00:
		break;

	case 0x01:
		*flags &= ~IOART_TRGRLVL;	/* *flags |= IOART_TRGREDG */
		return 0;

	case 0x03:
		*flags |= IOART_TRGRLVL;
		return 1;

	case -1:
	default:
		goto bad;
	}

	if ((id = apic_src_bus_id(apic, pin)) == -1)
		goto bad;

	switch (apic_bus_type(id)) {
	case ISA:
		*flags &= ~IOART_TRGRLVL;	/* *flags |= IOART_TRGREDG; */
		return 0;

	case EISA:
		eirq = apic_src_bus_irq(apic, pin);

		if (eirq < 0 || eirq > 15) {
			kprintf("EISA IRQ %d?!?!\n", eirq);
			goto bad;
		}

		if (intcontrol == -1) {
			intcontrol = inb(ELCR1) << 8;
			intcontrol |= inb(ELCR0);
			kprintf("EISA INTCONTROL = %08x\n", intcontrol);
		}

		/* Use ELCR settings to determine level or edge mode */
		level = (intcontrol >> eirq) & 1;

		/*
		 * Note that on older Neptune chipset based systems, any
		 * pci interrupts often show up here and in the ELCR as well
		 * as level sensitive interrupts attributed to the EISA bus.
		 */

		if (level)
			*flags |= IOART_TRGRLVL;
		else
			*flags &= ~IOART_TRGRLVL;

		return level;

	case PCI:
		*flags |= IOART_TRGRLVL;
		return 1;

	case -1:
	default:
		goto bad;
	}

bad:
	panic("bad APIC IO INT flags");
}


/*
 * Set the polarity value for an IO APIC pin.
 */
static void
polarity(int apic, int pin, u_int32_t * flags, int level)
{
	int     id;

	switch (apic_polarity(apic, pin)) {

	case 0x00:
		break;

	case 0x01:
		*flags &= ~IOART_INTALO;	/* *flags |= IOART_INTAHI */
		return;

	case 0x03:
		*flags |= IOART_INTALO;
		return;

	case -1:
	default:
		goto bad;
	}

	if ((id = apic_src_bus_id(apic, pin)) == -1)
		goto bad;

	switch (apic_bus_type(id)) {
	case ISA:
		*flags &= ~IOART_INTALO;	/* *flags |= IOART_INTAHI */
		return;

	case EISA:
		/* polarity converter always gives active high */
		*flags &= ~IOART_INTALO;
		return;

	case PCI:
		*flags |= IOART_INTALO;
		return;

	case -1:
	default:
		goto bad;
	}

bad:
	panic("bad APIC IO INT flags");
}


/*
 * Print contents of unmasked IRQs.
 */
void
imen_dump(void)
{
	int x;

	kprintf("SMP: enabled INTs: ");
	for (x = 0; x < APIC_INTMAPSIZE; ++x) {
		if ((int_to_apicintpin[x].flags & IOAPIC_IM_FLAG_MASKED) == 0)
        		kprintf("%d ", x);
	}
	kprintf("\n");
}


/*
 * Inter Processor Interrupt functions.
 */

#endif	/* SMP APIC-IO */

/*
 * Send APIC IPI 'vector' to 'destType' via 'deliveryMode'.
 *
 *  destType is 1 of: APIC_DEST_SELF, APIC_DEST_ALLISELF, APIC_DEST_ALLESELF
 *  vector is any valid SYSTEM INT vector
 *  delivery_mode is 1 of: APIC_DELMODE_FIXED, APIC_DELMODE_LOWPRIO
 *
 * A backlog of requests can create a deadlock between cpus.  To avoid this
 * we have to be able to accept IPIs at the same time we are trying to send
 * them.  The critical section prevents us from attempting to send additional
 * IPIs reentrantly, but also prevents IPIQ processing so we have to call
 * lwkt_process_ipiq() manually.  It's rather messy and expensive for this
 * to occur but fortunately it does not happen too often.
 */
int
apic_ipi(int dest_type, int vector, int delivery_mode)
{
	u_long  icr_lo;

	crit_enter();
	if ((lapic.icr_lo & APIC_DELSTAT_MASK) != 0) {
	    unsigned int eflags = read_eflags();
	    cpu_enable_intr();
	    DEBUG_PUSH_INFO("apic_ipi");
	    while ((lapic.icr_lo & APIC_DELSTAT_MASK) != 0) {
		lwkt_process_ipiq();
	    }
	    DEBUG_POP_INFO();
	    write_eflags(eflags);
	}

	icr_lo = (lapic.icr_lo & APIC_ICRLO_RESV_MASK) | dest_type | 
		delivery_mode | vector;
	lapic.icr_lo = icr_lo;
	crit_exit();
	return 0;
}

void
single_apic_ipi(int cpu, int vector, int delivery_mode)
{
	u_long  icr_lo;
	u_long  icr_hi;

	crit_enter();
	if ((lapic.icr_lo & APIC_DELSTAT_MASK) != 0) {
	    unsigned int eflags = read_eflags();
	    cpu_enable_intr();
	    DEBUG_PUSH_INFO("single_apic_ipi");
	    while ((lapic.icr_lo & APIC_DELSTAT_MASK) != 0) {
		lwkt_process_ipiq();
	    }
	    DEBUG_POP_INFO();
	    write_eflags(eflags);
	}
	icr_hi = lapic.icr_hi & ~APIC_ID_MASK;
	icr_hi |= (CPU_TO_ID(cpu) << 24);
	lapic.icr_hi = icr_hi;

	/* build ICR_LOW */
	icr_lo = (lapic.icr_lo & APIC_ICRLO_RESV_MASK)
	    | APIC_DEST_DESTFLD | delivery_mode | vector;

	/* write APIC ICR */
	lapic.icr_lo = icr_lo;
	crit_exit();
}

#if 0	

/*
 * Returns 0 if the apic is busy, 1 if we were able to queue the request.
 *
 * NOT WORKING YET!  The code as-is may end up not queueing an IPI at all
 * to the target, and the scheduler does not 'poll' for IPI messages.
 */
int
single_apic_ipi_passive(int cpu, int vector, int delivery_mode)
{
	u_long  icr_lo;
	u_long  icr_hi;

	crit_enter();
	if ((lapic.icr_lo & APIC_DELSTAT_MASK) != 0) {
	    crit_exit();
	    return(0);
	}
	icr_hi = lapic.icr_hi & ~APIC_ID_MASK;
	icr_hi |= (CPU_TO_ID(cpu) << 24);
	lapic.icr_hi = icr_hi;

	/* build IRC_LOW */
	icr_lo = (lapic.icr_lo & APIC_RESV2_MASK)
	    | APIC_DEST_DESTFLD | delivery_mode | vector;

	/* write APIC ICR */
	lapic.icr_lo = icr_lo;
	crit_exit();
	return(1);
}

#endif

/*
 * Send APIC IPI 'vector' to 'target's via 'delivery_mode'.
 *
 * target is a bitmask of destination cpus.  Vector is any
 * valid system INT vector.  Delivery mode may be either
 * APIC_DELMODE_FIXED or APIC_DELMODE_LOWPRIO.
 */
void
selected_apic_ipi(cpumask_t target, int vector, int delivery_mode)
{
	crit_enter();
	while (target) {
		int n = BSFCPUMASK(target);
		target &= ~CPUMASK(n);
		single_apic_ipi(n, vector, delivery_mode);
	}
	crit_exit();
}

/*
 * Timer code, in development...
 *  - suggested by rgrimes@gndrsh.aac.dev.com
 */
int
get_apic_timer_frequency(void)
{
	return(lapic_cputimer_intr.freq);
}

/*
 * Load a 'downcount time' in uSeconds.
 */
void
set_apic_timer(int us)
{
	u_int count;

	/*
	 * When we reach here, lapic timer's frequency
	 * must have been calculated as well as the
	 * divisor (lapic.dcr_timer is setup during the
	 * divisor calculation).
	 */
	KKASSERT(lapic_cputimer_intr.freq != 0 &&
		 lapic_timer_divisor_idx >= 0);

	count = ((us * (int64_t)lapic_cputimer_intr.freq) + 999999) / 1000000;
	lapic_timer_oneshot(count);
}


/*
 * Read remaining time in timer.
 */
int
read_apic_timer(void)
{
#if 0
	/** XXX FIXME: we need to return the actual remaining time,
         *         for now we just return the remaining count.
         */
#else
	return lapic.ccr_timer;
#endif
}


/*
 * Spin-style delay, set delay time in uS, spin till it drains.
 */
void
u_sleep(int count)
{
	set_apic_timer(count);
	while (read_apic_timer())
		 /* spin */ ;
}

void
lapic_map(vm_offset_t lapic_addr)
{
	/* Local apic is mapped on last page */
	SMPpt[NPTEPG - 1] = (pt_entry_t)(PG_V | PG_RW | PG_N |
	    pmap_get_pgeflag() | (lapic_addr & PG_FRAME));

	kprintf("lapic: at %p\n", (void *)lapic_addr);
}

static TAILQ_HEAD(, lapic_enumerator) lapic_enumerators =
	TAILQ_HEAD_INITIALIZER(lapic_enumerators);

void
lapic_config(void)
{
	struct lapic_enumerator *e;
	int error;

	TAILQ_FOREACH(e, &lapic_enumerators, lapic_link) {
		error = e->lapic_probe(e);
		if (!error)
			break;
	}
	if (e == NULL)
		panic("can't config lapic\n");

	e->lapic_enumerate(e);
}

void
lapic_enumerator_register(struct lapic_enumerator *ne)
{
	struct lapic_enumerator *e;

	TAILQ_FOREACH(e, &lapic_enumerators, lapic_link) {
		if (e->lapic_prio < ne->lapic_prio) {
			TAILQ_INSERT_BEFORE(e, ne, lapic_link);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&lapic_enumerators, ne, lapic_link);
}

static TAILQ_HEAD(, ioapic_enumerator) ioapic_enumerators =
	TAILQ_HEAD_INITIALIZER(ioapic_enumerators);

void
ioapic_config(void)
{
	struct ioapic_enumerator *e;
	int error, i;
	u_long ef = 0;

	TAILQ_INIT(&ioapic_conf.ioc_list);
	/* XXX magic number */
	for (i = 0; i < 16; ++i)
		ioapic_conf.ioc_intsrc[i] = -1;

	TAILQ_FOREACH(e, &ioapic_enumerators, ioapic_link) {
		error = e->ioapic_probe(e);
		if (!error)
			break;
	}
	if (e == NULL) {
#ifdef notyet
		panic("can't config I/O APIC\n");
#else
		kprintf("no I/O APIC\n");
		return;
#endif
	}

	if (!ioapic_use_old) {
		crit_enter();

		ef = read_eflags();
		cpu_disable_intr();

		/*
		 * Switch to I/O APIC MachIntrABI and reconfigure
		 * the default IDT entries.
		 */
		MachIntrABI = MachIntrABI_IOAPIC;
		MachIntrABI.setdefault();
	}

	e->ioapic_enumerate(e);

	if (!ioapic_use_old) {
		struct ioapic_info *info;

		/*
		 * Fixup the rest of the fields of ioapic_info
		 */
		i = 0;
		TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
			const struct ioapic_info *prev_info;

			info->io_idx = i++;
			info->io_apic_id = info->io_idx + lapic_id_max + 1;

			if (bootverbose) {
				kprintf("IOAPIC: idx %d, apic id %d, "
					"gsi base %d, npin %d\n",
					info->io_idx,
					info->io_apic_id,
					info->io_gsi_base,
					info->io_npin);
			}

			/* Warning about possible GSI hole */
			prev_info = TAILQ_PREV(info, ioapic_info_list, io_link);
			if (prev_info != NULL) {
				if (info->io_gsi_base !=
				prev_info->io_gsi_base + prev_info->io_npin) {
					kprintf("IOAPIC: warning gsi hole "
						"[%d, %d]\n",
						prev_info->io_gsi_base +
						prev_info->io_npin,
						info->io_gsi_base - 1);
				}
			}
		}

		/*
		 * Setup all I/O APIC
		 */
		TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link)
			ioapic_setup(info);
		ioapic_abi_fixup_irqmap();

		write_eflags(ef);

		MachIntrABI.cleanup();

		crit_exit();
	}
}

void
ioapic_enumerator_register(struct ioapic_enumerator *ne)
{
	struct ioapic_enumerator *e;

	TAILQ_FOREACH(e, &ioapic_enumerators, ioapic_link) {
		if (e->ioapic_prio < ne->ioapic_prio) {
			TAILQ_INSERT_BEFORE(e, ne, ioapic_link);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&ioapic_enumerators, ne, ioapic_link);
}

void
ioapic_add(void *addr, int gsi_base, int npin)
{
	struct ioapic_info *info, *ninfo;
	int gsi_end;

	gsi_end = gsi_base + npin - 1;
	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		if ((gsi_base >= info->io_gsi_base &&
		     gsi_base < info->io_gsi_base + info->io_npin) ||
		    (gsi_end >= info->io_gsi_base &&
		     gsi_end < info->io_gsi_base + info->io_npin)) {
			panic("ioapic_add: overlapped gsi, base %d npin %d, "
			      "hit base %d, npin %d\n", gsi_base, npin,
			      info->io_gsi_base, info->io_npin);
		}
		if (info->io_addr == addr)
			panic("ioapic_add: duplicated addr %p\n", addr);
	}

	ninfo = kmalloc(sizeof(*ninfo), M_DEVBUF, M_WAITOK | M_ZERO);
	ninfo->io_addr = addr;
	ninfo->io_npin = npin;
	ninfo->io_gsi_base = gsi_base;

	/*
	 * Create IOAPIC list in ascending order of GSI base
	 */
	TAILQ_FOREACH_REVERSE(info, &ioapic_conf.ioc_list,
	    ioapic_info_list, io_link) {
		if (ninfo->io_gsi_base > info->io_gsi_base) {
			TAILQ_INSERT_AFTER(&ioapic_conf.ioc_list,
			    info, ninfo, io_link);
			break;
		}
	}
	if (info == NULL)
		TAILQ_INSERT_HEAD(&ioapic_conf.ioc_list, ninfo, io_link);
}

void
ioapic_intsrc(int irq, int gsi)
{
	KKASSERT(irq != gsi);
	KKASSERT(irq < 16);

	if (gsi == 0) {
		/* Don't allow mixed mode */
		kprintf("IOAPIC: warning intsrc irq %d -> gsi 0\n", irq);
		return;
	}

	if (ioapic_conf.ioc_intsrc[irq] != -1 &&
	    ioapic_conf.ioc_intsrc[irq] != gsi) {
		kprintf("IOAPIC: warning intsrc irq %d, gsi %d -> gsi %d\n",
			irq, ioapic_conf.ioc_intsrc[irq], gsi);
	}
	ioapic_conf.ioc_intsrc[irq] = gsi;
}

static void
ioapic_set_apic_id(const struct ioapic_info *info)
{
	uint32_t id;

	id = ioapic_read(info->io_addr, IOAPIC_ID);

	id &= ~APIC_ID_MASK;
	id |= (info->io_apic_id << 24);

	ioapic_write(info->io_addr, IOAPIC_ID, id);

	/*
	 * Re-read && test
	 */
	id = ioapic_read(info->io_addr, IOAPIC_ID);
	if (((id & APIC_ID_MASK) >> 24) != info->io_apic_id) {
		panic("ioapic_set_apic_id: can't set apic id to %d\n",
		      info->io_apic_id);
	}
}

static void
ioapic_gsi_setup(int gsi)
{
	enum intr_trigger trig;
	enum intr_polarity pola;
	int irq;

	if (gsi == 0) {
		/* ExtINT */
		imen_lock();
		ioapic_extpin_setup(ioapic_gsi_ioaddr(gsi),
		    ioapic_gsi_pin(gsi), 0);
		imen_unlock();
		return;
	}

	for (irq = 0; irq < 16; ++irq) {
		if (gsi == ioapic_conf.ioc_intsrc[irq]) {
			trig = INTR_TRIGGER_EDGE;
			pola = INTR_POLARITY_HIGH;
			break;
		}
	}

	if (irq == 16) {
		if (gsi < 16) {
			trig = INTR_TRIGGER_EDGE;
			pola = INTR_POLARITY_HIGH;
		} else {
			trig = INTR_TRIGGER_LEVEL;
			pola = INTR_POLARITY_LOW;
		}
		irq = gsi;
	}

	ioapic_abi_set_irqmap(irq, gsi, trig, pola);
}

void *
ioapic_gsi_ioaddr(int gsi)
{
	const struct ioapic_info *info;

	info = ioapic_gsi_search(gsi);
	return info->io_addr;
}

int
ioapic_gsi_pin(int gsi)
{
	const struct ioapic_info *info;

	info = ioapic_gsi_search(gsi);
	return gsi - info->io_gsi_base;
}

static const struct ioapic_info *
ioapic_gsi_search(int gsi)
{
	const struct ioapic_info *info;

	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		if (gsi >= info->io_gsi_base &&
		    gsi < info->io_gsi_base + info->io_npin)
			return info;
	}
	panic("ioapic_gsi_search: no I/O APIC\n");
}

int
ioapic_gsi(int idx, int pin)
{
	const struct ioapic_info *info;

	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		if (info->io_idx == idx)
			break;
	}
	if (info == NULL)
		return -1;
	if (pin >= info->io_npin)
		return -1;
	return info->io_gsi_base + pin;
}

void
ioapic_extpin_setup(void *addr, int pin, int vec)
{
	ioapic_pin_prog(addr, pin, vec,
	    INTR_TRIGGER_CONFORM, INTR_POLARITY_CONFORM, IOART_DELEXINT);
}

int
ioapic_extpin_gsi(void)
{
	return 0;
}

void
ioapic_pin_setup(void *addr, int pin, int vec,
    enum intr_trigger trig, enum intr_polarity pola)
{
	/*
	 * Always clear an I/O APIC pin before [re]programming it.  This is
	 * particularly important if the pin is set up for a level interrupt
	 * as the IOART_REM_IRR bit might be set.   When we reprogram the
	 * vector any EOI from pending ints on this pin could be lost and
	 * IRR might never get reset.
	 *
	 * To fix this problem, clear the vector and make sure it is 
	 * programmed as an edge interrupt.  This should theoretically
	 * clear IRR so we can later, safely program it as a level 
	 * interrupt.
	 */
	ioapic_pin_prog(addr, pin, vec, INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH,
	    IOART_DELFIXED);
	ioapic_pin_prog(addr, pin, vec, trig, pola, IOART_DELFIXED);
}

static void
ioapic_pin_prog(void *addr, int pin, int vec,
    enum intr_trigger trig, enum intr_polarity pola, uint32_t del_mode)
{
	uint32_t flags, target;
	int select;

	KKASSERT(del_mode == IOART_DELEXINT || del_mode == IOART_DELFIXED);

	select = IOAPIC_REDTBL0 + (2 * pin);

	flags = ioapic_read(addr, select) & IOART_RESV;
	flags |= IOART_INTMSET | IOART_DESTPHY;
#ifdef foo
	flags |= del_mode;
#else
	/*
	 * We only support limited I/O APIC mixed mode,
	 * so even for ExtINT, we still use "fixed"
	 * delivery mode.
	 */
	flags |= IOART_DELFIXED;
#endif

	if (del_mode == IOART_DELEXINT) {
		KKASSERT(trig == INTR_TRIGGER_CONFORM &&
			 pola == INTR_POLARITY_CONFORM);
		flags |= IOART_TRGREDG | IOART_INTAHI;
	} else {
		switch (trig) {
		case INTR_TRIGGER_EDGE:
			flags |= IOART_TRGREDG;
			break;

		case INTR_TRIGGER_LEVEL:
			flags |= IOART_TRGRLVL;
			break;

		case INTR_TRIGGER_CONFORM:
			panic("ioapic_pin_prog: trig conform is not "
			      "supported\n");
		}
		switch (pola) {
		case INTR_POLARITY_HIGH:
			flags |= IOART_INTAHI;
			break;

		case INTR_POLARITY_LOW:
			flags |= IOART_INTALO;
			break;

		case INTR_POLARITY_CONFORM:
			panic("ioapic_pin_prog: pola conform is not "
			      "supported\n");
		}
	}

	target = ioapic_read(addr, select + 1) & IOART_HI_DEST_RESV;
	target |= 0;

	ioapic_write(addr, select, flags | vec);
	ioapic_write(addr, select + 1, target);
}

static void
ioapic_setup(const struct ioapic_info *info)
{
	int i;

	ioapic_set_apic_id(info);

	for (i = 0; i < info->io_npin; ++i)
		ioapic_gsi_setup(info->io_gsi_base + i);
}
