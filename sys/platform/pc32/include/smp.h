/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/include/smp.h,v 1.50.2.5 2001/02/13 22:32:45 tegge Exp $
 * $DragonFly: src/sys/platform/pc32/include/smp.h,v 1.20 2006/11/07 06:43:24 dillon Exp $
 *
 */

#ifndef _MACHINE_SMP_H_
#define _MACHINE_SMP_H_

#ifdef _KERNEL

#if defined(SMP)

#ifndef LOCORE

/*
 * For sending values to POST displays.
 * XXX FIXME: where does this really belong, isa.h/isa.c perhaps?
 */
extern int current_postcode;  /** XXX currently in mp_machdep.c */
#define POSTCODE(X)	current_postcode = (X), \
			outb(0x80, current_postcode)
#define POSTCODE_LO(X)	current_postcode &= 0xf0, \
			current_postcode |= ((X) & 0x0f), \
			outb(0x80, current_postcode)
#define POSTCODE_HI(X)	current_postcode &= 0x0f, \
			current_postcode |= (((X) << 4) & 0xf0), \
			outb(0x80, current_postcode)


#include <machine_base/apic/apicreg.h>
#include <machine/pcb.h>

/* global data in mpboot.s */
extern int			bootMP_size;

/* functions in mpboot.s */
void	bootMP			(void);

/* global data in apic_vector.s */
extern volatile cpumask_t	stopped_cpus;
extern volatile cpumask_t	started_cpus;

extern volatile u_int		checkstate_probed_cpus;
extern void (*cpustop_restartfunc) (void);

/* functions in apic_ipl.s */
u_int	ioapic_read		(volatile void *, int);
void	ioapic_write		(volatile void *, int, u_int);

/* global data in mp_machdep.c */
extern int			imcr_present;
extern int			mp_naps;
extern int			mp_napics;
extern vm_offset_t		io_apic_address[];
extern u_int32_t		cpu_apic_versions[];
extern u_int32_t		*io_apic_versions;
extern int			cpu_num_to_apic_id[];
extern int			io_num_to_apic_id[];
extern int			apic_id_to_logical[];
#define APIC_INTMAPSIZE 192
/*
 * NOTE:
 * - Keep size of apic_intmapinfo power of 2
 * - Update IOAPIC_IM_SZSHIFT after changing apic_intmapinfo size
 */
struct apic_intmapinfo {
  	int ioapic;
	int int_pin;
	volatile void *apic_address;
	int redirindex;
	uint32_t flags;		/* IOAPIC_IM_FLAG_ */
	uint32_t pad[3];
};
#define IOAPIC_IM_SZSHIFT	5

#define IOAPIC_IM_FLAG_LEVEL	0x1	/* default to edge trigger */
#define IOAPIC_IM_FLAG_MASKED	0x2

extern struct apic_intmapinfo	int_to_apicintpin[];
extern struct pcb		stoppcbs[];

/* functions in mp_machdep.c */
void	*ioapic_map(vm_paddr_t);
u_int	mp_bootaddress		(u_int);
void	mp_start		(void);
void	mp_announce		(void);
void	mp_set_cpuids		(int, int);
u_int	isa_apic_mask		(u_int);
int	isa_apic_irq		(int);
int	pci_apic_irq		(int, int, int);
int	apic_irq		(int, int);
int	next_apic_irq		(int);
int	undirect_isa_irq	(int);
int	undirect_pci_irq	(int);
int	apic_bus_type		(int);
int	apic_src_bus_id		(int, int);
int	apic_src_bus_irq	(int, int);
int	apic_int_type		(int, int);
int	apic_trigger		(int, int);
int	apic_polarity		(int, int);
void	assign_apic_irq		(int apic, int intpin, int irq);
void	revoke_apic_irq		(int irq);
void	init_secondary		(void);
int	stop_cpus		(cpumask_t);
void	ap_init			(void);
int	restart_cpus		(cpumask_t);
void	forward_signal		(struct proc *);
int	mptable_pci_int_route(int, int, int, int);
void	mptable_pci_int_dump(void);

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct lapic_enumerator {
	int	lapic_prio;
	TAILQ_ENTRY(lapic_enumerator) lapic_link;
	int	(*lapic_probe)(struct lapic_enumerator *);
	void	(*lapic_enumerate)(struct lapic_enumerator *);
};

#define LAPIC_ENUM_PRIO_MPTABLE		20
#define LAPIC_ENUM_PRIO_MADT		40

struct ioapic_enumerator {
	int	ioapic_prio;
	TAILQ_ENTRY(ioapic_enumerator) ioapic_link;
	int	(*ioapic_probe)(struct ioapic_enumerator *);
	void	(*ioapic_enumerate)(struct ioapic_enumerator *);
};

#define IOAPIC_ENUM_PRIO_MPTABLE	20
#define IOAPIC_ENUM_PRIO_MADT		10

/* global data in mpapic.c */
extern volatile lapic_t		lapic;
extern volatile ioapic_t	**ioapic;
extern int			lapic_id_max;

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

/* functions in mpapic.c */
void	apic_dump		(char*);
void	lapic_init		(boolean_t);
void	imen_dump		(void);
int	apic_ipi		(int, int, int);
void	selected_apic_ipi	(cpumask_t, int, int);
void	single_apic_ipi(int cpu, int vector, int delivery_mode);
int	single_apic_ipi_passive(int cpu, int vector, int delivery_mode);
int	io_apic_setup		(int);
void	io_apic_setup_intpin	(int, int);
void	io_apic_set_id		(int, int);
int	io_apic_get_id		(int);
int	ext_int_setup		(int, int);
void	lapic_config(void);
void	lapic_enumerator_register(struct lapic_enumerator *);
void	ioapic_config(void);
void	ioapic_enumerator_register(struct ioapic_enumerator *);
void	ioapic_add(void *, int, int);
void	ioapic_intsrc(int, int);
void	*ioapic_gsi_ioaddr(int);
int	ioapic_gsi_pin(int);
void	ioapic_pin_setup(void *, int, int,
	    enum intr_trigger, enum intr_polarity);
void	ioapic_extpin_setup(void *, int, int);
int	ioapic_extpin_gsi(void);
int	ioapic_gsi(int, int);

extern int apic_io_enable;
extern int ioapic_use_old;

#if defined(READY)
void	clr_io_apic_mask24	(int, u_int32_t);
void	set_io_apic_mask24	(int, u_int32_t);
#endif /* READY */

void	set_apic_timer		(int);
int	get_apic_timer_frequency(void);
int	read_apic_timer		(void);
void	u_sleep			(int);
void	cpu_send_ipiq		(int);
int	cpu_send_ipiq_passive	(int);

/* global data in init_smp.c */
extern cpumask_t		smp_active_mask;

#endif /* !LOCORE */
#else	/* !SMP */

#define	smp_active_mask	1	/* smp_active_mask always 1 on UP machines */

#endif

#endif /* _KERNEL */
#endif /* _MACHINE_SMP_H_ */
